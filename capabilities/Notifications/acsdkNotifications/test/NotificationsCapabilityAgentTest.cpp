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

#include <chrono>
#include <iostream>
#include <queue>
#include <sstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <acsdkShutdownManagerInterfaces/MockShutdownNotifier.h>
#include <AVSCommon/AVS/Attachment/MockAttachmentManager.h>
#include <AVSCommon/AVS/IndicatorState.h>
#include <AVSCommon/AVS/Initialization/AlexaClientSDKInit.h>
#include <AVSCommon/SDKInterfaces/Audio/NotificationsAudioFactoryInterface.h>
#include <AVSCommon/SDKInterfaces/MockContextManager.h>
#include <AVSCommon/SDKInterfaces/MockDirectiveHandlerResult.h>
#include <AVSCommon/SDKInterfaces/MockDirectiveSequencer.h>
#include <AVSCommon/SDKInterfaces/MockExceptionEncounteredSender.h>
#include <AVSCommon/Utils/JSON/JSONUtils.h>
#include <AVSCommon/Utils/Logger/ConsoleLogger.h>
#include <AVSCommon/Utils/Metrics/MockMetricRecorder.h>
#include <AVSCommon/SDKInterfaces/Endpoints/MockEndpointCapabilitiesRegistrar.h>
#include <RegistrationManager/MockCustomerDataManager.h>

#include "acsdkNotifications/NotificationIndicator.h"
#include "acsdkNotifications/NotificationsCapabilityAgent.h"
#include "acsdkNotifications/NotificationsNotifier.h"
#include <acsdkNotificationsInterfaces/NotificationRendererInterface.h>
#include <acsdkNotificationsInterfaces/NotificationRendererObserverInterface.h>

namespace alexaClientSDK {
namespace acsdkNotifications {
namespace test {

using namespace avsCommon::avs;
using namespace avsCommon::avs::initialization;
using namespace avsCommon::avs::attachment::test;
using namespace avsCommon::sdkInterfaces;
using namespace avsCommon::sdkInterfaces::audio;
using namespace avsCommon::sdkInterfaces::test;
using namespace ::testing;

/// Plenty of time for a test to complete.
static std::chrono::milliseconds MY_WAIT_TIMEOUT(1000);

/// Time to simulate a notification rendering.
static std::chrono::milliseconds RENDER_TIME(10);

/// Notifications namespace
static const std::string NAMESPACE_NOTIFICATIONS("Notifications");

/// Name for Notifications SetIndicator directive
static const std::string NAME_SET_INDICATOR("SetIndicator");

/// Name for Notifications ClearIndicator directive
static const std::string NAME_CLEAR_INDICATOR("ClearIndicator");

/// The @c NamespaceAndName to send to the @c ContextManager
static const NamespaceAndName NAMESPACE_AND_NAME_INDICATOR_STATE{NAMESPACE_NOTIFICATIONS, "IndicatorState"};

/// Message Id for testing.
static const std::string MESSAGE_ID_TEST("MessageId_Test");
static const std::string MESSAGE_ID_TEST2("MessageId_Test2");

/// Context ID for testing
static const std::string CONTEXT_ID_TEST("ContextId_Test");

/// Test fields for payloads
static const std::string ASSET_ID1("assetId1");
static const std::string ASSET_ID2("assetId2");
static const std::string ASSET_URL1("assetUrl1");
static const std::string ASSET_URL2("assetUrl2");

/// Default "audio" for testing
static const std::string DEFAULT_NOTIFICATION_AUDIO{"default notification audio"};

/// Mocking the json config file
// clang-format off
            static const std::string NOTIFICATIONS_CONFIG_JSON =
                    "{"
                    "\"notifications\":{"
                    "\"databaseFilePath\":\"notificationsUnitTest.db\""
                    "}"
                    "}";
// clang-format on

/// String to identify log entries originating from this file.
static const std::string TAG("NotificationsCapabilityAgentTest");

/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param The event string for this @c LogEntry.
 */
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

/**
 * A test class that acts as a NotificationsAudioFactory.
 */
class TestNotificationsAudioFactory : public NotificationsAudioFactoryInterface {
public:
    std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()> notificationDefault()
        const override;

private:
    static std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType> defaultNotification() {
        return std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>(
            std::unique_ptr<std::istream>(new std::stringstream(DEFAULT_NOTIFICATION_AUDIO)),
            avsCommon::utils::MediaType::MPEG);
    }
};

std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()>
TestNotificationsAudioFactory::notificationDefault() const {
    return defaultNotification;
}

/**
 * A test class that acts as a NotificationsObserver.
 */
class TestNotificationsObserver : public acsdkNotificationsInterfaces::NotificationsObserverInterface {
public:
    TestNotificationsObserver();

    /**
     * Waits for an IndicatorState change.
     *
     * @param state The IndicatorState to wait for.
     * @param timeout The amount of time to wait for the state change.
     */
    bool waitFor(IndicatorState state, std::chrono::milliseconds timeout);

    /**
     * Waits for IndicationCount to increase.
     *
     * @param count The number of indicator events to wait for.
     * @param timeout The amount of time to wait for the state change.
     */
    bool waitFor(int count, std::chrono::milliseconds timeout);

    void onSetIndicator(IndicatorState state) override;

    void onNotificationReceived() override;

private:
    /// The most recently observed IndicatorState.
    IndicatorState m_indicatorState;

    /// The number of notifications received.
    int m_indicationCount;

    /// Serializes access to m_conditionVariable.
    std::mutex m_mutex;

