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

#include <string>

#include <gtest/gtest.h>

#include <acsdk/Pkcs11/private/PKCS11Functions.h>
#include <acsdk/Pkcs11/private/PKCS11Slot.h>
#include <acsdk/Pkcs11/private/PKCS11Session.h>

namespace alexaClientSDK {
namespace pkcs11 {
namespace test {

using namespace ::testing;

/// Test that the constructor with a nullptr doesn't segfault.
TEST(PKCS11SlotTest, test_findSlot) {
    auto functions = PKCS11Functions::create(PKCS11_LIBRARY);
    ASSERT_NE(nullptr, functions);
    std::shared_ptr<PKCS11Slot> slot;
    ASSERT_TRUE(functions->findSlotByTokenName(PKCS11_TOKEN_NAME, slot));
    ASSERT_NE(nullptr, slot);
    std::string tokenName;
    ASSERT_TRUE(slot->getTokenName(tokenName));
    ASSERT_EQ(PKCS11_TOKEN_NAME, tokenName);
}

TEST(PKCS11SlotTest, test_openSession) {
    auto functions = PKCS11Functions::create(PKCS11_LIBRARY);
    ASSERT_NE(nullptr, functions);
    std::shared_ptr<PKCS11Slot> slot;
    ASSERT_TRUE(functions->findSlotByTokenName(PKCS11_TOKEN_NAME, slot));
    ASSERT_NE(nullptr, slot);
    auto session = slot->openSession();
    ASSERT_NE(nullptr, session);
}

}  // namespace test
}  // namespace pkcs11
}  // namespace alexaClientSDK
