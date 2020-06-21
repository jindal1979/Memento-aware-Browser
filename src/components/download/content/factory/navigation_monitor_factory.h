// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_DOWNLOAD_CONTENT_FACTORY_NAVIGATION_MONITOR_FACTORY_H_
#define COMPONENTS_DOWNLOAD_CONTENT_FACTORY_NAVIGATION_MONITOR_FACTORY_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/singleton.h"
#include "components/keyed_service/core/simple_keyed_service_factory.h"

namespace download {

class NavigationMonitor;

// Creates the DownloadNavigationMonitor instance.
class NavigationMonitorFactory : public SimpleKeyedServiceFactory {
 public:
  static NavigationMonitorFactory* GetInstance();
  static download::NavigationMonitor* GetForKey(SimpleFactoryKey* key);

 private:
  friend struct base::DefaultSingletonTraits<NavigationMonitorFactory>;

  NavigationMonitorFactory();
  ~NavigationMonitorFactory() override;

  // SimpleKeyedServiceFactory implementation.
  std::unique_ptr<KeyedService> BuildServiceInstanceFor(
      SimpleFactoryKey* key) const override;
  SimpleFactoryKey* GetKeyToUse(SimpleFactoryKey* key) const override;

  DISALLOW_COPY_AND_ASSIGN(NavigationMonitorFactory);
};

}  // namespace download

#endif  // COMPONENTS_DOWNLOAD_CONTENT_FACTORY_NAVIGATION_MONITOR_FACTORY_H_
