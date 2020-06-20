// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "ash/shell.h"
#include "ash/sticky_keys/sticky_keys_controller.h"
#include "ash/sticky_keys/sticky_keys_overlay.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/chromeos/events/event_rewriter_delegate_impl.h"
#include "chrome/browser/chromeos/input_method/input_method_configuration.h"
#include "chrome/browser/chromeos/input_method/mock_input_method_manager_impl.h"
#include "chrome/browser/chromeos/login/users/fake_chrome_user_manager.h"
#include "chrome/browser/chromeos/preferences.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/chrome_ash_test_base.h"
#include "chromeos/constants/chromeos_features.h"
#include "chromeos/constants/chromeos_switches.h"
#include "components/prefs/pref_member.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "components/user_manager/scoped_user_manager.h"
#include "device/udev_linux/fake_udev_loader.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/ime/chromeos/fake_ime_keyboard.h"
#include "ui/chromeos/events/event_rewriter_chromeos.h"
#include "ui/chromeos/events/modifier_key.h"
#include "ui/chromeos/events/pref_names.h"
#include "ui/events/devices/device_data_manager.h"
#include "ui/events/devices/device_data_manager_test_api.h"
#include "ui/events/event.h"
#include "ui/events/event_rewriter.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/test/events_test_utils.h"
#include "ui/events/test/test_event_processor.h"
#include "ui/events/test/test_event_rewriter_continuation.h"
#include "ui/wm/core/window_util.h"

namespace {

constexpr int kKeyboardDeviceId = 123;
constexpr uint32_t kNoScanCode = 0;
constexpr char kKbdSysPath[] = "/devices/platform/i8042/serio2/input/input1";
constexpr char kKbdTopRowPropertyName[] = "CROS_KEYBOARD_TOP_ROW_LAYOUT";
constexpr char kKbdTopRowLayoutAttributeName[] = "function_row_physmap";

constexpr char kKbdTopRowLayoutUnspecified[] = "";
constexpr char kKbdTopRowLayout1Tag[] = "1";
constexpr char kKbdTopRowLayout2Tag[] = "2";
constexpr char kKbdTopRowLayoutWilcoTag[] = "3";
constexpr char kKbdTopRowLayoutDrallionTag[] = "4";

// A default example of the layout string read from the function_row_physmap
// sysfs attribute. The values represent the scan codes for each position
// in the top row, which maps to F-Keys.
constexpr char kKbdDefaultCustomTopRowLayout[] =
    "01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f";

class TestEventRewriterContinuation
    : public ui::test::TestEventRewriterContinuation {
 public:
  TestEventRewriterContinuation() = default;
  ~TestEventRewriterContinuation() override = default;
  TestEventRewriterContinuation(const TestEventRewriterContinuation&) = delete;
  TestEventRewriterContinuation& operator=(
      const TestEventRewriterContinuation&) = delete;

  ui::EventDispatchDetails SendEvent(const ui::Event* event) override {
    passthrough_events.push_back(ui::Event::Clone(*event));
    return ui::EventDispatchDetails();
  }

  ui::EventDispatchDetails SendEventFinally(const ui::Event* event) override {
    rewritten_events.push_back(ui::Event::Clone(*event));
    return ui::EventDispatchDetails();
  }

  ui::EventDispatchDetails DiscardEvent() override {
    return ui::EventDispatchDetails();
  }

  std::vector<std::unique_ptr<ui::Event>> rewritten_events;
  std::vector<std::unique_ptr<ui::Event>> passthrough_events;

  base::WeakPtrFactory<TestEventRewriterContinuation> weak_ptr_factory_{this};
};

std::string GetExpectedResultAsString(ui::EventType ui_type,
                                      ui::KeyboardCode ui_keycode,
                                      ui::DomCode code,
                                      int ui_flags,  // ui::EventFlags
                                      ui::DomKey key,
                                      uint32_t scan_code) {
  return base::StringPrintf(
      "type=%d code=0x%06X flags=0x%X vk=0x%02X key=0x%08X scan=0x%08X",
      ui_type, static_cast<unsigned int>(code), ui_flags & ~ui::EF_IS_REPEAT,
      ui_keycode, static_cast<unsigned int>(key), scan_code);
}

std::string GetKeyEventAsString(const ui::KeyEvent& keyevent) {
  return GetExpectedResultAsString(keyevent.type(), keyevent.key_code(),
                                   keyevent.code(), keyevent.flags(),
                                   keyevent.GetDomKey(), keyevent.scan_code());
}

std::string GetRewrittenEventAsString(ui::EventRewriter* const rewriter,
                                      ui::EventType ui_type,
                                      ui::KeyboardCode ui_keycode,
                                      ui::DomCode code,
                                      int ui_flags,  // ui::EventFlags
                                      ui::DomKey key,
                                      uint32_t scan_code,
                                      int device_id = kKeyboardDeviceId) {
  ui::KeyEvent event(ui_type, ui_keycode, code, ui_flags, key,
                     ui::EventTimeForNow());
  event.set_scan_code(scan_code);
  event.set_source_device_id(device_id);
  TestEventRewriterContinuation continuation;
  rewriter->RewriteEvent(event, continuation.weak_ptr_factory_.GetWeakPtr());
  if (!continuation.rewritten_events.empty())
    return GetKeyEventAsString(*continuation.rewritten_events[0]->AsKeyEvent());
  return GetKeyEventAsString(event);
}

// Table entry for simple single key event rewriting tests.
struct KeyTestCase {
  ui::EventType type;
  struct Event {
    ui::KeyboardCode key_code;
    ui::DomCode code;
    int flags;  // ui::EventFlags
    ui::DomKey::Base key;
    uint32_t scan_code = kNoScanCode;
  } input, expected;
  int device_id = kKeyboardDeviceId;
};

std::string GetTestCaseAsString(ui::EventType ui_type,
                                const KeyTestCase::Event& test) {
  return GetExpectedResultAsString(ui_type, test.key_code, test.code,
                                   test.flags, test.key, test.scan_code);
}

// Tests a single stateless key rewrite operation.
void CheckKeyTestCase(ui::EventRewriter* const rewriter,
                      const KeyTestCase& test) {
  SCOPED_TRACE("\nSource:    " + GetTestCaseAsString(test.type, test.input));
  EXPECT_EQ(GetTestCaseAsString(test.type, test.expected),
            GetRewrittenEventAsString(rewriter, test.type, test.input.key_code,
                                      test.input.code, test.input.flags,
                                      test.input.key, test.input.scan_code,
                                      test.device_id));
}

}  // namespace

namespace chromeos {

class EventRewriterTest : public ChromeAshTestBase {
 public:
  EventRewriterTest()
      : fake_user_manager_(new chromeos::FakeChromeUserManager),
        user_manager_enabler_(base::WrapUnique(fake_user_manager_)) {}
  ~EventRewriterTest() override {}

  void SetUp() override {
    input_method_manager_mock_ = new input_method::MockInputMethodManagerImpl;
    chromeos::input_method::InitializeForTesting(
        input_method_manager_mock_);  // pass ownership
    delegate_ = std::make_unique<EventRewriterDelegateImpl>(nullptr);
    delegate_->set_pref_service_for_testing(prefs());
    device_data_manager_test_api_.SetKeyboardDevices({});
    rewriter_ = std::make_unique<ui::EventRewriterChromeOS>(delegate_.get(),
                                                            nullptr, false);
    ChromeAshTestBase::SetUp();
  }

  void TearDown() override {
    ChromeAshTestBase::TearDown();
    // Shutdown() deletes the IME mock object.
    chromeos::input_method::Shutdown();
  }

  ui::EventRewriter* rewriter() { return rewriter_.get(); }

 protected:
  void TestRewriteNumPadKeys();
  void TestRewriteNumPadKeysOnAppleKeyboard();

  // Parameterized version of test depending on feature flag values. The feature
  // kUseSearchClickForRightClick determines if this should test for alt-click
  // or search-click.
  void DontRewriteIfNotRewritten(int right_click_flags);

  ui::MouseEvent RewriteMouseButtonEvent(const ui::MouseEvent& event) {
    TestEventRewriterContinuation continuation;
    rewriter_->RewriteMouseButtonEventForTesting(
        event, continuation.weak_ptr_factory_.GetWeakPtr());
    if (!continuation.rewritten_events.empty())
      return ui::MouseEvent(*continuation.rewritten_events[0]->AsMouseEvent());
    return ui::MouseEvent(event);
  }

  sync_preferences::TestingPrefServiceSyncable* prefs() { return &prefs_; }

  void InitModifierKeyPref(IntegerPrefMember* int_pref,
                           const std::string& pref_name,
                           ui::chromeos::ModifierKey modifierKey) {
    if (int_pref->GetPrefName() != pref_name)  // skip if already initialized.
      int_pref->Init(pref_name, prefs());
    int_pref->SetValue(static_cast<int>(modifierKey));
  }

  void SetupKeyboard(const std::string& name,
                     const std::string& layout = "",
                     ui::InputDeviceType type = ui::INPUT_DEVICE_INTERNAL,
                     bool has_custom_top_row = false) {
    // Add a fake device to udev.
    const ui::InputDevice keyboard(kKeyboardDeviceId, type, name, /*phys=*/"",
                                   base::FilePath(kKbdSysPath), /*vendor=*/-1,
                                   /*product=*/-1, /*version=*/-1);

    // Old CrOS keyboards supply an integer/enum as a sysfs property to identify
    // their layout type. New keyboards provide the mapping of scan codes to
    // F-Key position via an attribute.
    std::map<std::string, std::string> sysfs_properties;
    std::map<std::string, std::string> sysfs_attributes;
    if (has_custom_top_row) {
      if (!layout.empty())
        sysfs_attributes[kKbdTopRowLayoutAttributeName] = layout;
    } else {
      if (!layout.empty())
        sysfs_properties[kKbdTopRowPropertyName] = layout;
    }

    fake_udev_.Reset();
    fake_udev_.AddFakeDevice(keyboard.name, keyboard.sys_path.value(),
                             std::move(sysfs_attributes),
                             std::move(sysfs_properties));

    // Reset the state of the device manager.
    device_data_manager_test_api_.SetKeyboardDevices({});
    device_data_manager_test_api_.SetKeyboardDevices({keyboard});

    // Reset the state of the EventRewriter.
    rewriter_->ResetStateForTesting();
    rewriter_->KeyboardDeviceAddedForTesting(kKeyboardDeviceId);
    rewriter_->set_last_keyboard_device_id_for_testing(kKeyboardDeviceId);
  }

  void TestKeyboard(const std::string& name,
                    const std::string& layout,
                    ui::InputDeviceType type,
                    bool has_custom_top_row,
                    const std::vector<KeyTestCase>& tests) {
    SetupKeyboard(name, layout, type, has_custom_top_row);
    for (const auto& test : tests)
      CheckKeyTestCase(rewriter(), test);
  }

  void TestInternalChromeKeyboard(const std::vector<KeyTestCase>& tests) {
    TestKeyboard("Internal Keyboard", kKbdTopRowLayoutUnspecified,
                 ui::INPUT_DEVICE_INTERNAL, /*has_custom_top_row=*/false,
                 tests);
  }

  void TestInternalChromeCustomLayoutKeyboard(
      const std::vector<KeyTestCase>& tests) {
    TestKeyboard("Internal Custom Layout Keyboard",
                 kKbdDefaultCustomTopRowLayout, ui::INPUT_DEVICE_INTERNAL,
                 /*has_custom_top_row=*/true, tests);
  }

  void TestExternalChromeKeyboard(const std::vector<KeyTestCase>& tests) {
    TestKeyboard("External Chrome Keyboard", kKbdTopRowLayout1Tag,
                 ui::INPUT_DEVICE_UNKNOWN, /*has_custom_top_row=*/false, tests);
  }

  void TestExternalChromeCustomLayoutKeyboard(
      const std::vector<KeyTestCase>& tests) {
    TestKeyboard("External Chrome Custom Layout Keyboard",
                 kKbdDefaultCustomTopRowLayout, ui::INPUT_DEVICE_UNKNOWN,
                 /*has_custom_top_row=*/true, tests);
  }

  void TestExternalGenericKeyboard(const std::vector<KeyTestCase>& tests) {
    TestKeyboard("PC Keyboard", kKbdTopRowLayoutUnspecified,
                 ui::INPUT_DEVICE_UNKNOWN, /*has_custom_top_row=*/false, tests);
  }

  void TestExternalAppleKeyboard(const std::vector<KeyTestCase>& tests) {
    TestKeyboard("Apple Keyboard", kKbdTopRowLayoutUnspecified,
                 ui::INPUT_DEVICE_UNKNOWN, /*has_custom_top_row=*/false, tests);
  }

  void TestChromeKeyboardVariants(const std::vector<KeyTestCase>& tests) {
    TestInternalChromeKeyboard(tests);
    TestExternalChromeKeyboard(tests);
  }

  void TestChromeCustomLayoutKeyboardVariants(
      const std::vector<KeyTestCase>& tests) {
    TestInternalChromeCustomLayoutKeyboard(tests);
    TestExternalChromeCustomLayoutKeyboard(tests);
  }

  void TestNonAppleKeyboardVariants(const std::vector<KeyTestCase>& tests) {
    TestChromeKeyboardVariants(tests);
    TestChromeCustomLayoutKeyboardVariants(tests);
    TestExternalGenericKeyboard(tests);
  }

  void TestNonAppleNonCustomLayoutKeyboardVariants(
      const std::vector<KeyTestCase>& tests) {
    TestChromeKeyboardVariants(tests);
    TestExternalGenericKeyboard(tests);
  }

  void TestAllKeyboardVariants(const std::vector<KeyTestCase>& tests) {
    TestNonAppleKeyboardVariants(tests);
    TestExternalAppleKeyboard(tests);
  }

  base::test::ScopedFeatureList scoped_feature_list_;
  FakeChromeUserManager* fake_user_manager_;  // Not owned.
  user_manager::ScopedUserManager user_manager_enabler_;
  input_method::MockInputMethodManagerImpl* input_method_manager_mock_;
  testing::FakeUdevLoader fake_udev_;
  ui::DeviceDataManagerTestApi device_data_manager_test_api_;

  sync_preferences::TestingPrefServiceSyncable prefs_;
  std::unique_ptr<EventRewriterDelegateImpl> delegate_;
  std::unique_ptr<ui::EventRewriterChromeOS> rewriter_;
};

TEST_F(EventRewriterTest, TestRewriteCommandToControl) {
  // First, test non Apple keyboards, they should all behave the same.
  TestNonAppleKeyboardVariants({
      // VKEY_A, Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN,
        ui::DomKey::UNIDENTIFIED}},

      // VKEY_A, Win modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED}},

      // VKEY_A, Alt+Win modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED}},

      // VKEY_LWIN (left Windows key), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN, ui::DomKey::META},
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN, ui::DomKey::META}},

      // VKEY_RWIN (right Windows key), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_RWIN, ui::DomCode::META_RIGHT,
        ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN, ui::DomKey::META},
       {ui::VKEY_RWIN, ui::DomCode::META_RIGHT,
        ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN, ui::DomKey::META}},
  });

  // Simulate the default initialization of the Apple Command key remap pref to
  // Ctrl.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  TestExternalAppleKeyboard({
      // VKEY_A, Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN,
        ui::DomKey::UNIDENTIFIED}},

      // VKEY_A, Win modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'a'>::Character}},

      // VKEY_A, Alt+Win modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'a'>::Character}},

      // VKEY_LWIN (left Windows key), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
        ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN, ui::DomKey::CONTROL}},

      // VKEY_RWIN (right Windows key), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_RWIN, ui::DomCode::META_RIGHT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_RIGHT,
        ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN, ui::DomKey::CONTROL}},
  });

  // Now simulate the user remapped the Command key back to Search.
  IntegerPrefMember command;
  InitModifierKeyPref(&command, prefs::kLanguageRemapExternalCommandKeyTo,
                      ui::chromeos::ModifierKey::kSearchKey);

  TestExternalAppleKeyboard({
      // VKEY_A, Win modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED}},

      // VKEY_A, Alt+Win modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED}},

      // VKEY_LWIN (left Windows key), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META},
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META}},

      // VKEY_RWIN (right Windows key), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_RWIN, ui::DomCode::META_RIGHT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META},
       {ui::VKEY_LWIN, ui::DomCode::META_RIGHT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META}},
  });
}

TEST_F(EventRewriterTest, TestRewriteExternalMetaKey) {
  // Simulate the default initialization of the Meta key on external keyboards
  // remap pref to Search.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  // By default, the Meta key on all keyboards, internal, external Chrome OS
  // branded keyboards, and Generic keyboards should produce Search.
  TestNonAppleKeyboardVariants({
      // VKEY_A, Win modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED}},

      // VKEY_A, Alt+Win modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED}},

      // VKEY_LWIN (left Windows key), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META},
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META}},

      // VKEY_RWIN (right Windows key), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_RWIN, ui::DomCode::META_RIGHT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META},
       {ui::VKEY_LWIN, ui::DomCode::META_RIGHT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META}},
  });

  // Both preferences for Search on Chrome keyboards, and external Meta on
  // generic external keyboards are independent, even if one or both are
  // modified.

  // Remap Chrome OS Search to Ctrl.
  IntegerPrefMember internal_search;
  InitModifierKeyPref(&internal_search, prefs::kLanguageRemapSearchKeyTo,
                      ui::chromeos::ModifierKey::kControlKey);

  // Remap external Meta to Alt.
  IntegerPrefMember meta;
  InitModifierKeyPref(&meta, prefs::kLanguageRemapExternalMetaKeyTo,
                      ui::chromeos::ModifierKey::kAltKey);

  TestChromeKeyboardVariants({
      // VKEY_A, Win modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'a'>::Character}},

      // VKEY_A, Alt+Win modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'a'>::Character}},

      // VKEY_LWIN (left Windows key), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
        ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN, ui::DomKey::CONTROL}},

      // VKEY_RWIN (right Windows key), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_RWIN, ui::DomCode::META_RIGHT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_RIGHT,
        ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN, ui::DomKey::CONTROL}},
  });

  TestExternalGenericKeyboard({
      // VKEY_A, Win modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'a'>::Character}},

      // VKEY_A, Alt+Win modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'a'>::Character}},

      // VKEY_LWIN (left Windows key), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META},
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT, ui::EF_ALT_DOWN,
        ui::DomKey::ALT}},

      // VKEY_RWIN (right Windows key), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_RWIN, ui::DomCode::META_RIGHT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META},
       {ui::VKEY_MENU, ui::DomCode::ALT_RIGHT, ui::EF_ALT_DOWN,
        ui::DomKey::ALT}},
  });
}

// For crbug.com/133896.
TEST_F(EventRewriterTest, TestRewriteCommandToControlWithControlRemapped) {
  // Remap Control to Alt.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  IntegerPrefMember control;
  InitModifierKeyPref(&control, prefs::kLanguageRemapControlKeyTo,
                      ui::chromeos::ModifierKey::kAltKey);

  TestNonAppleKeyboardVariants({
      // Control should be remapped to Alt.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::CONTROL},
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT, ui::EF_ALT_DOWN,
        ui::DomKey::ALT}},
  });

  // Now verify that remapping does not affect Apple keyboard.
  TestExternalAppleKeyboard({
      // VKEY_LWIN (left Command key) with  Alt modifier. The remapped Command
      // key should never be re-remapped to Alt.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
        ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN, ui::DomKey::CONTROL}},

      // VKEY_RWIN (right Command key) with  Alt modifier. The remapped Command
      // key should never be re-remapped to Alt.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_RWIN, ui::DomCode::META_RIGHT,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::META},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_RIGHT,
        ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN, ui::DomKey::CONTROL}},
  });
}

