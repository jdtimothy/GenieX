// Copyright (c) 2024-2026 Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <vector>

namespace geniex {

enum class PrefixCacheRollbackResult { reused, reset, reset_failed };

template <typename Token, typename RemoveTail, typename ResetCache>
PrefixCacheRollbackResult rollback_prefix_cache(int match_len, int& n_past, int& n_past_global,
    std::vector<Token>& past_prompt_tokens, const std::vector<Token>& prompt_ids, RemoveTail&& remove_tail,
    ResetCache&& reset_cache) {
    if (!remove_tail()) {
        return reset_cache() ? PrefixCacheRollbackResult::reset : PrefixCacheRollbackResult::reset_failed;
    }

    n_past        = match_len;
    n_past_global = match_len;
    past_prompt_tokens.assign(prompt_ids.begin(), prompt_ids.begin() + match_len);
    return PrefixCacheRollbackResult::reused;
}

}  // namespace geniex
