/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "PlaylistParser/ContentDecrypter.h"

#include <iomanip>
#include <openssl/evp.h>

#ifdef ENABLE_SAMPLE_AES
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/version.h>
#include <libavformat/avformat.h>
#include <libavformat/version.h>
}
#endif  // ENABLE_SAMPLE_AES

#include <AVSCommon/Utils/Logger/Logger.h>
#include <AVSCommon/Utils/WavUtils.h>

#include "PlaylistParser/FFMpegInputBuffer.h"

namespace alexaClientSDK {
namespace playlistParser {

using namespace avsCommon::utils::playlistParser;
using namespace avsCommon::avs::attachment;

/// Unique ptr to auto release EVP_CIPHER_CTX.
struct EVP_CIPHER_CTX_Deleter {
    void operator()(EVP_CIPHER_CTX* ctx) {
        EVP_CIPHER_CTX_free(ctx);
    }
};
using EVP_CIPHER_CTX_free_ptr = std::unique_ptr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_Deleter>;

/// String to identify log entries originating from this file.
#define TAG "ContentDecrypter"

/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param The event string for this @c LogEntry.
 */
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

/// Block size of AES encrypted content.
static const int AES_BLOCK_SIZE = 16;

/// Length of initilization vector as hex string.
static const int IV_HEX_STRING_LENGTH = 2 * AES_BLOCK_SIZE;

/// Timeout for write to stream.
static const std::chrono::milliseconds WRITE_TO_STREAM_TIMEOUT(100);

#ifdef ENABLE_SAMPLE_AES
/// Invalid location if mdat is not found.
static const int INVALID_MDAT_LOCATION = -1;

/// Size of FFMeg io buffer.
static const int AV_BUFFER_SIZE = 4 * 1024;

/// Invalid stream index.
static const int INVALID_STREAM_INDEX = -1;

/// Size of buffer to read AVError.
static const int AVERROR_BUFFER_SIZE = 64;

/// Number of unencrypted bytes for ADTS header with CRC.
static const int UNENCRYPTED_ADTS_HEADER_BYTES_WITH_CRC = 9 + AES_BLOCK_SIZE;

/// Number of unencrypted bytes for ADTS header without CRC.
static const int UNENCRYPTED_ADTS_HEADER_BYTES_WITHOUT_CRC = 7 + AES_BLOCK_SIZE;

/// FFMPEG wrappers
struct AVIOContextDeleter {
    void operator()(AVIOContext* ptr) {
        if (ptr) {
            av_free(ptr->buffer);
        }
        av_free(ptr);
    }
};
using AVIOContextPtr = std::unique_ptr<AVIOContext, AVIOContextDeleter>;

struct AVFormatContextDeleter {
    void operator()(AVFormatContext* ptr) {
        if (ptr) {
            avformat_close_input(&ptr);
        }
    }
};
using AVFormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;

struct AVPacketDeleter {
    void operator()(AVPacket* ptr) {
        av_packet_unref(ptr);
    }
};
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;

struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ptr) {
        avcodec_free_context(&ptr);
    }
};
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;

struct AVFrameDeleter {
    void operator()(AVFrame* ptr) {
        av_frame_free(&ptr);
    }
};
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

/**
 * FFMpeg callback for refilling the buffer.
 *
 * @param opaque Pointer to FFMpegInputBuffer.
 * @param buf Pointer to FFMPegs internal buffer to read data to.
 * @param buf_size Requested buffer size.
 * @return number of bytes read if successful, otherwise AVERROR(EIO).
 */
static int avioRead(void* opaque, uint8_t* buf, int buf_size) {
    if (!opaque || !buf) {
        return 0;
    }

    auto input = reinterpret_cast<FFMpegInputBuffer*>(opaque);
    auto result = input->read(buf_size, buf);
    if (result < 0) {
        ACSDK_ERROR(LX("avioRead").d("reason", "readFailed"));
        return AVERROR(EIO);
    }
    return result;
}

/**
 * FFMpeg callback for seeking to specified byte position.
 *
 * @param opaque Pointer to FFMpegInputBuffer.
 * @param offset The offset to seek.
 * @param whence The place from where to seek.
 * @return new offest if successful, otherwise AVERROR(EIO).
 */
