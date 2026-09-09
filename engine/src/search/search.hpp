#pragma once

#include "core/move.hpp"
#include "core/position.hpp"
#include "core/types.hpp"

#include <atomic>
#include <cstdint>
#include <functional>

namespace russell::search {

class transposition_table;

constexpr auto mate_value = 30000;

struct search_result {
  move best_move{};
  int score = 0;
  int depth = 0;
  std::uint64_t nodes = 0;
  std::uint64_t lmr_applied = 0;
  std::uint64_t lmr_research = 0;
  std::uint64_t lmp_pruned = 0;
  std::uint64_t rfp_pruned = 0;
  std::uint64_t razor_pruned = 0;
  std::uint64_t improving_count = 0;
  std::uint64_t capture_history_updates = 0;
  std::uint64_t see_ordered_captures = 0;
  std::uint64_t see_winning_captures = 0;
  std::uint64_t see_losing_captures = 0;
  std::uint64_t iir_applied = 0;
  std::uint64_t extensions_applied = 0;
  std::uint64_t check_extensions = 0;
  std::uint64_t aspiration_attempts = 0;
  std::uint64_t aspiration_fail_low = 0;
  std::uint64_t aspiration_fail_high = 0;
  std::uint64_t aspiration_researches = 0;
  std::uint64_t time_soft_stops = 0;
  std::uint64_t time_hard_stops = 0;
  std::uint64_t time_extensions = 0;
  bool has_move = false;
};

struct search_limits {
  int depth = 0;
  std::uint64_t nodes = 0;
  bool infinite = false;

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

inline constexpr int lmr_min_depth = 3;
inline constexpr int lmr_min_move_count = 6;
inline constexpr int lmr_max_reduction = 5;
inline constexpr int lmr_capture_adjust = 1;
inline constexpr int lmr_history_scale = 2048;
inline constexpr int lmr_history_max_adjust = 2;
inline constexpr int lmr_max_table_depth = 64;
inline constexpr int lmr_max_table_moves = 64;

inline constexpr int nmp_min_depth = 3;
inline constexpr int nmp_base_reduction = 2;
inline constexpr int nmp_depth_divisor = 4;
inline constexpr int nmp_max_reduction = 4;
inline constexpr int nmp_verification_depth = 7;
inline constexpr int nmp_min_non_pawn = 3;
inline constexpr int nmp_eval_margin = 128;

inline constexpr int lmp_max_depth = 4;
inline constexpr int lmp_base_count = 20;
inline constexpr int lmp_depth_factor = 3;
inline constexpr int lmp_history_scale = 8192;
inline constexpr int lmp_history_max_adjust = 1;

inline constexpr int rfp_max_depth = 6;
inline constexpr int rfp_base = 30;
inline constexpr int rfp_per_depth = 65;
inline constexpr int rfp_quad = 8;
inline constexpr int rfp_history_scale = 4096;
inline constexpr int rfp_history_max_adjust = 20;

inline constexpr int razor_max_depth = 3;
inline constexpr int razor_base = 120;
inline constexpr int razor_per_depth = 70;
inline constexpr int razor_quad = 10;
inline constexpr int razor_history_scale = 4096;
inline constexpr int razor_history_max_adjust = 10;

inline constexpr int improving_rfp_bonus = 40;
inline constexpr int improving_lmp_bonus = 1;
inline constexpr int improving_razor_bonus = 40;

inline constexpr int capture_history_max = 16384;
inline constexpr int capture_history_bonus = 32;
inline constexpr int capture_history_malus = 32;
inline constexpr int capture_history_scale = 1024;
inline constexpr int capture_history_lmr_scale = 4096;

inline constexpr double tm_increment_factor = 0.70;
inline constexpr double tm_hard_factor = 1.50;
inline constexpr int tm_score_stability = 50;
inline constexpr double tm_unstable_factor = 1.50;

inline constexpr int aspiration_initial = 25;
inline constexpr int aspiration_growth = 2;
inline constexpr int aspiration_max = 2000;
inline constexpr int aspiration_max_retries = 8;

inline constexpr int iir_min_depth = 4;
inline constexpr int iir_depth_reduction = 1;

inline constexpr int check_extension_value = 1;
inline constexpr int check_extension_max_ply = 128;

[[nodiscard]] auto lmr_reduction(int depth, int history) noexcept -> int;
[[nodiscard]] auto lmr_reduction(int depth, int move_count, bool is_capture,
                                 int history) noexcept -> int;
[[nodiscard]] auto lmr_base_reduction(int depth, int move_count) noexcept -> int;
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