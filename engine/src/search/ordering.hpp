#pragma once

#include "core/move.hpp"
#include "core/position.hpp"
#include "core/types.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace russell::search {

constexpr auto max_ply = 128;
constexpr auto max_killers = 2;

constexpr auto killer_min_depth = 2;

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
                           move prev_move = no_move, piece prev_moved = piece::none,
                           move prev2_move = no_move, piece prev2_moved = piece::none) const noexcept -> int;

  auto update_killers(move mv, int ply) noexcept -> void;
  auto update_history(color side, move mv, int depth) noexcept -> void;

  auto update_history(color side, int from, int to, int bonus) noexcept -> void;

  [[nodiscard]] auto is_killer(move mv, int ply) const noexcept -> bool;

  [[nodiscard]] auto is_countermove(move prev, move cur) const noexcept -> bool;
  [[nodiscard]] auto history_score(color side, int from, int to) const noexcept -> int;

  [[nodiscard]] auto countermove_history_score(move prev, move cur) const noexcept -> int;
  auto update_countermove_history(move prev, move cur, int depth) noexcept -> void;
  auto update_countermove_history(int prev_from, int prev_to, int cur_from, int cur_to,
                                  int bonus) noexcept -> void;

  [[nodiscard]] auto continuation_score_1ply(piece prev_moved, int prev_to, piece cur_piece,
                                             int cur_to) const noexcept -> int;
  [[nodiscard]] auto continuation_score_2ply(piece prev2_moved, int prev2_to, piece cur_piece,
                                             int cur_to) const noexcept -> int;
  auto update_continuation_1ply(piece prev_moved, int prev_to, piece cur_piece, int cur_to,
                                int bonus) noexcept -> void;
  auto update_continuation_2ply(piece prev2_moved, int prev2_to, piece cur_piece, int cur_to,
                                int bonus) noexcept -> void;

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

  std::vector<int16_t> counter_history_{};

  std::vector<int16_t> cont_history_1ply_{};
  std::vector<int16_t> cont_history_2ply_{};

  std::array<std::array<std::array<std::array<int, 6>, 64>, 6>, 2> capture_history_{};
};

[[nodiscard]] auto order_moves(const position& pos, const move_list& moves, int ply,
                               const move_ordering& order, move tt_move = no_move,
                               move prev_move = no_move) noexcept -> move_list;

enum class picker_stage : std::uint8_t { tt_move, captures, quiets, losing, done };

class move_picker {
public:
  move_picker(const move_ordering& order, int ply, move tt_move, bool tt_searchable,
              move_list cap_moves, bool have_cap_moves, move_list quiet_moves,
              bool have_quiet_moves, move prev_move, piece prev_moved, move prev2_move,
              piece prev2_moved, move excluded_move);

  move_picker(const move_picker&) = delete;
  auto operator=(const move_picker&) -> move_picker& = delete;

  auto next(position& pos, move& mv, int& see, int& idx) -> bool;

private:
  struct scored {
    move mv{};
    int score = 0;
    int see = std::numeric_limits<int>::max();
  };

  const move_ordering& order_;
  int ply_;
  move tt_move_;
  bool tt_searchable_;
  bool tt_emitted_ = false;
  move_list cap_moves_;
  bool have_cap_moves_;
  move_list quiet_moves_;
  bool have_quiet_moves_;
  move prev_move_;
  piece prev_moved_;
  move prev2_move_;
  piece prev2_moved_;
  move excluded_move_;
  picker_stage stage_ = picker_stage::tt_move;
  int emitted_ = 0;
  bool caps_prepared_ = false;
  bool quiets_prepared_ = false;

  std::array<scored, 256> caps_{};
  std::array<bool, 256> cap_skip_{};
  std::array<bool, 256> cap_taken_{};
  size_t cap_n_ = 0;
  std::array<scored, 256> losing_{};
  std::array<bool, 256> losing_taken_{};
  size_t losing_n_ = 0;
  std::array<scored, 256> quiets_{};
  std::array<bool, 256> quiet_taken_{};
  size_t quiet_n_ = 0;

  auto prepare_captures(position& pos) -> void;
  auto prepare_quiets(position& pos) -> void;

  static auto pick_best(const std::array<scored, 256>& list,
                        const std::array<bool, 256>& skip,
                        const std::array<bool, 256>& taken, size_t n) noexcept -> size_t;
  static auto pick_best(const std::array<scored, 256>& list,
                        const std::array<bool, 256>& taken, size_t n) noexcept -> size_t;
};

}