    /// Used to wait for a particular IndicatorState.
    std::condition_variable m_conditionVariable;
};

TestNotificationsObserver::TestNotificationsObserver() : m_indicatorState{IndicatorState::OFF}, m_indicationCount{0} {
}

bool TestNotificationsObserver::waitFor(IndicatorState state, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_conditionVariable.wait_for(lock, timeout, [this, state] { return m_indicatorState == state; });
}

bool TestNotificationsObserver::waitFor(int count, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_conditionVariable.wait_for(lock, timeout, [this, count] { return m_indicationCount == count; });
}

void TestNotificationsObserver::onSetIndicator(IndicatorState state) {
    ACSDK_INFO(LX("onSetIndicator").d("indicatorState", indicatorStateToInt(state)));
    std::lock_guard<std::mutex> lock(m_mutex);
    m_indicatorState = state;
    m_conditionVariable.notify_all();
}

void TestNotificationsObserver::onNotificationReceived() {
    ACSDK_INFO(LX("onNotificationReceived"));
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_indicationCount;
    m_conditionVariable.notify_all();
}

/**
 * A test class that acts as NotificationsStorage. This storage is implemented
 * using a std::queue.
 */
class TestNotificationsStorage : public acsdkNotificationsInterfaces::NotificationsStorageInterface {
public:
    bool createDatabase() override;

    bool open() override;

    void close() override;

    bool enqueue(const NotificationIndicator& notificationIndicator) override;

    bool dequeue() override;

    bool peek(NotificationIndicator* notificationIndicator) override;

    bool setIndicatorState(IndicatorState state) override;

    bool getIndicatorState(IndicatorState* state) override;

    bool checkForEmptyQueue(bool* empty) override;

    bool clearNotificationIndicators() override;

    bool getQueueSize(int* size) override;

    /**
     * Waits until the queue is a particular size.
     *
     * @param size The size to wait for.
     * @param timeout How much time to wait before failing.
     */
    bool waitForQueueSizeToBe(size_t size, std::chrono::milliseconds timeout = MY_WAIT_TIMEOUT);

private:
    /// The underlying NotificationIndicator queue.
    std::queue<NotificationIndicator> m_notificationQueue;

    /// The currently stored IndicatorState.
    IndicatorState m_indicatorState;

    /// For waiting on a particular queue size.
    std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
};

bool TestNotificationsStorage::createDatabase() {
    if (!setIndicatorState(IndicatorState::OFF)) {
        ACSDK_ERROR(LX("createTestDatabaseFailed").d("reason", "failed to set default indicator state"));
        return false;
    }
    return true;
}

bool TestNotificationsStorage::open() {
    return true;
}

void TestNotificationsStorage::close() {
    // no-op
}

bool TestNotificationsStorage::enqueue(const NotificationIndicator& notificationIndicator) {
    m_notificationQueue.push(notificationIndicator);
    return true;
}

bool TestNotificationsStorage::dequeue() {
    if (m_notificationQueue.empty()) {
        return false;
    }
    m_notificationQueue.pop();
    return true;
}

bool TestNotificationsStorage::peek(NotificationIndicator* notificationIndicator) {
    if (m_notificationQueue.empty() || !notificationIndicator) {
        return false;
    }
    *notificationIndicator = m_notificationQueue.front();
    return true;
}

bool TestNotificationsStorage::setIndicatorState(IndicatorState state) {
    m_indicatorState = state;
    return true;
}

bool TestNotificationsStorage::getIndicatorState(IndicatorState* state) {
    if (!state) {
        return false;
    }
    *state = m_indicatorState;
    return true;
}

bool TestNotificationsStorage::checkForEmptyQueue(bool* empty) {
    *empty = m_notificationQueue.empty();
    return true;
}

bool TestNotificationsStorage::clearNotificationIndicators() {
    m_notificationQueue = std::queue<NotificationIndicator>();
    return true;
}

bool TestNotificationsStorage::getQueueSize(int* size) {
    if (!size) {
        return false;
    }
    *size = m_notificationQueue.size();
    return true;
}

bool TestNotificationsStorage::waitForQueueSizeToBe(size_t size, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_conditionVariable.wait_for(lock, timeout, [this, size] { return m_notificationQueue.size() == size; });
}

class StubAudioFactory : public avsCommon::sdkInterfaces::audio::AudioFactoryInterface {
public:
    StubAudioFactory(const std::shared_ptr<avsCommon::sdkInterfaces::audio::NotificationsAudioFactoryInterface>&
                         notificationsAudioFactory);

    std::shared_ptr<avsCommon::sdkInterfaces::audio::AlertsAudioFactoryInterface> alerts() const override;
    std::shared_ptr<avsCommon::sdkInterfaces::audio::NotificationsAudioFactoryInterface> notifications() const override;
    std::shared_ptr<avsCommon::sdkInterfaces::audio::CommunicationsAudioFactoryInterface> communications()
        const override;
    std::shared_ptr<avsCommon::sdkInterfaces::audio::SystemSoundAudioFactoryInterface> systemSounds() const override;

private:
    std::shared_ptr<avsCommon::sdkInterfaces::audio::NotificationsAudioFactoryInterface> m_notificationsFactory;
};

StubAudioFactory::StubAudioFactory(
    const std::shared_ptr<avsCommon::sdkInterfaces::audio::NotificationsAudioFactoryInterface>&
        notificationsAudioFactory) :
        m_notificationsFactory{notificationsAudioFactory} {};

std::shared_ptr<avsCommon::sdkInterfaces::audio::AlertsAudioFactoryInterface> StubAudioFactory::alerts() const {
    return nullptr;
}
std::shared_ptr<avsCommon::sdkInterfaces::audio::NotificationsAudioFactoryInterface> StubAudioFactory::notifications()
    const {
    return m_notificationsFactory;
}
std::shared_ptr<avsCommon::sdkInterfaces::audio::CommunicationsAudioFactoryInterface> StubAudioFactory::communications()
    const {
    return nullptr;
}
std::shared_ptr<avsCommon::sdkInterfaces::audio::SystemSoundAudioFactoryInterface> StubAudioFactory::systemSounds()
    const {
    return nullptr;
}

class MockNotificationRenderer : public acsdkNotificationsInterfaces::NotificationRendererInterface {
public:
    ~MockNotificationRenderer();

