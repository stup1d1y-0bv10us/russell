#include "search/ordering.hpp"

#include "movegen/movegen.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace russell::search {

namespace {

constexpr auto capture_base = 100000;
constexpr auto promotion_base = 80000;
constexpr auto killer_1_score = 70000;
constexpr auto killer_2_score = 60000;
constexpr auto tt_move_score = 150000;

constexpr int capture_history_max = 16384;
constexpr int capture_history_scale = 1024;

constexpr auto clamp_ply(int ply) noexcept -> int
{
  return ply < max_ply ? ply : max_ply - 1;
}

auto mvv_lva(piece_type pt) noexcept -> int
{
  switch(pt) {
  case piece_type::pawn: return 1;
  case piece_type::knight: return 3;
  case piece_type::bishop: return 3;
  case piece_type::rook: return 5;
  case piece_type::queen: return 9;
  case piece_type::king: return 0;
  }
  return 0;
}

auto piece_value(piece_type pt) noexcept -> int
{
  switch(pt) {
  case piece_type::pawn: return 100;
  case piece_type::knight: return 320;
  case piece_type::bishop: return 330;
  case piece_type::rook: return 500;
  case piece_type::queen: return 900;
  case piece_type::king: return 20000;
  }
  return 0;
}

auto captured_value(const position& pos, move mv) noexcept -> int
{
  if(mv.flag == move_flag::en_passant) {
    return piece_value(piece_type::pawn);
  }
  const auto captured = pos.piece_on(mv.to);
  return captured == piece::none ? 0 : piece_value(piece_type_of(captured));
}

auto promotion_delta(move mv) noexcept -> int
{
  return is_promotion(mv) ? piece_value(mv.promotion) - piece_value(piece_type::pawn) : 0;
}

auto least_valuable_capture_to(position& pos, int target) noexcept -> move
{
  auto best = no_move;
  auto best_value = 30000;
  for(const auto mv : movegen::generate_legal(pos)) {
    if(!is_capture(mv) || static_cast<int>(mv.to) != target) {
      continue;
    }
    const auto attacker = pos.piece_on(mv.from);
    if(attacker == piece::none) {
      continue;
    }
    const auto value = piece_value(piece_type_of(attacker));
    if(value < best_value) {
      best_value = value;
      best = mv;
    }
  }
  return best;
}

auto see_rec(position& pos, int target) noexcept -> int
{
  const auto mv = least_valuable_capture_to(pos, target);
  if(mv == no_move) {
    return 0;
  }
  const auto gain = captured_value(pos, mv) + promotion_delta(mv);
  const auto st = pos.make_move(mv);
  const auto reply = see_rec(pos, target);
  pos.unmake_move(mv, st);
  return std::max(0, gain - reply);
}

}

auto is_capture(move mv) noexcept -> bool
{
  return mv.flag == move_flag::capture || mv.flag == move_flag::en_passant
         || mv.flag == move_flag::promotion_capture;
}

auto is_promotion(move mv) noexcept -> bool
{
  return mv.flag == move_flag::promotion || mv.flag == move_flag::promotion_capture;
}

auto is_quiet(move mv) noexcept -> bool
{
  return !is_capture(mv) && !is_promotion(mv);
}

auto static_exchange_eval(const position& pos, move mv) noexcept -> int
{
  if(!is_capture(mv)) {
    return 0;
  }
  auto copy = pos;
  const auto gain = captured_value(copy, mv) + promotion_delta(mv);
  const auto target = static_cast<int>(mv.to);
  const auto st = copy.make_move(mv);
  const auto reply = see_rec(copy, target);
  copy.unmake_move(mv, st);
  return gain - reply;
}

move_ordering::move_ordering()
    : cont_history_(64 * 64 * 64 * 64, 0)
{
}

