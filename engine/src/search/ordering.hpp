#pragma once

#include "core/move.hpp"
#include "core/position.hpp"
#include "core/types.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace russell::search {

constexpr auto max_ply = 128;
constexpr auto max_killers = 2;

constexpr auto no_move = move{};

[[nodiscard]] auto is_capture(move mv) noexcept -> bool;
[[nodiscard]] auto is_promotion(move mv) noexcept -> bool;
[[nodiscard]] auto is_quiet(move mv) noexcept -> bool;
[[nodiscard]] auto static_exchange_eval(const position& pos, move mv) noexcept -> int;

class move_ordering {
public:
  move_ordering();

  auto reset() noexcept -> void;

  [[nodiscard]] auto score(const position& pos, move mv, int ply, move tt_move = no_move,
                           move prev_move = no_move) const noexcept -> int;

  auto update_killers(move mv, int ply) noexcept -> void;
  auto update_history(color side, move mv, int depth) noexcept -> void;

  [[nodiscard]] auto is_killer(move mv, int ply) const noexcept -> bool;
  [[nodiscard]] auto history_score(color side, int from, int to) const noexcept -> int;
  auto update_counter(move prev, move cur) noexcept -> void;
  auto update_continuation(move prev, move cur, int depth) noexcept -> void;

  [[nodiscard]] auto capture_history_score(color side, piece_type moving, int to,
                                           piece_type captured) const noexcept -> int;
  auto update_capture_history(color side, piece_type moving, int to,
                              piece_type captured, int bonus) noexcept -> void;
  auto update_capture_history(color side, move mv, const position& pos, int bonus) noexcept -> void;

  mutable uint64_t see_ordered_captures = 0;
  mutable uint64_t see_winning_captures = 0;
  mutable uint64_t see_losing_captures = 0;

private:
  std::array<std::array<move, max_killers>, max_ply> killers_{};
  std::array<std::array<std::array<int, 64>, 64>, color_count> history_{};
  std::array<std::array<move, 64>, 64> counter_{};
  std::vector<int16_t> cont_history_{};

  std::array<std::array<std::array<std::array<int, 6>, 64>, 6>, 2> capture_history_{};
};

[[nodiscard]] auto order_moves(const position& pos, const move_list& moves, int ply,
                               const move_ordering& order, move tt_move = no_move,
                               move prev_move = no_move) noexcept -> move_list;

}