static int64_t avioSeek(void* opaque, int64_t offset, int whence) {
    auto error = AVERROR(EIO);
    if (!opaque) {
        return error;
    }

    auto input = reinterpret_cast<FFMpegInputBuffer*>(opaque);
    auto adjustedOffset = offset;
    switch (whence) {
        case SEEK_SET:
            break;
        case SEEK_CUR:
            adjustedOffset += input->getOffset();
            break;
        case SEEK_END:
            adjustedOffset += input->getSize();
            break;
        case AVSEEK_SIZE:
            return input->getSize();
        default:
            ACSDK_ERROR(LX("avioSeek").d("reason", "defaultCase"));
            return error;
    }
    if (!input->setOffset(adjustedOffset)) {
        return error;
    }
    return input->getOffset();
}

/**
 * Helper function to convert FFMpeg error codes to string.
 *
 * @param err Error code.
 * @return String of error.
 */
std::string ffmpegErrToStr(int err) {
    char errbuf[256];
    if (av_strerror(err, errbuf, sizeof(errbuf)) == 0) {
        return errbuf;
    }
    return strerror(AVUNERROR(err));
}
#endif  // ENABLE_SAMPLE_AES

ContentDecrypter::ContentDecrypter() :
        RequiresShutdown{"ContentDecrypter"},
        m_needWavHeader{false},
        m_totalDuration{std::chrono::milliseconds::zero()},
        m_shuttingDown{false} {
#ifdef ENABLE_SAMPLE_AES
// av_register_all is deprecated in FFMpeg 4.0.
#if (LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 9, 100))
    av_register_all();
#endif
#endif  // ENABLE_SAMPLE_AES
}

void ContentDecrypter::setMediaInitToDecryptedContent(const ByteVector& mis, std::chrono::milliseconds totalDuration) {
    auto size = mis.size();
    m_mediaInitSection = std::vector<unsigned char>(size);
    // Replace 'enca' with 'mp4a'. This is required to allow gstreamer to play the data.
    for (size_t i = 0; i < size; i++) {
        if (i < size - 4 && 'e' == mis[i] && 'n' == mis[i + 1] && 'c' == mis[i + 2] && 'a' == mis[i + 3]) {
            m_mediaInitSection[i] = 'm';
            m_mediaInitSection[i + 1] = 'p';
            m_mediaInitSection[i + 2] = '4';
            m_mediaInitSection[i + 3] = 'a';
            i += 3;
        } else {
            m_mediaInitSection[i] = mis[i];
        }
    }
    m_needWavHeader = true;
    m_totalDuration = totalDuration;
}

bool ContentDecrypter::decryptAndWrite(
    const ByteVector& encryptedContent,
    const ByteVector& key,
    const EncryptionInfo& encryptionInfo,
    const std::shared_ptr<AttachmentWriter>& streamWriter,
    const std::shared_ptr<Id3TagsRemover>& id3TagRemover) {
    if (!id3TagRemover) {
        ACSDK_WARN(LX("decryptAndWriteFailed").d("reason", "nullId3TagRemover"));
        // fallback to not remove ID3 tags
    }

    ByteVector ivByteArray;
    if (!convertIVToByteArray(encryptionInfo.initVector, &ivByteArray)) {
        ACSDK_ERROR(LX("decryptAndWriteFailed").d("reason", "convertIVToByteArrayFailed"));
        return false;
    }

    ByteVector decryptedContent;
    switch (encryptionInfo.method) {
        case EncryptionInfo::Method::AES_128:
            if (!decryptAES(encryptedContent, key, ivByteArray, &decryptedContent)) {
                ACSDK_ERROR(LX("decryptAndWriteFailed").d("reason", "aes128DecryptionFailed"));
                return false;
            }
            break;
        case EncryptionInfo::Method::SAMPLE_AES:
#ifdef ENABLE_SAMPLE_AES
            if (!decryptSampleAES(encryptedContent, key, ivByteArray, streamWriter)) {
                ACSDK_ERROR(LX("decryptAndWriteFailed").d("reason", "sampleAESDecryptionFailed"));
                return false;
            }
            // Stream written to during decryptSampleAES
            return true;
#else
            ACSDK_ERROR(LX("decryptAndWriteFailed").d("reason", "sampleAESDecryptionDisabled"));
            return false;
#endif  // ENABLE_SAMPLE_AES
            break;
        default:
            ACSDK_ERROR(LX("decryptAndWriteFailed")
                            .d("reason", "encryptionMethodNotSupported")
                            .d("method", static_cast<int>(encryptionInfo.method)));
            return false;
    }

    if (id3TagRemover) {
        id3TagRemover->stripID3Tags(decryptedContent);
    }
    if (!writeToStream(decryptedContent, streamWriter)) {
        ACSDK_ERROR(LX("decryptAndWriteFailed").d("reason", "writeFailed"));
        return false;
    }

    return true;
}