void EventRewriterTest::TestRewriteNumPadKeys() {
  // Even if most Chrome OS keyboards do not have numpad, they should still
  // handle it the same way as generic PC keyboards.
  TestNonAppleKeyboardVariants({
      // XK_KP_Insert (= NumPad 0 without Num Lock), no modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_INSERT, ui::DomCode::NUMPAD0, ui::EF_NONE, ui::DomKey::INSERT},
       {ui::VKEY_NUMPAD0, ui::DomCode::NUMPAD0, ui::EF_NONE,
        ui::DomKey::Constant<'0'>::Character}},

      // XK_KP_Insert (= NumPad 0 without Num Lock), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_INSERT, ui::DomCode::NUMPAD0, ui::EF_ALT_DOWN,
        ui::DomKey::INSERT},
       {ui::VKEY_NUMPAD0, ui::DomCode::NUMPAD0, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'0'>::Character}},

      // XK_KP_Delete (= NumPad . without Num Lock), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_DELETE, ui::DomCode::NUMPAD_DECIMAL, ui::EF_ALT_DOWN,
        ui::DomKey::DEL},
       {ui::VKEY_DECIMAL, ui::DomCode::NUMPAD_DECIMAL, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'.'>::Character}},

      // XK_KP_End (= NumPad 1 without Num Lock), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_END, ui::DomCode::NUMPAD1, ui::EF_ALT_DOWN, ui::DomKey::END},
       {ui::VKEY_NUMPAD1, ui::DomCode::NUMPAD1, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'1'>::Character}},

      // XK_KP_Down (= NumPad 2 without Num Lock), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_DOWN, ui::DomCode::NUMPAD2, ui::EF_ALT_DOWN,
        ui::DomKey::ARROW_DOWN},
       {ui::VKEY_NUMPAD2, ui::DomCode::NUMPAD2, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'2'>::Character}},

      // XK_KP_Next (= NumPad 3 without Num Lock), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_NEXT, ui::DomCode::NUMPAD3, ui::EF_ALT_DOWN,
        ui::DomKey::PAGE_DOWN},
       {ui::VKEY_NUMPAD3, ui::DomCode::NUMPAD3, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'3'>::Character}},

      // XK_KP_Left (= NumPad 4 without Num Lock), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LEFT, ui::DomCode::NUMPAD4, ui::EF_ALT_DOWN,
        ui::DomKey::ARROW_LEFT},
       {ui::VKEY_NUMPAD4, ui::DomCode::NUMPAD4, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'4'>::Character}},

      // XK_KP_Begin (= NumPad 5 without Num Lock), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_CLEAR, ui::DomCode::NUMPAD5, ui::EF_ALT_DOWN,
        ui::DomKey::CLEAR},
       {ui::VKEY_NUMPAD5, ui::DomCode::NUMPAD5, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'5'>::Character}},

      // XK_KP_Right (= NumPad 6 without Num Lock), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_RIGHT, ui::DomCode::NUMPAD6, ui::EF_ALT_DOWN,
        ui::DomKey::ARROW_RIGHT},
       {ui::VKEY_NUMPAD6, ui::DomCode::NUMPAD6, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'6'>::Character}},

      // XK_KP_Home (= NumPad 7 without Num Lock), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_HOME, ui::DomCode::NUMPAD7, ui::EF_ALT_DOWN, ui::DomKey::HOME},
       {ui::VKEY_NUMPAD7, ui::DomCode::NUMPAD7, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'7'>::Character}},

      // XK_KP_Up (= NumPad 8 without Num Lock), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_UP, ui::DomCode::NUMPAD8, ui::EF_ALT_DOWN,
        ui::DomKey::ARROW_UP},
       {ui::VKEY_NUMPAD8, ui::DomCode::NUMPAD8, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'8'>::Character}},

      // XK_KP_Prior (= NumPad 9 without Num Lock), Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_PRIOR, ui::DomCode::NUMPAD9, ui::EF_ALT_DOWN,
        ui::DomKey::PAGE_UP},
       {ui::VKEY_NUMPAD9, ui::DomCode::NUMPAD9, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'9'>::Character}},

      // XK_KP_0 (= NumPad 0 with Num Lock), Num Lock modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_NUMPAD0, ui::DomCode::NUMPAD0, ui::EF_NONE,
        ui::DomKey::Constant<'0'>::Character},
       {ui::VKEY_NUMPAD0, ui::DomCode::NUMPAD0, ui::EF_NONE,
        ui::DomKey::Constant<'0'>::Character}},

      // XK_KP_DECIMAL (= NumPad . with Num Lock), Num Lock modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_DECIMAL, ui::DomCode::NUMPAD_DECIMAL, ui::EF_NONE,
        ui::DomKey::Constant<'.'>::Character},
       {ui::VKEY_DECIMAL, ui::DomCode::NUMPAD_DECIMAL, ui::EF_NONE,
        ui::DomKey::Constant<'.'>::Character}},

      // XK_KP_1 (= NumPad 1 with Num Lock), Num Lock modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_NUMPAD1, ui::DomCode::NUMPAD1, ui::EF_NONE,
        ui::DomKey::Constant<'1'>::Character},
       {ui::VKEY_NUMPAD1, ui::DomCode::NUMPAD1, ui::EF_NONE,
        ui::DomKey::Constant<'1'>::Character}},

      // XK_KP_2 (= NumPad 2 with Num Lock), Num Lock modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_NUMPAD2, ui::DomCode::NUMPAD2, ui::EF_NONE,
        ui::DomKey::Constant<'2'>::Character},
       {ui::VKEY_NUMPAD2, ui::DomCode::NUMPAD2, ui::EF_NONE,
        ui::DomKey::Constant<'2'>::Character}},

      // XK_KP_3 (= NumPad 3 with Num Lock), Num Lock modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_NUMPAD3, ui::DomCode::NUMPAD3, ui::EF_NONE,
        ui::DomKey::Constant<'3'>::Character},
       {ui::VKEY_NUMPAD3, ui::DomCode::NUMPAD3, ui::EF_NONE,
        ui::DomKey::Constant<'3'>::Character}},

      // XK_KP_4 (= NumPad 4 with Num Lock), Num Lock modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_NUMPAD4, ui::DomCode::NUMPAD4, ui::EF_NONE,
        ui::DomKey::Constant<'4'>::Character},
       {ui::VKEY_NUMPAD4, ui::DomCode::NUMPAD4, ui::EF_NONE,
        ui::DomKey::Constant<'4'>::Character}},

      // XK_KP_5 (= NumPad 5 with Num Lock), Num Lock
      // modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_NUMPAD5, ui::DomCode::NUMPAD5, ui::EF_NONE,
        ui::DomKey::Constant<'5'>::Character},
       {ui::VKEY_NUMPAD5, ui::DomCode::NUMPAD5, ui::EF_NONE,
        ui::DomKey::Constant<'5'>::Character}},

      // XK_KP_6 (= NumPad 6 with Num Lock), Num Lock
      // modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_NUMPAD6, ui::DomCode::NUMPAD6, ui::EF_NONE,
        ui::DomKey::Constant<'6'>::Character},
       {ui::VKEY_NUMPAD6, ui::DomCode::NUMPAD6, ui::EF_NONE,
        ui::DomKey::Constant<'6'>::Character}},

      // XK_KP_7 (= NumPad 7 with Num Lock), Num Lock
      // modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_NUMPAD7, ui::DomCode::NUMPAD7, ui::EF_NONE,
        ui::DomKey::Constant<'7'>::Character},
       {ui::VKEY_NUMPAD7, ui::DomCode::NUMPAD7, ui::EF_NONE,
        ui::DomKey::Constant<'7'>::Character}},

      // XK_KP_8 (= NumPad 8 with Num Lock), Num Lock
      // modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_NUMPAD8, ui::DomCode::NUMPAD8, ui::EF_NONE,
        ui::DomKey::Constant<'8'>::Character},
       {ui::VKEY_NUMPAD8, ui::DomCode::NUMPAD8, ui::EF_NONE,
        ui::DomKey::Constant<'8'>::Character}},

      // XK_KP_9 (= NumPad 9 with Num Lock), Num Lock
      // modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_NUMPAD9, ui::DomCode::NUMPAD9, ui::EF_NONE,
        ui::DomKey::Constant<'9'>::Character},
       {ui::VKEY_NUMPAD9, ui::DomCode::NUMPAD9, ui::EF_NONE,
        ui::DomKey::Constant<'9'>::Character}},
  });
}

TEST_F(EventRewriterTest, TestRewriteNumPadKeys) {
  TestRewriteNumPadKeys();
}

// Tests if the rewriter can handle a Command + Num Pad event.
void EventRewriterTest::TestRewriteNumPadKeysOnAppleKeyboard() {
  // Simulate the default initialization of the Apple Command key remap pref to
  // Ctrl.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  TestExternalAppleKeyboard({
      // XK_KP_End (= NumPad 1 without Num Lock), Win modifier.
      // The result should be "Num Pad 1 with Control + Num Lock modifiers".
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_END, ui::DomCode::NUMPAD1, ui::EF_COMMAND_DOWN,
        ui::DomKey::END},
       {ui::VKEY_NUMPAD1, ui::DomCode::NUMPAD1, ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'1'>::Character}},

      // XK_KP_1 (= NumPad 1 with Num Lock), Win modifier.
      // The result should also be "Num Pad 1 with Control + Num Lock
      // modifiers".
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_NUMPAD1, ui::DomCode::NUMPAD1, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'1'>::Character},
       {ui::VKEY_NUMPAD1, ui::DomCode::NUMPAD1, ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'1'>::Character}},
  });
}

TEST_F(EventRewriterTest, TestRewriteNumPadKeysOnAppleKeyboard) {
  TestRewriteNumPadKeysOnAppleKeyboard();
}

TEST_F(EventRewriterTest, TestRewriteModifiersNoRemap) {
  TestAllKeyboardVariants({
      // Press Search. Confirm the event is not rewritten.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_NONE, ui::DomKey::META},
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_COMMAND_DOWN,
        ui::DomKey::META}},

      // Press left Control. Confirm the event is not rewritten.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::CONTROL},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::CONTROL}},

      // Press right Control. Confirm the event is not rewritten.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::CONTROL},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::CONTROL}},

      // Press left Alt. Confirm the event is not rewritten.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT, ui::EF_ALT_DOWN, ui::DomKey::ALT},
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT, ui::EF_ALT_DOWN,
        ui::DomKey::ALT}},

      // Press right Alt. Confirm the event is not rewritten.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT, ui::EF_ALT_DOWN, ui::DomKey::ALT},
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT, ui::EF_ALT_DOWN,
        ui::DomKey::ALT}},

      // Test KeyRelease event, just in case.
      // Release Search. Confirm the release event is not rewritten.
      {ui::ET_KEY_RELEASED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_NONE, ui::DomKey::META},
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_NONE, ui::DomKey::META}},
  });
}

TEST_F(EventRewriterTest, TestRewriteModifiersNoRemapMultipleKeys) {
  TestAllKeyboardVariants({
      // Press Alt with Shift. Confirm the event is not rewritten.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN, ui::DomKey::ALT},
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN, ui::DomKey::ALT}},

      // Press Escape with Alt and Shift. Confirm the event is not rewritten.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_ESCAPE, ui::DomCode::ESCAPE,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN, ui::DomKey::ESCAPE},
       {ui::VKEY_ESCAPE, ui::DomCode::ESCAPE,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN, ui::DomKey::ESCAPE}},

      // Press Search with Caps Lock mask. Confirm the event is not rewritten.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_CAPS_LOCK_ON | ui::EF_COMMAND_DOWN, ui::DomKey::META},
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_CAPS_LOCK_ON | ui::EF_COMMAND_DOWN, ui::DomKey::META}},

      // Release Search with Caps Lock mask. Confirm the event is not rewritten.
      {ui::ET_KEY_RELEASED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_CAPS_LOCK_ON,
        ui::DomKey::META},
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_CAPS_LOCK_ON,
        ui::DomKey::META}},

      // Press Shift+Ctrl+Alt+Search+Escape. Confirm the event is not rewritten.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_ESCAPE, ui::DomCode::ESCAPE,
        ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN |
            ui::EF_COMMAND_DOWN,
        ui::DomKey::ESCAPE},
       {ui::VKEY_ESCAPE, ui::DomCode::ESCAPE,
        ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN |
            ui::EF_COMMAND_DOWN,
        ui::DomKey::ESCAPE}},

      // Press Shift+Ctrl+Alt+Search+B. Confirm the event is not rewritten.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_B, ui::DomCode::US_B,
        ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN |
            ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'B'>::Character},
       {ui::VKEY_B, ui::DomCode::US_B,
        ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN |
            ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'B'>::Character}},
  });
}

TEST_F(EventRewriterTest, TestRewriteModifiersDisableSome) {
  // Disable Search, Control and Escape keys.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  IntegerPrefMember search;
  InitModifierKeyPref(&search, prefs::kLanguageRemapSearchKeyTo,
                      ui::chromeos::ModifierKey::kVoidKey);
  IntegerPrefMember control;
  InitModifierKeyPref(&control, prefs::kLanguageRemapControlKeyTo,
                      ui::chromeos::ModifierKey::kVoidKey);
  IntegerPrefMember escape;
  InitModifierKeyPref(&escape, prefs::kLanguageRemapEscapeKeyTo,
                      ui::chromeos::ModifierKey::kVoidKey);

  TestChromeKeyboardVariants({
      // Press Alt with Shift. This key press shouldn't be affected by the
      // pref. Confirm the event is not rewritten.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN, ui::DomKey::ALT},
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN, ui::DomKey::ALT}},

      // Press Search. Confirm the event is now VKEY_UNKNOWN.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_NONE, ui::DomKey::META},
       {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_NONE,
        ui::DomKey::UNIDENTIFIED}},

      // Press Control. Confirm the event is now VKEY_UNKNOWN.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::CONTROL},
       {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_NONE,
        ui::DomKey::UNIDENTIFIED}},

      // Press Escape. Confirm the event is now VKEY_UNKNOWN.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_ESCAPE, ui::DomCode::ESCAPE, ui::EF_NONE, ui::DomKey::ESCAPE},
       {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_NONE,
        ui::DomKey::UNIDENTIFIED}},

      // Press Control+Search. Confirm the event is now VKEY_UNKNOWN
      // without any modifiers.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::META},
       {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_NONE,
        ui::DomKey::UNIDENTIFIED}},

      // Press Control+Search+a. Confirm the event is now VKEY_A without any
      // modifiers.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'a'>::Character},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_NONE,
        ui::DomKey::Constant<'a'>::Character}},

      // Press Control+Search+Alt+a. Confirm the event is now VKEY_A only with
      // the Alt modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'a'>::Character},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'a'>::Character}},
  });

  // Remap Alt to Control.
  IntegerPrefMember alt;
  InitModifierKeyPref(&alt, prefs::kLanguageRemapAltKeyTo,
                      ui::chromeos::ModifierKey::kControlKey);

  TestChromeKeyboardVariants({
      // Press left Alt. Confirm the event is now VKEY_CONTROL
      // even though the Control key itself is disabled.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT, ui::EF_ALT_DOWN, ui::DomKey::ALT},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::CONTROL}},

      // Press Alt+a. Confirm the event is now Control+a even though the Control
      // key itself is disabled.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'a'>::Character},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'a'>::Character}},
  });
}

TEST_F(EventRewriterTest, TestRewriteModifiersRemapToControl) {
  // Remap Search to Control.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  IntegerPrefMember search;
  InitModifierKeyPref(&search, prefs::kLanguageRemapSearchKeyTo,
                      ui::chromeos::ModifierKey::kControlKey);

  TestChromeKeyboardVariants({
      // Press Search. Confirm the event is now VKEY_CONTROL.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_COMMAND_DOWN,
        ui::DomKey::META},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::CONTROL}},
  });

  // Remap Alt to Control too.
  IntegerPrefMember alt;
  InitModifierKeyPref(&alt, prefs::kLanguageRemapAltKeyTo,
                      ui::chromeos::ModifierKey::kControlKey);

  TestChromeKeyboardVariants({
      // Press Alt. Confirm the event is now VKEY_CONTROL.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT, ui::EF_ALT_DOWN, ui::DomKey::ALT},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::CONTROL}},

      // Press Alt+Search. Confirm the event is now VKEY_CONTROL.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN, ui::DomKey::META},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::CONTROL}},

      // Press Control+Alt+Search. Confirm the event is now VKEY_CONTROL.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN,
        ui::DomKey::META},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::CONTROL}},

      // Press Shift+Control+Alt+Search. Confirm the event is now Control with
      // Shift and Control modifiers.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT,
        ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN |
            ui::EF_COMMAND_DOWN,
        ui::DomKey::META},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
        ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN, ui::DomKey::CONTROL}},

      // Press Shift+Control+Alt+Search+B. Confirm the event is now B with Shift
      // and Control modifiers.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_B, ui::DomCode::US_B,
        ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN |
            ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'B'>::Character},
       {ui::VKEY_B, ui::DomCode::US_B, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'B'>::Character}},
  });
}

TEST_F(EventRewriterTest, TestRewriteModifiersRemapToEscape) {
  // Remap Search to Escape.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  IntegerPrefMember search;
  InitModifierKeyPref(&search, prefs::kLanguageRemapSearchKeyTo,
                      ui::chromeos::ModifierKey::kEscapeKey);

  TestChromeKeyboardVariants({
      // Press Search. Confirm the event is now VKEY_ESCAPE.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_COMMAND_DOWN,
        ui::DomKey::META},
       {ui::VKEY_ESCAPE, ui::DomCode::ESCAPE, ui::EF_NONE, ui::DomKey::ESCAPE}},
  });
}

TEST_F(EventRewriterTest, TestRewriteModifiersRemapEscapeToAlt) {
  // Remap Escape to Alt.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  IntegerPrefMember escape;
  InitModifierKeyPref(&escape, prefs::kLanguageRemapEscapeKeyTo,
                      ui::chromeos::ModifierKey::kAltKey);

  TestAllKeyboardVariants({
      // Press Escape. Confirm the event is now VKEY_MENU.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_ESCAPE, ui::DomCode::ESCAPE, ui::EF_NONE, ui::DomKey::ESCAPE},
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT, ui::EF_ALT_DOWN,
        ui::DomKey::ALT}},
      // Release Escape to clear flags.
      {ui::ET_KEY_RELEASED,
       {ui::VKEY_ESCAPE, ui::DomCode::ESCAPE, ui::EF_NONE, ui::DomKey::ESCAPE},
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT, ui::EF_NONE, ui::DomKey::ALT}},
  });
}

TEST_F(EventRewriterTest, TestRewriteModifiersRemapAltToControl) {
  // Remap Alt to Control.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  IntegerPrefMember alt;
  InitModifierKeyPref(&alt, prefs::kLanguageRemapAltKeyTo,
                      ui::chromeos::ModifierKey::kControlKey);

  TestAllKeyboardVariants({
      // Press left Alt. Confirm the event is now VKEY_CONTROL.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT, ui::EF_ALT_DOWN, ui::DomKey::ALT},
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::CONTROL}},
      // Press Shift+comma. Verify that only the flags are changed.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_OEM_COMMA, ui::DomCode::COMMA,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN, ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_OEM_COMMA, ui::DomCode::COMMA,
        ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'<'>::Character}},
      // Press Shift+9. Verify that only the flags are changed.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_9, ui::DomCode::DIGIT9, ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_9, ui::DomCode::DIGIT9,
        ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'('>::Character}},
  });
}

TEST_F(EventRewriterTest, TestRewriteModifiersRemapUnderEscapeControlAlt) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  // Remap Escape to Alt.
  IntegerPrefMember escape;
  InitModifierKeyPref(&escape, prefs::kLanguageRemapEscapeKeyTo,
                      ui::chromeos::ModifierKey::kAltKey);

  // Remap Alt to Control.
  IntegerPrefMember alt;
  InitModifierKeyPref(&alt, prefs::kLanguageRemapAltKeyTo,
                      ui::chromeos::ModifierKey::kControlKey);

  // Remap Control to Search.
  IntegerPrefMember control;
  InitModifierKeyPref(&control, prefs::kLanguageRemapControlKeyTo,
                      ui::chromeos::ModifierKey::kSearchKey);

  TestAllKeyboardVariants({
      // Press left Control. Confirm the event is now VKEY_LWIN.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_CONTROL_DOWN,
        ui::DomKey::CONTROL},
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_COMMAND_DOWN,
        ui::DomKey::META}},

      // Then, press all of the three, Control+Alt+Escape.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_ESCAPE, ui::DomCode::ESCAPE,
        ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN, ui::DomKey::CONTROL},
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT,
        ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN,
        ui::DomKey::ALT}},

      // Press Shift+Control+Alt+Escape.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_ESCAPE, ui::DomCode::ESCAPE,
        ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN,
        ui::DomKey::ESCAPE},
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT,
        ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN |
            ui::EF_COMMAND_DOWN,
        ui::DomKey::ALT}},

      // Press Shift+Control+Alt+B
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_B, ui::DomCode::US_B,
        ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN,
        ui::DomKey::Constant<'B'>::Character},
       {ui::VKEY_B, ui::DomCode::US_B,
        ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN |
            ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'B'>::Character}},
  });
}

TEST_F(EventRewriterTest,
       TestRewriteModifiersRemapUnderEscapeControlAltSearch) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  // Remap Escape to Alt.
  IntegerPrefMember escape;
  InitModifierKeyPref(&escape, prefs::kLanguageRemapEscapeKeyTo,
                      ui::chromeos::ModifierKey::kAltKey);

  // Remap Alt to Control.
  IntegerPrefMember alt;
  InitModifierKeyPref(&alt, prefs::kLanguageRemapAltKeyTo,
                      ui::chromeos::ModifierKey::kControlKey);

  // Remap Control to Search.
  IntegerPrefMember control;
  InitModifierKeyPref(&control, prefs::kLanguageRemapControlKeyTo,
                      ui::chromeos::ModifierKey::kSearchKey);

  // Remap Search to Backspace.
  IntegerPrefMember search;
  InitModifierKeyPref(&search, prefs::kLanguageRemapSearchKeyTo,
                      ui::chromeos::ModifierKey::kBackspaceKey);

  TestChromeKeyboardVariants({
      // Release Control and Escape, as Search and Alt would transform Backspace
      // to Delete.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_NONE,
        ui::DomKey::CONTROL},
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_COMMAND_DOWN,
        ui::DomKey::META}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_ESCAPE, ui::DomCode::ESCAPE, ui::EF_NONE, ui::DomKey::ESCAPE},
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT, ui::EF_ALT_DOWN,
        ui::DomKey::ALT}},
      {ui::ET_KEY_RELEASED,
       {ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT, ui::EF_NONE,
        ui::DomKey::CONTROL},
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_ALT_DOWN,
        ui::DomKey::META}},
      {ui::ET_KEY_RELEASED,
       {ui::VKEY_ESCAPE, ui::DomCode::ESCAPE, ui::EF_NONE, ui::DomKey::ESCAPE},
       {ui::VKEY_MENU, ui::DomCode::ALT_LEFT, ui::EF_NONE, ui::DomKey::ALT}},
      // Press Search. Confirm the event is now VKEY_BACK.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LWIN, ui::DomCode::META_LEFT, ui::EF_COMMAND_DOWN,
        ui::DomKey::META},
       {ui::VKEY_BACK, ui::DomCode::BACKSPACE, ui::EF_NONE,
        ui::DomKey::BACKSPACE}},
  });
}

