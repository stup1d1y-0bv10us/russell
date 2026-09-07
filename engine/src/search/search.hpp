#pragma once

#include "core/move.hpp"
#include "core/position.hpp"
#include "core/types.hpp"

#include <atomic>
#include <cstdint>
#include <functional>

namespace tuna::search {

class transposition_table;

constexpr auto mate_value = 30000;

struct search_result {
  move best_move{};
  int score = 0;
  int depth = 0;
  std::uint64_t nodes = 0;
  std::uint64_t lmr_applied = 0;
  std::uint64_t lmr_research = 0;
  bool has_move = false;
};

struct search_limits {
  int depth = 0;

  int soft_time_ms = 0;
  int hard_time_ms = 0;
};

struct search_stopper {
  std::atomic<bool> stop{false};
};

[[nodiscard]] auto minimax(position& pos, int depth) -> search_result;
[[nodiscard]] auto alpha_beta(position& pos, int depth, bool use_null_move = true,
                               bool use_lmr = true, bool use_quiescence = true,
                               bool use_futility = true, bool use_check_extension = true) -> search_result;
[[nodiscard]] auto lmr_reduction(int depth, int history) noexcept -> int;
[[nodiscard]] auto iterative_deepening(position& pos, int max_depth) -> search_result;
[[nodiscard]] auto search(position& pos, const search_limits& limits, const search_stopper& stopper,
                          const std::function<void(const search_result&)>& on_iteration = {}) -> search_result;

[[nodiscard]] auto search(position& pos, const search_limits& limits, const search_stopper& stopper,
                          transposition_table& tt,
                          const std::function<void(const search_result&)>& on_iteration = {}) -> search_result;

[[nodiscard]] auto parallel_search(position& pos, const search_limits& limits, search_stopper& stopper,
                                   int threads,
                                   const std::function<void(const search_result&)>& on_iteration = {}) -> search_result;

[[nodiscard]] auto parallel_search(position& pos, const search_limits& limits, search_stopper& stopper,
                                   int threads, transposition_table& tt,
                                   const std::function<void(const search_result&)>& on_iteration = {}) -> search_result;

}