bool ContentDecrypter::convertIVToByteArray(const std::string& hexIV, ByteVector* ivByteArray) {
    if (!ivByteArray) {
        ACSDK_ERROR(LX("convertIVToByteArray").d("reason", "nullIVByteArray"));
        return false;
    }

    if (hexIV.length() != IV_HEX_STRING_LENGTH + 2) {
        ACSDK_ERROR(LX("convertIVToByteArray").d("reason", "incorrectLength").d("length", hexIV.length()));
        return false;
    }

    std::string trimmedHexIV;
    if (0 == hexIV.compare(0, 2, "0x") || 0 == hexIV.compare(0, 2, "0X")) {
        trimmedHexIV = hexIV.substr(2);
    } else {
        ACSDK_WARN(LX("convertIVToByteArray").d("reason", "ivStringNotStartWith0x"));
        trimmedHexIV = hexIV;
    }

    if (trimmedHexIV.length() != IV_HEX_STRING_LENGTH) {
        ACSDK_ERROR(LX("convertIVToByteArray").d("reason", "invalidIVStringLength"));
        return false;
    }

    ByteVector result(AES_BLOCK_SIZE);
    for (unsigned int i = 0; i < trimmedHexIV.length(); i += 2) {
        unsigned char byte;
        sscanf(trimmedHexIV.substr(i, 2).c_str(), "%2hhx", &byte);
        result[i / 2] = byte;
    }
    *ivByteArray = result;
    return true;
}

int ContentDecrypter::decryptAES(
    const ByteVector& encryptedContent,
    const ByteVector& key,
    const ByteVector& iv,
    ByteVector* decryptedContent,
    bool usePadding) {
    auto logFailure = [](const std::string& reason) { ACSDK_ERROR(LX("decryptAESFailed").d("reason", reason)); };

    if (!decryptedContent) {
        logFailure("nullDecryptedContent");
        return 0;
    }

    auto inputSize = encryptedContent.size();
    ByteVector output(inputSize);

    int outputSize = 0;
    int len = 0;
    EVP_CIPHER_CTX_free_ptr ctx(EVP_CIPHER_CTX_new());
    if (!ctx.get()) {
        logFailure("EVPContextIsNULL");
        return 0;
    }
    if (key.size() != static_cast<ByteVector::size_type>(EVP_CIPHER_key_length(EVP_aes_128_cbc()))) {
        logFailure("InvalidKeyLength");
        return 0;
    }
    if (iv.size() != static_cast<ByteVector::size_type>(EVP_CIPHER_iv_length(EVP_aes_128_cbc()))) {
        logFailure("InvalidIVLength");
        return 0;
    }
    if (!EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_cbc(), NULL, key.data(), iv.data())) {
        logFailure("UnableToInitializeDecryption");
        return 0;
    }

    EVP_CIPHER_CTX_set_padding(ctx.get(), usePadding);

    if (!EVP_DecryptUpdate(ctx.get(), output.data(), &len, encryptedContent.data(), static_cast<int>(inputSize))) {
        logFailure("UnableToDecryptUpdate");
        return 0;
    }
    outputSize = len;

    if (!EVP_DecryptFinal_ex(ctx.get(), output.data() + len, &len)) {
        logFailure("UnableToDecryptFinalize");
        return 0;
    }
    outputSize += len;

    output.resize(outputSize);
    *decryptedContent = output;
    return outputSize;
}

#ifdef ENABLE_SAMPLE_AES
ByteVector ContentDecrypter::prependMediaInitSection(const ByteVector& bytes) const {
    ByteVector result;
    result.reserve(m_mediaInitSection.size() + bytes.size());
    if (m_mediaInitSection.size() > 0) {
        result.insert(result.end(), m_mediaInitSection.begin(), m_mediaInitSection.end());
    }
    result.insert(result.end(), bytes.begin(), bytes.end());
    return result;
}

