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

#ifndef ALEXA_CLIENT_SDK_AVSCOMMON_SDKINTERFACES_INCLUDE_AVSCOMMON_SDKINTERFACES_CALLSTATEOBSERVERINTERFACE_H_
#define ALEXA_CLIENT_SDK_AVSCOMMON_SDKINTERFACES_INCLUDE_AVSCOMMON_SDKINTERFACES_CALLSTATEOBSERVERINTERFACE_H_

#include <iostream>

namespace alexaClientSDK {
namespace avsCommon {
namespace sdkInterfaces {

/**
 * An interface to allow being notified of changes to the state of a call.
 */
class CallStateObserverInterface {
public:
    /// An enumeration representing the state of a call.
    enum class CallState {
        /// The call is connecting.
        CONNECTING,
        /// An incoming call is causing a ringtone to be played.
        INBOUND_RINGING,
        /// The call has successfully connected.
        CALL_CONNECTED,
        /// The call has ended.
        CALL_DISCONNECTED,
        /// No current call state to be relayed to the user.
        NONE
    };

    /// An struct containing call state information
    typedef struct CallStateInfo {
        /// An enum representing the state of a call.
        CallState callState;
        /// The type of the call.
        std::string callType;
        /// Previous sipUserAgent state.
        std::string previousSipUserAgentState;
        /// Current sipUserAgent state.
        std::string currentSipUserAgentState;
        /// Contact name to be displayed.
        std::string displayName;
        /// Information about the endpoint of the contact.
        std::string endpointLabel;
        /// Name of callee for whom incoming call is intended.
        std::string inboundCalleeName;
        /// Textual description of exact call provider type.
        std::string callProviderType;
        /// Call provider image url.
        /// This image url may be null. For the case the provider image is not provided by url,
        /// the application should have local image to display instead of downloading from this url.
        std::string callProviderImageUrl;
        /// Inbound ringtone url.
        std::string inboundRingtoneUrl;
        /// Outbound ringtone url.
        std::string outboundRingbackUrl;
        /// This indicates if it's a drop in call or not
        bool isDropIn;
    } CallStateInfo;

    /**
     * Destructor
     */
    virtual ~CallStateObserverInterface() = default;

    /**
     * Allows the observer to react to a change in call state.
     *
     * @param state The new CallState.
     */
    virtual void onCallStateChange(CallState state) = 0;

    /**
     * Allows the observer to react to a change in call state info.
     *
     * @param stateInfo The new CallStateInfo.
     */
    virtual void onCallStateInfoChange(const CallStateInfo& stateInfo);

    /**
     * Checks the state of the provided call state to determine if a call is in
     * an "active" state
     * Active states are: CONNECTING
     *                    INBOUND_RINGING
     *                    CALL_CONNETED
     *
     * @param state The new CallState.
     * @return True on states that are considered "active", false otherwise.
     */
    static bool isStateActive(const CallStateObserverInterface::CallState& state);
};

/**
 * Write a @c CallState value to an @c ostream as a string.
 *
 * @param stream The stream to write the value to.
 * @param state The @c CallState value to write to the @c ostream as a string.
 * @return The @c ostream that was passed in and written to.
 */
inline std::ostream& operator<<(std::ostream& stream, const CallStateObserverInterface::CallState& state) {
    switch (state) {
        case CallStateObserverInterface::CallState::CONNECTING:
            stream << "CONNECTING";
            return stream;
        case CallStateObserverInterface::CallState::INBOUND_RINGING:
            stream << "INBOUND_RINGING";
            return stream;
        case CallStateObserverInterface::CallState::CALL_CONNECTED:
            stream << "CALL_CONNECTED";
            return stream;
        case CallStateObserverInterface::CallState::CALL_DISCONNECTED:
            stream << "CALL_DISCONNECTED";
            return stream;
        case CallStateObserverInterface::CallState::NONE:
            stream << "NONE";
            return stream;
    }
    return stream << "UNKNOWN STATE";
}

inline bool CallStateObserverInterface::isStateActive(const CallStateObserverInterface::CallState& state) {
    switch (state) {
        case CallStateObserverInterface::CallState::CONNECTING:
        case CallStateObserverInterface::CallState::INBOUND_RINGING:
        case CallStateObserverInterface::CallState::CALL_CONNECTED:
            return true;
        case CallStateObserverInterface::CallState::CALL_DISCONNECTED:
        case CallStateObserverInterface::CallState::NONE:
            return false;
    }
    return false;
}

inline void CallStateObserverInterface::onCallStateInfoChange(
    const CallStateObserverInterface::CallStateInfo& stateInfo) {
    return;
}

}  // namespace sdkInterfaces
}  // namespace avsCommon
}  // namespace alexaClientSDK

#endif  // ALEXA_CLIENT_SDK_AVSCOMMON_SDKINTERFACES_INCLUDE_AVSCOMMON_SDKINTERFACES_CALLSTATEOBSERVERINTERFACE_H_
