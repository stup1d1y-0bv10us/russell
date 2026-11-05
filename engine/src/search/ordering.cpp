#include "search/ordering.hpp"

#include "movegen/attacks.hpp"
#include "movegen/movegen.hpp"
#include "movegen/pin_state.hpp"
#include "search/diagnostics.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>

namespace russell::search {

namespace {

constexpr auto capture_base = 100000;
constexpr auto promotion_base = 80000;
constexpr auto killer_1_score = 70000;
constexpr auto killer_2_score = 60000;
constexpr auto tt_move_score = 150000;

constexpr int see_bonus_winning = 500;
constexpr int see_bonus_good = 2000;

constexpr int see_demote_losing = -120000;
constexpr int see_demote_bad = -121500;

constexpr int capture_history_max = 16384;
constexpr int capture_history_scale = 1024;

constexpr int countermove_max = 16384;
constexpr int countermove_gravity = 16384;
constexpr int countermove_base = 30000;

constexpr int countermove_min_score = 1;

constexpr int cont_history_max = 10000;
constexpr int cont_history_gravity = 16384;
constexpr int cont_base = 20000;
constexpr size_t cont_table_size = 6 * 64 * 6 * 64;

constexpr auto countermove_index(int prev_from, int prev_to, int cur_from,
                                 int cur_to) noexcept -> size_t
{
  return (static_cast<size_t>(prev_from) * 64 + static_cast<size_t>(prev_to)) * 4096
         + static_cast<size_t>(cur_from) * 64 + static_cast<size_t>(cur_to);
}

constexpr auto continuation_index(piece_type prev_pt, int prev_to, piece_type cur_pt,
                                  int cur_to) noexcept -> size_t
{
  return ((static_cast<size_t>(prev_pt) * 64 + static_cast<size_t>(prev_to)) * 6
          + static_cast<size_t>(cur_pt)) * 64 + static_cast<size_t>(cur_to);
}

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

auto see_attackers_to(const position& pos, int target) noexcept -> move_list
{
  auto moves = move_list{};
  const auto us = pos.side_to_move();
  const auto them = opposite(us);
  const auto to = target;
  const auto occupied_by_enemy = (pos.occupancy(them) & bit(to)) != 0;
  const auto is_ep_target = pos.en_passant_square() == to;
  const auto last_rank = us == color::white ? 7 : 0;
  const auto is_promo_target = rank_of(to) == last_rank;
  auto push_see = [&](int from, move_flag flag) {
    moves.push(move{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to),
                    piece_type::queen, flag});
  };
  auto pawns = movegen::pawn_attacks(them, to) & pos.pieces(us, piece_type::pawn);
  while(pawns != 0) {
    const auto from = static_cast<int>(std::countr_zero(pawns));
    pawns &= pawns - 1;
    if(is_ep_target) {
      push_see(from, move_flag::en_passant);
    } else if(is_promo_target) {
      const auto promo_flag =
          occupied_by_enemy ? move_flag::promotion_capture : move_flag::promotion;
      push_see(from, promo_flag);
      moves.push(move{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to),
                      piece_type::rook, promo_flag});
      moves.push(move{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to),
                      piece_type::bishop, promo_flag});
      moves.push(move{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to),
                      piece_type::knight, promo_flag});
    } else {
      push_see(from, occupied_by_enemy ? move_flag::capture : move_flag::quiet);
    }
  }
  const auto quiet_or_capture =
      occupied_by_enemy ? move_flag::capture : move_flag::quiet;
  auto knights = movegen::knight_attacks(to) & pos.pieces(us, piece_type::knight);
  while(knights != 0) {
    const auto from = static_cast<int>(std::countr_zero(knights));
    knights &= knights - 1;
    push_see(from, quiet_or_capture);
  }
  auto kings = movegen::king_attacks(to) & pos.pieces(us, piece_type::king);
  while(kings != 0) {
    const auto from = static_cast<int>(std::countr_zero(kings));
    kings &= kings - 1;
    push_see(from, quiet_or_capture);
  }

  const auto occupancy = pos.occupancy();
  const auto diag = movegen::bishop_attacks(to, occupancy);
  const auto orth = movegen::rook_attacks(to, occupancy);
  auto bishops = diag & pos.pieces(us, piece_type::bishop);
  while(bishops != 0) {
    const auto from = static_cast<int>(std::countr_zero(bishops));
    bishops &= bishops - 1;
    push_see(from, quiet_or_capture);
  }
  auto rooks = orth & pos.pieces(us, piece_type::rook);
  while(rooks != 0) {
    const auto from = static_cast<int>(std::countr_zero(rooks));
    rooks &= rooks - 1;
    push_see(from, quiet_or_capture);
  }
  auto queen_attackers = (diag | orth) & pos.pieces(us, piece_type::queen);
  while(queen_attackers != 0) {
    const auto from = static_cast<int>(std::countr_zero(queen_attackers));
    queen_attackers &= queen_attackers - 1;
    push_see(from, quiet_or_capture);
  }
  return moves;
}

