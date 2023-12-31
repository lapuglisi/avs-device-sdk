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

#include <ctime>
#include <string>

#include <gtest/gtest.h>

#include "AVSCommon/Utils/Timing/SafeCTimeAccess.h"
#include "AVSCommon/Utils/Timing/TimeUtils.h"

namespace alexaClientSDK {
namespace avsCommon {
namespace utils {
namespace timing {
namespace test {

TEST(TimeTest, test_stringConversion) {
    TimeUtils timeUtils;
    std::string dateStr{"1986-08-10T21:30:00+0000"};
    int64_t date;
    auto success = timeUtils.convert8601TimeStringToUnix(dateStr, &date);
    ASSERT_TRUE(success);

    auto dateTimeT = static_cast<time_t>(date);
    std::tm dateTm;
    auto safeCTimeAccess = SafeCTimeAccess::instance();
    ASSERT_TRUE(safeCTimeAccess->getGmtime(dateTimeT, &dateTm));
    ASSERT_EQ(dateTm.tm_year, 86);
    ASSERT_EQ(dateTm.tm_mon, 7);
    ASSERT_EQ(dateTm.tm_mday, 10);
    ASSERT_EQ(dateTm.tm_hour, 21);
    ASSERT_EQ(dateTm.tm_min, 30);
}

TEST(TimeTest, test_iso8601StringConversion) {
    TimeUtils timeUtils;
    std::string iso8601Str{"1986-08-10T21:30:00+0000"};
    int64_t unixTime;
    auto successUnix = timeUtils.convert8601TimeStringToUnix(iso8601Str, &unixTime);
    ASSERT_TRUE(successUnix);

    std::chrono::system_clock::time_point utcTimePoint;
    auto successUtcTimePoint = timeUtils.convert8601TimeStringToUtcTimePoint(iso8601Str, &utcTimePoint);
    ASSERT_TRUE(successUtcTimePoint);

    auto sec = std::chrono::duration_cast<std::chrono::seconds>(utcTimePoint.time_since_epoch());
    ASSERT_EQ(static_cast<int64_t>(sec.count()), unixTime);
}

TEST(TimeTest, test_iso8601StringConversionUTC) {
    TimeUtils timeUtils;
    std::string iso8601Str{"1986-08-10T21:30:00Z"};
    int64_t unixTime;
    auto successUnix = timeUtils.convert8601TimeStringToUnix(iso8601Str, &unixTime);
    ASSERT_TRUE(successUnix);

    std::chrono::system_clock::time_point utcTimePoint;
    auto successUtcTimePoint = timeUtils.convert8601TimeStringToUtcTimePoint(iso8601Str, &utcTimePoint);
    ASSERT_TRUE(successUtcTimePoint);

    time_t dateTimeT = std::chrono::system_clock::to_time_t(utcTimePoint);
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(utcTimePoint.time_since_epoch());
    ASSERT_EQ(static_cast<int64_t>(sec.count()), unixTime);

    std::tm dateTm;
    auto safeCTimeAccess = SafeCTimeAccess::instance();
    ASSERT_TRUE(safeCTimeAccess->getGmtime(dateTimeT, &dateTm));
    ASSERT_EQ(dateTm.tm_year, 86);
    ASSERT_EQ(dateTm.tm_mon, 7);
    ASSERT_EQ(dateTm.tm_mday, 10);
    ASSERT_EQ(dateTm.tm_hour, 21);
    ASSERT_EQ(dateTm.tm_min, 30);
    ASSERT_EQ(dateTm.tm_sec, 0);
}

TEST(TimeTest, test_stringConversionError) {
    TimeUtils timeUtils;
    std::string dateStr{"1986-8-10T21:30:00+0000"};
    int64_t date;
    auto success = timeUtils.convert8601TimeStringToUnix(dateStr, &date);
    ASSERT_FALSE(success);
}

TEST(TimeTest, test_stringConversionNullParam) {
    TimeUtils timeUtils;
    std::string dateStr{"1986-8-10T21:30:00+0000"};
    auto success = timeUtils.convert8601TimeStringToUnix(dateStr, nullptr);
    ASSERT_FALSE(success);
}

TEST(TimeTest, test_timeConversion) {
    TimeUtils timeUtils;
    std::time_t randomDate = 524089800;
    std::tm date;
    auto safeCTimeAccess = SafeCTimeAccess::instance();
    ASSERT_TRUE(safeCTimeAccess->getGmtime(randomDate, &date));
    std::time_t convertBack;
    auto success = timeUtils.convertToUtcTimeT(&date, &convertBack);

    ASSERT_TRUE(success);
    ASSERT_EQ(randomDate, convertBack);
}

TEST(TimeTest, test_timeConversionCurrentTime) {
    TimeUtils timeUtils;
    int64_t time = -1;
    ASSERT_TRUE(timeUtils.getCurrentUnixTime(&time));

    std::time_t currentDate = static_cast<std::time_t>(time);
    std::tm date;

    auto safeCTimeAccess = SafeCTimeAccess::instance();
    ASSERT_TRUE(safeCTimeAccess->getGmtime(currentDate, &date));
    std::time_t convertBack;
    auto success = timeUtils.convertToUtcTimeT(&date, &convertBack);

    ASSERT_TRUE(success);
    ASSERT_EQ(currentDate, convertBack);
}

TEST(TimeTest, test_currentTime) {
    TimeUtils timeUtils;
    int64_t time = -1;
    auto success = timeUtils.getCurrentUnixTime(&time);

    ASSERT_TRUE(success);
    ASSERT_GT(time, 0);
}

TEST(TimeTest, test_currentTimeNullParam) {
    TimeUtils timeUtils;
    auto success = timeUtils.getCurrentUnixTime(nullptr);
    ASSERT_FALSE(success);
}

/**
 * Helper function to run through the test cases for time to string conversions.
 *
 * @param expectedString The string that convertTimeToUtcIso8601Rfc3339 should generate.
 * @param sec The seconds since epoch to convert.
 * @param us The microseconds since epoch to convert.
 */
static void testIso8601ConversionHelper(
    const std::string& expectedString,
    const std::chrono::seconds sec,
    const std::chrono::microseconds us) {
    TimeUtils timeUtils;
    std::string resultString;
    std::chrono::system_clock::time_point tp;
    tp += sec;
    tp += us;
    EXPECT_TRUE(timeUtils.convertTimeToUtcIso8601Rfc3339(tp, &resultString));
    EXPECT_EQ(expectedString, resultString);
}

TEST(TimeTest, test_iso8601Conversion) {
    testIso8601ConversionHelper("1970-01-01T00:00:00.000Z", std::chrono::seconds{0}, std::chrono::microseconds{0});
    testIso8601ConversionHelper("1970-01-01T00:00:01.000Z", std::chrono::seconds{1}, std::chrono::microseconds{0});
    testIso8601ConversionHelper("1970-01-01T00:00:00.001Z", std::chrono::seconds{0}, std::chrono::microseconds{1000});
    testIso8601ConversionHelper("1970-01-01T00:01:00.000Z", std::chrono::seconds{60}, std::chrono::microseconds{0});
    testIso8601ConversionHelper(
        "1970-01-01T01:00:00.000Z", std::chrono::seconds{60 * 60}, std::chrono::microseconds{0});
    testIso8601ConversionHelper(
        "1970-01-02T00:00:00.000Z", std::chrono::seconds{60 * 60 * 24}, std::chrono::microseconds{0});
    testIso8601ConversionHelper(
        "1970-02-01T00:00:00.000Z", std::chrono::seconds{60 * 60 * 24 * 31}, std::chrono::microseconds{0});
    testIso8601ConversionHelper(
        "1971-01-01T00:00:00.000Z", std::chrono::seconds{60 * 60 * 24 * 365}, std::chrono::microseconds{0});

    testIso8601ConversionHelper(
        "1970-01-02T00:00:00.000Z", std::chrono::seconds{60 * 60 * 24}, std::chrono::microseconds{999});
    testIso8601ConversionHelper(
        "1970-01-02T00:00:00.001Z", std::chrono::seconds{60 * 60 * 24}, std::chrono::microseconds{1000});
    testIso8601ConversionHelper(
        "1970-01-02T00:00:00.001Z", std::chrono::seconds{60 * 60 * 24}, std::chrono::microseconds{1001});
    testIso8601ConversionHelper(
        "1970-01-02T00:00:00.001Z", std::chrono::seconds{60 * 60 * 24}, std::chrono::microseconds{1999});
    testIso8601ConversionHelper(
        "1970-01-02T00:00:00.002Z", std::chrono::seconds{60 * 60 * 24}, std::chrono::microseconds{2000});
    testIso8601ConversionHelper(
        "1970-01-02T00:00:00.002Z", std::chrono::seconds{60 * 60 * 24}, std::chrono::microseconds{2001});
    testIso8601ConversionHelper(
        "1970-01-02T00:00:00.202Z", std::chrono::seconds{60 * 60 * 24}, std::chrono::microseconds{202001});
}

}  // namespace test
}  // namespace timing
}  // namespace utils
}  // namespace avsCommon
}  // namespace alexaClientSDK