TEST_F(EventRewriterTest, TestRewriteModifiersRemapBackspaceToEscape) {
  // Remap Backspace to Escape.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  IntegerPrefMember backspace;
  InitModifierKeyPref(&backspace, prefs::kLanguageRemapBackspaceKeyTo,
                      ui::chromeos::ModifierKey::kEscapeKey);

  TestAllKeyboardVariants({
      // Press Backspace. Confirm the event is now VKEY_ESCAPE.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BACK, ui::DomCode::BACKSPACE, ui::EF_NONE,
        ui::DomKey::BACKSPACE},
       {ui::VKEY_ESCAPE, ui::DomCode::ESCAPE, ui::EF_NONE, ui::DomKey::ESCAPE}},
  });
}

TEST_F(EventRewriterTest, TestRewriteModifiersRemapToCapsLock) {
  // Remap Search to Caps Lock.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  IntegerPrefMember search;
  InitModifierKeyPref(&search, prefs::kLanguageRemapSearchKeyTo,
                      ui::chromeos::ModifierKey::kCapsLockKey);

  chromeos::input_method::FakeImeKeyboard ime_keyboard;
  SetupKeyboard("Internal Keyboard");
  rewriter_->set_ime_keyboard_for_testing(&ime_keyboard);
  EXPECT_FALSE(ime_keyboard.caps_lock_is_enabled_);

  // Press Search.
  EXPECT_EQ(
      GetExpectedResultAsString(ui::ET_KEY_PRESSED, ui::VKEY_CAPITAL,
                                ui::DomCode::CAPS_LOCK,
                                ui::EF_MOD3_DOWN | ui::EF_CAPS_LOCK_ON,
                                ui::DomKey::CAPS_LOCK, kNoScanCode),
      GetRewrittenEventAsString(rewriter(), ui::ET_KEY_PRESSED, ui::VKEY_LWIN,
                                ui::DomCode::META_LEFT, ui::EF_COMMAND_DOWN,
                                ui::DomKey::META, kNoScanCode));
  EXPECT_FALSE(ime_keyboard.caps_lock_is_enabled_);

  // Release Search.
  EXPECT_EQ(
      GetExpectedResultAsString(ui::ET_KEY_RELEASED, ui::VKEY_CAPITAL,
                                ui::DomCode::CAPS_LOCK, ui::EF_NONE,
                                ui::DomKey::CAPS_LOCK, kNoScanCode),
      GetRewrittenEventAsString(rewriter(), ui::ET_KEY_RELEASED, ui::VKEY_LWIN,
                                ui::DomCode::META_LEFT, ui::EF_NONE,
                                ui::DomKey::META, kNoScanCode));
  EXPECT_TRUE(ime_keyboard.caps_lock_is_enabled_);

  // Press Search.
  EXPECT_EQ(GetExpectedResultAsString(ui::ET_KEY_PRESSED, ui::VKEY_CAPITAL,
                                      ui::DomCode::CAPS_LOCK,
                                      ui::EF_CAPS_LOCK_ON | ui::EF_MOD3_DOWN,
                                      ui::DomKey::CAPS_LOCK, kNoScanCode),
            GetRewrittenEventAsString(rewriter(), ui::ET_KEY_PRESSED,
                                      ui::VKEY_LWIN, ui::DomCode::META_LEFT,
                                      ui::EF_COMMAND_DOWN | ui::EF_CAPS_LOCK_ON,
                                      ui::DomKey::META, kNoScanCode));
  EXPECT_TRUE(ime_keyboard.caps_lock_is_enabled_);

  // Release Search.
  EXPECT_EQ(
      GetExpectedResultAsString(ui::ET_KEY_RELEASED, ui::VKEY_CAPITAL,
                                ui::DomCode::CAPS_LOCK, ui::EF_NONE,
                                ui::DomKey::CAPS_LOCK, kNoScanCode),
      GetRewrittenEventAsString(rewriter(), ui::ET_KEY_RELEASED, ui::VKEY_LWIN,
                                ui::DomCode::META_LEFT, ui::EF_NONE,
                                ui::DomKey::META, kNoScanCode));
  EXPECT_FALSE(ime_keyboard.caps_lock_is_enabled_);

  // Do the same on external Chrome OS keyboard.
  SetupKeyboard("External Chrome Keyboard", kKbdTopRowLayout1Tag,
                ui::INPUT_DEVICE_UNKNOWN);

  // Press Search.
  EXPECT_EQ(
      GetExpectedResultAsString(ui::ET_KEY_PRESSED, ui::VKEY_CAPITAL,
                                ui::DomCode::CAPS_LOCK,
                                ui::EF_MOD3_DOWN | ui::EF_CAPS_LOCK_ON,
                                ui::DomKey::CAPS_LOCK, kNoScanCode),
      GetRewrittenEventAsString(rewriter(), ui::ET_KEY_PRESSED, ui::VKEY_LWIN,
                                ui::DomCode::META_LEFT, ui::EF_COMMAND_DOWN,
                                ui::DomKey::META, kNoScanCode));
  EXPECT_FALSE(ime_keyboard.caps_lock_is_enabled_);

  // Release Search.
  EXPECT_EQ(
      GetExpectedResultAsString(ui::ET_KEY_RELEASED, ui::VKEY_CAPITAL,
                                ui::DomCode::CAPS_LOCK, ui::EF_NONE,
                                ui::DomKey::CAPS_LOCK, kNoScanCode),
      GetRewrittenEventAsString(rewriter(), ui::ET_KEY_RELEASED, ui::VKEY_LWIN,
                                ui::DomCode::META_LEFT, ui::EF_NONE,
                                ui::DomKey::META, kNoScanCode));
  EXPECT_TRUE(ime_keyboard.caps_lock_is_enabled_);

  // Press Search.
  EXPECT_EQ(GetExpectedResultAsString(ui::ET_KEY_PRESSED, ui::VKEY_CAPITAL,
                                      ui::DomCode::CAPS_LOCK,
                                      ui::EF_CAPS_LOCK_ON | ui::EF_MOD3_DOWN,
                                      ui::DomKey::CAPS_LOCK, kNoScanCode),
            GetRewrittenEventAsString(rewriter(), ui::ET_KEY_PRESSED,
                                      ui::VKEY_LWIN, ui::DomCode::META_LEFT,
                                      ui::EF_COMMAND_DOWN | ui::EF_CAPS_LOCK_ON,
                                      ui::DomKey::META, kNoScanCode));
  EXPECT_TRUE(ime_keyboard.caps_lock_is_enabled_);

  // Release Search.
  EXPECT_EQ(
      GetExpectedResultAsString(ui::ET_KEY_RELEASED, ui::VKEY_CAPITAL,
                                ui::DomCode::CAPS_LOCK, ui::EF_NONE,
                                ui::DomKey::CAPS_LOCK, kNoScanCode),
      GetRewrittenEventAsString(rewriter(), ui::ET_KEY_RELEASED, ui::VKEY_LWIN,
                                ui::DomCode::META_LEFT, ui::EF_NONE,
                                ui::DomKey::META, kNoScanCode));
  EXPECT_FALSE(ime_keyboard.caps_lock_is_enabled_);

  // Try external keyboard with Caps Lock.
  SetupKeyboard("External Generic Keyboard", kKbdTopRowLayoutUnspecified,
                ui::INPUT_DEVICE_UNKNOWN);

  // Press Caps Lock.
  EXPECT_EQ(GetExpectedResultAsString(ui::ET_KEY_PRESSED, ui::VKEY_CAPITAL,
                                      ui::DomCode::CAPS_LOCK,
                                      ui::EF_CAPS_LOCK_ON | ui::EF_MOD3_DOWN,
                                      ui::DomKey::CAPS_LOCK, kNoScanCode),
            GetRewrittenEventAsString(rewriter(), ui::ET_KEY_PRESSED,
                                      ui::VKEY_CAPITAL, ui::DomCode::CAPS_LOCK,
                                      ui::EF_CAPS_LOCK_ON | ui::EF_MOD3_DOWN,
                                      ui::DomKey::CAPS_LOCK, kNoScanCode));
  EXPECT_FALSE(ime_keyboard.caps_lock_is_enabled_);

  // Release Caps Lock.
  EXPECT_EQ(GetExpectedResultAsString(ui::ET_KEY_RELEASED, ui::VKEY_CAPITAL,
                                      ui::DomCode::CAPS_LOCK, ui::EF_NONE,
                                      ui::DomKey::CAPS_LOCK, kNoScanCode),
            GetRewrittenEventAsString(rewriter(), ui::ET_KEY_RELEASED,
                                      ui::VKEY_CAPITAL, ui::DomCode::CAPS_LOCK,
                                      ui::EF_NONE, ui::DomKey::CAPS_LOCK,
                                      kNoScanCode));
  EXPECT_TRUE(ime_keyboard.caps_lock_is_enabled_);
}

TEST_F(EventRewriterTest, TestRewriteCapsLock) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  chromeos::input_method::FakeImeKeyboard ime_keyboard;
  SetupKeyboard("External Generic Keyboard", kKbdTopRowLayoutUnspecified,
                ui::INPUT_DEVICE_UNKNOWN);
  rewriter_->set_ime_keyboard_for_testing(&ime_keyboard);
  EXPECT_FALSE(ime_keyboard.caps_lock_is_enabled_);

  // On Chrome OS, CapsLock is mapped to CapsLock with Mod3Mask.
  EXPECT_EQ(GetExpectedResultAsString(ui::ET_KEY_PRESSED, ui::VKEY_CAPITAL,
                                      ui::DomCode::CAPS_LOCK,
                                      ui::EF_CAPS_LOCK_ON | ui::EF_MOD3_DOWN,
                                      ui::DomKey::CAPS_LOCK, kNoScanCode),
            GetRewrittenEventAsString(rewriter(), ui::ET_KEY_PRESSED,
                                      ui::VKEY_CAPITAL, ui::DomCode::CAPS_LOCK,
                                      ui::EF_MOD3_DOWN, ui::DomKey::CAPS_LOCK,
                                      kNoScanCode));
  EXPECT_FALSE(ime_keyboard.caps_lock_is_enabled_);

  EXPECT_EQ(GetExpectedResultAsString(ui::ET_KEY_RELEASED, ui::VKEY_CAPITAL,
                                      ui::DomCode::CAPS_LOCK, ui::EF_NONE,
                                      ui::DomKey::CAPS_LOCK, kNoScanCode),
            GetRewrittenEventAsString(rewriter(), ui::ET_KEY_RELEASED,
                                      ui::VKEY_CAPITAL, ui::DomCode::CAPS_LOCK,
                                      ui::EF_MOD3_DOWN, ui::DomKey::CAPS_LOCK,
                                      kNoScanCode));
  EXPECT_TRUE(ime_keyboard.caps_lock_is_enabled_);

  // Remap Caps Lock to Control.
  IntegerPrefMember caps_lock;
  InitModifierKeyPref(&caps_lock, prefs::kLanguageRemapCapsLockKeyTo,
                      ui::chromeos::ModifierKey::kControlKey);

  // Press Caps Lock.
  EXPECT_EQ(GetExpectedResultAsString(
                ui::ET_KEY_PRESSED, ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                ui::EF_CONTROL_DOWN, ui::DomKey::CONTROL, kNoScanCode),
            GetRewrittenEventAsString(rewriter(), ui::ET_KEY_PRESSED,
                                      ui::VKEY_CAPITAL, ui::DomCode::CAPS_LOCK,
                                      ui::EF_CAPS_LOCK_ON | ui::EF_MOD3_DOWN,
                                      ui::DomKey::CAPS_LOCK, kNoScanCode));

  // Release Caps Lock.
  EXPECT_EQ(GetExpectedResultAsString(ui::ET_KEY_RELEASED, ui::VKEY_CONTROL,
                                      ui::DomCode::CONTROL_LEFT, ui::EF_NONE,
                                      ui::DomKey::CONTROL, kNoScanCode),
            GetRewrittenEventAsString(rewriter(), ui::ET_KEY_RELEASED,
                                      ui::VKEY_CAPITAL, ui::DomCode::CAPS_LOCK,
                                      ui::EF_NONE, ui::DomKey::CAPS_LOCK,
                                      kNoScanCode));
}

TEST_F(EventRewriterTest, TestRewriteCapsLockToControl) {
  // Remap CapsLock to Control.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  IntegerPrefMember control;
  InitModifierKeyPref(&control, prefs::kLanguageRemapCapsLockKeyTo,
                      ui::chromeos::ModifierKey::kControlKey);

  TestExternalGenericKeyboard({
      // Press CapsLock+a. Confirm that Mod3Mask is rewritten to ControlMask.
      // On Chrome OS, CapsLock works as a Mod3 modifier.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_MOD3_DOWN,
        ui::DomKey::Constant<'a'>::Character},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'a'>::Character}},

      // Press Control+CapsLock+a. Confirm that Mod3Mask is rewritten to
      // ControlMask
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_CONTROL_DOWN | ui::EF_MOD3_DOWN,
        ui::DomKey::Constant<'a'>::Character},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'a'>::Character}},

      // Press Alt+CapsLock+a. Confirm that Mod3Mask is rewritten to
      // ControlMask.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN | ui::EF_MOD3_DOWN,
        ui::DomKey::Constant<'a'>::Character},
       {ui::VKEY_A, ui::DomCode::US_A, ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'a'>::Character}},
  });
}

TEST_F(EventRewriterTest, TestRewriteCapsLockMod3InUse) {
  // Remap CapsLock to Control.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  IntegerPrefMember control;
  InitModifierKeyPref(&control, prefs::kLanguageRemapCapsLockKeyTo,
                      ui::chromeos::ModifierKey::kControlKey);

  SetupKeyboard("External Generic Keyboard", kKbdTopRowLayoutUnspecified,
                ui::INPUT_DEVICE_UNKNOWN);
  input_method_manager_mock_->set_mod3_used(true);

  // Press CapsLock+a. Confirm that Mod3Mask is NOT rewritten to ControlMask
  // when Mod3Mask is already in use by the current XKB layout.
  EXPECT_EQ(
      GetExpectedResultAsString(
          ui::ET_KEY_PRESSED, ui::VKEY_A, ui::DomCode::US_A, ui::EF_NONE,
          ui::DomKey::Constant<'a'>::Character, kNoScanCode),
      GetRewrittenEventAsString(
          rewriter(), ui::ET_KEY_PRESSED, ui::VKEY_A, ui::DomCode::US_A,
          ui::EF_NONE, ui::DomKey::Constant<'a'>::Character, kNoScanCode));

  input_method_manager_mock_->set_mod3_used(false);
}

TEST_F(EventRewriterTest, TestRewriteExtendedKeys) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  TestNonAppleKeyboardVariants({
      // Alt+Backspace -> Delete
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BACK, ui::DomCode::BACKSPACE, ui::EF_ALT_DOWN,
        ui::DomKey::BACKSPACE},
       {ui::VKEY_DELETE, ui::DomCode::DEL, ui::EF_NONE, ui::DomKey::DEL}},
      // Control+Alt+Backspace -> Control+Delete
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BACK, ui::DomCode::BACKSPACE,
        ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN, ui::DomKey::BACKSPACE},
       {ui::VKEY_DELETE, ui::DomCode::DEL, ui::EF_CONTROL_DOWN,
        ui::DomKey::DEL}},
      // Search+Alt+Backspace -> Alt+Backspace
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BACK, ui::DomCode::BACKSPACE,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN, ui::DomKey::BACKSPACE},
       {ui::VKEY_BACK, ui::DomCode::BACKSPACE, ui::EF_ALT_DOWN,
        ui::DomKey::BACKSPACE}},
      // Search+Control+Alt+Backspace -> Control+Alt+Backspace
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BACK, ui::DomCode::BACKSPACE,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN,
        ui::DomKey::BACKSPACE},
       {ui::VKEY_BACK, ui::DomCode::BACKSPACE,
        ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN, ui::DomKey::BACKSPACE}},
      // Alt+Up -> Prior
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_UP, ui::DomCode::ARROW_UP, ui::EF_ALT_DOWN,
        ui::DomKey::ARROW_UP},
       {ui::VKEY_PRIOR, ui::DomCode::PAGE_UP, ui::EF_NONE,
        ui::DomKey::PAGE_UP}},
      // Alt+Down -> Next
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_DOWN, ui::DomCode::ARROW_DOWN, ui::EF_ALT_DOWN,
        ui::DomKey::ARROW_DOWN},
       {ui::VKEY_NEXT, ui::DomCode::PAGE_DOWN, ui::EF_NONE,
        ui::DomKey::PAGE_DOWN}},
      // Ctrl+Alt+Up -> Home
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_UP, ui::DomCode::ARROW_UP,
        ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN, ui::DomKey::ARROW_UP},
       {ui::VKEY_HOME, ui::DomCode::HOME, ui::EF_NONE, ui::DomKey::HOME}},
      // Ctrl+Alt+Down -> End
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_DOWN, ui::DomCode::ARROW_DOWN,
        ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN, ui::DomKey::ARROW_DOWN},
       {ui::VKEY_END, ui::DomCode::END, ui::EF_NONE, ui::DomKey::END}},

      // Search+Ctrl+Alt+Up -> Ctrl+Alt+Up
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_UP, ui::DomCode::ARROW_UP,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN,
        ui::DomKey::ARROW_UP},
       {ui::VKEY_UP, ui::DomCode::ARROW_UP,
        ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN, ui::DomKey::ARROW_UP}},
      // Search+Ctrl+Alt+Down -> Ctrl+Alt+Down
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_DOWN, ui::DomCode::ARROW_DOWN,
        ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN,
        ui::DomKey::ARROW_DOWN},
       {ui::VKEY_DOWN, ui::DomCode::ARROW_DOWN,
        ui::EF_ALT_DOWN | ui::EF_CONTROL_DOWN, ui::DomKey::ARROW_DOWN}},

      // Period -> Period
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_OEM_PERIOD, ui::DomCode::PERIOD, ui::EF_NONE,
        ui::DomKey::Constant<'.'>::Character},
       {ui::VKEY_OEM_PERIOD, ui::DomCode::PERIOD, ui::EF_NONE,
        ui::DomKey::Constant<'.'>::Character}},

      // Search+Backspace -> Delete
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BACK, ui::DomCode::BACKSPACE, ui::EF_COMMAND_DOWN,
        ui::DomKey::BACKSPACE},
       {ui::VKEY_DELETE, ui::DomCode::DEL, ui::EF_NONE, ui::DomKey::DEL}},
      // Search+Up -> Prior
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_UP, ui::DomCode::ARROW_UP, ui::EF_COMMAND_DOWN,
        ui::DomKey::ARROW_UP},
       {ui::VKEY_PRIOR, ui::DomCode::PAGE_UP, ui::EF_NONE,
        ui::DomKey::PAGE_UP}},
      // Search+Down -> Next
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_DOWN, ui::DomCode::ARROW_DOWN, ui::EF_COMMAND_DOWN,
        ui::DomKey::ARROW_DOWN},
       {ui::VKEY_NEXT, ui::DomCode::PAGE_DOWN, ui::EF_NONE,
        ui::DomKey::PAGE_DOWN}},
      // Search+Left -> Home
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LEFT, ui::DomCode::ARROW_LEFT, ui::EF_COMMAND_DOWN,
        ui::DomKey::ARROW_LEFT},
       {ui::VKEY_HOME, ui::DomCode::HOME, ui::EF_NONE, ui::DomKey::HOME}},
      // Control+Search+Left -> Home
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_LEFT, ui::DomCode::ARROW_LEFT,
        ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN, ui::DomKey::ARROW_LEFT},
       {ui::VKEY_HOME, ui::DomCode::HOME, ui::EF_CONTROL_DOWN,
        ui::DomKey::HOME}},
      // Search+Right -> End
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_RIGHT, ui::DomCode::ARROW_RIGHT, ui::EF_COMMAND_DOWN,
        ui::DomKey::ARROW_RIGHT},
       {ui::VKEY_END, ui::DomCode::END, ui::EF_NONE, ui::DomKey::END}},
      // Control+Search+Right -> End
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_RIGHT, ui::DomCode::ARROW_RIGHT,
        ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN, ui::DomKey::ARROW_RIGHT},
       {ui::VKEY_END, ui::DomCode::END, ui::EF_CONTROL_DOWN, ui::DomKey::END}},
      // Search+Period -> Insert
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_OEM_PERIOD, ui::DomCode::PERIOD, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'.'>::Character},
       {ui::VKEY_INSERT, ui::DomCode::INSERT, ui::EF_NONE, ui::DomKey::INSERT}},
      // Control+Search+Period -> Control+Insert
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_OEM_PERIOD, ui::DomCode::PERIOD,
        ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN,
        ui::DomKey::Constant<'.'>::Character},
       {ui::VKEY_INSERT, ui::DomCode::INSERT, ui::EF_CONTROL_DOWN,
        ui::DomKey::INSERT}},
  });
}

