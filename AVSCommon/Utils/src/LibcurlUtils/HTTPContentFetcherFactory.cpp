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

#include <AVSCommon/Utils/LibcurlUtils/HTTPContentFetcherFactory.h>
#include <AVSCommon/Utils/LibcurlUtils/LibCurlHttpContentFetcher.h>
#include <AVSCommon/Utils/Logger/Logger.h>
#include <AVSCommon/Utils/Memory/Memory.h>

namespace alexaClientSDK {
namespace avsCommon {
namespace utils {
namespace libcurlUtils {

/// String to identify log entries originating from this file.
#define TAG "HTTPContentFetcherFactory"

/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param event The event string for this @c LogEntry.
 */
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

std::shared_ptr<avsCommon::sdkInterfaces::HTTPContentFetcherInterfaceFactoryInterface> HTTPContentFetcherFactory::
    createHTTPContentFetcherInterfaceFactoryInterface(
        const std::shared_ptr<LibcurlSetCurlOptionsCallbackFactoryInterface>& setCurlOptionsCallbackFactory) {
    return std::make_shared<HTTPContentFetcherFactory>(setCurlOptionsCallbackFactory);
}

HTTPContentFetcherFactory::HTTPContentFetcherFactory(
    const std::shared_ptr<LibcurlSetCurlOptionsCallbackFactoryInterface>& setCurlOptionsCallbackFactory) :
        m_setCurlOptionsCallbackFactory{setCurlOptionsCallbackFactory} {
}

std::unique_ptr<avsCommon::sdkInterfaces::HTTPContentFetcherInterface> HTTPContentFetcherFactory::create(
    const std::string& url) {
    std::shared_ptr<LibcurlSetCurlOptionsCallbackInterface> setCurlOptionsCallback;
    if (m_setCurlOptionsCallbackFactory) {
        setCurlOptionsCallback = m_setCurlOptionsCallbackFactory->createSetCurlOptionsCallback();
    }

    ACSDK_DEBUG9(LX("create").sensitive("URL", url).m("Creating a new http content fetcher"));
    return avsCommon::utils::memory::make_unique<LibCurlHttpContentFetcher>(url, setCurlOptionsCallback);
}

}  // namespace libcurlUtils
}  // namespace utils
}  // namespace avsCommon
}  // namespace alexaClientSDK