auto move_ordering::reset() noexcept -> void
{
  killers_ = {};
  history_ = {};
  counter_ = {};
  std::fill(cont_history_.begin(), cont_history_.end(), int16_t{0});
  for(auto& c : capture_history_) for(auto& m : c) for(auto& t : m) for(auto& v : t) v = 0;
  see_ordered_captures = 0;
  see_winning_captures = 0;
  see_losing_captures = 0;
}

auto move_ordering::score(const position& pos, move mv, int ply, move tt_move,
                           move prev_move) const noexcept -> int
{
  if(tt_move != no_move && mv == tt_move) {
    return tt_move_score;
  }
  if(is_capture(mv)) {
    auto victim = piece_type::pawn;
    if(mv.flag != move_flag::en_passant) {
      const auto captured = pos.piece_on(mv.to);
      if(captured != piece::none) {
        victim = piece_type_of(captured);
      }
    }
    const auto attacker_piece = pos.piece_on(mv.from);
    const auto attacker = attacker_piece == piece::none ? piece_type::pawn : piece_type_of(attacker_piece);
    int base = capture_base + mvv_lva(victim) * 16 - mvv_lva(attacker);

    int ch = capture_history_score(pos.side_to_move(), attacker, mv.to, victim);
    base += ch / capture_history_scale;

    if(is_promotion(mv)) {
      base += mvv_lva(mv.promotion) * 8;
    }
    return base;
  }
  if(is_promotion(mv)) {
    return promotion_base + mvv_lva(mv.promotion);
  }
  const auto idx = clamp_ply(ply);
  if(killers_[idx][0] == mv) {
    return killer_1_score;
  }
  if(killers_[idx][1] == mv) {
    return killer_2_score;
  }
  if(prev_move != no_move) {
    const auto cm = counter_[prev_move.from][prev_move.to];
    if(cm != no_move && cm == mv) {
      return 40000;
    }
    size_t cont_idx = (static_cast<size_t>(prev_move.from) * 64 + prev_move.to) * 4096 + static_cast<size_t>(mv.from) * 64 + mv.to;
    if(cont_idx < cont_history_.size()) {
      int bonus = cont_history_[cont_idx];
      if(bonus != 0) {
        return 20000 + bonus;
      }
    }
  }
  return history_[color_index(pos.side_to_move())][mv.from][mv.to];
}

auto move_ordering::update_killers(move mv, int ply) noexcept -> void
{
  const auto idx = clamp_ply(ply);
  if(killers_[idx][0] == mv) {
    return;
  }
  killers_[idx][1] = killers_[idx][0];
  killers_[idx][0] = mv;
}

auto move_ordering::update_history(color side, move mv, int depth) noexcept -> void
{
  const auto bonus = depth * depth;
  auto& entry = history_[color_index(side)][mv.from][mv.to];
  entry += bonus - entry * std::abs(bonus) / 16384;
}

auto move_ordering::is_killer(move mv, int ply) const noexcept -> bool
{
  const auto idx = clamp_ply(ply);
  return killers_[idx][0] == mv || killers_[idx][1] == mv;
}

auto move_ordering::history_score(color side, int from, int to) const noexcept -> int
{
  return history_[color_index(side)][from][to];
}

auto move_ordering::update_counter(move prev, move cur) noexcept -> void
{
  if(prev == no_move || cur == no_move) return;
  counter_[prev.from][prev.to] = cur;
}

auto move_ordering::update_continuation(move prev, move cur, int depth) noexcept -> void
{
  if(prev == no_move || cur == no_move) return;
  if(!is_quiet(cur)) return;
  size_t idx = (static_cast<size_t>(prev.from) * 64 + prev.to) * 4096 + static_cast<size_t>(cur.from) * 64 + cur.to;
  if(idx >= cont_history_.size()) return;
  int bonus = depth * depth;
  auto &entry = cont_history_[idx];
  entry = static_cast<int16_t>(entry + bonus - entry * std::abs(bonus) / 16384);
  if(entry > 10000) entry = 10000;
  if(entry < -10000) entry = -10000;
}