    MockNotificationRenderer();

    static std::shared_ptr<NiceMock<MockNotificationRenderer>> create() {
        auto renderer = std::make_shared<NiceMock<MockNotificationRenderer>>();

        // tie the mock methods to mock implementations
        ON_CALL(*renderer.get(), renderNotificationShim(_, _))
            .WillByDefault(Invoke(renderer.get(), &MockNotificationRenderer::mockRender));
        ON_CALL(*renderer.get(), cancelNotificationRenderingShim())
            .WillByDefault(InvokeWithoutArgs(renderer.get(), &MockNotificationRenderer::mockCancel));

        return renderer;
    }

    void addObserver(
        std::shared_ptr<acsdkNotificationsInterfaces::NotificationRendererObserverInterface> observer) override;

    void removeObserver(
        std::shared_ptr<acsdkNotificationsInterfaces::NotificationRendererObserverInterface> observer) override;

    bool renderNotification(
        std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()> audioFactory,
        const std::string& url) override;

    bool cancelNotificationRendering() override;

    // create shim methods to prevent compiler from complaining
    MOCK_METHOD2(
        renderNotificationShim,
        bool(
            std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()> audioFactory,
            const std::string& url));
    MOCK_METHOD0(cancelNotificationRenderingShim, bool());

    /**
     * A method mocking renderNotification().
     * This method kicks off two threads (for waitForRenderCall() and
     * waitForRenderCallDone()) and then notifies those threads. This method may
     * be interrupted by mockCancel().
     *
     * Both params are ignored in this mock implementation.
     */
    bool mockRender(
        std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()> audioFactory,
        const std::string& url);

    /**
     * A method mocking cancelRenderingNotification().
     * This method attempts to sneak in between waitForRenderCall() and
     * waitForRenderCallDone() by triggering m_renderTrigger before
     * m_finishedRendering has been set to true;
     */
    bool mockCancel();

    /**
     * Waits for mockRender() to set m_startedRendering to true.
     */
    bool waitForRenderCall();

    /**
     * Waits for mockRender() to set m_finishedRendering to true.
     */
    bool waitForRenderCallDone();

    /**
     * Waits for the fulfillment of m_renderStartedPromise, then resets any needed
     * variables.
     */
    bool waitUntilRenderingStarted(std::chrono::milliseconds timeout = MY_WAIT_TIMEOUT);

    /**
     * Waits for the fulfillment of m_renderFinishedPromise, then resets any
     * needed variables.
     */
    bool waitUntilRenderingFinished(std::chrono::milliseconds timeout = MY_WAIT_TIMEOUT);

private:
    /// The current renderer observer.
    std::shared_ptr<acsdkNotificationsInterfaces::NotificationRendererObserverInterface> m_observer;

    /// Threads that are kicked off by mockRender() to wait for the rendering to
    /// begin and end, respectively.
    std::thread m_renderStartedThread;
    std::thread m_renderFinishedThread;

    /// Used to notify the above threads.
    std::condition_variable m_renderTrigger;

    /// Flags for the state of the rendering operation.
    bool m_startedRendering;
    bool m_finishedRendering;
    bool m_cancelling;

    /// Promise to be fulfilled when @c renderNotification is called.
    std::promise<void> m_renderStartedPromise;

    /// Future to notify when @c renderNotification is called.
    std::future<void> m_renderStartedFuture;

    /// Promise to be fulfilled when @c renderNotification is done.
    std::promise<void> m_renderFinishedPromise;

    /// Future to notify when @c renderNotification is done.
    std::future<void> m_renderFinishedFuture;

    /// Serializes access to m_renderTrigger;
    std::mutex m_mutex;
};

MockNotificationRenderer::~MockNotificationRenderer() {
    if (m_renderStartedThread.joinable()) {
        m_renderStartedThread.join();
    }
    if (m_renderFinishedThread.joinable()) {
        m_renderFinishedThread.join();
    }
}

MockNotificationRenderer::MockNotificationRenderer() :
        m_observer{nullptr},
        m_startedRendering{false},
        m_finishedRendering{false},
        m_cancelling{false},
        m_renderStartedPromise{},
        m_renderStartedFuture{m_renderStartedPromise.get_future()},
        m_renderFinishedPromise{},
        m_renderFinishedFuture{m_renderFinishedPromise.get_future()} {
}

void MockNotificationRenderer::addObserver(
    std::shared_ptr<acsdkNotificationsInterfaces::NotificationRendererObserverInterface> observer) {
    if (observer) {
        m_observer = observer;
    }
}

void MockNotificationRenderer::removeObserver(
    std::shared_ptr<acsdkNotificationsInterfaces::NotificationRendererObserverInterface> observer) {
    m_observer = nullptr;
}

bool MockNotificationRenderer::renderNotification(
    std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()> audioFactory,
    const std::string& url) {
    return renderNotificationShim(audioFactory, url);
}

bool MockNotificationRenderer::cancelNotificationRendering() {
    return cancelNotificationRenderingShim();
}

bool MockNotificationRenderer::mockRender(
    std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()> audioFactory,
    const std::string& url) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_renderStartedThread.joinable() && m_renderFinishedThread.joinable()) {
        m_renderStartedThread.join();
        m_renderFinishedThread.join();
    }

