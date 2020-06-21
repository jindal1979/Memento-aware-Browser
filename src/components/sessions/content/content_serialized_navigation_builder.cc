// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sessions/content/content_serialized_navigation_builder.h"

#include <string>

#include "base/check_op.h"
#include "components/sessions/content/content_record_password_state.h"
#include "components/sessions/content/content_serialized_navigation_driver.h"
#include "components/sessions/content/extended_info_handler.h"
#include "components/sessions/content/navigation_task_id.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/replaced_navigation_entry_data.h"
#include "content/public/common/page_state.h"
#include "content/public/common/referrer.h"

namespace sessions {
namespace {

base::Optional<SerializedNavigationEntry::ReplacedNavigationEntryData>
ConvertReplacedEntryData(
    const base::Optional<content::ReplacedNavigationEntryData>& input_data) {
  if (!input_data.has_value())
    return base::nullopt;

  SerializedNavigationEntry::ReplacedNavigationEntryData output_data;
  output_data.first_committed_url = input_data->first_committed_url;
  output_data.first_timestamp = input_data->first_timestamp;
  output_data.first_transition_type = input_data->first_transition_type;
  return output_data;
}

}  // namespace

// static
SerializedNavigationEntry
ContentSerializedNavigationBuilder::FromNavigationEntry(
    int index,
    content::NavigationEntry* entry,
    SerializationOptions serialization_options) {
  SerializedNavigationEntry navigation;
  navigation.index_ = index;
  navigation.unique_id_ = entry->GetUniqueID();
  navigation.referrer_url_ = entry->GetReferrer().url;
  navigation.referrer_policy_ = static_cast<int>(entry->GetReferrer().policy);
  navigation.virtual_url_ = entry->GetVirtualURL();
  navigation.title_ = entry->GetTitle();
  if (!(serialization_options & SerializationOptions::EXCLUDE_PAGE_STATE))
    navigation.encoded_page_state_ = entry->GetPageState().ToEncodedData();
  navigation.transition_type_ = entry->GetTransitionType();
  navigation.has_post_data_ = entry->GetHasPostData();
  navigation.post_id_ = entry->GetPostID();
  navigation.original_request_url_ = entry->GetOriginalRequestURL();
  navigation.is_overriding_user_agent_ = entry->GetIsOverridingUserAgent();
  navigation.timestamp_ = entry->GetTimestamp();
  navigation.is_restored_ = entry->IsRestored();
  if (entry->GetFavicon().valid)
    navigation.favicon_url_ = entry->GetFavicon().url;
  navigation.http_status_code_ = entry->GetHttpStatusCode();
  navigation.redirect_chain_ = entry->GetRedirectChain();
  navigation.replaced_entry_data_ =
      ConvertReplacedEntryData(entry->GetReplacedEntryData());
  navigation.password_state_ = GetPasswordStateFromNavigation(entry);
  navigation.task_id_ = NavigationTaskId::Get(entry)->id();
  navigation.parent_task_id_ = NavigationTaskId::Get(entry)->parent_id();
  navigation.root_task_id_ = NavigationTaskId::Get(entry)->root_id();
  navigation.children_task_ids_ = NavigationTaskId::Get(entry)->children_ids();

  for (const auto& handler_entry :
       ContentSerializedNavigationDriver::GetInstance()
           ->GetAllExtendedInfoHandlers()) {
    ExtendedInfoHandler* handler = handler_entry.second.get();
    DCHECK(handler);
    std::string value = handler->GetExtendedInfo(entry);
    if (!value.empty())
      navigation.extended_info_map_[handler_entry.first] = value;
  }

  return navigation;
}

// static
std::unique_ptr<content::NavigationEntry>
ContentSerializedNavigationBuilder::ToNavigationEntry(
    const SerializedNavigationEntry* navigation,
    content::BrowserContext* browser_context) {
  DCHECK(navigation);
  DCHECK(browser_context);

  // The initial values of the NavigationEntry are only temporary - they
  // will get cloberred by one of the SetPageState calls below.
  //
  // This means that things like |navigation->referrer_url| are ignored
  // in favor of using the data stored in |navigation->encoded_page_state|.
  GURL temporary_url;
  content::Referrer temporary_referrer;
  base::Optional<url::Origin> temporary_initiator_origin;

  std::unique_ptr<content::NavigationEntry> entry(
      content::NavigationController::CreateNavigationEntry(
          temporary_url, temporary_referrer, temporary_initiator_origin,
          // Use a transition type of reload so that we don't incorrectly
          // increase the typed count.
          ui::PAGE_TRANSITION_RELOAD, false,
          // The extra headers are not sync'ed across sessions.
          std::string(), browser_context,
          nullptr /* blob_url_loader_factory */));

  // In some cases the |encoded_page_state| might be empty - we
  // need to gracefully handle such data when it is deserialized.
  //
  // One case is tests for "foreign" session restore entries, such as
  // SessionRestoreTest.RestoreForeignTab.  We hypothesise that old session
  // restore entries might also contain an empty |encoded_page_state|.
  if (navigation->encoded_page_state_.empty()) {
    // Ensure that the deserialized/restored content::NavigationEntry (and
    // the content::FrameNavigationEntry underneath) has a valid PageState.
    entry->SetPageState(
        content::PageState::CreateFromURL(navigation->virtual_url_));

    // The |navigation|-based referrer set below might be inconsistent with the
    // referrer embedded inside the PageState set above.  Nevertheless, to
    // minimize changes to behavior of old session restore entries, we restore
    // the deserialized referrer here.
    //
    // TODO(lukasza): Consider including the |deserialized_referrer| in the
    // PageState set above + drop the SetReferrer call below.  This will
    // slightly change the legacy behavior, but will make PageState and
    // Referrer consistent.
    content::Referrer referrer(
        navigation->referrer_url(),
        content::Referrer::ConvertToPolicy(navigation->referrer_policy()));
    entry->SetReferrer(referrer);
  } else {
    // Note that PageState covers some of the values inside |navigation| (e.g.
    // URL, Referrer).  Calling SetPageState will clobber these values in
    // content::NavigationEntry (and FrameNavigationEntry(s) below).
    entry->SetPageState(content::PageState::CreateFromEncodedData(
        navigation->encoded_page_state_));

    // |navigation|-level referrer information is redundant wrt PageState, but
    // they should be consistent / in-sync.
    DCHECK_EQ(navigation->referrer_url(), entry->GetReferrer().url);
    DCHECK_EQ(navigation->referrer_policy(),
              static_cast<int>(entry->GetReferrer().policy));
  }

  entry->SetTitle(navigation->title_);
  entry->SetHasPostData(navigation->has_post_data_);
  entry->SetPostID(navigation->post_id_);
  entry->SetOriginalRequestURL(navigation->original_request_url_);
  entry->SetIsOverridingUserAgent(navigation->is_overriding_user_agent_);
  entry->SetTimestamp(navigation->timestamp_);
  entry->SetHttpStatusCode(navigation->http_status_code_);
  entry->SetRedirectChain(navigation->redirect_chain_);
  entry->SetVirtualURL(navigation->virtual_url_);
  sessions::NavigationTaskId* navigation_task_id =
      sessions::NavigationTaskId::Get(entry.get());
  navigation_task_id->set_id(navigation->task_id());
  navigation_task_id->set_parent_id(navigation->parent_task_id());
  navigation_task_id->set_root_id(navigation->root_task_id());

  const ContentSerializedNavigationDriver::ExtendedInfoHandlerMap&
      extended_info_handlers = ContentSerializedNavigationDriver::GetInstance()
                                   ->GetAllExtendedInfoHandlers();
  for (const auto& extended_info_entry : navigation->extended_info_map_) {
    const std::string& key = extended_info_entry.first;
    if (!extended_info_handlers.count(key))
      continue;
    ExtendedInfoHandler* extended_info_handler =
        extended_info_handlers.at(key).get();
    DCHECK(extended_info_handler);
    extended_info_handler->RestoreExtendedInfo(extended_info_entry.second,
                                               entry.get());
  }

  entry->InitRestoredEntry(browser_context);

  // These fields should have default values.
  DCHECK_EQ(SerializedNavigationEntry::STATE_INVALID,
            navigation->blocked_state_);
  DCHECK_EQ(0u, navigation->content_pack_categories_.size());

  return entry;
}

// static
std::vector<std::unique_ptr<content::NavigationEntry>>
ContentSerializedNavigationBuilder::ToNavigationEntries(
    const std::vector<SerializedNavigationEntry>& navigations,
    content::BrowserContext* browser_context) {
  std::vector<std::unique_ptr<content::NavigationEntry>> entries;
  entries.reserve(navigations.size());
  for (const auto& navigation : navigations)
    entries.push_back(ToNavigationEntry(&navigation, browser_context));
  return entries;
}

}  // namespace sessions
