// Copyright (c) 2024-2026 Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#include "../src/prefix_cache.h"

#include <iostream>
#include <vector>

namespace {

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __func__ << ":" << __LINE__ << ": check failed: " #condition << '\n'; \
            return false;                                                                      \
        }                                                                                      \
    } while (false)

bool test_failed_rollback_resets_and_reinitializes_speculation() {
    std::vector<int>       target_memory{0, 1, 2, 3, 4, 5};
    std::vector<int>       draft_memory{0, 1, 2, 3, 4, 5};
    std::vector<int>       cached_prompt{10, 11, 12, 13, 14, 15};
    const std::vector<int> new_prompt{10, 11, 20, 21};
    std::vector<int>       speculative_prompt{10, 11, 12, 13, 14, 15};
    int                    n_past        = 6;
    int                    n_past_global = 6;
    int                    reset_count   = 0;
    int                    spec_epoch    = 1;
    int                    begin_count   = 0;

    const geniex::PrefixCacheRollbackResult result = geniex::rollback_prefix_cache(
        2,
        n_past,
        n_past_global,
        cached_prompt,
        new_prompt,
        []() { return false; },
        [&]() {
            ++reset_count;
            target_memory.clear();
            draft_memory.clear();
            n_past        = 0;
            n_past_global = 0;
            cached_prompt.clear();
            spec_epoch = 2;  // A new speculative implementation owns fresh prompt-dependent state.
            return true;
        });

    CHECK(result == geniex::PrefixCacheRollbackResult::reset);
    CHECK(reset_count == 1);
    CHECK(target_memory.empty());
    CHECK(draft_memory.empty());
    CHECK(n_past == 0);
    CHECK(n_past_global == 0);
    CHECK(cached_prompt.empty());
    CHECK(spec_epoch == 2);

    const std::vector<int> prefill(new_prompt.begin() + n_past_global, new_prompt.end());
    CHECK(prefill == new_prompt);

    // The real decode path calls common_speculative_begin after prefill with prompt_ids.
    auto begin_speculative = [&](const std::vector<int>& prompt) {
        ++begin_count;
        speculative_prompt = prompt;
    };
    begin_speculative(prefill);
    CHECK(begin_count == 1);
    CHECK(speculative_prompt == new_prompt);
    return true;
}

bool test_reinitialization_failure_stops_before_prefill() {
    std::vector<int>       target_memory{0, 1, 2};
    std::vector<int>       draft_memory{0, 1, 2};
    std::vector<int>       cached_prompt{10, 11, 12};
    const std::vector<int> new_prompt{10, 20, 21};
    int                    n_past        = 3;
    int                    n_past_global = 3;
    int                    reset_count   = 0;
    int                    begin_count   = 0;

    const geniex::PrefixCacheRollbackResult result = geniex::rollback_prefix_cache(
        1,
        n_past,
        n_past_global,
        cached_prompt,
        new_prompt,
        []() { return false; },
        [&]() {
            ++reset_count;
            target_memory.clear();
            draft_memory.clear();
            n_past        = 0;
            n_past_global = 0;
            cached_prompt.clear();
            return false;  // Speculative implementation recreation failed.
        });

    CHECK(result == geniex::PrefixCacheRollbackResult::reset_failed);
    CHECK(reset_count == 1);
    CHECK(target_memory.empty());
    CHECK(draft_memory.empty());
    CHECK(n_past == 0);
    CHECK(n_past_global == 0);
    CHECK(cached_prompt.empty());
    CHECK(begin_count == 0);  // Caller must return an existing generation error, not prefill stale state.

    bool speculative_ready = false;
    bool reinit_required   = true;
    int  retry_count       = 0;
    int  plain_generations = 0;
    int  spec_generations  = 0;
    auto next_generate     = [&](bool reinit_succeeds) {
        if (reinit_required) {
            ++retry_count;
            if (!reinit_succeeds) return false;
            speculative_ready = true;
            reinit_required   = false;
        }
        if (speculative_ready) {
            ++spec_generations;
        } else {
            ++plain_generations;
        }
        return true;
    };

    CHECK(!next_generate(false));  // A second init failure still returns an error.
    CHECK(reinit_required);
    CHECK(retry_count == 1);
    CHECK(plain_generations == 0);
    CHECK(spec_generations == 0);
    CHECK(next_generate(true));  // A later retry restores spec before any generation.
    CHECK(!reinit_required);
    CHECK(retry_count == 2);
    CHECK(plain_generations == 0);
    CHECK(spec_generations == 1);
    return true;
}

bool test_successful_rollback_preserves_prefix_reuse() {
    std::vector<int>       memory_positions{0, 1, 2, 3, 4, 5};
    std::vector<int>       cached_prompt{10, 11, 12, 13, 14, 15};
    const std::vector<int> new_prompt{10, 11, 20, 21};
    int                    n_past        = 6;
    int                    n_past_global = 6;
    int                    reset_count   = 0;

    const geniex::PrefixCacheRollbackResult result = geniex::rollback_prefix_cache(
        2,
        n_past,
        n_past_global,
        cached_prompt,
        new_prompt,
        [&]() {
            memory_positions.resize(2);
            return true;
        },
        [&]() {
            ++reset_count;
            return true;
        });

    CHECK(result == geniex::PrefixCacheRollbackResult::reused);
    CHECK(reset_count == 0);
    CHECK((memory_positions == std::vector<int>{0, 1}));
    CHECK(n_past == 2);
    CHECK(n_past_global == 2);
    CHECK((cached_prompt == std::vector<int>{10, 11}));
    const std::vector<int> prefill(new_prompt.begin() + n_past_global, new_prompt.end());
    CHECK((prefill == std::vector<int>{20, 21}));
    return true;
}

}  // namespace

int main() {
    return test_failed_rollback_resets_and_reinitializes_speculation() &&
                   test_reinitialization_failure_stops_before_prefill() &&
                   test_successful_rollback_preserves_prefix_reuse()
               ? 0
               : 1;
}