    m_renderStartedThread = std::thread(&MockNotificationRenderer::waitForRenderCall, this);
    m_renderFinishedThread = std::thread(&MockNotificationRenderer::waitForRenderCallDone, this);

    m_startedRendering = true;
    m_renderTrigger.notify_all();

    std::this_thread::sleep_for(RENDER_TIME);

    m_finishedRendering = true;
    m_renderTrigger.notify_all();

    return true;
}

bool MockNotificationRenderer::mockCancel() {
    m_cancelling = true;
    m_renderTrigger.notify_all();
    return true;
}

bool MockNotificationRenderer::waitForRenderCall() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_renderTrigger.wait_for(lock, MY_WAIT_TIMEOUT, [this]() { return m_startedRendering; });
    m_renderStartedPromise.set_value();
    return true;
}

bool MockNotificationRenderer::waitForRenderCallDone() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_renderTrigger.wait_for(lock, MY_WAIT_TIMEOUT, [this]() { return m_cancelling || m_finishedRendering; });
    m_renderFinishedPromise.set_value();
    return true;
}

bool MockNotificationRenderer::waitUntilRenderingStarted(std::chrono::milliseconds timeout) {
    if (m_renderStartedFuture.wait_for(timeout) == std::future_status::ready) {
        m_startedRendering = false;
        m_renderStartedPromise = std::promise<void>();
        m_renderStartedFuture = m_renderStartedPromise.get_future();
        return true;
    }
    return false;
}

bool MockNotificationRenderer::waitUntilRenderingFinished(std::chrono::milliseconds timeout) {
    if (m_renderFinishedFuture.wait_for(timeout) == std::future_status::ready) {
        m_finishedRendering = false;
        m_renderFinishedPromise = std::promise<void>();
        m_renderFinishedFuture = m_renderFinishedPromise.get_future();
        m_observer->onNotificationRenderingFinished();
        return true;
    }
    return false;
}

class NotificationsCapabilityAgentTest : public ::testing::Test {
public:
    void SetUp() override;
    void TearDown() override;

    /**
     * Utility function to create the capability agent. This allows modifying of
     * subcomponents before the CA is created.
     */
    void initializeCapabilityAgent();

    /**
     * Utility function to send SetIndicatorDirective
     */
    void sendSetIndicatorDirective(const std::string& payload, const std::string& messageId);

    /**
     * Utility function to send ClearIndicatorDirective
     */
    void sendClearIndicatorDirective(const std::string& messageId);

    /**
     * Utility function to generate direcive payloads
     */
    const std::string generatePayload(
        bool persistVisualIndicator,
        bool playAudioIndicator,
        const std::string& assetId = ASSET_ID1,
        const std::string& assetUrl = ASSET_URL1);

    /// The metric recorder.
    std::shared_ptr<avsCommon::utils::metrics::MetricRecorderInterface> m_metricRecorder;

    /// @c A test observer to wait for @c AudioPlayer state changes
    std::shared_ptr<TestNotificationsObserver> m_testNotificationsObserver;

    /// @c NotificationsCapabilityAgent to test
    std::shared_ptr<NotificationsCapabilityAgent> m_notificationsCapabilityAgent;

    /// @c Storage for notifications
    std::shared_ptr<TestNotificationsStorage> m_notificationsStorage;

    /// Player to play notification audio assets
    std::shared_ptr<MockNotificationRenderer> m_renderer;

    /// @c ContextManager to provide state and update state.
    std::shared_ptr<MockContextManager> m_mockContextManager;

    /// A directive handler result to send the result to.
    std::unique_ptr<MockDirectiveHandlerResult> m_mockDirectiveHandlerResult;

    /// An exception sender used to send exception encountered events to AVS.
    std::shared_ptr<MockExceptionEncounteredSender> m_mockExceptionSender;

    /// A audio factory for testing.
    std::shared_ptr<StubAudioFactory> m_stubAudioFactory;

    /// A notifications audio factory for testing.
    std::shared_ptr<TestNotificationsAudioFactory> m_testNotificationsAudioFactory;

    /// An endpoint capabilities registrar with which to register the Bluetooth CA.
    acsdkManufactory::Annotated<
        avsCommon::sdkInterfaces::endpoints::DefaultEndpointAnnotation,
        avsCommon::sdkInterfaces::endpoints::EndpointCapabilitiesRegistrarInterface>
        m_mockEndpointCapabilitiesRegistrar;

    /// Mock ShutdownNotifier.
    std::shared_ptr<acsdkShutdownManagerInterfaces::test::MockShutdownNotifier> m_shutdownNotifier;

    /// Serializes access to m_setIndicatorTrigger.
    std::mutex m_mutex;

    /// Triggers threads waiting on a SetIndicator directive to be processed.
    std::condition_variable m_setIndicatorTrigger;

    /// Shared pointer to @c CustomerDataManager
    std::shared_ptr<NiceMock<registrationManager::MockCustomerDataManager>> m_dataManager;

    /// A count of how many SetIndicator directives have been processed.
    unsigned int m_numSetIndicatorsProcessed;
};

void NotificationsCapabilityAgentTest::initializeCapabilityAgent() {
    EXPECT_CALL(*(m_shutdownNotifier.get()), addObserver(_));
    m_notificationsCapabilityAgent = NotificationsCapabilityAgent::createNotificationsCapabilityAgent(
        m_notificationsStorage,
        m_renderer,
        m_mockContextManager,
        m_mockExceptionSender,
        m_stubAudioFactory,
        m_dataManager,
        m_shutdownNotifier,
        m_mockEndpointCapabilitiesRegistrar,
        m_metricRecorder);
    ASSERT_TRUE(m_notificationsCapabilityAgent);

    auto notifier = m_notificationsCapabilityAgent->getNotificationsNotifierInterface();
    ASSERT_TRUE(notifier);

    notifier->addObserver(m_testNotificationsObserver);
}

