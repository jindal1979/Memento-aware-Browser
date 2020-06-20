// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/string_matching/tokenized_string_match.h"

#include <stddef.h>

#include <cmath>

#include "base/i18n/string_search.h"
#include "base/strings/string_util.h"
#include "chrome/common/string_matching/prefix_matcher.h"

namespace {
// Used for each character if there is no prefix match.
const double kIsSubstringMultiplier = 0.4;

// A relevance score that represents no match.
const double kNoMatchScore = 0.0;
}  // namespace

TokenizedStringMatch::TokenizedStringMatch() : relevance_(kNoMatchScore) {}

TokenizedStringMatch::~TokenizedStringMatch() = default;

bool TokenizedStringMatch::Calculate(const TokenizedString& query,
                                     const TokenizedString& text) {
  relevance_ = kNoMatchScore;
  hits_.clear();

  // If there is an exact match, relevance will be 1.0 and there is only 1 hit
  // that is the entire text/query.
  const auto& query_text = query.text();
  const auto& text_text = text.text();
  const auto query_size = query_text.size();
  const auto text_size = text_text.size();
  if (query_size > 0 && query_size == text_size &&
      base::EqualsCaseInsensitiveASCII(query_text, text_text)) {
    hits_.push_back(gfx::Range(0, query_size));
    relevance_ = 1.0;
    return true;
  }

  PrefixMatcher matcher(query, text);
  if (matcher.Match()) {
    relevance_ = matcher.relevance();
    hits_.assign(matcher.hits().begin(), matcher.hits().end());
  }

  // Substring match as a fallback.
  if (relevance_ == kNoMatchScore) {
    size_t substr_match_start = 0;
    size_t substr_match_length = 0;
    if (base::i18n::StringSearchIgnoringCaseAndAccents(
            query.text(), text.text(), &substr_match_start,
            &substr_match_length)) {
      relevance_ = kIsSubstringMultiplier * substr_match_length;
      hits_.push_back(gfx::Range(substr_match_start,
                                 substr_match_start + substr_match_length));
    }
  }

  // Temper the relevance score with an exponential curve. Each point of
  // relevance (roughly, each keystroke) is worth less than the last. This means
  // that typing a few characters of a word is enough to promote matches very
  // high, with any subsequent characters being worth comparatively less.
  // TODO(mgiuca): This doesn't really play well with Omnibox results, since as
  // you type more characters, the app/omnibox results tend to jump over each
  // other.
  relevance_ = 1.0 - std::pow(0.5, relevance_);

  return relevance_ > kNoMatchScore;
}

bool TokenizedStringMatch::Calculate(const base::string16& query,
                                     const base::string16& text) {
  const TokenizedString tokenized_query(query);
  const TokenizedString tokenized_text(text);
  return Calculate(tokenized_query, tokenized_text);
}