TEST_F(EventRewriterTest, TestRewriteFunctionKeysCommon) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  TestNonAppleNonCustomLayoutKeyboardVariants({
      // The number row should not be rewritten without Search key.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_1, ui::DomCode::DIGIT1, ui::EF_NONE,
        ui::DomKey::Constant<'1'>::Character},
       {ui::VKEY_1, ui::DomCode::DIGIT1, ui::EF_NONE,
        ui::DomKey::Constant<'1'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_2, ui::DomCode::DIGIT2, ui::EF_NONE,
        ui::DomKey::Constant<'2'>::Character},
       {ui::VKEY_2, ui::DomCode::DIGIT2, ui::EF_NONE,
        ui::DomKey::Constant<'2'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_3, ui::DomCode::DIGIT3, ui::EF_NONE,
        ui::DomKey::Constant<'3'>::Character},
       {ui::VKEY_3, ui::DomCode::DIGIT3, ui::EF_NONE,
        ui::DomKey::Constant<'3'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_4, ui::DomCode::DIGIT4, ui::EF_NONE,
        ui::DomKey::Constant<'4'>::Character},
       {ui::VKEY_4, ui::DomCode::DIGIT4, ui::EF_NONE,
        ui::DomKey::Constant<'4'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_5, ui::DomCode::DIGIT5, ui::EF_NONE,
        ui::DomKey::Constant<'5'>::Character},
       {ui::VKEY_5, ui::DomCode::DIGIT5, ui::EF_NONE,
        ui::DomKey::Constant<'5'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_6, ui::DomCode::DIGIT6, ui::EF_NONE,
        ui::DomKey::Constant<'6'>::Character},
       {ui::VKEY_6, ui::DomCode::DIGIT6, ui::EF_NONE,
        ui::DomKey::Constant<'6'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_7, ui::DomCode::DIGIT7, ui::EF_NONE,
        ui::DomKey::Constant<'7'>::Character},
       {ui::VKEY_7, ui::DomCode::DIGIT7, ui::EF_NONE,
        ui::DomKey::Constant<'7'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_8, ui::DomCode::DIGIT8, ui::EF_NONE,
        ui::DomKey::Constant<'8'>::Character},
       {ui::VKEY_8, ui::DomCode::DIGIT8, ui::EF_NONE,
        ui::DomKey::Constant<'8'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_9, ui::DomCode::DIGIT9, ui::EF_NONE,
        ui::DomKey::Constant<'9'>::Character},
       {ui::VKEY_9, ui::DomCode::DIGIT9, ui::EF_NONE,
        ui::DomKey::Constant<'9'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_0, ui::DomCode::DIGIT0, ui::EF_NONE,
        ui::DomKey::Constant<'0'>::Character},
       {ui::VKEY_0, ui::DomCode::DIGIT0, ui::EF_NONE,
        ui::DomKey::Constant<'0'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_OEM_MINUS, ui::DomCode::MINUS, ui::EF_NONE,
        ui::DomKey::Constant<'-'>::Character},
       {ui::VKEY_OEM_MINUS, ui::DomCode::MINUS, ui::EF_NONE,
        ui::DomKey::Constant<'-'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_OEM_PLUS, ui::DomCode::EQUAL, ui::EF_NONE,
        ui::DomKey::Constant<'='>::Character},
       {ui::VKEY_OEM_PLUS, ui::DomCode::EQUAL, ui::EF_NONE,
        ui::DomKey::Constant<'='>::Character}},

      // The number row should be rewritten as the F<number> row with Search
      // key.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_1, ui::DomCode::DIGIT1, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'1'>::Character},
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_NONE, ui::DomKey::F1}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_2, ui::DomCode::DIGIT2, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'2'>::Character},
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_3, ui::DomCode::DIGIT3, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'3'>::Character},
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_4, ui::DomCode::DIGIT4, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'4'>::Character},
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_5, ui::DomCode::DIGIT5, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'5'>::Character},
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_NONE, ui::DomKey::F5}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_6, ui::DomCode::DIGIT6, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'6'>::Character},
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_NONE, ui::DomKey::F6}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_7, ui::DomCode::DIGIT7, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'7'>::Character},
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_8, ui::DomCode::DIGIT8, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'8'>::Character},
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_NONE, ui::DomKey::F8}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_9, ui::DomCode::DIGIT9, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'9'>::Character},
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_NONE, ui::DomKey::F9}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_0, ui::DomCode::DIGIT0, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'0'>::Character},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_OEM_MINUS, ui::DomCode::MINUS, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'-'>::Character},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_OEM_PLUS, ui::DomCode::EQUAL, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'='>::Character},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},

      // The function keys should not be rewritten with Search key pressed.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_COMMAND_DOWN, ui::DomKey::F1},
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_NONE, ui::DomKey::F1}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_COMMAND_DOWN, ui::DomKey::F2},
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_COMMAND_DOWN, ui::DomKey::F3},
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_COMMAND_DOWN, ui::DomKey::F4},
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_COMMAND_DOWN, ui::DomKey::F5},
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_NONE, ui::DomKey::F5}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_COMMAND_DOWN, ui::DomKey::F6},
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_NONE, ui::DomKey::F6}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_COMMAND_DOWN, ui::DomKey::F7},
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_COMMAND_DOWN, ui::DomKey::F8},
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_NONE, ui::DomKey::F8}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_COMMAND_DOWN, ui::DomKey::F9},
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_NONE, ui::DomKey::F9}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_COMMAND_DOWN, ui::DomKey::F10},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_COMMAND_DOWN, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_COMMAND_DOWN, ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},
  });
}

TEST_F(EventRewriterTest, TestRewriteFunctionKeysNonCustomLayouts) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  // Old CrOS keyboards that do not have custom layouts send F-Keys by default
  // and are translated by default to Actions based on hardcoded mappings.
  // New CrOS keyboards are not tested here because they do not remap F-Keys.
  TestNonAppleNonCustomLayoutKeyboardVariants({
      // F1 -> Back
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_NONE, ui::DomKey::F1},
       {ui::VKEY_BROWSER_BACK, ui::DomCode::BROWSER_BACK, ui::EF_NONE,
        ui::DomKey::BROWSER_BACK}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_CONTROL_DOWN, ui::DomKey::F1},
       {ui::VKEY_BROWSER_BACK, ui::DomCode::BROWSER_BACK, ui::EF_CONTROL_DOWN,
        ui::DomKey::BROWSER_BACK}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_ALT_DOWN, ui::DomKey::F1},
       {ui::VKEY_BROWSER_BACK, ui::DomCode::BROWSER_BACK, ui::EF_ALT_DOWN,
        ui::DomKey::BROWSER_BACK}},
      // F2 -> Forward
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2},
       {ui::VKEY_BROWSER_FORWARD, ui::DomCode::BROWSER_FORWARD, ui::EF_NONE,
        ui::DomKey::BROWSER_FORWARD}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_CONTROL_DOWN, ui::DomKey::F2},
       {ui::VKEY_BROWSER_FORWARD, ui::DomCode::BROWSER_FORWARD,
        ui::EF_CONTROL_DOWN, ui::DomKey::BROWSER_FORWARD}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_ALT_DOWN, ui::DomKey::F2},
       {ui::VKEY_BROWSER_FORWARD, ui::DomCode::BROWSER_FORWARD, ui::EF_ALT_DOWN,
        ui::DomKey::BROWSER_FORWARD}},
      // F3 -> Refresh
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3},
       {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH, ui::EF_NONE,
        ui::DomKey::BROWSER_REFRESH}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_CONTROL_DOWN, ui::DomKey::F3},
       {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH,
        ui::EF_CONTROL_DOWN, ui::DomKey::BROWSER_REFRESH}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_ALT_DOWN, ui::DomKey::F3},
       {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH, ui::EF_ALT_DOWN,
        ui::DomKey::BROWSER_REFRESH}},
      // F4 -> Launch App 2
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::ZOOM_TOGGLE, ui::EF_NONE,
        ui::DomKey::ZOOM_TOGGLE}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_CONTROL_DOWN, ui::DomKey::F4},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::ZOOM_TOGGLE,
        ui::EF_CONTROL_DOWN, ui::DomKey::ZOOM_TOGGLE}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_ALT_DOWN, ui::DomKey::F4},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::ZOOM_TOGGLE, ui::EF_ALT_DOWN,
        ui::DomKey::ZOOM_TOGGLE}},
      // F5 -> Launch App 1
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_NONE, ui::DomKey::F5},
       {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::SELECT_TASK, ui::EF_NONE,
        ui::DomKey::LAUNCH_MY_COMPUTER}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_CONTROL_DOWN, ui::DomKey::F5},
       {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::SELECT_TASK,
        ui::EF_CONTROL_DOWN, ui::DomKey::LAUNCH_MY_COMPUTER}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_ALT_DOWN, ui::DomKey::F5},
       {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::SELECT_TASK, ui::EF_ALT_DOWN,
        ui::DomKey::LAUNCH_MY_COMPUTER}},
      // F6 -> Brightness down
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_NONE, ui::DomKey::F6},
       {ui::VKEY_BRIGHTNESS_DOWN, ui::DomCode::BRIGHTNESS_DOWN, ui::EF_NONE,
        ui::DomKey::BRIGHTNESS_DOWN}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_CONTROL_DOWN, ui::DomKey::F6},
       {ui::VKEY_BRIGHTNESS_DOWN, ui::DomCode::BRIGHTNESS_DOWN,
        ui::EF_CONTROL_DOWN, ui::DomKey::BRIGHTNESS_DOWN}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_ALT_DOWN, ui::DomKey::F6},
       {ui::VKEY_BRIGHTNESS_DOWN, ui::DomCode::BRIGHTNESS_DOWN, ui::EF_ALT_DOWN,
        ui::DomKey::BRIGHTNESS_DOWN}},
      // F7 -> Brightness up
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7},
       {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP, ui::EF_NONE,
        ui::DomKey::BRIGHTNESS_UP}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_CONTROL_DOWN, ui::DomKey::F7},
       {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP, ui::EF_CONTROL_DOWN,
        ui::DomKey::BRIGHTNESS_UP}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_ALT_DOWN, ui::DomKey::F7},
       {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP, ui::EF_ALT_DOWN,
        ui::DomKey::BRIGHTNESS_UP}},
      // F8 -> Volume Mute
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_NONE, ui::DomKey::F8},
       {ui::VKEY_VOLUME_MUTE, ui::DomCode::VOLUME_MUTE, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_MUTE}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_CONTROL_DOWN, ui::DomKey::F8},
       {ui::VKEY_VOLUME_MUTE, ui::DomCode::VOLUME_MUTE, ui::EF_CONTROL_DOWN,
        ui::DomKey::AUDIO_VOLUME_MUTE}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_ALT_DOWN, ui::DomKey::F8},
       {ui::VKEY_VOLUME_MUTE, ui::DomCode::VOLUME_MUTE, ui::EF_ALT_DOWN,
        ui::DomKey::AUDIO_VOLUME_MUTE}},
      // F9 -> Volume Down
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_NONE, ui::DomKey::F9},
       {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_DOWN}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_CONTROL_DOWN, ui::DomKey::F9},
       {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN, ui::EF_CONTROL_DOWN,
        ui::DomKey::AUDIO_VOLUME_DOWN}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_ALT_DOWN, ui::DomKey::F9},
       {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN, ui::EF_ALT_DOWN,
        ui::DomKey::AUDIO_VOLUME_DOWN}},
      // F10 -> Volume Up
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10},
       {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_UP}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_CONTROL_DOWN, ui::DomKey::F10},
       {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_CONTROL_DOWN,
        ui::DomKey::AUDIO_VOLUME_UP}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_ALT_DOWN, ui::DomKey::F10},
       {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_ALT_DOWN,
        ui::DomKey::AUDIO_VOLUME_UP}},
      // F11 -> F11
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_CONTROL_DOWN, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_CONTROL_DOWN, ui::DomKey::F11}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_ALT_DOWN, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_ALT_DOWN, ui::DomKey::F11}},
      // F12 -> F12
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_CONTROL_DOWN, ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_CONTROL_DOWN, ui::DomKey::F12}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_ALT_DOWN, ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_ALT_DOWN, ui::DomKey::F12}},
  });
}

TEST_F(EventRewriterTest, TestRewriteFunctionKeysCustomLayoutsFKeyUnchanged) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  // On devices with custom layouts, the F-Keys are never remapped.
  TestChromeCustomLayoutKeyboardVariants({
      // F1-> F1
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_NONE, ui::DomKey::F1},
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_NONE, ui::DomKey::F1}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_CONTROL_DOWN, ui::DomKey::F1},
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_CONTROL_DOWN, ui::DomKey::F1}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_ALT_DOWN, ui::DomKey::F1},
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_ALT_DOWN, ui::DomKey::F1}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_COMMAND_DOWN, ui::DomKey::F1},
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_COMMAND_DOWN, ui::DomKey::F1}},
      // F2 -> F2
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2},
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_CONTROL_DOWN, ui::DomKey::F2},
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_CONTROL_DOWN, ui::DomKey::F2}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_ALT_DOWN, ui::DomKey::F2},
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_ALT_DOWN, ui::DomKey::F2}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_COMMAND_DOWN, ui::DomKey::F2},
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_COMMAND_DOWN, ui::DomKey::F2}},
      // F3 -> F3
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3},
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_CONTROL_DOWN, ui::DomKey::F3},
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_CONTROL_DOWN, ui::DomKey::F3}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_ALT_DOWN, ui::DomKey::F3},
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_ALT_DOWN, ui::DomKey::F3}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_COMMAND_DOWN, ui::DomKey::F3},
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_COMMAND_DOWN, ui::DomKey::F3}},
      // F4 -> F4
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4},
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_CONTROL_DOWN, ui::DomKey::F4},
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_CONTROL_DOWN, ui::DomKey::F4}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_ALT_DOWN, ui::DomKey::F4},
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_ALT_DOWN, ui::DomKey::F4}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_COMMAND_DOWN, ui::DomKey::F4},
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_COMMAND_DOWN, ui::DomKey::F4}},
      // F5 -> F5
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_NONE, ui::DomKey::F5},
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_NONE, ui::DomKey::F5}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_CONTROL_DOWN, ui::DomKey::F5},
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_CONTROL_DOWN, ui::DomKey::F5}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_ALT_DOWN, ui::DomKey::F5},
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_ALT_DOWN, ui::DomKey::F5}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_COMMAND_DOWN, ui::DomKey::F5},
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_COMMAND_DOWN, ui::DomKey::F5}},
      // F6 -> F6
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_NONE, ui::DomKey::F6},
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_NONE, ui::DomKey::F6}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_CONTROL_DOWN, ui::DomKey::F6},
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_CONTROL_DOWN, ui::DomKey::F6}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_ALT_DOWN, ui::DomKey::F6},
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_ALT_DOWN, ui::DomKey::F6}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_COMMAND_DOWN, ui::DomKey::F6},
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_COMMAND_DOWN, ui::DomKey::F6}},
      // F7 -> F7
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7},
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_CONTROL_DOWN, ui::DomKey::F7},
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_CONTROL_DOWN, ui::DomKey::F7}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_ALT_DOWN, ui::DomKey::F7},
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_ALT_DOWN, ui::DomKey::F7}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_COMMAND_DOWN, ui::DomKey::F7},
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_COMMAND_DOWN, ui::DomKey::F7}},
      // F8 -> F8
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_NONE, ui::DomKey::F8},
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_NONE, ui::DomKey::F8}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_CONTROL_DOWN, ui::DomKey::F8},
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_CONTROL_DOWN, ui::DomKey::F8}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_ALT_DOWN, ui::DomKey::F8},
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_ALT_DOWN, ui::DomKey::F8}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_COMMAND_DOWN, ui::DomKey::F8},
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_COMMAND_DOWN, ui::DomKey::F8}},
      // F9 -> F9
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_NONE, ui::DomKey::F9},
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_NONE, ui::DomKey::F9}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_CONTROL_DOWN, ui::DomKey::F9},
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_CONTROL_DOWN, ui::DomKey::F9}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_ALT_DOWN, ui::DomKey::F9},
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_ALT_DOWN, ui::DomKey::F9}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_COMMAND_DOWN, ui::DomKey::F9},
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_COMMAND_DOWN, ui::DomKey::F9}},
      // F10 -> F10
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_CONTROL_DOWN, ui::DomKey::F10},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_CONTROL_DOWN, ui::DomKey::F10}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_ALT_DOWN, ui::DomKey::F10},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_ALT_DOWN, ui::DomKey::F10}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_COMMAND_DOWN, ui::DomKey::F10},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_COMMAND_DOWN, ui::DomKey::F10}},
      // F11 -> F11
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_CONTROL_DOWN, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_CONTROL_DOWN, ui::DomKey::F11}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_ALT_DOWN, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_ALT_DOWN, ui::DomKey::F11}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_COMMAND_DOWN, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_COMMAND_DOWN, ui::DomKey::F11}},
      // F12 -> F12
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_CONTROL_DOWN, ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_CONTROL_DOWN, ui::DomKey::F12}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_ALT_DOWN, ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_ALT_DOWN, ui::DomKey::F12}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_COMMAND_DOWN, ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_COMMAND_DOWN, ui::DomKey::F12}},
      // F13 -> F13
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F13, ui::DomCode::F13, ui::EF_NONE, ui::DomKey::F13},
       {ui::VKEY_F13, ui::DomCode::F13, ui::EF_NONE, ui::DomKey::F13}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F13, ui::DomCode::F13, ui::EF_CONTROL_DOWN, ui::DomKey::F13},
       {ui::VKEY_F13, ui::DomCode::F13, ui::EF_CONTROL_DOWN, ui::DomKey::F13}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F13, ui::DomCode::F13, ui::EF_ALT_DOWN, ui::DomKey::F13},
       {ui::VKEY_F13, ui::DomCode::F13, ui::EF_ALT_DOWN, ui::DomKey::F13}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F13, ui::DomCode::F13, ui::EF_COMMAND_DOWN, ui::DomKey::F13},
       {ui::VKEY_F13, ui::DomCode::F13, ui::EF_COMMAND_DOWN, ui::DomKey::F13}},
      // F14 -> F14
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F14, ui::DomCode::F14, ui::EF_NONE, ui::DomKey::F14},
       {ui::VKEY_F14, ui::DomCode::F14, ui::EF_NONE, ui::DomKey::F14}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F14, ui::DomCode::F14, ui::EF_CONTROL_DOWN, ui::DomKey::F14},
       {ui::VKEY_F14, ui::DomCode::F14, ui::EF_CONTROL_DOWN, ui::DomKey::F14}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F14, ui::DomCode::F14, ui::EF_ALT_DOWN, ui::DomKey::F14},
       {ui::VKEY_F14, ui::DomCode::F14, ui::EF_ALT_DOWN, ui::DomKey::F14}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F14, ui::DomCode::F14, ui::EF_COMMAND_DOWN, ui::DomKey::F14},
       {ui::VKEY_F14, ui::DomCode::F14, ui::EF_COMMAND_DOWN, ui::DomKey::F14}},
      // F15 -> F15
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F15, ui::DomCode::F15, ui::EF_NONE, ui::DomKey::F15},
       {ui::VKEY_F15, ui::DomCode::F15, ui::EF_NONE, ui::DomKey::F15}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F15, ui::DomCode::F15, ui::EF_CONTROL_DOWN, ui::DomKey::F15},
       {ui::VKEY_F15, ui::DomCode::F15, ui::EF_CONTROL_DOWN, ui::DomKey::F15}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F15, ui::DomCode::F15, ui::EF_ALT_DOWN, ui::DomKey::F15},
       {ui::VKEY_F15, ui::DomCode::F15, ui::EF_ALT_DOWN, ui::DomKey::F15}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F15, ui::DomCode::F15, ui::EF_COMMAND_DOWN, ui::DomKey::F15},
       {ui::VKEY_F15, ui::DomCode::F15, ui::EF_COMMAND_DOWN, ui::DomKey::F15}},
  });
}

