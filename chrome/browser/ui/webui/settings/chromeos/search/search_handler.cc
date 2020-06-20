// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/chromeos/search/search_handler.h"

#include <algorithm>

#include "base/strings/string_number_conversions.h"
#include "chrome/browser/chromeos/local_search_service/local_search_service.h"
#include "chrome/browser/ui/webui/settings/chromeos/hierarchy.h"
#include "chrome/browser/ui/webui/settings/chromeos/os_settings_sections.h"
#include "chrome/browser/ui/webui/settings/chromeos/search/search_concept.h"
#include "chrome/browser/ui/webui/settings/chromeos/search/search_result_icon.mojom.h"
#include "chrome/browser/ui/webui/settings/chromeos/search/search_tag_registry.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace chromeos {
namespace settings {
namespace {

bool ContainsSectionResult(const std::vector<mojom::SearchResultPtr>& results,
                           mojom::Section section) {
  return std::find_if(
             results.begin(), results.end(), [section](const auto& result) {
               return result->type == mojom::SearchResultType::kSection &&
                      section == result->id->get_section();
             }) != results.end();
}

bool ContainsSubpageResult(const std::vector<mojom::SearchResultPtr>& results,
                           mojom::Subpage subpage) {
  return std::find_if(
             results.begin(), results.end(), [subpage](const auto& result) {
               return result->type == mojom::SearchResultType::kSubpage &&
                      subpage == result->id->get_subpage();
             }) != results.end();
}

}  // namespace

SearchHandler::SearchHandler(
    SearchTagRegistry* search_tag_registry,
    OsSettingsSections* sections,
    Hierarchy* hierarchy,
    local_search_service::LocalSearchService* local_search_service)
    : search_tag_registry_(search_tag_registry),
      sections_(sections),
      hierarchy_(hierarchy),
      index_(local_search_service->GetIndex(
          local_search_service::IndexId::kCrosSettings)) {}

SearchHandler::~SearchHandler() = default;

void SearchHandler::BindInterface(
    mojo::PendingReceiver<mojom::SearchHandler> pending_receiver) {
  receivers_.Add(this, std::move(pending_receiver));
}

std::vector<mojom::SearchResultPtr> SearchHandler::Search(
    const base::string16& query,
    uint32_t max_num_results,
    mojom::ParentResultBehavior parent_result_behavior) {
  // Search for 5x the maximum set of results. If there are many matches for
  // a query, it may be the case that |index_| returns some matches with higher
  // SearchResultDefaultRank values later in the list. Requesting up to 5x the
  // maximum number ensures that such results will be returned and can be ranked
  // accordingly when sorted.
  uint32_t max_local_search_service_results = 5 * max_num_results;

  std::vector<local_search_service::Result> local_search_service_results;
  local_search_service::ResponseStatus response_status = index_->Find(
      query, max_local_search_service_results, &local_search_service_results);

  if (response_status != local_search_service::ResponseStatus::kSuccess) {
    LOG(ERROR) << "Cannot search; LocalSearchService returned "
               << static_cast<int>(response_status)
               << ". Returning empty results array.";
    return {};
  }

  return GenerateSearchResultsArray(local_search_service_results,
                                    max_num_results, parent_result_behavior);
}

void SearchHandler::Search(const base::string16& query,
                           uint32_t max_num_results,
                           mojom::ParentResultBehavior parent_result_behavior,
                           SearchCallback callback) {
  std::move(callback).Run(
      Search(query, max_num_results, parent_result_behavior));
}

std::vector<mojom::SearchResultPtr> SearchHandler::GenerateSearchResultsArray(
    const std::vector<local_search_service::Result>&
        local_search_service_results,
    uint32_t max_num_results,
    mojom::ParentResultBehavior parent_result_behavior) const {
  std::vector<mojom::SearchResultPtr> search_results;
  for (const auto& result : local_search_service_results) {
    mojom::SearchResultPtr result_ptr = ResultToSearchResult(result);
    if (result_ptr)
      search_results.push_back(std::move(result_ptr));
  }

  std::sort(search_results.begin(), search_results.end(), CompareSearchResults);

  // Now that the results have been sorted, limit the size of to
  // |max_num_results|.
  search_results.resize(
      std::min(static_cast<size_t>(max_num_results), search_results.size()));

  if (parent_result_behavior ==
      mojom::ParentResultBehavior::kAllowParentResults) {
    AddParentResults(max_num_results, &search_results);
  }

  return search_results;
}

void SearchHandler::AddParentResults(
    uint32_t max_num_results,
    std::vector<mojom::SearchResultPtr>* search_results) const {
  auto it = search_results->begin();
  while (search_results->size() < max_num_results &&
         it != search_results->end()) {
    const mojom::SearchResultPtr& result = *it;
    switch (result->type) {
      case mojom::SearchResultType::kSection:
        // Sections have no parents; nothing to do.
        break;

      case mojom::SearchResultType::kSubpage: {
        const Hierarchy::SubpageMetadata& metadata =
            hierarchy_->GetSubpageMetadata(result->id->get_subpage());

        // Nested subpage.
        if (metadata.parent_subpage) {
          it = AddSubpageResultIfPossible(it, *metadata.parent_subpage,
                                          result->relevance_score,
                                          search_results);
          break;
        }

        // Top-level subpage.
        it = AddSectionResultIfPossible(it, result, metadata.section,
                                        search_results);
        break;
      }

      case mojom::SearchResultType::kSetting: {
        const Hierarchy::SettingMetadata& metadata =
            hierarchy_->GetSettingMetadata(result->id->get_setting());

        // Nested setting.
        if (metadata.primary.second) {
          it = AddSubpageResultIfPossible(it, *metadata.primary.second,
                                          result->relevance_score,
                                          search_results);
          break;
        }

        // Top-level setting.
        it = AddSectionResultIfPossible(it, result, metadata.primary.first,
                                        search_results);
        break;
      }
    }

    ++it;
  }
}

std::vector<mojom::SearchResultPtr>::iterator
SearchHandler::AddSectionResultIfPossible(
    const std::vector<mojom::SearchResultPtr>::iterator& curr_position,
    const mojom::SearchResultPtr& child_result,
    mojom::Section section,
    std::vector<mojom::SearchResultPtr>* results) const {
  // If |results| already includes |section|, do not add it again.
  if (ContainsSectionResult(*results, section))
    return curr_position;

  mojom::SearchResultPtr section_result =
      hierarchy_->GetSectionMetadata(section).ToSearchResult(
          child_result->relevance_score);

  // Don't add a result for a parent section if it has the exact same text as
  // the child result, since this results in a broken-looking UI.
  if (section_result->result_text == child_result->result_text)
    return curr_position;

  return results->insert(curr_position + 1, std::move(section_result));
}

std::vector<mojom::SearchResultPtr>::iterator
SearchHandler::AddSubpageResultIfPossible(
    const std::vector<mojom::SearchResultPtr>::iterator& curr_position,
    mojom::Subpage subpage,
    double relevance_score,
    std::vector<mojom::SearchResultPtr>* results) const {
  // If |results| already includes |subpage|, do not add it again.
  if (ContainsSubpageResult(*results, subpage))
    return curr_position;

  return results->insert(
      curr_position + 1,
      hierarchy_->GetSubpageMetadata(subpage).ToSearchResult(relevance_score));
}

mojom::SearchResultPtr SearchHandler::ResultToSearchResult(
    const local_search_service::Result& result) const {
  const SearchConcept* concept =
      search_tag_registry_->GetTagMetadata(result.id);

  // If the concept was not registered, no metadata is available. This can occur
  // if the search tag was dynamically unregistered during the asynchronous
  // Find() call.
  if (!concept)
    return nullptr;

  // |result| is expected to have one position, whose ID is a stringified int.
  DCHECK_EQ(1u, result.positions.size());
  int content_id;
  if (!base::StringToInt(result.positions[0].content_id, &content_id))
    return nullptr;

  std::string url;
  mojom::SearchResultIdentifierPtr result_id;
  std::vector<base::string16> hierarchy_strings;
  switch (concept->type) {
    case mojom::SearchResultType::kSection: {
      mojom::Section section = concept->id.section;
      url = GetModifiedUrl(*concept, section);
      result_id = mojom::SearchResultIdentifier::NewSection(section);
      hierarchy_strings.push_back(
          l10n_util::GetStringUTF16(IDS_INTERNAL_APP_SETTINGS));
      break;
    }
    case mojom::SearchResultType::kSubpage: {
      mojom::Subpage subpage = concept->id.subpage;
      url = GetModifiedUrl(*concept,
                           hierarchy_->GetSubpageMetadata(subpage).section);
      result_id = mojom::SearchResultIdentifier::NewSubpage(subpage);
      hierarchy_strings = hierarchy_->GenerateAncestorHierarchyStrings(subpage);
      break;
    }
    case mojom::SearchResultType::kSetting: {
      mojom::Setting setting = concept->id.setting;
      url = GetModifiedUrl(
          *concept, hierarchy_->GetSettingMetadata(setting).primary.first);
      result_id = mojom::SearchResultIdentifier::NewSetting(setting);
      hierarchy_strings = hierarchy_->GenerateAncestorHierarchyStrings(setting);
      break;
    }
  }

  return mojom::SearchResult::New(
      /*result_text=*/l10n_util::GetStringUTF16(content_id),
      /*canonical_result_text=*/
      l10n_util::GetStringUTF16(concept->canonical_message_id), url,
      concept->icon, result.score, hierarchy_strings, concept->default_rank,
      concept->type, std::move(result_id));
}

std::string SearchHandler::GetModifiedUrl(const SearchConcept& concept,
                                          mojom::Section section) const {
  return sections_->GetSection(section)->ModifySearchResultUrl(
      concept.type, concept.id, concept.url_path_with_parameters);
}

// static
bool SearchHandler::CompareSearchResults(const mojom::SearchResultPtr& first,
                                         const mojom::SearchResultPtr& second) {
  // Compute the difference between the results' default rankings. Note that
  // kHigh is declared before kMedium which is declared before kLow, so a
  // negative value indicates that |first| is ranked higher than |second| and a
  // positive value indicates that |second| is ranked higher than |first|.
  int32_t default_rank_diff = static_cast<int32_t>(first->default_rank) -
                              static_cast<int32_t>(second->default_rank);
  if (default_rank_diff < 0)
    return true;
  if (default_rank_diff > 0)
    return false;

  // At this point, the default ranks are equal, so compare relevance scores. A
  // higher relevance score indicates a better text match, so the reverse is
  // true this time.
  if (first->relevance_score > second->relevance_score)
    return true;
  if (first->relevance_score < second->relevance_score)
    return false;

  // Default rank and relevance scores are equal, so prefer the result which is
  // higher on the hierarchy. kSection is declared before kSubpage which is
  // declared before kSetting, so follow the same pattern from default ranks
  // above. Note that if the types are equal, this will return false, which
  // induces a strict weak ordering.
  return static_cast<int32_t>(first->type) < static_cast<int32_t>(second->type);
}

}  // namespace settings
}  // namespace chromeos