auto least_valuable_capture_to(position& pos, int target) noexcept -> move
{

  auto best = no_move;
  auto best_value = 30000;
  const auto us = pos.side_to_move();
  const auto them = opposite(us);
  const auto king_bb = pos.pieces(us, piece_type::king);
  const auto king_sq = king_bb == 0 ? -1 : static_cast<int>(std::countr_zero(king_bb));

  auto pins = movegen::pin_state{};
  auto pins_computed = false;
  const validation_scope validation_here{};
  for(const auto mv : see_attackers_to(pos, target)) {
    if(!is_capture(mv) || static_cast<int>(mv.to) != target) {
      continue;
    }
    const auto attacker = pos.piece_on(mv.from);
    if(attacker == piece::none) {
      continue;
    }

    auto ok = false;
    if(king_bb != 0 && mv.flag != move_flag::en_passant
       && piece_type_of(attacker) != piece_type::king) {
      if(!pins_computed) {
        pins = movegen::compute_pin_state(pos, us, king_sq);
        pins_computed = true;
      }
      ok = movegen::pin_state_legal_ordinary(pins, static_cast<int>(mv.from),
                                             static_cast<int>(mv.to));
      if(active_diagnostics != nullptr) {
        if(ok) {
          ++active_diagnostics->validation_accepted;
          ++active_diagnostics->fast_accepted;
        } else {
          ++active_diagnostics->validation_rejected;
          ++active_diagnostics->fast_rejected;
        }
      }
    } else {
      if(active_diagnostics != nullptr) ++active_diagnostics->fallback_validations;
      const auto st = pos.make_move(mv);
      ok = true;
      if(king_bb != 0) {
        const auto post_king_sq = static_cast<int>(std::countr_zero(pos.pieces(us, piece_type::king)));
        ok = !movegen::is_square_attacked(pos, post_king_sq, them);
      }
      pos.unmake_move(mv, st);
      if(active_diagnostics != nullptr) {
        if(ok) ++active_diagnostics->validation_accepted;
        else ++active_diagnostics->validation_rejected;
      }
    }
    if(!ok) {
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

auto see_rec(position& pos, int target, std::uint64_t depth) noexcept -> int
{
  if(active_diagnostics != nullptr) {
    ++active_diagnostics->see_recursive_calls;
    if(active_diagnostics->see_max_recursion < depth) active_diagnostics->see_max_recursion = depth;
  }
  const auto mv = least_valuable_capture_to(pos, target);
  if(mv == no_move) {
    return 0;
  }
  const auto gain = captured_value(pos, mv) + promotion_delta(mv);
  const auto st = pos.make_move(mv);
  const auto reply = see_rec(pos, target, depth + 1);
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
  if(active_diagnostics != nullptr) {
    ++active_diagnostics->see_calls;
    switch(active_diagnostic_phase) {
    case diagnostic_phase::qsearch: ++active_diagnostics->see_qsearch; break;
    case diagnostic_phase::probcut: ++active_diagnostics->see_probcut; break;
    case diagnostic_phase::threat: ++active_diagnostics->see_threat; break;
    default: ++active_diagnostics->see_ordering; break;
    }
    ++active_diagnostics->depth_see[static_cast<std::size_t>(std::clamp(active_diagnostic_depth, 0, 16))];
  }
  if(!is_capture(mv)) {
    if(active_diagnostics != nullptr) ++active_diagnostics->see_quiet_moves;
    return 0;
  }
  if(active_diagnostics != nullptr) {
    if(is_promotion(mv)) ++active_diagnostics->see_promotion_moves;
    else ++active_diagnostics->see_capture_moves;
    ++active_diagnostics->see_position_copies;
  }
  diagnostic_scope see_scope(diagnostic_phase::see);
  auto copy = pos;
  const auto gain = captured_value(copy, mv) + promotion_delta(mv);
  const auto target = static_cast<int>(mv.to);
  const auto st = copy.make_move(mv);
  const auto reply = see_rec(copy, target, 1);
  copy.unmake_move(mv, st);
  return gain - reply;
}

move_ordering::move_ordering()
    : counter_history_(64 * 64 * 64 * 64, 0)
    , cont_history_1ply_(cont_table_size, 0)
    , cont_history_2ply_(cont_table_size, 0)
{
}

auto move_ordering::reset() noexcept -> void
{
  killers_ = {};
  history_ = {};
  std::fill(counter_history_.begin(), counter_history_.end(), int16_t{0});
  std::fill(cont_history_1ply_.begin(), cont_history_1ply_.end(), int16_t{0});
  std::fill(cont_history_2ply_.begin(), cont_history_2ply_.end(), int16_t{0});
  for(auto& c : capture_history_) for(auto& m : c) for(auto& t : m) for(auto& v : t) v = 0;
  see_ordered_captures = 0;
  see_winning_captures = 0;
  see_losing_captures = 0;
}

auto move_ordering::score(const position& pos, move mv, int ply, move tt_move,
                           move prev_move, piece prev_moved, move prev2_move,
                           piece prev2_moved) const noexcept -> int
{
  if(active_diagnostics != nullptr) {
    ++active_diagnostics->move_score_calls;
    ++active_diagnostics->moves_scored;
  }
  if(tt_move != no_move && mv == tt_move) {
    return tt_move_score;
  }
  if(is_capture(mv)) {

    const auto us = pos.side_to_move();
    const auto them = opposite(us);
    auto victim = piece_type::pawn;
    if(mv.flag != move_flag::en_passant) {
      const auto to_bb = bit(static_cast<int>(mv.to));
      if((pos.pieces(them, piece_type::pawn) & to_bb) != 0) victim = piece_type::pawn;
      else if((pos.pieces(them, piece_type::knight) & to_bb) != 0) victim = piece_type::knight;
      else if((pos.pieces(them, piece_type::bishop) & to_bb) != 0) victim = piece_type::bishop;
      else if((pos.pieces(them, piece_type::rook) & to_bb) != 0) victim = piece_type::rook;
      else if((pos.pieces(them, piece_type::queen) & to_bb) != 0) victim = piece_type::queen;
      else if((pos.pieces(them, piece_type::king) & to_bb) != 0) victim = piece_type::king;
    }
    const auto from_bb = bit(static_cast<int>(mv.from));
    auto attacker = piece_type::pawn;
    if((pos.pieces(us, piece_type::pawn) & from_bb) != 0) attacker = piece_type::pawn;
    else if((pos.pieces(us, piece_type::knight) & from_bb) != 0) attacker = piece_type::knight;
    else if((pos.pieces(us, piece_type::bishop) & from_bb) != 0) attacker = piece_type::bishop;
    else if((pos.pieces(us, piece_type::rook) & from_bb) != 0) attacker = piece_type::rook;
    else if((pos.pieces(us, piece_type::queen) & from_bb) != 0) attacker = piece_type::queen;
    else if((pos.pieces(us, piece_type::king) & from_bb) != 0) attacker = piece_type::king;
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

    const auto ch = countermove_history_score(prev_move, mv);
    if(ch > 0) {
      return countermove_base + ch;
    }

    if(prev_moved != piece::none) {
      const auto cur_piece = pos.piece_on(mv.from);
      if(cur_piece != piece::none) {
        const int c1 = continuation_score_1ply(prev_moved, prev_move.to, cur_piece, mv.to);
        auto c2 = 0;
        if(ply >= 1 && prev2_move != no_move && prev2_moved != piece::none) {
          c2 = continuation_score_2ply(prev2_moved, prev2_move.to, cur_piece, mv.to);
        }
        const int combined = (c1 + c2) / 2;
        if(combined != 0) {
          return cont_base + combined;
        }
      }
    }
  }
  return history_[color_index(pos.side_to_move())][mv.from][mv.to];
}

auto move_ordering::update_killers(move mv, int ply) noexcept -> void
{

  if(mv == no_move || !is_quiet(mv)) {
    return;
  }
  const auto idx = clamp_ply(ply);
  if(killers_[idx][0] == mv) {
    return;
  }
  killers_[idx][1] = killers_[idx][0];
  killers_[idx][0] = mv;
}

auto move_ordering::update_history(color side, int from, int to, int bonus) noexcept -> void
{

  auto& entry = history_[color_index(side)][from][to];
  entry += bonus - entry * std::abs(bonus) / 16384;
  if(entry > 16384) entry = 16384;
  if(entry < -16384) entry = -16384;
}

auto move_ordering::update_history(color side, move mv, int depth) noexcept -> void
{
  update_history(side, mv.from, mv.to, depth * depth);
}

auto move_ordering::is_killer(move mv, int ply) const noexcept -> bool
{
  const auto idx = clamp_ply(ply);
  return killers_[idx][0] == mv || killers_[idx][1] == mv;
}

auto move_ordering::is_countermove(move prev, move cur) const noexcept -> bool
{
  if(prev == no_move || cur == no_move) {
    return false;
  }
  return countermove_history_score(prev, cur) >= countermove_min_score;
}

auto move_ordering::history_score(color side, int from, int to) const noexcept -> int
{
  return history_[color_index(side)][from][to];
}

auto move_ordering::countermove_history_score(move prev, move cur) const noexcept -> int
{
  if(prev == no_move || cur == no_move) {
    return 0;
  }
  const size_t idx = countermove_index(prev.from, prev.to, cur.from, cur.to);
  if(idx >= counter_history_.size()) {
    return 0;
  }
  return counter_history_[idx];
}

auto move_ordering::update_countermove_history(move prev, move cur, int depth) noexcept -> void
{
  if(prev == no_move || cur == no_move) return;
  if(!is_quiet(cur)) return;
  update_countermove_history(prev.from, prev.to, cur.from, cur.to, depth * depth);
}

auto move_ordering::update_countermove_history(int prev_from, int prev_to, int cur_from, int cur_to,
                                               int bonus) noexcept -> void
{
  const size_t idx = countermove_index(prev_from, prev_to, cur_from, cur_to);
  if(idx >= counter_history_.size()) return;

  auto& entry = counter_history_[idx];
  const int updated = entry + bonus - entry * std::abs(bonus) / countermove_gravity;
  entry = static_cast<int16_t>(updated > countermove_max ? countermove_max
                               : (updated < -countermove_max ? -countermove_max : updated));
}

auto move_ordering::continuation_score_1ply(piece prev_moved, int prev_to, piece cur_piece,
                                              int cur_to) const noexcept -> int
{
  if(prev_moved == piece::none || cur_piece == piece::none) {
    return 0;
  }
  const size_t idx = continuation_index(piece_type_of(prev_moved), prev_to,
                                        piece_type_of(cur_piece), cur_to);
  if(idx >= cont_history_1ply_.size()) {
    return 0;
  }
  return cont_history_1ply_[idx];
}

auto move_ordering::continuation_score_2ply(piece prev2_moved, int prev2_to, piece cur_piece,
                                            int cur_to) const noexcept -> int
{
  if(prev2_moved == piece::none || cur_piece == piece::none) {
    return 0;
  }
  const size_t idx = continuation_index(piece_type_of(prev2_moved), prev2_to,
                                        piece_type_of(cur_piece), cur_to);
  if(idx >= cont_history_2ply_.size()) {
    return 0;
  }
  return cont_history_2ply_[idx];
}

auto move_ordering::update_continuation_1ply(piece prev_moved, int prev_to, piece cur_piece,
                                             int cur_to, int bonus) noexcept -> void
{
  if(prev_moved == piece::none || cur_piece == piece::none) return;
  const size_t idx = continuation_index(piece_type_of(prev_moved), prev_to,
                                        piece_type_of(cur_piece), cur_to);
  if(idx >= cont_history_1ply_.size()) return;

  auto& entry = cont_history_1ply_[idx];
  const int updated = entry + bonus - entry * std::abs(bonus) / cont_history_gravity;
  entry = static_cast<int16_t>(updated > cont_history_max ? cont_history_max
                                 : (updated < -cont_history_max ? -cont_history_max : updated));
}

auto move_ordering::update_continuation_2ply(piece prev2_moved, int prev2_to, piece cur_piece,
                                             int cur_to, int bonus) noexcept -> void
{
  if(prev2_moved == piece::none || cur_piece == piece::none) return;
  const size_t idx = continuation_index(piece_type_of(prev2_moved), prev2_to,
                                        piece_type_of(cur_piece), cur_to);
  if(idx >= cont_history_2ply_.size()) return;
  auto& entry = cont_history_2ply_[idx];
  const int updated = entry + bonus - entry * std::abs(bonus) / cont_history_gravity;
  entry = static_cast<int16_t>(updated > cont_history_max ? cont_history_max
                                 : (updated < -cont_history_max ? -cont_history_max : updated));
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
        if(active_diagnostics != nullptr) ++active_diagnostics->see_site_order_moves;
        int see = static_exchange_eval(pos, scored[i].mv);
        ++order.see_ordered_captures;
        if(see >= 0) ++order.see_winning_captures;
        else ++order.see_losing_captures;
        int bonus = 0;
        if(see >= 300) bonus = see_bonus_good;
        else if(see >= 0) bonus = see_bonus_winning;
        else if(see >= -100) bonus = see_demote_losing;
        else bonus = see_demote_bad;
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

constexpr int picker_refine_window = 32;

move_picker::move_picker(const move_ordering& order, int ply, move tt_move, bool tt_searchable,
                         move_list cap_moves, bool have_cap_moves, move_list quiet_moves,
                         bool have_quiet_moves, move prev_move, piece prev_moved, move prev2_move,
                         piece prev2_moved, move excluded_move)
    : order_(order)
    , ply_(ply)
    , tt_move_(tt_move)
    , tt_searchable_(tt_searchable)
    , cap_moves_(cap_moves)
    , have_cap_moves_(have_cap_moves)
    , quiet_moves_(quiet_moves)
    , have_quiet_moves_(have_quiet_moves)
    , prev_move_(prev_move)
    , prev_moved_(prev_moved)
    , prev2_move_(prev2_move)
    , prev2_moved_(prev2_moved)
    , excluded_move_(excluded_move)
{
  if(active_diagnostics != nullptr) ++active_diagnostics->picker_initializations;
  cap_skip_.fill(false);
  cap_taken_.fill(false);
  losing_taken_.fill(false);
  quiet_taken_.fill(false);
}

auto move_picker::pick_best(const std::array<scored, 256>& list,
                            const std::array<bool, 256>& skip,
                            const std::array<bool, 256>& taken, size_t n) noexcept -> size_t
{
  if(active_diagnostics != nullptr) {
    ++active_diagnostics->picker_selections;
    ++active_diagnostics->picker_scans;
    active_diagnostics->picker_elements_scanned += n;
  }
  auto pick = n;
  for(auto k = std::size_t{0}; k < n; ++k) {
    if(skip[k] || taken[k]) {
      continue;
    }
    if(pick == n || list[k].score > list[pick].score) {
      pick = k;
    }
  }
  return pick;
}

auto move_picker::pick_best(const std::array<scored, 256>& list,
                            const std::array<bool, 256>& taken, size_t n) noexcept -> size_t
{
  if(active_diagnostics != nullptr) {
    ++active_diagnostics->picker_selections;
    ++active_diagnostics->picker_scans;
    active_diagnostics->picker_elements_scanned += n;
  }
  auto pick = n;
  for(auto k = std::size_t{0}; k < n; ++k) {
    if(taken[k]) {
      continue;
    }
    if(pick == n || list[k].score > list[pick].score) {
      pick = k;
    }
  }
  return pick;
}

auto move_picker::prepare_captures(position& pos) -> void
{
  if(!have_cap_moves_) {
    if(active_diagnostics != nullptr) ++active_diagnostics->caps_gen_picker_lazy;
    cap_moves_ = movegen::generate_captures_promotions(pos);
    have_cap_moves_ = true;
  }
  cap_n_ = 0;
  for(const auto candidate : cap_moves_) {
    if(tt_searchable_ && candidate == tt_move_) {
      continue;
    }
    caps_[cap_n_].mv = candidate;
    caps_[cap_n_].score = order_.score(pos, candidate, ply_, no_move, prev_move_, prev_moved_,
                                       prev2_move_, prev2_moved_);
    caps_[cap_n_].see = std::numeric_limits<int>::max();
    ++cap_n_;
  }

  auto best_cap = 0;
  auto have_cap = false;
  if(tt_searchable_ && is_capture(tt_move_)) {
    best_cap = order_.score(pos, tt_move_, ply_, tt_move_, prev_move_, prev_moved_, prev2_move_,
                            prev2_moved_);
    have_cap = true;
  }
  for(auto k = std::size_t{0}; k < cap_n_; ++k) {
    if(is_capture(caps_[k].mv) && (!have_cap || caps_[k].score > best_cap)) {
      best_cap = caps_[k].score;
      have_cap = true;
    }
  }
  if(have_cap) {
    auto refined = std::size_t{0};
    for(auto k = std::size_t{0}; k < cap_n_; ++k) {
      if(is_capture(caps_[k].mv) && best_cap - caps_[k].score <= picker_refine_window) {
        ++refined;
      }
    }
    if(refined >= 2) {
      for(auto k = std::size_t{0}; k < cap_n_; ++k) {
        if(!is_capture(caps_[k].mv) || best_cap - caps_[k].score > picker_refine_window) {
          continue;
        }
        if(active_diagnostics != nullptr) ++active_diagnostics->see_site_picker;
        const int see = static_exchange_eval(pos, caps_[k].mv);
        caps_[k].see = see;
        ++order_.see_ordered_captures;
        if(see >= 0) {
          ++order_.see_winning_captures;
        } else {
          ++order_.see_losing_captures;
        }
        auto bonus = 0;
        if(see >= 300) {
          bonus = see_bonus_good;
        } else if(see >= 0) {
          bonus = see_bonus_winning;
        } else if(see >= -100) {
          bonus = see_demote_losing;
        } else {
          bonus = see_demote_bad;
        }
        caps_[k].score += bonus;
      }
    }
  }

  losing_n_ = 0;
  for(auto k = std::size_t{0}; k < cap_n_; ++k) {
    if(caps_[k].see != std::numeric_limits<int>::max() && caps_[k].see < 0) {
      losing_[losing_n_] = caps_[k];
      ++losing_n_;
      cap_skip_[k] = true;
    }
  }
}

auto move_picker::prepare_quiets(position& pos) -> void
{
  if(!have_quiet_moves_) {
    if(active_diagnostics != nullptr) ++active_diagnostics->quiet_gen_picker_lazy;
    quiet_moves_ = movegen::generate_quiet_moves(pos);
    have_quiet_moves_ = true;
  }
  quiet_n_ = 0;
  for(const auto candidate : quiet_moves_) {
    if(tt_searchable_ && candidate == tt_move_) {
      continue;
    }
    if(is_promotion(candidate)) {
      continue;
    }
    quiets_[quiet_n_].mv = candidate;
    quiets_[quiet_n_].score = order_.score(pos, candidate, ply_, no_move, prev_move_, prev_moved_,
                                           prev2_move_, prev2_moved_);
    quiets_[quiet_n_].see = std::numeric_limits<int>::max();
    ++quiet_n_;
  }
}

auto move_picker::next(position& pos, move& mv, int& see, int& idx) -> bool
{
  for(;;) {
    switch(stage_) {
    case picker_stage::tt_move:
      stage_ = picker_stage::captures;
      if(tt_searchable_ && !tt_emitted_) {
        if(active_diagnostics != nullptr) ++active_diagnostics->tt_move_processing;
        tt_emitted_ = true;
        mv = tt_move_;
        see = std::numeric_limits<int>::max();
        idx = emitted_++;
        return true;
      }
      break;
    case picker_stage::captures:
      if(!caps_prepared_) {
        prepare_captures(pos);
        caps_prepared_ = true;
      }
      if(const auto pick = pick_best(caps_, cap_skip_, cap_taken_, cap_n_); pick < cap_n_) {
        if(active_diagnostics != nullptr) ++active_diagnostics->capture_stage_processing;
        cap_taken_[pick] = true;
        if(caps_[pick].mv == excluded_move_) {
          ++emitted_;
          continue;
        }
        mv = caps_[pick].mv;
        see = caps_[pick].see;
        idx = emitted_++;
        return true;
      }
      stage_ = picker_stage::quiets;
      break;
    case picker_stage::quiets:
      if(!quiets_prepared_) {
        prepare_quiets(pos);
        quiets_prepared_ = true;
      }
      if(const auto pick = pick_best(quiets_, quiet_taken_, quiet_n_); pick < quiet_n_) {
        if(active_diagnostics != nullptr) ++active_diagnostics->quiet_stage_processing;
        quiet_taken_[pick] = true;
        if(quiets_[pick].mv == excluded_move_) {
          ++emitted_;
          continue;
        }
        mv = quiets_[pick].mv;
        see = quiets_[pick].see;
        idx = emitted_++;
        return true;
      }
      stage_ = picker_stage::losing;
      break;
    case picker_stage::losing:
      if(const auto pick = pick_best(losing_, losing_taken_, losing_n_); pick < losing_n_) {
        if(active_diagnostics != nullptr) ++active_diagnostics->losing_capture_processing;
        losing_taken_[pick] = true;
        if(losing_[pick].mv == excluded_move_) {
          ++emitted_;
          continue;
        }
        mv = losing_[pick].mv;
        see = losing_[pick].see;
        idx = emitted_++;
        return true;
      }
      stage_ = picker_stage::done;
      break;
    case picker_stage::done:
      return false;
    }
  }
}

}