void NotificationsCapabilityAgentTest::SetUp() {
    auto inString = std::shared_ptr<std::istringstream>(new std::istringstream(NOTIFICATIONS_CONFIG_JSON));
    ASSERT_TRUE(AlexaClientSDKInit::initialize({inString}));
    m_metricRecorder = std::make_shared<NiceMock<avsCommon::utils::metrics::test::MockMetricRecorder>>();
    m_notificationsStorage = std::make_shared<TestNotificationsStorage>();
    m_renderer = MockNotificationRenderer::create();
    m_mockContextManager = std::make_shared<NiceMock<MockContextManager>>();
    m_mockExceptionSender = std::make_shared<NiceMock<MockExceptionEncounteredSender>>();
    m_testNotificationsAudioFactory = std::make_shared<TestNotificationsAudioFactory>();
    m_stubAudioFactory = std::make_shared<StubAudioFactory>(m_testNotificationsAudioFactory);
    auto registrar =
        std::make_shared<NiceMock<avsCommon::sdkInterfaces::endpoints::test::MockEndpointCapabilitiesRegistrar>>();
    m_mockEndpointCapabilitiesRegistrar = acsdkManufactory::Annotated<
        avsCommon::sdkInterfaces::endpoints::DefaultEndpointAnnotation,
        avsCommon::sdkInterfaces::endpoints::EndpointCapabilitiesRegistrarInterface>(registrar);
    EXPECT_CALL(
        *(registrar.get()),
        withCapability(A<const std::shared_ptr<avsCommon::sdkInterfaces::CapabilityConfigurationInterface>&>(), _))
        .WillRepeatedly(ReturnRef(
            *(std::make_shared<avsCommon::sdkInterfaces::endpoints::test::MockEndpointCapabilitiesRegistrar>()).get()));

    m_shutdownNotifier = std::make_shared<StrictMock<acsdkShutdownManagerInterfaces::test::MockShutdownNotifier>>();
    m_testNotificationsObserver = std::make_shared<TestNotificationsObserver>();
    m_mockDirectiveHandlerResult = std::unique_ptr<MockDirectiveHandlerResult>(new MockDirectiveHandlerResult);

    m_numSetIndicatorsProcessed = 0;
    m_dataManager = std::make_shared<NiceMock<registrationManager::MockCustomerDataManager>>();
}

void NotificationsCapabilityAgentTest::TearDown() {
    if (m_notificationsCapabilityAgent) {
        m_notificationsCapabilityAgent->shutdown();
    }

    AlexaClientSDKInit::uninitialize();
}

void NotificationsCapabilityAgentTest::sendSetIndicatorDirective(
    const std::string& payload,
    const std::string& messageId) {
    auto avsMessageHeader = std::make_shared<AVSMessageHeader>(NAMESPACE_NOTIFICATIONS, NAME_SET_INDICATOR, messageId);

    auto mockAttachmentManager = std::make_shared<MockAttachmentManager>();

    std::shared_ptr<AVSDirective> setIndicatorDirective =
        AVSDirective::create("", avsMessageHeader, payload, mockAttachmentManager, CONTEXT_ID_TEST);

    // cast to DirectiveHandlerInterface so the compiler can find the correct
    // preHandleDirective signature
    std::shared_ptr<DirectiveHandlerInterface> agentAsDirectiveHandler = m_notificationsCapabilityAgent;

    agentAsDirectiveHandler->preHandleDirective(setIndicatorDirective, std::move(m_mockDirectiveHandlerResult));
    agentAsDirectiveHandler->handleDirective(messageId);

    m_numSetIndicatorsProcessed++;
    m_setIndicatorTrigger.notify_all();
}

void NotificationsCapabilityAgentTest::sendClearIndicatorDirective(const std::string& messageId) {
    auto avsMessageHeader =
        std::make_shared<AVSMessageHeader>(NAMESPACE_NOTIFICATIONS, NAME_CLEAR_INDICATOR, messageId);

    auto mockAttachmentManager = std::make_shared<MockAttachmentManager>();

    std::shared_ptr<AVSDirective> clearIndicatorDirective =
        AVSDirective::create("", avsMessageHeader, "", mockAttachmentManager, CONTEXT_ID_TEST);

    // cast to DirectiveHandlerInterface so the compiler can find the correct
    // preHandleDirective signature
    std::shared_ptr<DirectiveHandlerInterface> agentAsDirectiveHandler = m_notificationsCapabilityAgent;

    agentAsDirectiveHandler->preHandleDirective(clearIndicatorDirective, std::move(m_mockDirectiveHandlerResult));
    agentAsDirectiveHandler->handleDirective(messageId);
}

const std::string NotificationsCapabilityAgentTest::generatePayload(
    bool persistVisualIndicator,
    bool playAudioIndicator,
    const std::string& assetId,
    const std::string& assetUrl) {
    std::string stringPersistVisualIndicator(persistVisualIndicator ? "true" : "false");
    std::string stringPlayAudioIndicator(playAudioIndicator ? "true" : "false");

    // clang-format off
    const std::string payload =
            "{"
            "\"persistVisualIndicator\":" + stringPersistVisualIndicator + ","
                                                                           "\"playAudioIndicator\":" + stringPlayAudioIndicator + ","
                                                                                                                                  "\"asset\": {"
                                                                                                                                  "\"assetId\":\"" + assetId + "\","
                                                                                                                                                               "\"url\":\"" + assetUrl + "\""

                                                                                                                                                                                         "}"
                                                                                                                                                                                         "}";
    // clang-format on
    return payload;
}