TEST_F(EventRewriterTest, TestRewriteFunctionKeysCustomLayoutsActionUnchanged) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  // An action key on these devices is one where the scan code matches an entry
  // in the layout map. It doesn't matter what the action is, as long the
  // search key isn't pressed it will pass through unchanged.
  const std::string layout = "a1 a2 a3";
  TestKeyboard("Internal Custom Layout Keyboard", layout,
               ui::INPUT_DEVICE_INTERNAL, /*has_custom_top_row=*/true,
               {
                   {ui::ET_KEY_PRESSED,
                    {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH,
                     ui::EF_NONE, ui::DomKey::BROWSER_REFRESH, 0xa1},
                    {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH,
                     ui::EF_NONE, ui::DomKey::BROWSER_REFRESH, 0xa1}},
                   {ui::ET_KEY_PRESSED,
                    {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_NONE,
                     ui::DomKey::AUDIO_VOLUME_UP, 0xa2},
                    {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_NONE,
                     ui::DomKey::AUDIO_VOLUME_UP, 0xa2}},
                   {ui::ET_KEY_PRESSED,
                    {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN,
                     ui::EF_NONE, ui::DomKey::AUDIO_VOLUME_DOWN, 0xa3},
                    {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN,
                     ui::EF_NONE, ui::DomKey::AUDIO_VOLUME_DOWN, 0xa3}},
               });
}

TEST_F(EventRewriterTest, TestRewriteFunctionKeysCustomLayouts) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  // On devices with custom layouts, scan codes that match the layout
  // map get mapped to F-Keys based only on the scan code. The search
  // key also gets treated as unpressed in the remapped event.
  const std::string layout = "a1 a2 a3 a4 a5 a6 a7 a8 a9 aa ab ac ad ae af";
  TestKeyboard(
      "Internal Custom Layout Keyboard", layout, ui::INPUT_DEVICE_INTERNAL,
      /*has_custom_top_row=*/true,
      {
          // Action -> F1
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xa1},
           {ui::VKEY_F1, ui::DomCode::F1, ui::EF_NONE, ui::DomKey::F1, 0xa1}},
          // Action -> F2
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xa2},
           {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2, 0xa2}},
          // Action -> F3
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xa3},
           {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3, 0xa3}},
          // Action -> F4
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xa4},
           {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4, 0xa4}},
          // Action -> F5
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xa5},
           {ui::VKEY_F5, ui::DomCode::F5, ui::EF_NONE, ui::DomKey::F5, 0xa5}},
          // Action -> F6
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xa6},
           {ui::VKEY_F6, ui::DomCode::F6, ui::EF_NONE, ui::DomKey::F6, 0xa6}},
          // Action -> F7
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xa7},
           {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7, 0xa7}},
          // Action -> F8
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xa8},
           {ui::VKEY_F8, ui::DomCode::F8, ui::EF_NONE, ui::DomKey::F8, 0xa8}},
          // Action -> F9
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xa9},
           {ui::VKEY_F9, ui::DomCode::F9, ui::EF_NONE, ui::DomKey::F9, 0xa9}},
          // Action -> F10
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xaa},
           {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10,
            0xaa}},
          // Action -> F11
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xab},
           {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11,
            0xab}},
          // Action -> F12
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xac},
           {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12,
            0xac}},
          // Action -> F13
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xad},
           {ui::VKEY_F13, ui::DomCode::F13, ui::EF_NONE, ui::DomKey::F13,
            0xad}},
          // Action -> F14
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xae},
           {ui::VKEY_F14, ui::DomCode::F14, ui::EF_NONE, ui::DomKey::F14,
            0xae}},
          // Action -> F15
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_UNKNOWN, ui::DomCode::NONE, ui::EF_COMMAND_DOWN,
            ui::DomKey::NONE, 0xaf},
           {ui::VKEY_F15, ui::DomCode::F15, ui::EF_NONE, ui::DomKey::F15,
            0xaf}},
      });
}

TEST_F(EventRewriterTest, TestRewriteFunctionKeysLayout2) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  TestKeyboard(
      "Internal Keyboard", kKbdTopRowLayout2Tag, ui::INPUT_DEVICE_INTERNAL,
      /*has_custom_top_row=*/false,
      {
          // F1 -> Back
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F1, ui::DomCode::F1, ui::EF_NONE, ui::DomKey::F1},
           {ui::VKEY_BROWSER_BACK, ui::DomCode::BROWSER_BACK, ui::EF_NONE,
            ui::DomKey::BROWSER_BACK}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F1, ui::DomCode::F1, ui::EF_CONTROL_DOWN, ui::DomKey::F1},
           {ui::VKEY_BROWSER_BACK, ui::DomCode::BROWSER_BACK,
            ui::EF_CONTROL_DOWN, ui::DomKey::BROWSER_BACK}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F1, ui::DomCode::F1, ui::EF_ALT_DOWN, ui::DomKey::F1},
           {ui::VKEY_BROWSER_BACK, ui::DomCode::BROWSER_BACK, ui::EF_ALT_DOWN,
            ui::DomKey::BROWSER_BACK}},
          // F2 -> Refresh
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2},
           {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH, ui::EF_NONE,
            ui::DomKey::BROWSER_REFRESH}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F2, ui::DomCode::F2, ui::EF_CONTROL_DOWN, ui::DomKey::F2},
           {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH,
            ui::EF_CONTROL_DOWN, ui::DomKey::BROWSER_REFRESH}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F2, ui::DomCode::F2, ui::EF_ALT_DOWN, ui::DomKey::F2},
           {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH,
            ui::EF_ALT_DOWN, ui::DomKey::BROWSER_REFRESH}},
          // F3 -> Launch App 2
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3},
           {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::ZOOM_TOGGLE, ui::EF_NONE,
            ui::DomKey::ZOOM_TOGGLE}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F3, ui::DomCode::F3, ui::EF_CONTROL_DOWN, ui::DomKey::F3},
           {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::ZOOM_TOGGLE,
            ui::EF_CONTROL_DOWN, ui::DomKey::ZOOM_TOGGLE}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F3, ui::DomCode::F3, ui::EF_ALT_DOWN, ui::DomKey::F3},
           {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::ZOOM_TOGGLE,
            ui::EF_ALT_DOWN, ui::DomKey::ZOOM_TOGGLE}},
          // F4 -> Launch App 1
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4},
           {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::SELECT_TASK, ui::EF_NONE,
            ui::DomKey::LAUNCH_MY_COMPUTER}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F4, ui::DomCode::F4, ui::EF_CONTROL_DOWN, ui::DomKey::F4},
           {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::SELECT_TASK,
            ui::EF_CONTROL_DOWN, ui::DomKey::LAUNCH_MY_COMPUTER}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F4, ui::DomCode::F4, ui::EF_ALT_DOWN, ui::DomKey::F4},
           {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::SELECT_TASK,
            ui::EF_ALT_DOWN, ui::DomKey::LAUNCH_MY_COMPUTER}},
          // F5 -> Brightness down
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F5, ui::DomCode::F5, ui::EF_NONE, ui::DomKey::F5},
           {ui::VKEY_BRIGHTNESS_DOWN, ui::DomCode::BRIGHTNESS_DOWN, ui::EF_NONE,
            ui::DomKey::BRIGHTNESS_DOWN}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F5, ui::DomCode::F5, ui::EF_CONTROL_DOWN, ui::DomKey::F5},
           {ui::VKEY_BRIGHTNESS_DOWN, ui::DomCode::BRIGHTNESS_DOWN,
            ui::EF_CONTROL_DOWN, ui::DomKey::BRIGHTNESS_DOWN}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F5, ui::DomCode::F5, ui::EF_ALT_DOWN, ui::DomKey::F5},
           {ui::VKEY_BRIGHTNESS_DOWN, ui::DomCode::BRIGHTNESS_DOWN,
            ui::EF_ALT_DOWN, ui::DomKey::BRIGHTNESS_DOWN}},
          // F6 -> Brightness up
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F6, ui::DomCode::F6, ui::EF_NONE, ui::DomKey::F6},
           {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP, ui::EF_NONE,
            ui::DomKey::BRIGHTNESS_UP}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F6, ui::DomCode::F6, ui::EF_CONTROL_DOWN, ui::DomKey::F6},
           {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP,
            ui::EF_CONTROL_DOWN, ui::DomKey::BRIGHTNESS_UP}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F6, ui::DomCode::F6, ui::EF_ALT_DOWN, ui::DomKey::F6},
           {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP, ui::EF_ALT_DOWN,
            ui::DomKey::BRIGHTNESS_UP}},
          // F7 -> Media Play/Pause
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7},
           {ui::VKEY_MEDIA_PLAY_PAUSE, ui::DomCode::MEDIA_PLAY_PAUSE,
            ui::EF_NONE, ui::DomKey::MEDIA_PLAY_PAUSE}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F7, ui::DomCode::F7, ui::EF_CONTROL_DOWN, ui::DomKey::F7},
           {ui::VKEY_MEDIA_PLAY_PAUSE, ui::DomCode::MEDIA_PLAY_PAUSE,
            ui::EF_CONTROL_DOWN, ui::DomKey::MEDIA_PLAY_PAUSE}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F7, ui::DomCode::F7, ui::EF_ALT_DOWN, ui::DomKey::F7},
           {ui::VKEY_MEDIA_PLAY_PAUSE, ui::DomCode::MEDIA_PLAY_PAUSE,
            ui::EF_ALT_DOWN, ui::DomKey::MEDIA_PLAY_PAUSE}},
          // F8 -> Volume Mute
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F8, ui::DomCode::F8, ui::EF_NONE, ui::DomKey::F8},
           {ui::VKEY_VOLUME_MUTE, ui::DomCode::VOLUME_MUTE, ui::EF_NONE,
            ui::DomKey::AUDIO_VOLUME_MUTE}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F8, ui::DomCode::F8, ui::EF_CONTROL_DOWN, ui::DomKey::F8},
           {ui::VKEY_VOLUME_MUTE, ui::DomCode::VOLUME_MUTE, ui::EF_CONTROL_DOWN,
            ui::DomKey::AUDIO_VOLUME_MUTE}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F8, ui::DomCode::F8, ui::EF_ALT_DOWN, ui::DomKey::F8},
           {ui::VKEY_VOLUME_MUTE, ui::DomCode::VOLUME_MUTE, ui::EF_ALT_DOWN,
            ui::DomKey::AUDIO_VOLUME_MUTE}},
          // F9 -> Volume Down
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F9, ui::DomCode::F9, ui::EF_NONE, ui::DomKey::F9},
           {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN, ui::EF_NONE,
            ui::DomKey::AUDIO_VOLUME_DOWN}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F9, ui::DomCode::F9, ui::EF_CONTROL_DOWN, ui::DomKey::F9},
           {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN, ui::EF_CONTROL_DOWN,
            ui::DomKey::AUDIO_VOLUME_DOWN}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F9, ui::DomCode::F9, ui::EF_ALT_DOWN, ui::DomKey::F9},
           {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN, ui::EF_ALT_DOWN,
            ui::DomKey::AUDIO_VOLUME_DOWN}},
          // F10 -> Volume Up
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10},
           {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_NONE,
            ui::DomKey::AUDIO_VOLUME_UP}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F10, ui::DomCode::F10, ui::EF_CONTROL_DOWN,
            ui::DomKey::F10},
           {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_CONTROL_DOWN,
            ui::DomKey::AUDIO_VOLUME_UP}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F10, ui::DomCode::F10, ui::EF_ALT_DOWN, ui::DomKey::F10},
           {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_ALT_DOWN,
            ui::DomKey::AUDIO_VOLUME_UP}},
          // F11 -> F11
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11},
           {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F11, ui::DomCode::F11, ui::EF_CONTROL_DOWN,
            ui::DomKey::F11},
           {ui::VKEY_F11, ui::DomCode::F11, ui::EF_CONTROL_DOWN,
            ui::DomKey::F11}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F11, ui::DomCode::F11, ui::EF_ALT_DOWN, ui::DomKey::F11},
           {ui::VKEY_F11, ui::DomCode::F11, ui::EF_ALT_DOWN, ui::DomKey::F11}},
          // F12 -> F12
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12},
           {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F12, ui::DomCode::F12, ui::EF_CONTROL_DOWN,
            ui::DomKey::F12},
           {ui::VKEY_F12, ui::DomCode::F12, ui::EF_CONTROL_DOWN,
            ui::DomKey::F12}},
          {ui::ET_KEY_PRESSED,
           {ui::VKEY_F12, ui::DomCode::F12, ui::EF_ALT_DOWN, ui::DomKey::F12},
           {ui::VKEY_F12, ui::DomCode::F12, ui::EF_ALT_DOWN, ui::DomKey::F12}},
      });
}

TEST_F(EventRewriterTest, TestRewriteFunctionKeysWilcoLayouts) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  std::vector<KeyTestCase> wilco_standard_tests({
      // F1 -> F1, Search + F1 -> Back
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_NONE, ui::DomKey::F1},
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_NONE, ui::DomKey::F1}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_COMMAND_DOWN, ui::DomKey::F1},
       {ui::VKEY_BROWSER_BACK, ui::DomCode::BROWSER_BACK, ui::EF_NONE,
        ui::DomKey::BROWSER_BACK}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_CONTROL_DOWN, ui::DomKey::F1},
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_CONTROL_DOWN, ui::DomKey::F1}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_ALT_DOWN, ui::DomKey::F1},
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_ALT_DOWN, ui::DomKey::F1}},
      // F2 -> F2, Search + F2 -> Refresh
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2},
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_COMMAND_DOWN, ui::DomKey::F2},
       {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH, ui::EF_NONE,
        ui::DomKey::BROWSER_REFRESH}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_CONTROL_DOWN, ui::DomKey::F2},
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_CONTROL_DOWN, ui::DomKey::F2}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_ALT_DOWN, ui::DomKey::F2},
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_ALT_DOWN, ui::DomKey::F2}},
      // F3 -> F3, Search + F3 -> Full Screen
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3},
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_COMMAND_DOWN, ui::DomKey::F3},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::ZOOM_TOGGLE, ui::EF_NONE,
        ui::DomKey::ZOOM_TOGGLE}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_CONTROL_DOWN, ui::DomKey::F3},
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_CONTROL_DOWN, ui::DomKey::F3}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_ALT_DOWN, ui::DomKey::F3},
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_ALT_DOWN, ui::DomKey::F3}},
      // F4 -> F4, Search + F4 -> Launch App 1
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4},
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_COMMAND_DOWN, ui::DomKey::F4},
       {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::F4, ui::EF_NONE,
        ui::DomKey::F4}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_CONTROL_DOWN, ui::DomKey::F4},
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_CONTROL_DOWN, ui::DomKey::F4}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_ALT_DOWN, ui::DomKey::F4},
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_ALT_DOWN, ui::DomKey::F4}},
      // F5 -> F5, Search + F5 -> Brightness down
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_NONE, ui::DomKey::F5},
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_NONE, ui::DomKey::F5}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_COMMAND_DOWN, ui::DomKey::F5},
       {ui::VKEY_BRIGHTNESS_DOWN, ui::DomCode::BRIGHTNESS_DOWN, ui::EF_NONE,
        ui::DomKey::BRIGHTNESS_DOWN}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_CONTROL_DOWN, ui::DomKey::F5},
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_CONTROL_DOWN, ui::DomKey::F5}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_ALT_DOWN, ui::DomKey::F5},
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_ALT_DOWN, ui::DomKey::F5}},
      // F6 -> F6, Search + F6 -> Brightness up
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_NONE, ui::DomKey::F6},
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_NONE, ui::DomKey::F6}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_COMMAND_DOWN, ui::DomKey::F6},
       {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP, ui::EF_NONE,
        ui::DomKey::BRIGHTNESS_UP}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_CONTROL_DOWN, ui::DomKey::F6},
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_CONTROL_DOWN, ui::DomKey::F6}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_ALT_DOWN, ui::DomKey::F6},
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_ALT_DOWN, ui::DomKey::F6}},
      // F7 -> F7, Search + F7 -> Volume mute
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7},
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_COMMAND_DOWN, ui::DomKey::F7},
       {ui::VKEY_VOLUME_MUTE, ui::DomCode::VOLUME_MUTE, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_MUTE}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_CONTROL_DOWN, ui::DomKey::F7},
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_CONTROL_DOWN, ui::DomKey::F7}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_ALT_DOWN, ui::DomKey::F7},
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_ALT_DOWN, ui::DomKey::F7}},
      // F8 -> F8, Search + F8 -> Volume Down
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_NONE, ui::DomKey::F8},
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_NONE, ui::DomKey::F8}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_COMMAND_DOWN, ui::DomKey::F8},
       {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_DOWN}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_CONTROL_DOWN, ui::DomKey::F8},
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_CONTROL_DOWN, ui::DomKey::F8}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_ALT_DOWN, ui::DomKey::F8},
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_ALT_DOWN, ui::DomKey::F8}},
      // F9 -> F9, Search + F9 -> Volume Up
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_NONE, ui::DomKey::F9},
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_NONE, ui::DomKey::F9}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_COMMAND_DOWN, ui::DomKey::F9},
       {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_UP}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_CONTROL_DOWN, ui::DomKey::F9},
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_CONTROL_DOWN, ui::DomKey::F9}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_ALT_DOWN, ui::DomKey::F9},
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_ALT_DOWN, ui::DomKey::F9}},
      // F10 -> F10, Search + F10 -> F10
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_COMMAND_DOWN, ui::DomKey::F10},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_CONTROL_DOWN, ui::DomKey::F10},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_CONTROL_DOWN, ui::DomKey::F10}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_ALT_DOWN, ui::DomKey::F10},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_ALT_DOWN, ui::DomKey::F10}},
      // F11 -> F11, Search + F11 -> F11
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_COMMAND_DOWN, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_CONTROL_DOWN, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_CONTROL_DOWN, ui::DomKey::F11}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_ALT_DOWN, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_ALT_DOWN, ui::DomKey::F11}},
      // F12 -> F12
      // Search + F12 differs between Wilco devices so it is tested separately.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_CONTROL_DOWN, ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_CONTROL_DOWN, ui::DomKey::F12}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_ALT_DOWN, ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_ALT_DOWN, ui::DomKey::F12}},
      // The number row should not be rewritten without Search key.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_1, ui::DomCode::DIGIT1, ui::EF_NONE,
        ui::DomKey::Constant<'1'>::Character},
       {ui::VKEY_1, ui::DomCode::DIGIT1, ui::EF_NONE,
        ui::DomKey::Constant<'1'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_2, ui::DomCode::DIGIT2, ui::EF_NONE,
        ui::DomKey::Constant<'2'>::Character},
       {ui::VKEY_2, ui::DomCode::DIGIT2, ui::EF_NONE,
        ui::DomKey::Constant<'2'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_3, ui::DomCode::DIGIT3, ui::EF_NONE,
        ui::DomKey::Constant<'3'>::Character},
       {ui::VKEY_3, ui::DomCode::DIGIT3, ui::EF_NONE,
        ui::DomKey::Constant<'3'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_4, ui::DomCode::DIGIT4, ui::EF_NONE,
        ui::DomKey::Constant<'4'>::Character},
       {ui::VKEY_4, ui::DomCode::DIGIT4, ui::EF_NONE,
        ui::DomKey::Constant<'4'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_5, ui::DomCode::DIGIT5, ui::EF_NONE,
        ui::DomKey::Constant<'5'>::Character},
       {ui::VKEY_5, ui::DomCode::DIGIT5, ui::EF_NONE,
        ui::DomKey::Constant<'5'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_6, ui::DomCode::DIGIT6, ui::EF_NONE,
        ui::DomKey::Constant<'6'>::Character},
       {ui::VKEY_6, ui::DomCode::DIGIT6, ui::EF_NONE,
        ui::DomKey::Constant<'6'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_7, ui::DomCode::DIGIT7, ui::EF_NONE,
        ui::DomKey::Constant<'7'>::Character},
       {ui::VKEY_7, ui::DomCode::DIGIT7, ui::EF_NONE,
        ui::DomKey::Constant<'7'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_8, ui::DomCode::DIGIT8, ui::EF_NONE,
        ui::DomKey::Constant<'8'>::Character},
       {ui::VKEY_8, ui::DomCode::DIGIT8, ui::EF_NONE,
        ui::DomKey::Constant<'8'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_9, ui::DomCode::DIGIT9, ui::EF_NONE,
        ui::DomKey::Constant<'9'>::Character},
       {ui::VKEY_9, ui::DomCode::DIGIT9, ui::EF_NONE,
        ui::DomKey::Constant<'9'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_0, ui::DomCode::DIGIT0, ui::EF_NONE,
        ui::DomKey::Constant<'0'>::Character},
       {ui::VKEY_0, ui::DomCode::DIGIT0, ui::EF_NONE,
        ui::DomKey::Constant<'0'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_OEM_MINUS, ui::DomCode::MINUS, ui::EF_NONE,
        ui::DomKey::Constant<'-'>::Character},
       {ui::VKEY_OEM_MINUS, ui::DomCode::MINUS, ui::EF_NONE,
        ui::DomKey::Constant<'-'>::Character}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_OEM_PLUS, ui::DomCode::EQUAL, ui::EF_NONE,
        ui::DomKey::Constant<'='>::Character},
       {ui::VKEY_OEM_PLUS, ui::DomCode::EQUAL, ui::EF_NONE,
        ui::DomKey::Constant<'='>::Character}},

      // The number row should be rewritten as the F<number> row with Search
      // key.
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_1, ui::DomCode::DIGIT1, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'1'>::Character},
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_NONE, ui::DomKey::F1}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_2, ui::DomCode::DIGIT2, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'2'>::Character},
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_3, ui::DomCode::DIGIT3, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'3'>::Character},
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_4, ui::DomCode::DIGIT4, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'4'>::Character},
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_5, ui::DomCode::DIGIT5, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'5'>::Character},
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_NONE, ui::DomKey::F5}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_6, ui::DomCode::DIGIT6, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'6'>::Character},
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_NONE, ui::DomKey::F6}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_7, ui::DomCode::DIGIT7, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'7'>::Character},
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_8, ui::DomCode::DIGIT8, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'8'>::Character},
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_NONE, ui::DomKey::F8}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_9, ui::DomCode::DIGIT9, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'9'>::Character},
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_NONE, ui::DomKey::F9}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_0, ui::DomCode::DIGIT0, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'0'>::Character},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_OEM_MINUS, ui::DomCode::MINUS, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'-'>::Character},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_OEM_PLUS, ui::DomCode::EQUAL, ui::EF_COMMAND_DOWN,
        ui::DomKey::Constant<'='>::Character},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},
  });

  KeyTestCase wilco_1_test =
      // Search + F12 -> Ctrl + Launch App 2 (Display toggle)
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_COMMAND_DOWN, ui::DomKey::F12},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::F12, ui::EF_CONTROL_DOWN,
        ui::DomKey::F12}};

  KeyTestCase drallion_test_no_privacy_screen =
      // Search + F12 -> F12 (Privacy screen not supported)
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_COMMAND_DOWN, ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}};

  KeyTestCase drallion_test_privacy_screen =
      // F12 -> F12, Search + F12 -> Privacy Screen Toggle
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_COMMAND_DOWN, ui::DomKey::F12},
       {ui::VKEY_PRIVACY_SCREEN_TOGGLE, ui::DomCode::PRIVACY_SCREEN_TOGGLE,
        ui::EF_NONE, ui::DomKey::F12}};

  // Set keyboard layout to Wilco 1.0
  SetupKeyboard("Wilco Keyboard", kKbdTopRowLayoutWilcoTag);
  // Standard key tests using Wilco 1.0 keyboard
  for (const auto& test : wilco_standard_tests)
    CheckKeyTestCase(rewriter(), test);
  CheckKeyTestCase(rewriter(), wilco_1_test);

  // Set keyboard layout to Drallion (Wilco 1.5)
  SetupKeyboard("Drallion Keyboard", kKbdTopRowLayoutDrallionTag);

  // Run key tests using Drallion keyboard layout (no privacy screen)
  rewriter_->set_privacy_screen_for_testing(false);
  for (const auto& test : wilco_standard_tests)
    CheckKeyTestCase(rewriter(), test);
  CheckKeyTestCase(rewriter(), drallion_test_no_privacy_screen);

  // Run key tests using Drallion keyboard layout (privacy screen supported)
  rewriter_->set_privacy_screen_for_testing(true);
  for (const auto& test : wilco_standard_tests)
    CheckKeyTestCase(rewriter(), test);
  CheckKeyTestCase(rewriter(), drallion_test_privacy_screen);
}

