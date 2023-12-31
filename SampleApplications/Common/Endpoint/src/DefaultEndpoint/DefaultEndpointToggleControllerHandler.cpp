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

#include "acsdk/Sample/Endpoint/DefaultEndpoint/DefaultEndpointToggleControllerHandler.h"

#include <acsdk/Sample/Console/ConsolePrinter.h>
#include <AVSCommon/Utils/Configuration/ConfigurationNode.h>
#include <AVSCommon/Utils/JSON/JSONUtils.h>
#include <AVSCommon/Utils/Logger/Logger.h>
//#include <Settings/SettingStringConversion.h>

/// String to identify log entries originating from this file.
#define TAG "DefaultEndpointToggleControllerHandler"

/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param The event string for this @c LogEntry.
 */
#define LX(event) avsCommon::utils::logger::LogEntry(TAG, event)

namespace alexaClientSDK {
namespace sampleApplications {
namespace common {

using namespace avsCommon::avs;
using namespace avsCommon::sdkInterfaces;
using namespace avsCommon::sdkInterfaces::toggleController;
using namespace sampleApplications::common;

/**
 * Helper function to notify toggle state change to the observers of @c ToggleControllerObserverInterface.
 *
 * @param toggleState The changed toggle state to be notified to the observer.
 * @param cause The change cause represented using @c AlexaStateChangeCauseType.
 * @param observers The list of observer to be notified.
 */
static void notifyObservers(
    const avsCommon::sdkInterfaces::toggleController::ToggleControllerInterface::ToggleState& toggleState,
    avsCommon::sdkInterfaces::AlexaStateChangeCauseType cause,
    const std::list<std::shared_ptr<avsCommon::sdkInterfaces::toggleController::ToggleControllerObserverInterface>>&
        observers) {
    ACSDK_DEBUG5(LX(__func__));
    for (auto& observer : observers) {
        observer->onToggleStateChanged(toggleState, cause);
    }
}

std::shared_ptr<DefaultEndpointToggleControllerHandler> DefaultEndpointToggleControllerHandler::create(
    const std::string& instance) {
    auto toggleControllerHandler =
        std::shared_ptr<DefaultEndpointToggleControllerHandler>(new DefaultEndpointToggleControllerHandler(instance));
    return toggleControllerHandler;
}

DefaultEndpointToggleControllerHandler::DefaultEndpointToggleControllerHandler(const std::string& instance) :
        m_instance{instance},
        m_currentToggleState{false} {
}

std::pair<AlexaResponseType, std::string> DefaultEndpointToggleControllerHandler::setToggleState(
    bool state,
    AlexaStateChangeCauseType cause) {
    std::list<std::shared_ptr<ToggleControllerObserverInterface>> copyOfObservers;
    bool notifyObserver = false;

    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_currentToggleState != state) {
        std::string stateStr = state ? "ON" : "OFF";
        ConsolePrinter::prettyPrint(
            {"ENDPOINT: Default Endpoint", "INSTANCE: " + m_instance, "TOGGLED STATE TO: " + stateStr});

        m_currentToggleState = state;
        copyOfObservers = m_observers;
        notifyObserver = true;
    }

    lock.unlock();

    if (notifyObserver) {
        notifyObservers(
            ToggleControllerInterface::ToggleState{
                m_currentToggleState, avsCommon::utils::timing::TimePoint::now(), std::chrono::milliseconds(0)},
            cause,
            copyOfObservers);
    }

    return std::make_pair(AlexaResponseType::SUCCESS, "");
}

std::pair<AlexaResponseType, avsCommon::utils::Optional<ToggleControllerInterface::ToggleState>>
DefaultEndpointToggleControllerHandler::getToggleState() {
    std::lock_guard<std::mutex> lock{m_mutex};
    return std::make_pair(
        AlexaResponseType::SUCCESS,
        avsCommon::utils::Optional<ToggleControllerInterface::ToggleState>(ToggleControllerInterface::ToggleState{
            m_currentToggleState, avsCommon::utils::timing::TimePoint::now(), std::chrono::milliseconds(0)}));
}

bool DefaultEndpointToggleControllerHandler::addObserver(std::shared_ptr<ToggleControllerObserverInterface> observer) {
    std::lock_guard<std::mutex> lock{m_mutex};
    m_observers.push_back(observer);
    return true;
}

void DefaultEndpointToggleControllerHandler::removeObserver(
    const std::shared_ptr<ToggleControllerObserverInterface>& observer) {
    std::lock_guard<std::mutex> lock{m_mutex};
    m_observers.remove(observer);
}

}  // namespace common
}  // namespace sampleApplications
}  // namespace alexaClientSDK