auto move_ordering::capture_history_score(color side, piece_type moving, int to,
                                          piece_type captured) const noexcept -> int
{
  return capture_history_[color_index(side)][piece_type_index(moving)][to][piece_type_index(captured)];
}

auto move_ordering::update_capture_history(color side, piece_type moving, int to,
                                           piece_type captured, int bonus) noexcept -> void
{
  int b = bonus;
  if(b > capture_history_max) b = capture_history_max;
  if(b < -capture_history_max) b = -capture_history_max;
  auto& entry = capture_history_[color_index(side)][piece_type_index(moving)][to][piece_type_index(captured)];
  entry += b - entry * std::abs(b) / capture_history_max;
  if(entry > capture_history_max) entry = capture_history_max;
  if(entry < -capture_history_max) entry = -capture_history_max;
}

auto move_ordering::update_capture_history(color side, move mv, const position& pos, int bonus) noexcept -> void
{
  if(!is_capture(mv)) return;
  auto moving_piece = pos.piece_on(mv.from);
  if(moving_piece == piece::none) return;
  piece_type moving = piece_type_of(moving_piece);
  piece_type captured = piece_type::pawn;
  if(mv.flag == move_flag::en_passant) {
    captured = piece_type::pawn;
  } else {
    auto cap = pos.piece_on(mv.to);
    if(cap == piece::none) return;
    captured = piece_type_of(cap);
  }
  update_capture_history(side, moving, mv.to, captured, bonus);
}

auto order_moves(const position& pos, const move_list& moves, int ply,
                 const move_ordering& order, move tt_move, move prev_move) noexcept -> move_list
{
  struct scored_move {
    move mv;
    int score;
  };
  std::array<scored_move, 256> scored{};
  const auto n = moves.size();
  auto best_capture = 0;
  auto have_capture = false;
  for(auto i = std::size_t{0}; i < n; ++i) {
    scored[i] = { moves[i], order.score(pos, moves[i], ply, tt_move, prev_move) };
    if(is_capture(moves[i]) && (!have_capture || scored[i].score > best_capture)) {
      best_capture = scored[i].score;
      have_capture = true;
    }
  }
  for(auto i = std::size_t{0}; i + 1 < n; ++i) {
    auto best = i;
    for(auto j = i + 1; j < n; ++j) {
      if(scored[j].score > scored[best].score) {
        best = j;
      }
    }
    if(best != i) {
      std::swap(scored[i], scored[best]);
    }
  }

  if(have_capture) {
    constexpr int refine_window = 32;
    size_t refined = 0;
    for(auto i = std::size_t{0}; i < n; ++i) {
      if(is_capture(scored[i].mv) && best_capture - scored[i].score <= refine_window) {
        ++refined;
      }
    }
    if(refined >= 2) {
      for(auto i = std::size_t{0}; i < n; ++i) {
        if(!is_capture(scored[i].mv) || best_capture - scored[i].score > refine_window) continue;
        int see = static_exchange_eval(pos, scored[i].mv);
        ++order.see_ordered_captures;
        if(see >= 0) ++order.see_winning_captures;
        else ++order.see_losing_captures;
        int bonus = 0;
        if(see >= 300) bonus = 2000;
        else if(see >= 0) bonus = 500;
        else if(see >= -100) bonus = -200;
        else bonus = -1500;
        scored[i].score += bonus;
      }
      for(auto i = std::size_t{0}; i + 1 < n; ++i) {
        auto best = i;
        for(auto j = i + 1; j < n; ++j) {
          if(scored[j].score > scored[best].score) {
            best = j;
          }
        }
        if(best != i) {
          std::swap(scored[i], scored[best]);
        }
      }
    }
  }

  auto out = move_list{};
  for(auto i = std::size_t{0}; i < n; ++i) {
    out.push(scored[i].mv);
  }
  return out;
}

}