TEST_F(EventRewriterTest, TestRewriteActionKeysWilcoLayouts) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  KeyTestCase wilco_standard_tests[] = {
      // Back -> Back, Search + Back -> F1
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BROWSER_BACK, ui::DomCode::BROWSER_BACK, ui::EF_NONE,
        ui::DomKey::BROWSER_BACK},
       {ui::VKEY_BROWSER_BACK, ui::DomCode::BROWSER_BACK, ui::EF_NONE,
        ui::DomKey::BROWSER_BACK}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BROWSER_BACK, ui::DomCode::BROWSER_BACK, ui::EF_COMMAND_DOWN,
        ui::DomKey::BROWSER_BACK},
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_NONE, ui::DomKey::F1}},
      // Refresh -> Refresh, Search + Refresh -> F2
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH, ui::EF_NONE,
        ui::DomKey::BROWSER_REFRESH},
       {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH, ui::EF_NONE,
        ui::DomKey::BROWSER_REFRESH}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH,
        ui::EF_COMMAND_DOWN, ui::DomKey::BROWSER_REFRESH},
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2}},
      // Full Screen -> Full Screen, Search + Full Screen -> F3
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_ZOOM, ui::DomCode::ZOOM_TOGGLE, ui::EF_NONE,
        ui::DomKey::ZOOM_TOGGLE},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::ZOOM_TOGGLE, ui::EF_NONE,
        ui::DomKey::ZOOM_TOGGLE}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_ZOOM, ui::DomCode::ZOOM_TOGGLE, ui::EF_COMMAND_DOWN,
        ui::DomKey::ZOOM_TOGGLE},
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3}},
      // Launch App 1 -> Launch App 1, Search + Launch App 1 -> F4
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::F4, ui::EF_NONE,
        ui::DomKey::F4},
       {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::F4, ui::EF_NONE,
        ui::DomKey::F4}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::F4, ui::EF_COMMAND_DOWN,
        ui::DomKey::F4},
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4}},
      // Brightness down -> Brightness Down, Search Brightness Down -> F5
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BRIGHTNESS_DOWN, ui::DomCode::BRIGHTNESS_DOWN, ui::EF_NONE,
        ui::DomKey::BRIGHTNESS_DOWN},
       {ui::VKEY_BRIGHTNESS_DOWN, ui::DomCode::BRIGHTNESS_DOWN, ui::EF_NONE,
        ui::DomKey::BRIGHTNESS_DOWN}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BRIGHTNESS_DOWN, ui::DomCode::BRIGHTNESS_DOWN,
        ui::EF_COMMAND_DOWN, ui::DomKey::BRIGHTNESS_DOWN},
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_NONE, ui::DomKey::F5}},
      // Brightness up -> Brightness Up, Search + Brightness Up -> F6
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP, ui::EF_NONE,
        ui::DomKey::BRIGHTNESS_UP},
       {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP, ui::EF_NONE,
        ui::DomKey::BRIGHTNESS_UP}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP, ui::EF_COMMAND_DOWN,
        ui::DomKey::BRIGHTNESS_UP},
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_NONE, ui::DomKey::F6}},
      // Volume mute -> Volume Mute, Search + Volume Mute -> F7
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_VOLUME_MUTE, ui::DomCode::VOLUME_MUTE, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_MUTE},
       {ui::VKEY_VOLUME_MUTE, ui::DomCode::VOLUME_MUTE, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_MUTE}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_VOLUME_MUTE, ui::DomCode::VOLUME_MUTE, ui::EF_COMMAND_DOWN,
        ui::DomKey::AUDIO_VOLUME_MUTE},
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7}},
      // Volume Down -> Volume Down, Search + Volume Down -> F8
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_DOWN},
       {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_DOWN}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN, ui::EF_COMMAND_DOWN,
        ui::DomKey::AUDIO_VOLUME_DOWN},
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_NONE, ui::DomKey::F8}},
      // Volume Up -> Volume Up, Search + Volume Up -> F9
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_UP},
       {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_UP}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_COMMAND_DOWN,
        ui::DomKey::AUDIO_VOLUME_UP},
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_NONE, ui::DomKey::F9}},
      // F10 -> F10
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_COMMAND_DOWN, ui::DomKey::F10},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10}},
      // F11 -> F11
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_COMMAND_DOWN, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11}}};

  KeyTestCase wilco_1_tests[] = {
      // Ctrl + Launch App 1 (Display toggle) -> Unchanged
      // Search + Ctrl + Launch App 1 (Display toggle) -> F12
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::F12, ui::EF_CONTROL_DOWN,
        ui::DomKey::F12},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::F12, ui::EF_CONTROL_DOWN,
        ui::DomKey::F12}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::F12,
        ui::EF_COMMAND_DOWN + ui::EF_CONTROL_DOWN, ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}}};

  KeyTestCase drallion_tests_no_privacy_screen[] = {
      // Privacy Screen Toggle -> F12 (Privacy Screen not supported),
      // Search + Privacy Screen Toggle -> F12
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_PRIVACY_SCREEN_TOGGLE, ui::DomCode::PRIVACY_SCREEN_TOGGLE,
        ui::EF_NONE, ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_PRIVACY_SCREEN_TOGGLE, ui::DomCode::PRIVACY_SCREEN_TOGGLE,
        ui::EF_COMMAND_DOWN, ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},
      // Ctrl + Launch App 1 (Display toggle) -> Unchanged
      // Search + Ctrl + Launch App 1 (Display toggle) -> Unchanged
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::NONE, ui::EF_CONTROL_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::NONE, ui::EF_CONTROL_DOWN,
        ui::DomKey::UNIDENTIFIED}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::NONE,
        ui::EF_COMMAND_DOWN + ui::EF_CONTROL_DOWN, ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::NONE, ui::EF_CONTROL_DOWN,
        ui::DomKey::UNIDENTIFIED}}};

  KeyTestCase drallion_tests_privacy_screen[] = {
      // Privacy Screen Toggle -> Privacy Screen Toggle,
      // Search + Privacy Screen Toggle -> F12
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_PRIVACY_SCREEN_TOGGLE, ui::DomCode::PRIVACY_SCREEN_TOGGLE,
        ui::EF_NONE, ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_PRIVACY_SCREEN_TOGGLE, ui::DomCode::PRIVACY_SCREEN_TOGGLE,
        ui::EF_NONE, ui::DomKey::UNIDENTIFIED}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_PRIVACY_SCREEN_TOGGLE, ui::DomCode::PRIVACY_SCREEN_TOGGLE,
        ui::EF_COMMAND_DOWN, ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},
      // Ctrl + Launch App 1 (Display toggle) -> Unchanged
      // Search + Ctrl + Launch App 1 (Display toggle) -> Unchanged
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::NONE, ui::EF_CONTROL_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::NONE, ui::EF_CONTROL_DOWN,
        ui::DomKey::UNIDENTIFIED}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::NONE,
        ui::EF_COMMAND_DOWN + ui::EF_CONTROL_DOWN, ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::NONE, ui::EF_CONTROL_DOWN,
        ui::DomKey::UNIDENTIFIED}}};

  // Set keyboard layout to Wilco 1.0
  SetupKeyboard("Wilco Keyboard", kKbdTopRowLayoutWilcoTag);
  // Standard key tests using Wilco 1.0 keyboard
  for (const auto& test : wilco_standard_tests)
    CheckKeyTestCase(rewriter(), test);
  // Wilco 1.0 specific key tests
  for (const auto& test : wilco_1_tests)
    CheckKeyTestCase(rewriter(), test);

  // Set keyboard layout to Drallion (Wilco 1.5)
  SetupKeyboard("Drallion Keyboard", kKbdTopRowLayoutDrallionTag);

  // Standard key tests using Drallion keyboard layout
  for (const auto& test : wilco_standard_tests)
    CheckKeyTestCase(rewriter(), test);

  // Drallion specific key tests (no privacy screen)
  rewriter_->set_privacy_screen_for_testing(false);
  for (const auto& test : drallion_tests_no_privacy_screen)
    CheckKeyTestCase(rewriter(), test);

  // Drallion specific key tests (privacy screen supported)
  rewriter_->set_privacy_screen_for_testing(true);
  for (const auto& test : drallion_tests_privacy_screen)
    CheckKeyTestCase(rewriter(), test);
}

TEST_F(EventRewriterTest, TestTopRowAsFnKeysForKeyboardWilcoLayouts) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  // Enable preference treat-top-row-as-function-keys.
  // That causes action keys to be mapped back to Fn keys, unless the search
  // key is pressed.
  BooleanPrefMember top_row_as_fn_key;
  top_row_as_fn_key.Init(prefs::kLanguageSendFunctionKeys, prefs());
  top_row_as_fn_key.SetValue(true);

  KeyTestCase wilco_standard_tests[] = {
      // Back -> F1, Search + Back -> Back
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BROWSER_BACK, ui::DomCode::BROWSER_BACK, ui::EF_NONE,
        ui::DomKey::BROWSER_BACK},
       {ui::VKEY_F1, ui::DomCode::F1, ui::EF_NONE, ui::DomKey::F1}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BROWSER_BACK, ui::DomCode::BROWSER_BACK, ui::EF_COMMAND_DOWN,
        ui::DomKey::BROWSER_BACK},
       {ui::VKEY_BROWSER_BACK, ui::DomCode::BROWSER_BACK, ui::EF_NONE,
        ui::DomKey::BROWSER_BACK}},
      // Refresh -> F2, Search + Refresh -> Refresh
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH, ui::EF_NONE,
        ui::DomKey::BROWSER_REFRESH},
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH,
        ui::EF_COMMAND_DOWN, ui::DomKey::BROWSER_REFRESH},
       {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH, ui::EF_NONE,
        ui::DomKey::BROWSER_REFRESH}},
      // Full Screen -> F3, Search + Full Screen -> Full Screen
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_ZOOM, ui::DomCode::ZOOM_TOGGLE, ui::EF_NONE,
        ui::DomKey::ZOOM_TOGGLE},
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_ZOOM, ui::DomCode::ZOOM_TOGGLE, ui::EF_COMMAND_DOWN,
        ui::DomKey::ZOOM_TOGGLE},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::ZOOM_TOGGLE, ui::EF_NONE,
        ui::DomKey::ZOOM_TOGGLE}},
      // Launch App 1 -> F4, Search + Launch App 1 -> Launch App 1
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::F4, ui::EF_NONE,
        ui::DomKey::F4},
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::F4, ui::EF_COMMAND_DOWN,
        ui::DomKey::F4},
       {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::F4, ui::EF_NONE,
        ui::DomKey::F4}},
      // Brightness down -> F5, Search Brightness Down -> Brightness Down
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BRIGHTNESS_DOWN, ui::DomCode::BRIGHTNESS_DOWN, ui::EF_NONE,
        ui::DomKey::BRIGHTNESS_DOWN},
       {ui::VKEY_F5, ui::DomCode::F5, ui::EF_NONE, ui::DomKey::F5}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BRIGHTNESS_DOWN, ui::DomCode::BRIGHTNESS_DOWN,
        ui::EF_COMMAND_DOWN, ui::DomKey::BRIGHTNESS_DOWN},
       {ui::VKEY_BRIGHTNESS_DOWN, ui::DomCode::BRIGHTNESS_DOWN, ui::EF_NONE,
        ui::DomKey::BRIGHTNESS_DOWN}},
      // Brightness up -> F6, Search + Brightness Up -> Brightness Up
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP, ui::EF_NONE,
        ui::DomKey::BRIGHTNESS_UP},
       {ui::VKEY_F6, ui::DomCode::F6, ui::EF_NONE, ui::DomKey::F6}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP, ui::EF_COMMAND_DOWN,
        ui::DomKey::BRIGHTNESS_UP},
       {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP, ui::EF_NONE,
        ui::DomKey::BRIGHTNESS_UP}},
      // Volume mute -> F7, Search + Volume Mute -> Volume Mute
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_VOLUME_MUTE, ui::DomCode::VOLUME_MUTE, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_MUTE},
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_VOLUME_MUTE, ui::DomCode::VOLUME_MUTE, ui::EF_COMMAND_DOWN,
        ui::DomKey::AUDIO_VOLUME_MUTE},
       {ui::VKEY_VOLUME_MUTE, ui::DomCode::VOLUME_MUTE, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_MUTE}},
      // Volume Down -> F8, Search + Volume Down -> Volume Down
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_DOWN},
       {ui::VKEY_F8, ui::DomCode::F8, ui::EF_NONE, ui::DomKey::F8}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN, ui::EF_COMMAND_DOWN,
        ui::DomKey::AUDIO_VOLUME_DOWN},
       {ui::VKEY_VOLUME_DOWN, ui::DomCode::VOLUME_DOWN, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_DOWN}},
      // Volume Up -> F9, Search + Volume Up -> Volume Up
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_UP},
       {ui::VKEY_F9, ui::DomCode::F9, ui::EF_NONE, ui::DomKey::F9}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_COMMAND_DOWN,
        ui::DomKey::AUDIO_VOLUME_UP},
       {ui::VKEY_VOLUME_UP, ui::DomCode::VOLUME_UP, ui::EF_NONE,
        ui::DomKey::AUDIO_VOLUME_UP}},
      // F10 -> F10
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_COMMAND_DOWN, ui::DomKey::F10},
       {ui::VKEY_F10, ui::DomCode::F10, ui::EF_NONE, ui::DomKey::F10}},
      // F11 -> F11
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_COMMAND_DOWN, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11},
       {ui::VKEY_F11, ui::DomCode::F11, ui::EF_NONE, ui::DomKey::F11}}};

  KeyTestCase wilco_1_tests[] = {
      // Ctrl + Launch App 1 (Display toggle) -> F12
      // Search + Ctrl + Launch App 1 (Display toggle) -> Unchanged
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::F12, ui::EF_CONTROL_DOWN,
        ui::DomKey::F12},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::F12,
        ui::EF_COMMAND_DOWN + ui::EF_CONTROL_DOWN, ui::DomKey::F12},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::F12, ui::EF_CONTROL_DOWN,
        ui::DomKey::F12}}};

  KeyTestCase drallion_tests_no_privacy_screen[] = {
      // Privacy Screen Toggle -> F12,
      // Search + Privacy Screen Toggle -> F12 (Privacy screen not supported)
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_PRIVACY_SCREEN_TOGGLE, ui::DomCode::PRIVACY_SCREEN_TOGGLE,
        ui::EF_NONE, ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_PRIVACY_SCREEN_TOGGLE, ui::DomCode::PRIVACY_SCREEN_TOGGLE,
        ui::EF_COMMAND_DOWN, ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},
      // Ctrl + Launch App 1 (Display toggle) -> Unchanged
      // Search + Ctrl + Launch App 1 (Display toggle) -> Unchanged
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::NONE, ui::EF_CONTROL_DOWN,
        ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::NONE, ui::EF_CONTROL_DOWN,
        ui::DomKey::UNIDENTIFIED}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::NONE,
        ui::EF_COMMAND_DOWN + ui::EF_CONTROL_DOWN, ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::NONE, ui::EF_CONTROL_DOWN,
        ui::DomKey::UNIDENTIFIED}}};

  KeyTestCase drallion_tests_privacy_screen[] = {
      // Privacy Screen Toggle -> F12,
      // Search + Privacy Screen Toggle -> Unchanged
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_PRIVACY_SCREEN_TOGGLE, ui::DomCode::PRIVACY_SCREEN_TOGGLE,
        ui::EF_NONE, ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_F12, ui::DomCode::F12, ui::EF_NONE, ui::DomKey::F12}},
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_PRIVACY_SCREEN_TOGGLE, ui::DomCode::PRIVACY_SCREEN_TOGGLE,
        ui::EF_COMMAND_DOWN, ui::DomKey::UNIDENTIFIED},
       {ui::VKEY_PRIVACY_SCREEN_TOGGLE, ui::DomCode::PRIVACY_SCREEN_TOGGLE,
        ui::EF_NONE, ui::DomKey::UNIDENTIFIED}}};

  // Run key test cases for Wilco 1.0 keyboard layout
  SetupKeyboard("Wilco Keyboard", kKbdTopRowLayoutWilcoTag);
  // Standard key tests using Wilco 1.0 keyboard
  for (const auto& test : wilco_standard_tests)
    CheckKeyTestCase(rewriter(), test);
  // Wilco 1.0 specific key tests
  for (const auto& test : wilco_1_tests)
    CheckKeyTestCase(rewriter(), test);

  // Run key test cases for Drallion (Wilco 1.5) keyboard layout
  SetupKeyboard("Drallion Keyboard", kKbdTopRowLayoutDrallionTag);
  // Standard key tests using Drallion keyboard layout
  for (const auto& test : wilco_standard_tests)
    CheckKeyTestCase(rewriter(), test);

  // Drallion specific key tests (no privacy screen)
  rewriter_->set_privacy_screen_for_testing(false);
  for (const auto& test : drallion_tests_no_privacy_screen)
    CheckKeyTestCase(rewriter(), test);

  // Drallion specific key tests (privacy screen supported)
  rewriter_->set_privacy_screen_for_testing(true);
  for (const auto& test : drallion_tests_privacy_screen)
    CheckKeyTestCase(rewriter(), test);
}

