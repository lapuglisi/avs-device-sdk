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

#ifndef ALEXA_CLIENT_SDK_AVSCOMMON_AVS_INCLUDE_AVSCOMMON_AVS_CAPABILITYCHANGENOTIFIER_H_
#define ALEXA_CLIENT_SDK_AVSCOMMON_AVS_INCLUDE_AVSCOMMON_AVS_CAPABILITYCHANGENOTIFIER_H_

#include <acsdk/Notifier/Notifier.h>
#include <AVSCommon/SDKInterfaces/CapabilityConfigurationChangeObserverInterface.h>

namespace alexaClientSDK {
namespace avsCommon {
namespace avs {

/**
 * Implementation of CapabilityChangeNotifierInterface
 */
using CapabilityChangeNotifier =
    notifier::Notifier<avsCommon::sdkInterfaces::CapabilityConfigurationChangeObserverInterface>;

}  // namespace avs
}  // namespace avsCommon
}  // namespace alexaClientSDK

#endif  // ALEXA_CLIENT_SDK_AVSCOMMON_AVS_INCLUDE_AVSCOMMON_AVS_CAPABILITYCHANGENOTIFIER_H_
