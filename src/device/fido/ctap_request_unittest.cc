// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/cbor/reader.h"
#include "device/fido/ctap_empty_authenticator_request.h"
#include "device/fido/ctap_get_assertion_request.h"
#include "device/fido/ctap_make_credential_request.h"
#include "device/fido/fido_constants.h"
#include "device/fido/fido_parsing_utils.h"
#include "device/fido/fido_test_data.h"
#include "device/fido/mock_fido_device.h"
#include "device/fido/virtual_ctap2_device.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace device {

// Leveraging example 4 of section 6.1 of the spec
// https://fidoalliance.org/specs/fido-v2.0-rd-20170927/fido-client-to-authenticator-protocol-v2.0-rd-20170927.html
TEST(CTAPRequestTest, TestConstructMakeCredentialRequestParam) {
  PublicKeyCredentialRpEntity rp("acme.com");
  rp.name = "Acme";

  PublicKeyCredentialUserEntity user(
      fido_parsing_utils::Materialize(test_data::kUserId));
  user.name = "johnpsmith@example.com";
  user.display_name = "John P. Smith";
  user.icon_url = GURL("https://pics.acme.com/00/p/aBjjjpqPb.png");

  CtapMakeCredentialRequest make_credential_param(
      test_data::kClientDataJson, std::move(rp), std::move(user),
      PublicKeyCredentialParams({{CredentialType::kPublicKey, -7},
                                 {CredentialType::kPublicKey, 257}}));
  make_credential_param.resident_key_required = true;
  make_credential_param.user_verification =
      UserVerificationRequirement::kRequired;
  auto serialized_data = MockFidoDevice::EncodeCBORRequest(
      AsCTAPRequestValuePair(make_credential_param));
  EXPECT_THAT(serialized_data, ::testing::ElementsAreArray(
                                   test_data::kCtapMakeCredentialRequest));
}

TEST(CTAPRequestTest, TestConstructGetAssertionRequest) {
  CtapGetAssertionRequest get_assertion_req("acme.com",
                                            test_data::kClientDataJson);

  std::vector<PublicKeyCredentialDescriptor> allowed_list;
  allowed_list.push_back(PublicKeyCredentialDescriptor(
      CredentialType::kPublicKey,
      {0xf2, 0x20, 0x06, 0xde, 0x4f, 0x90, 0x5a, 0xf6, 0x8a, 0x43, 0x94,
       0x2f, 0x02, 0x4f, 0x2a, 0x5e, 0xce, 0x60, 0x3d, 0x9c, 0x6d, 0x4b,
       0x3d, 0xf8, 0xbe, 0x08, 0xed, 0x01, 0xfc, 0x44, 0x26, 0x46, 0xd0,
       0x34, 0x85, 0x8a, 0xc7, 0x5b, 0xed, 0x3f, 0xd5, 0x80, 0xbf, 0x98,
       0x08, 0xd9, 0x4f, 0xcb, 0xee, 0x82, 0xb9, 0xb2, 0xef, 0x66, 0x77,
       0xaf, 0x0a, 0xdc, 0xc3, 0x58, 0x52, 0xea, 0x6b, 0x9e}));
  allowed_list.push_back(PublicKeyCredentialDescriptor(
      CredentialType::kPublicKey,
      {0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
       0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
       0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
       0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
       0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03}));

  get_assertion_req.allow_list = std::move(allowed_list);
  get_assertion_req.user_presence_required = false;
  get_assertion_req.user_verification = UserVerificationRequirement::kRequired;

  auto serialized_data = MockFidoDevice::EncodeCBORRequest(
      AsCTAPRequestValuePair(get_assertion_req));
  EXPECT_THAT(serialized_data,
              ::testing::ElementsAreArray(
                  test_data::kTestComplexCtapGetAssertionRequest));
}

TEST(CTAPRequestTest, TestConstructCtapAuthenticatorRequestParam) {
  static constexpr uint8_t kSerializedGetInfoCmd = 0x04;
  static constexpr uint8_t kSerializedGetNextAssertionCmd = 0x08;
  static constexpr uint8_t kSerializedResetCmd = 0x07;

  EXPECT_THAT(AuthenticatorGetInfoRequest().Serialize(),
              ::testing::ElementsAre(kSerializedGetInfoCmd));
  EXPECT_THAT(AuthenticatorGetNextAssertionRequest().Serialize(),
              ::testing::ElementsAre(kSerializedGetNextAssertionCmd));
  EXPECT_THAT(AuthenticatorResetRequest().Serialize(),
              ::testing::ElementsAre(kSerializedResetCmd));
}

}  // namespace device