static size_t findMDATLocation(const ByteVector& bytes) {
    auto size = bytes.size();
    if (size < 4) {
        ACSDK_ERROR(LX(__func__).m("Header is too small"));
        return INVALID_MDAT_LOCATION;
    }
    for (size_t i = 0; i <= (size - 4); i++) {
        if ('m' == bytes[i] && 'd' == bytes[i + 1] && 'a' == bytes[i + 2] && 't' == bytes[i + 3]) {
            ACSDK_DEBUG5(LX("sampleAESDecrypt").d("reason", "mdatLocated").d("position", i));
            return i;
        }
    }
    return INVALID_MDAT_LOCATION;
}

int findAudioStreamIndex(AVFormatContext* formatContext) {
    if (!formatContext) {
        return INVALID_STREAM_INDEX;
    }

    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
#if LIBAVCODEC_VERSION_MAJOR < 57
        auto codecInfo = formatContext->streams[i]->codec;
#else
        // CodecContext is deprecated. Use CodecPar for ffmpeg > 3.1
        auto codecInfo = formatContext->streams[i]->codecpar;
#endif
        if (AVMEDIA_TYPE_AUDIO == codecInfo->codec_type) {
            return i;
        }
    }
    return INVALID_STREAM_INDEX;
}

bool ContentDecrypter::decryptSampleAES(
    const ByteVector& encryptedContent,
    const ByteVector& key,
    const ByteVector& iv,
    const std::shared_ptr<avsCommon::avs::attachment::AttachmentWriter>& streamWriter) {
    if (encryptedContent.size() == 0) {
        ACSDK_ERROR(LX("decryptSampleAESFailed").d("reason", "encryptedContentIsEmpty"));
        return false;
    }

    // Output is larger than input due to being decoded
    ByteVector output;

    // Locate 'mdat'. If mdat is located in the input then this is fragmented mp4
    int mdatLocation = findMDATLocation(encryptedContent);

    // Tag on the media initialization section to the input.
    FFMpegInputBuffer input(prependMediaInitSection(encryptedContent));
    auto avBuffer = static_cast<unsigned char*>(av_malloc(AV_BUFFER_SIZE));
    if (!avBuffer) {
        ACSDK_ERROR(LX("decryptSampleAESFailed").d("reason", "avBufferMallocFailed"));
        return false;
    }

    AVIOContextPtr ioContext(avio_alloc_context(avBuffer, AV_BUFFER_SIZE, 0, &input, avioRead, NULL, avioSeek));
    AVFormatContextPtr formatContext(avformat_alloc_context());
    formatContext->pb = ioContext.get();

    auto rawFormatContext = formatContext.get();
    auto averror = avformat_open_input(&rawFormatContext, NULL, NULL, NULL);
    if (averror != 0) {
        logAVError("decryptSampleAESFailed", "openInput", averror);
        // Note that a user-supplied AVFormatContext will be freed on failure.
        // Release the object to avoid double free.
        formatContext.release();
        return false;
    }

    avformat_find_stream_info(formatContext.get(), NULL);

    int audioIndex = findAudioStreamIndex(formatContext.get());
    if (INVALID_STREAM_INDEX == audioIndex) {
        ACSDK_ERROR(LX("decryptSampleAESFailed").d("reason", "audioStreamNotFound"));
        return false;
    }

#if LIBAVCODEC_VERSION_MAJOR < 57
    auto cid = formatContext->streams[audioIndex]->codec->codec_id;
#else
    // CodecContext is deprecated. Use CodecPar for ffmpeg > 3.1
    auto cid = formatContext->streams[audioIndex]->codecpar->codec_id;
#endif

    // find decoder for the stream
    auto dec = avcodec_find_decoder(cid);

    // Allocate a codec context for the decoder
    AVCodecContextPtr decCtx(avcodec_alloc_context3(dec));

    int ret = 0;
    ret = avcodec_parameters_to_context(decCtx.get(), formatContext->streams[audioIndex]->codecpar);
    if (ret < 0) {
        ACSDK_ERROR(LX("Error converting codec").d("error", ffmpegErrToStr(ret)));
        return false;
    }

    ret = avcodec_open2(decCtx.get(), dec, NULL);
    if (ret < 0) {
        ACSDK_ERROR(LX("Error opening codec").d("error", ffmpegErrToStr(ret)));
        return false;
    }

    // Allocate a frame for the decoded content
    AVFramePtr decodedFrame(av_frame_alloc());
    if (!decodedFrame) {
        ACSDK_ERROR(LX("decryptSampleAESFailed").d("reason", "Error allocating avFrame"));
        return false;
    }

    AVPacket packet;
    while ((averror = av_read_frame(formatContext.get(), &packet)) >= 0) {
        AVPacketPtr packetPtr(&packet);
        if (packetPtr->stream_index != audioIndex) {
            continue;
        }
        uint8_t* audioFrame = packetPtr->data;
        uint8_t* pFrame = audioFrame;

        if (AV_CODEC_ID_AAC == cid) {
            // ADTS headers can contain CRC checks.
            // If the CRC check bit is 0, CRC exists.
            //
            // Header (7 or 9 byte) + unencrypted leader (16 bytes)
            // Only skip header bytes if this is not fragmented mp4
            if (INVALID_MDAT_LOCATION == mdatLocation) {
                pFrame += (pFrame[1] & 0x01) ? UNENCRYPTED_ADTS_HEADER_BYTES_WITHOUT_CRC
                                             : UNENCRYPTED_ADTS_HEADER_BYTES_WITH_CRC;
            }
        } else {
            ACSDK_WARN(LX("decryptSampleAESFailed").d("reason", "unsupportedCodec").d("cid", cid));
            return false;
        }

        int remaining = bytesRemaining(pFrame, (audioFrame + packetPtr->size));
        if (remaining >= AES_BLOCK_SIZE) {
            int numBlocks = remaining / AES_BLOCK_SIZE;
            int encryptedSize = AES_BLOCK_SIZE * numBlocks;

            ByteVector decryptedBytes;
            auto decryptSize = decryptAES(ByteVector(pFrame, pFrame + encryptedSize), key, iv, &decryptedBytes, false);
            // Confirm encryptedSize == decryptSize
            if (encryptedSize != decryptSize) {
                ACSDK_ERROR(LX("decryptSampleAESFailed")
                                .d("reason", "encryptedSizeNotEqualDecryptedSize")
                                .d("encryptedSize", encryptedSize)
                                .d("decryptSize", decryptSize));
                return false;
            }

            memcpy(pFrame, decryptedBytes.data(), decryptSize);
        }

        // If mdatLocation is invalid, content is not fragmented mp4 and does not need to be decoded.
        if (INVALID_MDAT_LOCATION == mdatLocation) {
            if (!writeToStream(streamWriter, audioFrame, packetPtr->size)) {
                ACSDK_ERROR(LX("decryptSampleAESFailed").d("reason", "writeDecodedFrameToStreamFailed"));
                return false;
            }
            continue;
        }

        // Send packet to be decoded
        ret = avcodec_send_packet(decCtx.get(), packetPtr.get());
        if (ret < 0) {
            ACSDK_ERROR(LX("Error submitting packet to the decoder").d("error", ffmpegErrToStr(ret)));
            return false;
        }

        // Receive all decodedFrames
        while (avcodec_receive_frame(decCtx.get(), decodedFrame.get()) >= 0) {
            ACSDK_DEBUG9(LX("DecodedFrameInfo")
                             .d("totalFrame", decCtx->frame_number)
                             .d("numChannel", decCtx->channels)
                             .d("bitrate", decCtx->bit_rate)
                             .d("bitperRawSample", decCtx->bits_per_raw_sample));

            // Get WAVE Format
            auto format = static_cast<AVSampleFormat>(decodedFrame->format);
            bool isPCM = !(format == AV_SAMPLE_FMT_FLT || format == AV_SAMPLE_FMT_FLTP);

            // Get Number of Bytes per sample
            unsigned int bytesPerSample = av_get_bytes_per_sample(format);

            // If first PlaylistEntry parsed, write wav header to stream.
            if (m_needWavHeader) {
                ACSDK_DEBUG9(LX("writeWavHeader")
                                 .d("bytesPerSample", bytesPerSample)
                                 .d("channels", decodedFrame->channels)
                                 .d("sampleRate", decodedFrame->sample_rate)
                                 .d("totalDuration", m_totalDuration.count())
                                 .d("isPCM", isPCM));

                auto header = avsCommon::utils::generateWavHeader(
                    bytesPerSample, decodedFrame->channels, decodedFrame->sample_rate, m_totalDuration, isPCM);

                if (!writeToStream(streamWriter, header.data(), header.size())) {
                    ACSDK_ERROR(LX("decryptSampleAESFailed").d("reason", "writeWavHeaderToStreamFailed"));
                    return false;
                }
                m_needWavHeader = false;
            }

            // Write multichannel data into single buffer
            ByteVector decodedBuffer;
            decodedBuffer.reserve(decodedFrame->nb_samples * decodedFrame->channels * bytesPerSample);
            for (int i = 0; i < decodedFrame->nb_samples; i++) {
                for (int ch = 0; ch < decodedFrame->channels; ch++) {
                    decodedBuffer.insert(
                        decodedBuffer.end(),
                        decodedFrame->data[ch] + bytesPerSample * i,
                        decodedFrame->data[ch] + bytesPerSample * i + bytesPerSample);
                }
            }

            if (!writeToStream(streamWriter, decodedBuffer.data(), decodedBuffer.size())) {
                ACSDK_ERROR(LX("decryptSampleAESFailed").d("reason", "writeDecodedFrameToStreamFailed"));
                return false;
            }
        }
    }
    return true;
}