/**
 * Test createNotificationsCapabilityAgent() with nullptrs
 */
TEST_F(NotificationsCapabilityAgentTest, test_createNotificationsCapabilityAgent) {
    std::shared_ptr<acsdkNotifications::NotificationsCapabilityAgent> testNotificationsCapabilityAgent;

    testNotificationsCapabilityAgent = NotificationsCapabilityAgent::createNotificationsCapabilityAgent(
        nullptr,
        m_renderer,
        m_mockContextManager,
        m_mockExceptionSender,
        m_stubAudioFactory,
        m_dataManager,
        m_shutdownNotifier,
        m_mockEndpointCapabilitiesRegistrar,
        m_metricRecorder);
    EXPECT_EQ(testNotificationsCapabilityAgent, nullptr);

    testNotificationsCapabilityAgent = NotificationsCapabilityAgent::createNotificationsCapabilityAgent(
        m_notificationsStorage,
        nullptr,
        m_mockContextManager,
        m_mockExceptionSender,
        m_stubAudioFactory,
        m_dataManager,
        m_shutdownNotifier,
        m_mockEndpointCapabilitiesRegistrar,
        m_metricRecorder);
    EXPECT_EQ(testNotificationsCapabilityAgent, nullptr);

    testNotificationsCapabilityAgent = NotificationsCapabilityAgent::createNotificationsCapabilityAgent(
        m_notificationsStorage,
        m_renderer,
        nullptr,
        m_mockExceptionSender,
        m_stubAudioFactory,
        m_dataManager,
        m_shutdownNotifier,
        m_mockEndpointCapabilitiesRegistrar,
        m_metricRecorder);
    EXPECT_EQ(testNotificationsCapabilityAgent, nullptr);

    testNotificationsCapabilityAgent = NotificationsCapabilityAgent::createNotificationsCapabilityAgent(
        m_notificationsStorage,
        m_renderer,
        m_mockContextManager,
        nullptr,
        m_stubAudioFactory,
        m_dataManager,
        m_shutdownNotifier,
        m_mockEndpointCapabilitiesRegistrar,
        m_metricRecorder);
    EXPECT_EQ(testNotificationsCapabilityAgent, nullptr);

    testNotificationsCapabilityAgent = NotificationsCapabilityAgent::createNotificationsCapabilityAgent(
        m_notificationsStorage,
        m_renderer,
        m_mockContextManager,
        m_mockExceptionSender,
        nullptr,
        m_dataManager,
        m_shutdownNotifier,
        m_mockEndpointCapabilitiesRegistrar,
        m_metricRecorder);
    EXPECT_EQ(testNotificationsCapabilityAgent, nullptr);

    testNotificationsCapabilityAgent = NotificationsCapabilityAgent::createNotificationsCapabilityAgent(
        m_notificationsStorage,
        m_renderer,
        m_mockContextManager,
        m_mockExceptionSender,
        m_stubAudioFactory,
        nullptr,
        m_shutdownNotifier,
        m_mockEndpointCapabilitiesRegistrar,
        m_metricRecorder);
    EXPECT_EQ(testNotificationsCapabilityAgent, nullptr);

    testNotificationsCapabilityAgent = NotificationsCapabilityAgent::createNotificationsCapabilityAgent(
        m_notificationsStorage,
        m_renderer,
        m_mockContextManager,
        m_mockExceptionSender,
        m_stubAudioFactory,
        m_dataManager,
        nullptr,
        m_mockEndpointCapabilitiesRegistrar,
        m_metricRecorder);
    EXPECT_EQ(testNotificationsCapabilityAgent, nullptr);

    testNotificationsCapabilityAgent = NotificationsCapabilityAgent::createNotificationsCapabilityAgent(
        m_notificationsStorage,
        m_renderer,
        m_mockContextManager,
        m_mockExceptionSender,
        m_stubAudioFactory,
        m_dataManager,
        m_shutdownNotifier,
        nullptr,
        m_metricRecorder);
    EXPECT_EQ(testNotificationsCapabilityAgent, nullptr);
}

/**
 * Test starting up the capability agent with a non-empty queue.
 * Expect that the next item in the queue will be played.
 */
TEST_F(NotificationsCapabilityAgentTest, test_nonEmptyStartupQueue) {
    NotificationIndicator ni(true, true, ASSET_ID1, ASSET_URL1);
    ASSERT_TRUE(m_notificationsStorage->enqueue(ni));

    EXPECT_CALL(*(m_renderer.get()), renderNotificationShim(_, ASSET_URL1)).Times(1);
    initializeCapabilityAgent();

    ASSERT_TRUE(m_renderer->waitUntilRenderingFinished());
}

/**
 * Test a single SetIndicator directive with persistVisualIndicator and
 * playAudioIndicator set to false. Expect that the NotificationsObserver is
 * notified of the indicator's state remaining OFF. Expect no calls to render
 * notifications since playAudioIndicator is false.
 */
TEST_F(NotificationsCapabilityAgentTest, test_sendSetIndicator) {
    EXPECT_CALL(*(m_renderer.get()), renderNotificationShim(_, _)).Times(0);
    initializeCapabilityAgent();
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(IndicatorState::OFF, MY_WAIT_TIMEOUT));

    sendSetIndicatorDirective(generatePayload(true, false), MESSAGE_ID_TEST);
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(IndicatorState::ON, MY_WAIT_TIMEOUT));

    // check that the NotificationIndicator was dequeued as expected
    ASSERT_TRUE(m_notificationsStorage->waitForQueueSizeToBe(0));
}

/**
 * Test that duplicate SetIndicator directive with persistVisualIndicator and
 * playAudioIndicator set to various values. Expect that the
 * NotificationsObserver is notified of the indicator's count on initialization.
 * Expect all calls to increase notification count.
 */
