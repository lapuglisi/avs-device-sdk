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

#include <AVSCommon/Utils/Logger/Logger.h>

#include "AVSCommon/Utils/LibcurlUtils/LibcurlHTTP2Connection.h"
#include "AVSCommon/Utils/LibcurlUtils/LibcurlHTTP2ConnectionFactory.h"

namespace alexaClientSDK {
namespace avsCommon {
namespace utils {
namespace libcurlUtils {

/// String to identify log entries originating from this file.
#define TAG "LibcurlHTTP2ConnectionFactory"

/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param The event string for this @c LogEntry.
 */
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

std::shared_ptr<avsCommon::utils::http2::HTTP2ConnectionFactoryInterface> LibcurlHTTP2ConnectionFactory::
    createHTTP2ConnectionFactoryInterface(
        const std::shared_ptr<LibcurlSetCurlOptionsCallbackFactoryInterface>& curlSetOptionsCallbackFactory) {
    return std::make_shared<LibcurlHTTP2ConnectionFactory>(curlSetOptionsCallbackFactory);
}

LibcurlHTTP2ConnectionFactory::LibcurlHTTP2ConnectionFactory(
    const std::shared_ptr<LibcurlSetCurlOptionsCallbackFactoryInterface>& curlSetOptionsCallbackFactory) :
        m_setCurlOptionsCallbackFactory{curlSetOptionsCallbackFactory} {
}

std::shared_ptr<avsCommon::utils::http2::HTTP2ConnectionInterface> LibcurlHTTP2ConnectionFactory::
    createHTTP2Connection() {
    ACSDK_DEBUG5(LX(__func__));

    std::shared_ptr<LibcurlSetCurlOptionsCallbackInterface> setCurlOptionsCallback;
    if (m_setCurlOptionsCallbackFactory) {
        setCurlOptionsCallback = m_setCurlOptionsCallbackFactory->createSetCurlOptionsCallback();
    }

    auto result = LibcurlHTTP2Connection::create(setCurlOptionsCallback);
    return result;
}

}  // namespace libcurlUtils
}  // namespace utils
}  // namespace avsCommon
}  // namespace alexaClientSDK