TEST_F(EventRewriterTest, TestRewriteFunctionKeysInvalidLayout) {
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());

  // Not adding a keyboard simulates a failure in getting top row layout, which
  // will fallback to Layout1 in which case keys are rewritten to their default
  // values.
  KeyTestCase invalid_layout_tests[] = {
      // F2 -> Forward
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2},
       {ui::VKEY_BROWSER_FORWARD, ui::DomCode::BROWSER_FORWARD, ui::EF_NONE,
        ui::DomKey::BROWSER_FORWARD}},
      // F3 -> Refresh
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3},
       {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH, ui::EF_NONE,
        ui::DomKey::BROWSER_REFRESH}},
      // F4 -> Launch App 2
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::ZOOM_TOGGLE, ui::EF_NONE,
        ui::DomKey::ZOOM_TOGGLE}},
      // F7 -> Brightness up
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7},
       {ui::VKEY_BRIGHTNESS_UP, ui::DomCode::BRIGHTNESS_UP, ui::EF_NONE,
        ui::DomKey::BRIGHTNESS_UP}},
  };

  for (const auto& test : invalid_layout_tests)
    CheckKeyTestCase(rewriter(), test);

  // Adding a keyboard with a valid layout will take effect.
  const std::vector<KeyTestCase> layout2_tests({
      // F2 -> Refresh
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F2, ui::DomCode::F2, ui::EF_NONE, ui::DomKey::F2},
       {ui::VKEY_BROWSER_REFRESH, ui::DomCode::BROWSER_REFRESH, ui::EF_NONE,
        ui::DomKey::BROWSER_REFRESH}},
      // F3 -> Launch App 2
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F3, ui::DomCode::F3, ui::EF_NONE, ui::DomKey::F3},
       {ui::VKEY_MEDIA_LAUNCH_APP2, ui::DomCode::ZOOM_TOGGLE, ui::EF_NONE,
        ui::DomKey::ZOOM_TOGGLE}},
      // F4 -> Launch App 1
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F4, ui::DomCode::F4, ui::EF_NONE, ui::DomKey::F4},
       {ui::VKEY_MEDIA_LAUNCH_APP1, ui::DomCode::SELECT_TASK, ui::EF_NONE,
        ui::DomKey::LAUNCH_MY_COMPUTER}},
      // F7 -> Media Play/Pause
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_F7, ui::DomCode::F7, ui::EF_NONE, ui::DomKey::F7},
       {ui::VKEY_MEDIA_PLAY_PAUSE, ui::DomCode::MEDIA_PLAY_PAUSE, ui::EF_NONE,
        ui::DomKey::MEDIA_PLAY_PAUSE}},
  });

  TestKeyboard("Internal Keyboard", kKbdTopRowLayout2Tag,
               ui::INPUT_DEVICE_INTERNAL, /*has_custom_top_row=*/false,
               layout2_tests);
}

TEST_F(EventRewriterTest, TestRewriteExtendedKeysWithSearchRemapped) {
  // Remap Search to Control.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  IntegerPrefMember search;
  InitModifierKeyPref(&search, prefs::kLanguageRemapSearchKeyTo,
                      ui::chromeos::ModifierKey::kControlKey);

  TestChromeKeyboardVariants({
      // Alt+Search+Down -> End
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_DOWN, ui::DomCode::ARROW_DOWN,
        ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN, ui::DomKey::ARROW_DOWN},
       {ui::VKEY_END, ui::DomCode::END, ui::EF_NONE, ui::DomKey::END}},

      // Shift+Alt+Search+Down -> Shift+End
      {ui::ET_KEY_PRESSED,
       {ui::VKEY_DOWN, ui::DomCode::ARROW_DOWN,
        ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN,
        ui::DomKey::ARROW_DOWN},
       {ui::VKEY_END, ui::DomCode::END, ui::EF_SHIFT_DOWN, ui::DomKey::END}},
  });
}

TEST_F(EventRewriterTest, TestRewriteKeyEventSentByXSendEvent) {
  // Remap Control to Alt.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  IntegerPrefMember control;
  InitModifierKeyPref(&control, prefs::kLanguageRemapControlKeyTo,
                      ui::chromeos::ModifierKey::kAltKey);

  SetupKeyboard("Internal Keyboard");

  // Send left control press.
  {
    ui::KeyEvent keyevent(ui::ET_KEY_PRESSED, ui::VKEY_CONTROL,
                          ui::DomCode::CONTROL_LEFT, ui::EF_FINAL,
                          ui::DomKey::CONTROL, ui::EventTimeForNow());
    TestEventRewriterContinuation continuation;
    // Control should NOT be remapped to Alt if EF_FINAL is set.
    rewriter()->RewriteEvent(keyevent,
                             continuation.weak_ptr_factory_.GetWeakPtr());
    EXPECT_TRUE(continuation.rewritten_events.empty());
    EXPECT_EQ(1u, continuation.passthrough_events.size());
    EXPECT_TRUE(continuation.passthrough_events[0]->IsKeyEvent());
    const auto* result = continuation.passthrough_events[0]->AsKeyEvent();
    EXPECT_EQ(ui::VKEY_CONTROL, result->key_code());
  }
}

TEST_F(EventRewriterTest, TestRewriteNonNativeEvent) {
  // Remap Control to Alt.
  chromeos::Preferences::RegisterProfilePrefs(prefs()->registry());
  IntegerPrefMember control;
  InitModifierKeyPref(&control, prefs::kLanguageRemapControlKeyTo,
                      ui::chromeos::ModifierKey::kAltKey);

  SetupKeyboard("Internal Keyboard");

  const int kTouchId = 2;
  gfx::Point location(0, 0);
  ui::TouchEvent press(
      ui::ET_TOUCH_PRESSED, location, base::TimeTicks(),
      ui::PointerDetails(ui::EventPointerType::kTouch, kTouchId));
  press.set_flags(ui::EF_CONTROL_DOWN);

  TestEventRewriterContinuation continuation;
  rewriter()->RewriteEvent(press, continuation.weak_ptr_factory_.GetWeakPtr());
  EXPECT_TRUE(continuation.passthrough_events.empty());
  EXPECT_EQ(1u, continuation.rewritten_events.size());
  // Control should be remapped to Alt.
  EXPECT_EQ(ui::EF_ALT_DOWN, continuation.rewritten_events[0]->flags() &
                                 (ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN));
}

// Keeps a buffer of handled events.
class EventBuffer : public ui::test::TestEventProcessor {
 public:
  EventBuffer() {}
  ~EventBuffer() override {}

  void PopEvents(std::vector<std::unique_ptr<ui::Event>>* events) {
    events->clear();
    events->swap(events_);
  }

 private:
  // ui::EventSink overrides:
  ui::EventDispatchDetails OnEventFromSource(ui::Event* event) override {
    events_.push_back(ui::Event::Clone(*event));
    return ui::EventDispatchDetails();
  }

  std::vector<std::unique_ptr<ui::Event>> events_;

  DISALLOW_COPY_AND_ASSIGN(EventBuffer);
};

// Trivial EventSource that does nothing but send events.
class TestEventSource : public ui::EventSource {
 public:
  explicit TestEventSource(ui::EventProcessor* processor)
      : processor_(processor) {}
  ui::EventSink* GetEventSink() override { return processor_; }
  ui::EventDispatchDetails Send(ui::Event* event) {
    return SendEventToSink(event);
  }

 private:
  ui::EventProcessor* processor_;
};

// Tests of event rewriting that depend on the Ash window manager.
class EventRewriterAshTest : public ChromeAshTestBase {
 public:
  EventRewriterAshTest()
      : source_(&buffer_),
        fake_user_manager_(new chromeos::FakeChromeUserManager),
        user_manager_enabler_(base::WrapUnique(fake_user_manager_)) {}
  ~EventRewriterAshTest() override {}

  ui::EventDispatchDetails Send(ui::Event* event) {
    return source_.Send(event);
  }

  void SendKeyEvent(ui::EventType type,
                    ui::KeyboardCode key_code,
                    ui::DomCode code,
                    ui::DomKey key) {
    SendKeyEvent(type, key_code, code, key, ui::EF_NONE);
  }

  void SendKeyEvent(ui::EventType type,
                    ui::KeyboardCode key_code,
                    ui::DomCode code,
                    ui::DomKey key,
                    int flags) {
    ui::KeyEvent press(type, key_code, code, flags, key, ui::EventTimeForNow());
    ui::EventDispatchDetails details = Send(&press);
    CHECK(!details.dispatcher_destroyed);
  }

  void SendActivateStickyKeyPattern(ui::KeyboardCode key_code,
                                    ui::DomCode code,
                                    ui::DomKey key) {
    SendKeyEvent(ui::ET_KEY_PRESSED, key_code, code, key);
    SendKeyEvent(ui::ET_KEY_RELEASED, key_code, code, key);
  }

 protected:
  sync_preferences::TestingPrefServiceSyncable* prefs() { return &prefs_; }

  void InitModifierKeyPref(IntegerPrefMember* int_pref,
                           const std::string& pref_name,
                           ui::chromeos::ModifierKey modifierKey) {
    int_pref->Init(pref_name, prefs());
    int_pref->SetValue(static_cast<int>(modifierKey));
  }

  void PopEvents(std::vector<std::unique_ptr<ui::Event>>* events) {
    buffer_.PopEvents(events);
  }

  void SetUp() override {
    ChromeAshTestBase::SetUp();
    sticky_keys_controller_ = ash::Shell::Get()->sticky_keys_controller();
    delegate_ = std::make_unique<EventRewriterDelegateImpl>(nullptr);
    delegate_->set_pref_service_for_testing(prefs());
    rewriter_ = std::make_unique<ui::EventRewriterChromeOS>(
        delegate_.get(), sticky_keys_controller_, false);
    chromeos::Preferences::RegisterProfilePrefs(prefs_.registry());
    source_.AddEventRewriter(rewriter_.get());
    sticky_keys_controller_->Enable(true);
  }

  void TearDown() override {
    rewriter_.reset();
    ChromeAshTestBase::TearDown();
  }

 protected:
  ash::StickyKeysController* sticky_keys_controller_;

 private:
  std::unique_ptr<EventRewriterDelegateImpl> delegate_;
  std::unique_ptr<ui::EventRewriterChromeOS> rewriter_;

  EventBuffer buffer_;
  TestEventSource source_;

  chromeos::FakeChromeUserManager* fake_user_manager_;  // Not owned.
  user_manager::ScopedUserManager user_manager_enabler_;
  sync_preferences::TestingPrefServiceSyncable prefs_;

  DISALLOW_COPY_AND_ASSIGN(EventRewriterAshTest);
};

TEST_F(EventRewriterAshTest, TopRowKeysAreFunctionKeys) {
  std::unique_ptr<aura::Window> window(CreateTestWindowInShellWithId(1));
  wm::ActivateWindow(window.get());
  std::vector<std::unique_ptr<ui::Event>> events;

  // Create a simulated keypress of F1 targetted at the window.
  ui::KeyEvent press_f1(ui::ET_KEY_PRESSED, ui::VKEY_F1, ui::DomCode::F1,
                        ui::EF_NONE, ui::DomKey::F1, ui::EventTimeForNow());

  // The event should also not be rewritten if the send-function-keys pref is
  // additionally set, for both apps v2 and regular windows.
  BooleanPrefMember send_function_keys_pref;
  send_function_keys_pref.Init(prefs::kLanguageSendFunctionKeys, prefs());
  send_function_keys_pref.SetValue(true);
  ui::EventDispatchDetails details = Send(&press_f1);
  ASSERT_FALSE(details.dispatcher_destroyed);
  PopEvents(&events);
  EXPECT_EQ(1u, events.size());
  EXPECT_EQ(GetExpectedResultAsString(ui::ET_KEY_PRESSED, ui::VKEY_F1,
                                      ui::DomCode::F1, ui::EF_NONE,
                                      ui::DomKey::F1, kNoScanCode),
            GetKeyEventAsString(*static_cast<ui::KeyEvent*>(events[0].get())));

  // If the pref isn't set when an event is sent to a regular window, F1 is
  // rewritten to the back key.
  send_function_keys_pref.SetValue(false);
  details = Send(&press_f1);
  ASSERT_FALSE(details.dispatcher_destroyed);
  PopEvents(&events);
  EXPECT_EQ(1u, events.size());
  EXPECT_EQ(GetExpectedResultAsString(ui::ET_KEY_PRESSED, ui::VKEY_BROWSER_BACK,
                                      ui::DomCode::BROWSER_BACK, ui::EF_NONE,
                                      ui::DomKey::BROWSER_BACK, kNoScanCode),
            GetKeyEventAsString(*static_cast<ui::KeyEvent*>(events[0].get())));
}

