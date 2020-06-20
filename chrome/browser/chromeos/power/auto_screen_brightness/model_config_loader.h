// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_POWER_AUTO_SCREEN_BRIGHTNESS_MODEL_CONFIG_LOADER_H_
#define CHROME_BROWSER_CHROMEOS_POWER_AUTO_SCREEN_BRIGHTNESS_MODEL_CONFIG_LOADER_H_

#include "base/observer_list_types.h"
#include "base/optional.h"
#include "chrome/browser/chromeos/power/auto_screen_brightness/model_config.h"

namespace chromeos {
namespace power {
namespace auto_screen_brightness {

// Interface to the actual ModelConfigLoader of the on-device adaptive
// brightness model. ModelConfigLoader is responsible for managing all
// parameters required for model customization. These params may be passed in
// from experiment flags or from other device-specific config.
class ModelConfigLoader {
 public:
  // ModelConfigLoader must outlive its observers.
  class Observer : public base::CheckedObserver {
   public:
    Observer() = default;
    ~Observer() override = default;

    // Called when the ModelConfigLoader is initialized.  |model_config| is only
    // non-nullopt if a valid ModelConfig is created, either from the disk or
    // from experiment flags.
    virtual void OnModelConfigLoaded(
        base::Optional<ModelConfig> model_config) = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(Observer);
  };

  virtual ~ModelConfigLoader() = default;

  // Adds or removes an observer.
  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;
};

}  // namespace auto_screen_brightness
}  // namespace power
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_POWER_AUTO_SCREEN_BRIGHTNESS_MODEL_CONFIG_LOADER_H_