TEST_F(NotificationsCapabilityAgentTest, test_sendSetIndicatorIncreasesCount) {
    initializeCapabilityAgent();
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(0, MY_WAIT_TIMEOUT));

    sendSetIndicatorDirective(generatePayload(true, false), MESSAGE_ID_TEST);
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(1, MY_WAIT_TIMEOUT));

    // A duplicate indication should trigger an increase in indicator count
    sendSetIndicatorDirective(generatePayload(true, false), MESSAGE_ID_TEST);
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(2, MY_WAIT_TIMEOUT));

    sendSetIndicatorDirective(generatePayload(true, true), MESSAGE_ID_TEST);
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(3, MY_WAIT_TIMEOUT));
}

/**
 * Test that the indication count is preserved across shutdown.
 */
TEST_F(NotificationsCapabilityAgentTest, test_persistVisualIndicatorPreservedIncreasesCount) {
    initializeCapabilityAgent();

    sendSetIndicatorDirective(generatePayload(true, false, ASSET_ID1), MESSAGE_ID_TEST);
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(1, MY_WAIT_TIMEOUT));

    m_notificationsCapabilityAgent->shutdown();

    // reboot and check that the indicator count value is preserved
    initializeCapabilityAgent();
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(1, MY_WAIT_TIMEOUT));
}

/**
 * Test a single SetIndicator directive with with playAudioIndicator set to
 * true. Expect the renderer to start playback of the Notification. Expect that
 * the NotificationsObserver is notified of the indicator's state being OFF.
 */
TEST_F(NotificationsCapabilityAgentTest, test_sendSetIndicatorWithAudio) {
    EXPECT_CALL(*(m_renderer.get()), renderNotificationShim(_, ASSET_URL1));
    initializeCapabilityAgent();

    sendSetIndicatorDirective(generatePayload(false, true, ASSET_ID1, ASSET_URL1), MESSAGE_ID_TEST);
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(IndicatorState::OFF, MY_WAIT_TIMEOUT));

    ASSERT_TRUE(m_renderer->waitUntilRenderingFinished());
}

/**
 * Test a single SetIndicator directive with with persistVisualIndicator set to
 * true. Expect that the NotificationsObserver is notified of the indicator's
 * state being ON.
 */
TEST_F(NotificationsCapabilityAgentTest, test_sendSetIndicatorWithVisualIndicator) {
    EXPECT_CALL(*(m_renderer.get()), renderNotificationShim(_, _)).Times(0);
    initializeCapabilityAgent();

    sendSetIndicatorDirective(generatePayload(true, false), MESSAGE_ID_TEST);
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(IndicatorState::ON, MY_WAIT_TIMEOUT));
}

/**
 * Test sending two SetIndicator directives where the second has the same
 * assetId as the first. Expect that the renderer only gets one call to
 * renderNotification().
 */
TEST_F(NotificationsCapabilityAgentTest, test_sameAssetId) {
    EXPECT_CALL(*(m_renderer.get()), renderNotificationShim(_, ASSET_URL1))
        .Times(1)
        .WillOnce(
            Invoke([this](
                       std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()>
                           audioFactory,
                       const std::string& url) {
                unsigned int expectedNumSetIndicators = 2;

                std::unique_lock<std::mutex> lock(m_mutex);
                if (!m_setIndicatorTrigger.wait_for(lock, MY_WAIT_TIMEOUT, [this, expectedNumSetIndicators]() {
                        return m_numSetIndicatorsProcessed == expectedNumSetIndicators;
                    })) {
                    return false;
                }
                m_renderer->mockRender(audioFactory, url);
                EXPECT_TRUE(m_renderer->waitUntilRenderingStarted());
                return true;
            }));
    initializeCapabilityAgent();

    sendSetIndicatorDirective(generatePayload(true, true, ASSET_ID1), MESSAGE_ID_TEST);

    // send a second SetIndicator with the same assetId but persistVisualIndicator
    // set to false.
    sendSetIndicatorDirective(generatePayload(false, true, ASSET_ID1), MESSAGE_ID_TEST2);

    // the IndicatorState should not have changed since the second directive
    // should have been ignored.
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(IndicatorState::ON, MY_WAIT_TIMEOUT));
}

/**
 * Test that the persistVisualIndicator setting is preserved and used across
 * shutdown.
 */
TEST_F(NotificationsCapabilityAgentTest, test_persistVisualIndicatorPreserved) {
    initializeCapabilityAgent();

    // set IndicatorState to ON
    sendSetIndicatorDirective(generatePayload(true, false, ASSET_ID1), MESSAGE_ID_TEST);
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(IndicatorState::ON, MY_WAIT_TIMEOUT));

    m_notificationsCapabilityAgent->shutdown();

    // reboot and check that the persistVisualIndicator value has been preserved
    initializeCapabilityAgent();
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(IndicatorState::ON, MY_WAIT_TIMEOUT));

    // same test but with IndicatorState set to OFF
    sendSetIndicatorDirective(generatePayload(false, false, ASSET_ID1), MESSAGE_ID_TEST);
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(IndicatorState::OFF, MY_WAIT_TIMEOUT));

    m_notificationsCapabilityAgent->shutdown();

    initializeCapabilityAgent();
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(IndicatorState::OFF, MY_WAIT_TIMEOUT));
}

/**
 * Test sending a ClearIndicator directive with an empty queue, expecting
 * nothing to happen.
 */
TEST_F(NotificationsCapabilityAgentTest, test_clearIndicatorWithEmptyQueue) {
    initializeCapabilityAgent();
    sendClearIndicatorDirective(MESSAGE_ID_TEST);
    ASSERT_TRUE(m_testNotificationsObserver->waitFor(IndicatorState::OFF, MY_WAIT_TIMEOUT));
}