// Parameterized version of test with the same name that accepts the
// event flags that correspond to a right-click. This will be either
// Alt+Click or Search+Click. After a transition period this will
// default to Search+Click and the Alt+Click logic will be removed.
void EventRewriterTest::DontRewriteIfNotRewritten(int right_click_flags) {
  ui::DeviceDataManager* device_data_manager =
      ui::DeviceDataManager::GetInstance();
  std::vector<ui::InputDevice> touchpad_devices(2);
  constexpr int kTouchpadId1 = 10;
  constexpr int kTouchpadId2 = 11;
  touchpad_devices[0].id = kTouchpadId1;
  touchpad_devices[1].id = kTouchpadId2;
  static_cast<ui::DeviceHotplugEventObserver*>(device_data_manager)
      ->OnTouchpadDevicesUpdated(touchpad_devices);
  std::vector<ui::InputDevice> mouse_devices(1);
  constexpr int kMouseId = 12;
  touchpad_devices[0].id = kMouseId;
  static_cast<ui::DeviceHotplugEventObserver*>(device_data_manager)
      ->OnMouseDevicesUpdated(mouse_devices);

  // Test (Alt|Search) + Left click.
  {
    ui::MouseEvent press(ui::ET_MOUSE_PRESSED, gfx::Point(), gfx::Point(),
                         ui::EventTimeForNow(), right_click_flags,
                         ui::EF_LEFT_MOUSE_BUTTON);
    ui::EventTestApi test_press(&press);
    test_press.set_source_device_id(kTouchpadId1);
    // Sanity check.
    EXPECT_EQ(ui::ET_MOUSE_PRESSED, press.type());
    EXPECT_EQ(right_click_flags, press.flags());
    const ui::MouseEvent result = RewriteMouseButtonEvent(press);
    EXPECT_TRUE(ui::EF_RIGHT_MOUSE_BUTTON & result.flags());
    EXPECT_NE(right_click_flags, right_click_flags & result.flags());
    EXPECT_EQ(ui::EF_RIGHT_MOUSE_BUTTON, result.changed_button_flags());
  }
  {
    ui::MouseEvent release(ui::ET_MOUSE_RELEASED, gfx::Point(), gfx::Point(),
                           ui::EventTimeForNow(), right_click_flags,
                           ui::EF_LEFT_MOUSE_BUTTON);
    ui::EventTestApi test_release(&release);
    test_release.set_source_device_id(kTouchpadId1);
    const ui::MouseEvent result = RewriteMouseButtonEvent(release);
    EXPECT_TRUE(ui::EF_RIGHT_MOUSE_BUTTON & result.flags());
    EXPECT_NE(right_click_flags, right_click_flags & result.flags());
    EXPECT_EQ(ui::EF_RIGHT_MOUSE_BUTTON, result.changed_button_flags());
  }

  // No (ALT|SEARCH) in first click.
  {
    ui::MouseEvent press(ui::ET_MOUSE_PRESSED, gfx::Point(), gfx::Point(),
                         ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
    ui::EventTestApi test_press(&press);
    test_press.set_source_device_id(kTouchpadId1);
    const ui::MouseEvent result = RewriteMouseButtonEvent(press);
    EXPECT_TRUE(ui::EF_LEFT_MOUSE_BUTTON & result.flags());
    EXPECT_EQ(ui::EF_LEFT_MOUSE_BUTTON, result.changed_button_flags());
  }
  {
    ui::MouseEvent release(ui::ET_MOUSE_RELEASED, gfx::Point(), gfx::Point(),
                           ui::EventTimeForNow(), right_click_flags,
                           ui::EF_LEFT_MOUSE_BUTTON);
    ui::EventTestApi test_release(&release);
    test_release.set_source_device_id(kTouchpadId1);
    const ui::MouseEvent result = RewriteMouseButtonEvent(release);
    EXPECT_EQ(right_click_flags, right_click_flags & result.flags());
    EXPECT_EQ(ui::EF_LEFT_MOUSE_BUTTON, result.changed_button_flags());
  }

  // ALT on different device.
  {
    ui::MouseEvent press(ui::ET_MOUSE_PRESSED, gfx::Point(), gfx::Point(),
                         ui::EventTimeForNow(), right_click_flags,
                         ui::EF_LEFT_MOUSE_BUTTON);
    ui::EventTestApi test_press(&press);
    test_press.set_source_device_id(kTouchpadId2);
    const ui::MouseEvent result = RewriteMouseButtonEvent(press);
    EXPECT_TRUE(ui::EF_RIGHT_MOUSE_BUTTON & result.flags());
    EXPECT_NE(right_click_flags, right_click_flags & result.flags());
    EXPECT_EQ(ui::EF_RIGHT_MOUSE_BUTTON, result.changed_button_flags());
  }
  {
    ui::MouseEvent release(ui::ET_MOUSE_RELEASED, gfx::Point(), gfx::Point(),
                           ui::EventTimeForNow(), right_click_flags,
                           ui::EF_LEFT_MOUSE_BUTTON);
    ui::EventTestApi test_release(&release);
    test_release.set_source_device_id(kTouchpadId1);
    const ui::MouseEvent result = RewriteMouseButtonEvent(release);
    EXPECT_EQ(right_click_flags, right_click_flags & result.flags());
    EXPECT_EQ(ui::EF_LEFT_MOUSE_BUTTON, result.changed_button_flags());
  }
  {
    ui::MouseEvent release(ui::ET_MOUSE_RELEASED, gfx::Point(), gfx::Point(),
                           ui::EventTimeForNow(), right_click_flags,
                           ui::EF_LEFT_MOUSE_BUTTON);
    ui::EventTestApi test_release(&release);
    test_release.set_source_device_id(kTouchpadId2);
    const ui::MouseEvent result = RewriteMouseButtonEvent(release);
    EXPECT_TRUE(ui::EF_RIGHT_MOUSE_BUTTON & result.flags());
    EXPECT_NE(right_click_flags, right_click_flags & result.flags());
    EXPECT_EQ(ui::EF_RIGHT_MOUSE_BUTTON, result.changed_button_flags());
  }

  // No rewrite for non-touchpad devices.
  {
    ui::MouseEvent press(ui::ET_MOUSE_PRESSED, gfx::Point(), gfx::Point(),
                         ui::EventTimeForNow(), right_click_flags,
                         ui::EF_LEFT_MOUSE_BUTTON);
    ui::EventTestApi test_press(&press);
    test_press.set_source_device_id(kMouseId);
    EXPECT_EQ(ui::ET_MOUSE_PRESSED, press.type());
    EXPECT_EQ(right_click_flags, press.flags());
    const ui::MouseEvent result = RewriteMouseButtonEvent(press);
    EXPECT_EQ(right_click_flags, right_click_flags & result.flags());
    EXPECT_EQ(ui::EF_LEFT_MOUSE_BUTTON, result.changed_button_flags());
  }
  {
    ui::MouseEvent release(ui::ET_MOUSE_RELEASED, gfx::Point(), gfx::Point(),
                           ui::EventTimeForNow(), right_click_flags,
                           ui::EF_LEFT_MOUSE_BUTTON);
    ui::EventTestApi test_release(&release);
    test_release.set_source_device_id(kMouseId);
    const ui::MouseEvent result = RewriteMouseButtonEvent(release);
    EXPECT_EQ(right_click_flags, right_click_flags & result.flags());
    EXPECT_EQ(ui::EF_LEFT_MOUSE_BUTTON, result.changed_button_flags());
  }
}

TEST_F(EventRewriterTest, DontRewriteIfNotRewritten_AltClickIsRightClick) {
  DontRewriteIfNotRewritten(ui::EF_LEFT_MOUSE_BUTTON | ui::EF_ALT_DOWN);
}

TEST_F(EventRewriterTest, DontRewriteIfNotRewritten_SearchClickIsRightClick) {
  scoped_feature_list_.InitAndEnableFeature(
      chromeos::features::kUseSearchClickForRightClick);
  DontRewriteIfNotRewritten(ui::EF_LEFT_MOUSE_BUTTON | ui::EF_COMMAND_DOWN);
}

TEST_F(EventRewriterAshTest, StickyKeyEventDispatchImpl) {
  // Test the actual key event dispatch implementation.
  std::vector<std::unique_ptr<ui::Event>> events;

  SendActivateStickyKeyPattern(ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                               ui::DomKey::CONTROL);
  PopEvents(&events);
  EXPECT_EQ(1u, events.size());
  EXPECT_EQ(ui::ET_KEY_PRESSED, events[0]->type());
  EXPECT_EQ(ui::VKEY_CONTROL,
            static_cast<ui::KeyEvent*>(events[0].get())->key_code());

  // Test key press event is correctly modified and modifier release
  // event is sent.
  ui::KeyEvent press(ui::ET_KEY_PRESSED, ui::VKEY_C, ui::DomCode::US_C,
                     ui::EF_NONE, ui::DomKey::Constant<'c'>::Character,
                     ui::EventTimeForNow());
  ui::EventDispatchDetails details = Send(&press);
  PopEvents(&events);
  EXPECT_EQ(2u, events.size());
  EXPECT_EQ(ui::ET_KEY_PRESSED, events[0]->type());
  EXPECT_EQ(ui::VKEY_C,
            static_cast<ui::KeyEvent*>(events[0].get())->key_code());
  EXPECT_TRUE(events[0]->flags() & ui::EF_CONTROL_DOWN);
  EXPECT_EQ(ui::ET_KEY_RELEASED, events[1]->type());
  EXPECT_EQ(ui::VKEY_CONTROL,
            static_cast<ui::KeyEvent*>(events[1].get())->key_code());

  // Test key release event is not modified.
  ui::KeyEvent release(ui::ET_KEY_RELEASED, ui::VKEY_C, ui::DomCode::US_C,
                       ui::EF_NONE, ui::DomKey::Constant<'c'>::Character,
                       ui::EventTimeForNow());
  details = Send(&release);
  ASSERT_FALSE(details.dispatcher_destroyed);
  PopEvents(&events);
  EXPECT_EQ(1u, events.size());
  EXPECT_EQ(ui::ET_KEY_RELEASED, events[0]->type());
  EXPECT_EQ(ui::VKEY_C,
            static_cast<ui::KeyEvent*>(events[0].get())->key_code());
  EXPECT_FALSE(events[0]->flags() & ui::EF_CONTROL_DOWN);
}

TEST_F(EventRewriterAshTest, MouseEventDispatchImpl) {
  std::vector<std::unique_ptr<ui::Event>> events;

  SendActivateStickyKeyPattern(ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                               ui::DomKey::CONTROL);
  PopEvents(&events);

  // Test mouse press event is correctly modified.
  gfx::Point location(0, 0);
  ui::MouseEvent press(ui::ET_MOUSE_PRESSED, location, location,
                       ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  ui::EventDispatchDetails details = Send(&press);
  ASSERT_FALSE(details.dispatcher_destroyed);
  PopEvents(&events);
  EXPECT_EQ(1u, events.size());
  EXPECT_EQ(ui::ET_MOUSE_PRESSED, events[0]->type());
  EXPECT_TRUE(events[0]->flags() & ui::EF_CONTROL_DOWN);

  // Test mouse release event is correctly modified and modifier release
  // event is sent. The mouse event should have the correct DIP location.
  ui::MouseEvent release(ui::ET_MOUSE_RELEASED, location, location,
                         ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
  details = Send(&release);
  ASSERT_FALSE(details.dispatcher_destroyed);
  PopEvents(&events);
  EXPECT_EQ(2u, events.size());
  EXPECT_EQ(ui::ET_MOUSE_RELEASED, events[0]->type());
  EXPECT_TRUE(events[0]->flags() & ui::EF_CONTROL_DOWN);
  EXPECT_EQ(ui::ET_KEY_RELEASED, events[1]->type());
  EXPECT_EQ(ui::VKEY_CONTROL,
            static_cast<ui::KeyEvent*>(events[1].get())->key_code());
}

TEST_F(EventRewriterAshTest, MouseWheelEventDispatchImpl) {
  std::vector<std::unique_ptr<ui::Event>> events;

  // Test positive mouse wheel event is correctly modified and modifier release
  // event is sent.
  SendActivateStickyKeyPattern(ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                               ui::DomKey::CONTROL);
  PopEvents(&events);
  gfx::Point location(0, 0);
  ui::MouseWheelEvent positive(
      gfx::Vector2d(0, ui::MouseWheelEvent::kWheelDelta), location, location,
      ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
      ui::EF_LEFT_MOUSE_BUTTON);
  ui::EventDispatchDetails details = Send(&positive);
  ASSERT_FALSE(details.dispatcher_destroyed);
  PopEvents(&events);
  EXPECT_EQ(2u, events.size());
  EXPECT_TRUE(events[0]->IsMouseWheelEvent());
  EXPECT_TRUE(events[0]->flags() & ui::EF_CONTROL_DOWN);
  EXPECT_EQ(ui::ET_KEY_RELEASED, events[1]->type());
  EXPECT_EQ(ui::VKEY_CONTROL,
            static_cast<ui::KeyEvent*>(events[1].get())->key_code());

  // Test negative mouse wheel event is correctly modified and modifier release
  // event is sent.
  SendActivateStickyKeyPattern(ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                               ui::DomKey::CONTROL);
  PopEvents(&events);
  ui::MouseWheelEvent negative(
      gfx::Vector2d(0, -ui::MouseWheelEvent::kWheelDelta), location, location,
      ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
      ui::EF_LEFT_MOUSE_BUTTON);
  details = Send(&negative);
  ASSERT_FALSE(details.dispatcher_destroyed);
  PopEvents(&events);
  EXPECT_EQ(2u, events.size());
  EXPECT_TRUE(events[0]->IsMouseWheelEvent());
  EXPECT_TRUE(events[0]->flags() & ui::EF_CONTROL_DOWN);
  EXPECT_EQ(ui::ET_KEY_RELEASED, events[1]->type());
  EXPECT_EQ(ui::VKEY_CONTROL,
            static_cast<ui::KeyEvent*>(events[1].get())->key_code());
}

// Tests that if modifier keys are remapped, the flags of a mouse wheel event
// will be rewritten properly.
TEST_F(EventRewriterAshTest, MouseWheelEventModifiersRewritten) {
  // Generate a mouse wheel event that has a CONTROL_DOWN modifier flag and
  // expect that no rewriting happens as no modifier remapping is active.
  std::vector<std::unique_ptr<ui::Event>> events;
  gfx::Point location(0, 0);
  ui::MouseWheelEvent positive(
      gfx::Vector2d(0, ui::MouseWheelEvent::kWheelDelta), location, location,
      ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON | ui::EF_CONTROL_DOWN,
      ui::EF_LEFT_MOUSE_BUTTON);
  ui::EventDispatchDetails details = Send(&positive);
  ASSERT_FALSE(details.dispatcher_destroyed);
  PopEvents(&events);
  EXPECT_EQ(1u, events.size());
  EXPECT_TRUE(events[0]->IsMouseWheelEvent());
  EXPECT_TRUE(events[0]->flags() & ui::EF_CONTROL_DOWN);

  // Remap Control to Alt.
  IntegerPrefMember control;
  InitModifierKeyPref(&control, prefs::kLanguageRemapControlKeyTo,
                      ui::chromeos::ModifierKey::kAltKey);

  // Sends the same events once again and expect that it will be rewritten to
  // ALT_DOWN.
  details = Send(&positive);
  ASSERT_FALSE(details.dispatcher_destroyed);
  PopEvents(&events);
  EXPECT_EQ(1u, events.size());
  EXPECT_TRUE(events[0]->IsMouseWheelEvent());
  EXPECT_FALSE(events[0]->flags() & ui::EF_CONTROL_DOWN);
  EXPECT_TRUE(events[0]->flags() & ui::EF_ALT_DOWN);
}

// Tests edge cases of key event rewriting (see https://crbug.com/913209).
TEST_F(EventRewriterAshTest, KeyEventRewritingEdgeCases) {
  std::vector<std::unique_ptr<ui::Event>> events;

  // Edge case 1: Press the Launcher button first. Then press the Up Arrow
  // button.
  SendKeyEvent(ui::ET_KEY_PRESSED, ui::VKEY_COMMAND, ui::DomCode::META_LEFT,
               ui::DomKey::META);
  SendKeyEvent(ui::ET_KEY_PRESSED, ui::VKEY_UP, ui::DomCode::ARROW_UP,
               ui::DomKey::ARROW_UP, ui::EF_COMMAND_DOWN);

  PopEvents(&events);
  EXPECT_EQ(2u, events.size());
  events.clear();

  SendKeyEvent(ui::ET_KEY_RELEASED, ui::VKEY_COMMAND, ui::DomCode::META_LEFT,
               ui::DomKey::META);
  PopEvents(&events);

  // When releasing the Launcher button, the rewritten event should be released
  // as well.
  EXPECT_EQ(2u, events.size());
  EXPECT_EQ(ui::VKEY_COMMAND,
            static_cast<ui::KeyEvent*>(events[0].get())->key_code());
  EXPECT_EQ(ui::VKEY_PRIOR,
            static_cast<ui::KeyEvent*>(events[1].get())->key_code());

  events.clear();

  // Edge case 2: Press the Up Arrow button first. Then press the Launch button.
  SendKeyEvent(ui::ET_KEY_PRESSED, ui::VKEY_UP, ui::DomCode::ARROW_UP,
               ui::DomKey::ARROW_UP);
  SendKeyEvent(ui::ET_KEY_PRESSED, ui::VKEY_COMMAND, ui::DomCode::META_LEFT,
               ui::DomKey::META);

  PopEvents(&events);
  EXPECT_EQ(2u, events.size());
  events.clear();

  SendKeyEvent(ui::ET_KEY_RELEASED, ui::VKEY_UP, ui::DomCode::ARROW_UP,
               ui::DomKey::ARROW_UP, ui::EF_COMMAND_DOWN);
  PopEvents(&events);

  // When releasing the Up Arrow button, the rewritten event should be blocked.
  EXPECT_EQ(1u, events.size());
  EXPECT_EQ(ui::VKEY_UP,
            static_cast<ui::KeyEvent*>(events[0].get())->key_code());
}

TEST_F(EventRewriterAshTest, ScrollEventDispatchImpl) {
  std::vector<std::unique_ptr<ui::Event>> events;

  // Test scroll event is correctly modified.
  SendActivateStickyKeyPattern(ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                               ui::DomKey::CONTROL);
  PopEvents(&events);
  gfx::PointF location(0, 0);
  ui::ScrollEvent scroll(ui::ET_SCROLL, location, location,
                         ui::EventTimeForNow(), 0 /* flag */, 0 /* x_offset */,
                         1 /* y_offset */, 0 /* x_offset_ordinal */,
                         1 /* y_offset_ordinal */, 2 /* finger */);
  ui::EventDispatchDetails details = Send(&scroll);
  ASSERT_FALSE(details.dispatcher_destroyed);
  PopEvents(&events);
  EXPECT_EQ(1u, events.size());
  EXPECT_TRUE(events[0]->IsScrollEvent());
  EXPECT_TRUE(events[0]->flags() & ui::EF_CONTROL_DOWN);

  // Test FLING_START event deactivates the sticky key, but is modified.
  ui::ScrollEvent fling_start(
      ui::ET_SCROLL_FLING_START, location, location, ui::EventTimeForNow(),
      0 /* flag */, 0 /* x_offset */, 0 /* y_offset */,
      0 /* x_offset_ordinal */, 0 /* y_offset_ordinal */, 2 /* finger */);
  details = Send(&fling_start);
  PopEvents(&events);
  EXPECT_EQ(2u, events.size());
  EXPECT_TRUE(events[0]->IsScrollEvent());
  EXPECT_TRUE(events[0]->flags() & ui::EF_CONTROL_DOWN);
  EXPECT_EQ(ui::ET_KEY_RELEASED, events[1]->type());
  EXPECT_EQ(ui::VKEY_CONTROL,
            static_cast<ui::KeyEvent*>(events[1].get())->key_code());

  // Test scroll direction change causes that modifier release event is sent.
  SendActivateStickyKeyPattern(ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                               ui::DomKey::CONTROL);
  details = Send(&scroll);
  ASSERT_FALSE(details.dispatcher_destroyed);
  PopEvents(&events);

  ui::ScrollEvent scroll2(ui::ET_SCROLL, location, location,
                          ui::EventTimeForNow(), 0 /* flag */, 0 /* x_offset */,
                          -1 /* y_offset */, 0 /* x_offset_ordinal */,
                          -1 /* y_offset_ordinal */, 2 /* finger */);
  details = Send(&scroll2);
  ASSERT_FALSE(details.dispatcher_destroyed);
  PopEvents(&events);
  EXPECT_EQ(2u, events.size());
  EXPECT_TRUE(events[0]->IsScrollEvent());
  EXPECT_FALSE(events[0]->flags() & ui::EF_CONTROL_DOWN);
  EXPECT_EQ(ui::ET_KEY_RELEASED, events[1]->type());
  EXPECT_EQ(ui::VKEY_CONTROL,
            static_cast<ui::KeyEvent*>(events[1].get())->key_code());
}

class StickyKeysOverlayTest : public EventRewriterAshTest {
 public:
  StickyKeysOverlayTest() : overlay_(NULL) {}

  ~StickyKeysOverlayTest() override {}

  void SetUp() override {
    EventRewriterAshTest::SetUp();
    overlay_ = sticky_keys_controller_->GetOverlayForTest();
    ASSERT_TRUE(overlay_);
  }

  ash::StickyKeysOverlay* overlay_;
};

TEST_F(StickyKeysOverlayTest, OneModifierEnabled) {
  EXPECT_FALSE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_CONTROL_DOWN));

  // Pressing modifier key should show overlay.
  SendActivateStickyKeyPattern(ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                               ui::DomKey::CONTROL);
  EXPECT_TRUE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_ENABLED,
            overlay_->GetModifierKeyState(ui::EF_CONTROL_DOWN));

  // Pressing a normal key should hide overlay.
  SendActivateStickyKeyPattern(ui::VKEY_T, ui::DomCode::US_T,
                               ui::DomKey::Constant<'t'>::Character);
  EXPECT_FALSE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_CONTROL_DOWN));
}

TEST_F(StickyKeysOverlayTest, TwoModifiersEnabled) {
  EXPECT_FALSE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_CONTROL_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_SHIFT_DOWN));

  // Pressing two modifiers should show overlay.
  SendActivateStickyKeyPattern(ui::VKEY_SHIFT, ui::DomCode::SHIFT_LEFT,
                               ui::DomKey::SHIFT);
  SendActivateStickyKeyPattern(ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                               ui::DomKey::CONTROL);
  EXPECT_TRUE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_ENABLED,
            overlay_->GetModifierKeyState(ui::EF_SHIFT_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_ENABLED,
            overlay_->GetModifierKeyState(ui::EF_CONTROL_DOWN));

  // Pressing a normal key should hide overlay.
  SendActivateStickyKeyPattern(ui::VKEY_N, ui::DomCode::US_N,
                               ui::DomKey::Constant<'n'>::Character);
  EXPECT_FALSE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_CONTROL_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_SHIFT_DOWN));
}

TEST_F(StickyKeysOverlayTest, LockedModifier) {
  EXPECT_FALSE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_ALT_DOWN));

  // Pressing a modifier key twice should lock modifier and show overlay.
  SendActivateStickyKeyPattern(ui::VKEY_LMENU, ui::DomCode::ALT_LEFT,
                               ui::DomKey::ALT);
  SendActivateStickyKeyPattern(ui::VKEY_LMENU, ui::DomCode::ALT_LEFT,
                               ui::DomKey::ALT);
  EXPECT_TRUE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_LOCKED,
            overlay_->GetModifierKeyState(ui::EF_ALT_DOWN));

  // Pressing a normal key should not hide overlay.
  SendActivateStickyKeyPattern(ui::VKEY_D, ui::DomCode::US_D,
                               ui::DomKey::Constant<'d'>::Character);
  EXPECT_TRUE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_LOCKED,
            overlay_->GetModifierKeyState(ui::EF_ALT_DOWN));
}

TEST_F(StickyKeysOverlayTest, LockedAndNormalModifier) {
  EXPECT_FALSE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_CONTROL_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_SHIFT_DOWN));

  // Pressing a modifier key twice should lock modifier and show overlay.
  SendActivateStickyKeyPattern(ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                               ui::DomKey::CONTROL);
  SendActivateStickyKeyPattern(ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                               ui::DomKey::CONTROL);
  EXPECT_TRUE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_LOCKED,
            overlay_->GetModifierKeyState(ui::EF_CONTROL_DOWN));

  // Pressing another modifier key should still show overlay.
  SendActivateStickyKeyPattern(ui::VKEY_SHIFT, ui::DomCode::SHIFT_LEFT,
                               ui::DomKey::SHIFT);
  EXPECT_TRUE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_LOCKED,
            overlay_->GetModifierKeyState(ui::EF_CONTROL_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_ENABLED,
            overlay_->GetModifierKeyState(ui::EF_SHIFT_DOWN));

  // Pressing a normal key should not hide overlay but disable normal modifier.
  SendActivateStickyKeyPattern(ui::VKEY_D, ui::DomCode::US_D,
                               ui::DomKey::Constant<'d'>::Character);
  EXPECT_TRUE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_LOCKED,
            overlay_->GetModifierKeyState(ui::EF_CONTROL_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_SHIFT_DOWN));
}

TEST_F(StickyKeysOverlayTest, ModifiersDisabled) {
  EXPECT_FALSE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_CONTROL_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_SHIFT_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_ALT_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_COMMAND_DOWN));

  // Enable modifiers.
  SendActivateStickyKeyPattern(ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                               ui::DomKey::CONTROL);
  SendActivateStickyKeyPattern(ui::VKEY_SHIFT, ui::DomCode::SHIFT_LEFT,
                               ui::DomKey::SHIFT);
  SendActivateStickyKeyPattern(ui::VKEY_SHIFT, ui::DomCode::SHIFT_LEFT,
                               ui::DomKey::SHIFT);
  SendActivateStickyKeyPattern(ui::VKEY_LMENU, ui::DomCode::ALT_LEFT,
                               ui::DomKey::ALT);
  SendActivateStickyKeyPattern(ui::VKEY_COMMAND, ui::DomCode::META_LEFT,
                               ui::DomKey::META);
  SendActivateStickyKeyPattern(ui::VKEY_COMMAND, ui::DomCode::META_LEFT,
                               ui::DomKey::META);

  EXPECT_TRUE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_ENABLED,
            overlay_->GetModifierKeyState(ui::EF_CONTROL_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_LOCKED,
            overlay_->GetModifierKeyState(ui::EF_SHIFT_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_ENABLED,
            overlay_->GetModifierKeyState(ui::EF_ALT_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_LOCKED,
            overlay_->GetModifierKeyState(ui::EF_COMMAND_DOWN));

  // Disable modifiers and overlay should be hidden.
  SendActivateStickyKeyPattern(ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                               ui::DomKey::CONTROL);
  SendActivateStickyKeyPattern(ui::VKEY_CONTROL, ui::DomCode::CONTROL_LEFT,
                               ui::DomKey::CONTROL);
  SendActivateStickyKeyPattern(ui::VKEY_SHIFT, ui::DomCode::SHIFT_LEFT,
                               ui::DomKey::SHIFT);
  SendActivateStickyKeyPattern(ui::VKEY_LMENU, ui::DomCode::ALT_LEFT,
                               ui::DomKey::ALT);
  SendActivateStickyKeyPattern(ui::VKEY_LMENU, ui::DomCode::ALT_LEFT,
                               ui::DomKey::ALT);
  SendActivateStickyKeyPattern(ui::VKEY_COMMAND, ui::DomCode::META_LEFT,
                               ui::DomKey::META);

  EXPECT_FALSE(overlay_->is_visible());
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_CONTROL_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_SHIFT_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_ALT_DOWN));
  EXPECT_EQ(ash::STICKY_KEY_STATE_DISABLED,
            overlay_->GetModifierKeyState(ui::EF_COMMAND_DOWN));
}

TEST_F(StickyKeysOverlayTest, ModifierVisibility) {
  // All but AltGr and Mod3 should initially be visible.
  EXPECT_TRUE(overlay_->GetModifierVisible(ui::EF_CONTROL_DOWN));
  EXPECT_TRUE(overlay_->GetModifierVisible(ui::EF_SHIFT_DOWN));
  EXPECT_TRUE(overlay_->GetModifierVisible(ui::EF_ALT_DOWN));
  EXPECT_TRUE(overlay_->GetModifierVisible(ui::EF_COMMAND_DOWN));
  EXPECT_FALSE(overlay_->GetModifierVisible(ui::EF_ALTGR_DOWN));
  EXPECT_FALSE(overlay_->GetModifierVisible(ui::EF_MOD3_DOWN));

  // Turn all modifiers on.
  sticky_keys_controller_->SetModifiersEnabled(true, true);
  EXPECT_TRUE(overlay_->GetModifierVisible(ui::EF_CONTROL_DOWN));
  EXPECT_TRUE(overlay_->GetModifierVisible(ui::EF_SHIFT_DOWN));
  EXPECT_TRUE(overlay_->GetModifierVisible(ui::EF_ALT_DOWN));
  EXPECT_TRUE(overlay_->GetModifierVisible(ui::EF_COMMAND_DOWN));
  EXPECT_TRUE(overlay_->GetModifierVisible(ui::EF_ALTGR_DOWN));
  EXPECT_TRUE(overlay_->GetModifierVisible(ui::EF_MOD3_DOWN));

  // Turn off Mod3.
  sticky_keys_controller_->SetModifiersEnabled(false, true);
  EXPECT_TRUE(overlay_->GetModifierVisible(ui::EF_ALTGR_DOWN));
  EXPECT_FALSE(overlay_->GetModifierVisible(ui::EF_MOD3_DOWN));

  // Turn off AltGr.
  sticky_keys_controller_->SetModifiersEnabled(true, false);
  EXPECT_FALSE(overlay_->GetModifierVisible(ui::EF_ALTGR_DOWN));
  EXPECT_TRUE(overlay_->GetModifierVisible(ui::EF_MOD3_DOWN));

  // Turn off AltGr and Mod3.
  sticky_keys_controller_->SetModifiersEnabled(false, false);
  EXPECT_FALSE(overlay_->GetModifierVisible(ui::EF_ALTGR_DOWN));
  EXPECT_FALSE(overlay_->GetModifierVisible(ui::EF_MOD3_DOWN));
}

}  // namespace chromeos