int ContentDecrypter::bytesRemaining(uint8_t* pos, uint8_t* end) {
    return static_cast<int>(end - pos);
}

void ContentDecrypter::logAVError(const char* event, const char* reason, int averror) {
    char buffer[AVERROR_BUFFER_SIZE];
    if (0 == av_strerror(averror, buffer, sizeof(buffer))) {
        ACSDK_ERROR(LX(event).d("reason", reason).d("buffer", buffer));
    } else {
        ACSDK_ERROR(LX(event).d("reason", reason).d("averror", averror));
    }
}

#endif  // ENABLE_SAMPLE_AES

bool ContentDecrypter::writeToStream(
    const std::shared_ptr<AttachmentWriter>& streamWriter,
    const unsigned char* buffer,
    size_t size) {
    if (!streamWriter) {
        ACSDK_ERROR(LX("writeToStreamFailed").d("reason", "streamWriterIsNULL"));
        return false;
    }
    if (!buffer) {
        ACSDK_ERROR(LX("writeToStreamFailed").d("reason", "streamWriterIsNULL"));
        return false;
    }

    size_t totalBytesWritten = 0;
    auto writeStatus = AttachmentWriter::WriteStatus::OK;
    while (totalBytesWritten < size && !m_shuttingDown) {
        auto bytesWritten = streamWriter->write(
            buffer + totalBytesWritten, size - totalBytesWritten, &writeStatus, WRITE_TO_STREAM_TIMEOUT);
        totalBytesWritten += bytesWritten;

        switch (writeStatus) {
            case AttachmentWriter::WriteStatus::CLOSED:
                break;
            case AttachmentWriter::WriteStatus::TIMEDOUT:
            case AttachmentWriter::WriteStatus::OK:
                continue;
            case AttachmentWriter::WriteStatus::ERROR_BYTES_LESS_THAN_WORD_SIZE:
            case AttachmentWriter::WriteStatus::ERROR_INTERNAL:
            case AttachmentWriter::WriteStatus::OK_BUFFER_FULL:
                ACSDK_ERROR(LX("writeToStreamFailed")
                                .d("reason", "writeFailed")
                                .d("writeStatus", static_cast<int>(writeStatus)));
                return false;
        }
    }

    ACSDK_DEBUG9(LX("writeToStreamSuccess"));
    return true;
}

bool ContentDecrypter::writeToStream(const ByteVector& content, std::shared_ptr<AttachmentWriter> streamWriter) {
    if (!streamWriter) {
        ACSDK_ERROR(LX("writeToStreamFailed").d("reason", "streamWriterIsNULL"));
        return false;
    }

    return writeToStream(streamWriter, content.data(), content.size());
}

void ContentDecrypter::doShutdown() {
    m_shuttingDown = true;
}

}  // namespace playlistParser
}  // namespace alexaClientSDK