/**
 * Test sending a ClearIndicator directive with an empty queue and the indicator
 * state set to ON. Expect that the indicator is set to OFF.
 */
TEST_F(NotificationsCapabilityAgentTest, test_clearIndicatorWithEmptyQueueAndIndicatorOn) {
    EXPECT_CALL(*(m_renderer.get()), renderNotificationShim(_, ASSET_URL1)).Times(1);
    initializeCapabilityAgent();

    sendSetIndicatorDirective(generatePayload(true, true, ASSET_ID1), MESSAGE_ID_TEST);

    ASSERT_TRUE(m_renderer->waitUntilRenderingFinished());

    ASSERT_TRUE(m_testNotificationsObserver->waitFor(IndicatorState::ON, MY_WAIT_TIMEOUT));

    sendClearIndicatorDirective(MESSAGE_ID_TEST2);

    ASSERT_TRUE(m_testNotificationsObserver->waitFor(IndicatorState::OFF, MY_WAIT_TIMEOUT));
}

/**
 * Test sending a ClearIndicator directive after multiple SetIndicator
 * directives. Expect that the indicator is set to OFF.
 */
TEST_F(NotificationsCapabilityAgentTest, testSlow_clearIndicatorAfterMultipleSetIndicators) {
    EXPECT_CALL(*(m_renderer.get()), renderNotificationShim(_, ASSET_URL1)).Times(1);
    EXPECT_CALL(*(m_renderer.get()), cancelNotificationRenderingShim()).Times(1);
    initializeCapabilityAgent();

    sendSetIndicatorDirective(generatePayload(true, true, "assetId1"), "firstIndicatorMessageId");
    sendSetIndicatorDirective(generatePayload(true, true, "assetId2"), "secondIndicatorMessageId");
    sendSetIndicatorDirective(generatePayload(true, true, "assetId3"), "thirdIndicatorMessageId");

    ASSERT_TRUE(m_renderer->waitUntilRenderingStarted());
    sendClearIndicatorDirective(MESSAGE_ID_TEST);

    // the renderer still calls onNotificationRenderingFinished() when a
    // notification has been cancelled
    ASSERT_TRUE(m_renderer->waitUntilRenderingFinished());

    ASSERT_TRUE(m_notificationsStorage->waitForQueueSizeToBe(0));
}

/**
 * Test sending multiple SetIndicators and letting them all render.
 * Expect multiple calls to renderNotification().
 */
TEST_F(NotificationsCapabilityAgentTest, test_multipleSetIndicators) {
    EXPECT_CALL(*(m_renderer.get()), renderNotificationShim(_, ASSET_URL1)).Times(3);
    initializeCapabilityAgent();

    sendSetIndicatorDirective(generatePayload(true, true, "id1"), "firstIndicatorMessageId");
    sendSetIndicatorDirective(generatePayload(true, true, "id2"), "secondIndicatorMessageId");
    sendSetIndicatorDirective(generatePayload(true, true, "id3"), "thirdIndicatorMessageId");

    ASSERT_TRUE(m_renderer->waitUntilRenderingStarted());
    ASSERT_TRUE(m_renderer->waitUntilRenderingFinished());

    ASSERT_TRUE(m_renderer->waitUntilRenderingStarted());
    ASSERT_TRUE(m_renderer->waitUntilRenderingFinished());

    ASSERT_TRUE(m_renderer->waitUntilRenderingStarted());
    ASSERT_TRUE(m_renderer->waitUntilRenderingFinished());
}

/**
 * Test that @c clearData() removes all notifications and sets the indicator to
 * OFF.
 */
TEST_F(NotificationsCapabilityAgentTest, test_clearData) {
    initializeCapabilityAgent();
    sendSetIndicatorDirective(generatePayload(true, true, "assetId1"), "firstIndicatorMessageId");
    ASSERT_TRUE(m_renderer->waitUntilRenderingStarted());

    // Check that indicator is ON
    IndicatorState state = IndicatorState::UNDEFINED;
    m_notificationsStorage->getIndicatorState(&state);
    ASSERT_EQ(state, IndicatorState::ON);

    // Check that the notification queue is not empty
    int queueSize;
    m_notificationsStorage->getQueueSize(&queueSize);
    ASSERT_GT(queueSize, 0);

    m_notificationsCapabilityAgent->clearData();
    ASSERT_TRUE(m_notificationsStorage->waitForQueueSizeToBe(0));

    m_notificationsStorage->getIndicatorState(&state);
    ASSERT_EQ(state, IndicatorState::OFF);

    ASSERT_TRUE(m_testNotificationsObserver->waitFor(IndicatorState::OFF, MY_WAIT_TIMEOUT));
}

TEST_F(NotificationsCapabilityAgentTest, test_addObserverToNotificationsAfterCapabilityAgentDestroyed) {
    initializeCapabilityAgent();
    auto notifier = m_notificationsCapabilityAgent->getNotificationsNotifierInterface();
    ASSERT_TRUE(notifier);

    m_renderer.reset();
    m_notificationsCapabilityAgent->shutdown();
    m_notificationsCapabilityAgent.reset();

    notifier->addObserver(m_testNotificationsObserver);
}

}  // namespace test
}  // namespace acsdkNotifications
}  // namespace alexaClientSDK

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

// ACSDK-1367 - Some tests fail on Windows
#if defined(_WIN32) && !defined(RESOLVED_ACSDK_1367)
    ::testing::GTEST_FLAG(filter) = "-NotificationsCapabilityAgentTest.testSameAssetId";
#endif
    return RUN_ALL_TESTS();
}
