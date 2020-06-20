// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COOKIE_CONTROLS_COOKIE_CONTROLS_CONTROLLER_H_
#define CHROME_BROWSER_UI_COOKIE_CONTROLS_COOKIE_CONTROLS_CONTROLLER_H_

#include "base/memory/scoped_refptr.h"
#include "base/observer_list.h"
#include "components/content_settings/browser/tab_specific_content_settings.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/cookie_controls_enforcement.h"
#include "components/content_settings/core/common/cookie_controls_status.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_contents.h"

namespace content {
class BrowserContext;
class WebContents;
}

namespace content_settings {
class CookieSettings;
}

class CookieControlsView;

// Handles the tab specific state for cookie controls.
class CookieControlsController : content_settings::CookieSettings::Observer {
 public:
  CookieControlsController(content::WebContents* web_contents,
                           content::BrowserContext* original_context);
  ~CookieControlsController() override;

  // Called when the web_contents has changed.
  void Update(content::WebContents* web_contents);

  // Called when CookieControlsView is closing.
  void OnUiClosing();

  // Called when the user clicks on the button to enable/disable cookie
  // blocking.
  void OnCookieBlockingEnabledForSite(bool block_third_party_cookies);


  void AddObserver(CookieControlsView* obs);
  void RemoveObserver(CookieControlsView* obs);

 private:
  // The observed WebContents changes during the lifetime of the
  // CookieControlsController. SiteDataObserver can't change the observed
  // object, so we need an inner class that can be recreated when necessary.
  // TODO(dullweber): Make it possible to change the observed class and maybe
  // convert SiteDataObserver to a pure virtual interface.
  class TabObserver
      : public content_settings::TabSpecificContentSettings::SiteDataObserver {
   public:
    TabObserver(CookieControlsController* cookie_controls,
                content::WebContents* web_contents);

    // TabSpecificContentSettings::SiteDataObserver:
    void OnSiteDataAccessed() override;

   private:
    CookieControlsController* cookie_controls_;

    DISALLOW_COPY_AND_ASSIGN(TabObserver);
  };

  void OnThirdPartyCookieBlockingChanged(
      bool block_third_party_cookies) override;
  void OnCookieSettingChanged() override;

  // Determine the CookieControlsStatus based on |web_contents|.
  std::pair<CookieControlsStatus, CookieControlsEnforcement> GetStatus(
      content::WebContents* web_contents);

  // Updates the blocked cookie count of |icon_|.
  void PresentBlockedCookieCounter();

  // Returns the number of blocked cookies.
  int GetBlockedCookieCount();

  content::WebContents* GetWebContents();

  std::unique_ptr<TabObserver> tab_observer_;
  scoped_refptr<content_settings::CookieSettings> cookie_settings_;
  // Cookie_settings for the original profile associated with
  // |cookie_settings_|, if there is one. For example, in Chrome, this
  // corresponds to the regular profile when |cookie_settings_| is incognito.
  // This may be null.
  scoped_refptr<content_settings::CookieSettings> original_cookie_settings_;

  ScopedObserver<content_settings::CookieSettings,
                 content_settings::CookieSettings::Observer>
      cookie_observer_{this};

  bool should_reload_ = false;

  base::ObserverList<CookieControlsView> observers_;

  DISALLOW_COPY_AND_ASSIGN(CookieControlsController);
};

#endif  // CHROME_BROWSER_UI_COOKIE_CONTROLS_COOKIE_CONTROLS_CONTROLLER_H_
