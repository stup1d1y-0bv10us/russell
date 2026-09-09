#include "search/search.hpp"

#include "eval/evaluate.hpp"
#include "eval/nnue.hpp"
#include "movegen/movegen.hpp"
#include "search/ordering.hpp"
#include "search/transposition_table.hpp"
#include "tb/tablebase.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <thread>
#include <vector>

namespace russell::search {

namespace {

constexpr auto inf = mate_value;
constexpr auto null_move_r = 2;
constexpr auto lmr_min_non_pawn_material = 6;
constexpr auto futility_margin = 200;
constexpr auto delta_margin = 200;

[[nodiscard]] auto nmp_reduction(int depth, int static_eval, int beta) noexcept -> int
{
  int R = nmp_base_reduction + depth / nmp_depth_divisor;

  if(static_eval - beta > nmp_eval_margin) {
    R += 1;
  }
  if(R > nmp_max_reduction) R = nmp_max_reduction;
  if(R < 1) R = 1;
  if(R > depth - 1) R = depth - 1;
  return R;
}

[[nodiscard]] auto lmp_threshold(int depth, int history) noexcept -> int
{

  int base = lmp_base_count + depth * lmp_depth_factor + (depth * depth) / 4;
  int adj = history / lmp_history_scale;
  if(adj > lmp_history_max_adjust) adj = lmp_history_max_adjust;
  if(adj < -lmp_history_max_adjust) adj = -lmp_history_max_adjust;
  int t = base + adj;
  if(t < 1) t = 1;
  if(t > 64) t = 64;
  return t;
}

[[nodiscard]] auto rfp_margin(int depth, int history) noexcept -> int
{

  int m = rfp_base + rfp_per_depth * depth + rfp_quad * depth * depth;
  int adj = history / rfp_history_scale;
  if(adj > rfp_history_max_adjust) adj = rfp_history_max_adjust;
  if(adj < -rfp_history_max_adjust) adj = -rfp_history_max_adjust;

  m += adj;
  if(m < 0) m = 0;
  return m;
}

[[nodiscard]] auto razor_margin(int depth, int history) noexcept -> int
{

  int m = razor_base + razor_per_depth * depth + razor_quad * depth * depth;
  int adj = history / razor_history_scale;
  if(adj > razor_history_max_adjust) adj = razor_history_max_adjust;
  if(adj < -razor_history_max_adjust) adj = -razor_history_max_adjust;

  m += adj;
  if(m < 0) m = 0;
  return m;
}

const auto lmr_base_table = [] {
  std::array<std::array<int, 64>, 64> t{};
  for(int d = 1; d < 64; ++d) {
    for(int m = 1; m < 64; ++m) {

      double rd = std::log(static_cast<double>(d));
      double rm = std::log(static_cast<double>(m));
      double r = 0.30 + 0.40 * rd * rm;
      int v = static_cast<int>(r);
      if(v < 0) v = 0;
      if(v > lmr_max_reduction) v = lmr_max_reduction;
      t[d][m] = v;
    }
  }
  return t;
}();

auto non_pawn_material(const position& pos) noexcept -> int
{
  const auto count = [&](piece_type pt, int value) {
    auto bb = pos.pieces(color::white, pt) | pos.pieces(color::black, pt);
    return static_cast<int>(std::popcount(bb)) * value;
  };
  return count(piece_type::knight, 3) + count(piece_type::bishop, 3)
         + count(piece_type::rook, 5) + count(piece_type::queen, 9);
}

struct search_context {
  std::uint64_t nodes = 0;
  std::uint64_t lmr_applied = 0;
  std::uint64_t lmr_research = 0;
  std::uint64_t nmp_tried = 0;
  std::uint64_t nmp_cutoff = 0;
  std::uint64_t nmp_verification = 0;
  std::uint64_t lmp_considered = 0;
  std::uint64_t lmp_pruned = 0;
  std::uint64_t rfp_considered = 0;
  std::uint64_t rfp_pruned = 0;
  std::uint64_t razor_considered = 0;
  std::uint64_t razor_pruned = 0;
  std::uint64_t improving_count = 0;
  std::uint64_t capture_history_updates = 0;
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

  std::array<int, 128> static_eval_stack{};
  bool aborted = false;
  bool use_null_move = true;
  bool use_lmr = true;
  bool use_quiescence = true;
  bool use_futility = true;
  bool use_check_extension = true;
  bool in_nmp_verification = false;
  const search_stopper* stopper = nullptr;
  move_ordering order;

  std::chrono::steady_clock::time_point start{};
  int hard_time_ms = 0;
  int depth_limit = 0;
  std::uint64_t nodes_limit = 0;
  bool infinite = false;
  std::atomic<std::uint64_t>* shared_nodes = nullptr;

  transposition_table* tt = nullptr;

  eval::nnue::evaluator* nnue = nullptr;
};

auto side_factor(const position& pos) noexcept -> int
{
  return pos.side_to_move() == color::white ? 1 : -1;
}

auto nnue_begin(const position& pos) noexcept -> eval::nnue::evaluator*
{
  auto* ev = eval::nnue_accumulator();
  if(ev != nullptr) {
    ev->refresh(pos);
  }
  return ev;
}

auto nnue_after_move(eval::nnue::evaluator* ev, const position& after, move mv,
                     const move_state& st) noexcept -> void
{
  if(ev != nullptr) {
    ev->make_move(after, mv, st);
  }
}

auto nnue_before_unmake(eval::nnue::evaluator* ev) noexcept -> void
{
  if(ev != nullptr) {
    ev->unmake_move();
  }
}

auto nnue_after_null(eval::nnue::evaluator* ev, const position& after) noexcept -> void
{
  if(ev != nullptr) {
    ev->make_null_move(after);
  }
}

auto in_check(position& pos) noexcept -> bool
{
  const auto king_bb = pos.pieces(pos.side_to_move(), piece_type::king);
  if(king_bb == 0) {
    return false;
  }
  return movegen::is_square_attacked(pos, static_cast<int>(std::countr_zero(king_bb)),
                                     opposite(pos.side_to_move()));
}

auto hard_time_exceeded(const search_context& ctx) noexcept -> bool
{

  if(ctx.infinite || ctx.depth_limit > 0 || ctx.nodes_limit > 0) {
    return false;
  }
  if(ctx.hard_time_ms <= 0) {
    return false;
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - ctx.start)
                           .count();
  return elapsed >= ctx.hard_time_ms;
}

auto nodes_exceeded(const search_context& ctx) noexcept -> bool
{
  if(ctx.nodes_limit == 0) {
    return false;
  }
  if(ctx.shared_nodes != nullptr) {
    return ctx.shared_nodes->load(std::memory_order_relaxed) >= ctx.nodes_limit;
  }
  return ctx.nodes >= ctx.nodes_limit;
}

inline auto inc_nodes(search_context& ctx) noexcept -> void
{
  ++ctx.nodes;
  if(ctx.shared_nodes != nullptr) {
    ctx.shared_nodes->fetch_add(1, std::memory_order_relaxed);
  }
}

struct scored_move {
  move mv;
  int score;
  int see = std::numeric_limits<int>::max();
};

auto negamax_plain(position& pos, int depth, int ply, std::uint64_t& nodes,
                   eval::nnue::evaluator* nnue = nullptr) -> int
{
  ++nodes;
  const auto moves = movegen::generate_legal(pos);
  if(moves.size() == 0) {
    return in_check(pos) ? -inf + ply : 0;
  }
  if(depth == 0) {
    return side_factor(pos) * eval::evaluate(pos);
  }
  auto best = -inf;
  for(const auto mv : moves) {
    const auto st = pos.make_move(mv);
    nnue_after_move(nnue, pos, mv, st);
    const auto value = -negamax_plain(pos, depth - 1, ply + 1, nodes, nnue);
    nnue_before_unmake(nnue);
    pos.unmake_move(mv, st);
    if(value > best) {
      best = value;
    }
  }
  return best;
}

auto quiescence(position& pos, int ply, int alpha, int beta, search_context& ctx, move prev_move = no_move) -> int
{
  inc_nodes(ctx);
  if((ctx.nodes & 0xfff) == 0
     && (ctx.stopper->stop.load(std::memory_order_relaxed) || hard_time_exceeded(ctx)
         || nodes_exceeded(ctx))) {
    ctx.aborted = true;
    return alpha;
  }

  const auto checked = in_check(pos);

  auto stand_pat = -inf;
  if(!checked) {
    stand_pat = side_factor(pos) * eval::evaluate(pos);
    if(stand_pat >= beta) {
      return beta;
    }
    if(stand_pat > alpha) {
      alpha = stand_pat;
    }
  }
  if(ply >= max_ply - 1) {
    if(checked) {
      const auto ev = movegen::generate_evasions(pos);
      if(ev.size() == 0) return -inf + ply;
    }
    return alpha;
  }

  const auto moves = checked ? movegen::generate_evasions(pos)
                             : movegen::generate_captures_promotions(pos);
  if(moves.size() == 0) {
    return checked ? -inf + ply : alpha;
  }

  std::array<scored_move, 256> scored{};
  size_t n = moves.size();
  int best_cap = 0; bool have_cap = false;
  for(size_t i = 0; i < n; ++i) {
    scored[i].mv = moves[i];
    scored[i].score = ctx.order.score(pos, moves[i], ply, no_move, prev_move);
    if(is_capture(moves[i]) && (!have_cap || scored[i].score > best_cap)) { best_cap = scored[i].score; have_cap = true; }
  }
  if(have_cap) {
    constexpr int refine_window = 32;
    size_t refined = 0;
    for(size_t i = 0; i < n; ++i) if(is_capture(scored[i].mv) && best_cap - scored[i].score <= refine_window) ++refined;
    if(refined >= 2) {
      for(size_t i = 0; i < n; ++i) if(is_capture(scored[i].mv) && best_cap - scored[i].score <= refine_window) {
        int see = static_exchange_eval(pos, scored[i].mv);
        scored[i].see = see;
        ++ctx.order.see_ordered_captures;
        if(see >= 0) ++ctx.order.see_winning_captures; else ++ctx.order.see_losing_captures;
        int bonus = 0;
        if(see >= 300) bonus = 2000;
        else if(see >= 0) bonus = 500;
        else if(see >= -100) bonus = -200;
        else bonus = -1500;
        scored[i].score += bonus;
      }
    }
  }
  for(size_t i = 0; i < n; ++i) {
    size_t best_idx = i;
    for(size_t j = i + 1; j < n; ++j) if(scored[j].score > scored[best_idx].score) best_idx = j;
    if(best_idx != i) std::swap(scored[i], scored[best_idx]);
    const auto mv = scored[i].mv;
    if(!checked && is_capture(mv) && std::abs(alpha) <= mate_score_threshold
       && std::abs(beta) <= mate_score_threshold) {
      int see = scored[i].see;
      if(see == std::numeric_limits<int>::max()) {
        see = static_exchange_eval(pos, mv);
      }
      if(stand_pat + see + delta_margin <= alpha) {
        continue;
      }
    }
    const auto st = pos.make_move(mv);
    nnue_after_move(ctx.nnue, pos, mv, st);
    const auto value = -quiescence(pos, ply + 1, -beta, -alpha, ctx, mv);
    nnue_before_unmake(ctx.nnue);
    pos.unmake_move(mv, st);
    if(ctx.aborted) {
      return alpha;
    }
    if(value >= beta) {
      return beta;
    }
    if(value > alpha) {
      alpha = value;
    }
  }
  return alpha;
}

auto negamax_ab(position& pos, int depth, int ply, int alpha, int beta, search_context& ctx,
                 bool was_null = false, move prev_move = no_move) -> int
{
  inc_nodes(ctx);
  if((ctx.nodes & 0xfff) == 0
     && (ctx.stopper->stop.load(std::memory_order_relaxed) || hard_time_exceeded(ctx)
         || nodes_exceeded(ctx))) {
    ctx.aborted = true;
    return alpha;
  }

  if(ctx.use_check_extension && in_check(pos)) {
    if(ply < max_ply - 1) {
      int new_depth = depth + check_extension_value;
      if(new_depth > max_ply - ply) new_depth = max_ply - ply;
      if(new_depth > depth) {
        ++ctx.extensions_applied;
        ++ctx.check_extensions;
        depth = new_depth;
      }
    }
  }
  const auto key = pos.key();
  auto tt_move = move{};
  auto tt_score = 0;
  auto tt_depth = 0;
  auto probe_bound = tt_bound::none;
  if(ctx.tt->probe(key, tt_move, tt_score, tt_depth, probe_bound) && tt_depth >= depth) {
    tt_score = read_value(tt_score, ply);
    if(probe_bound == tt_bound::exact) {
      return tt_score;
    }
    if(probe_bound == tt_bound::lower && tt_score >= beta) {
      return tt_score;
    }
    if(probe_bound == tt_bound::upper && tt_score <= alpha) {
      return tt_score;
    }
  }
  const auto moves = movegen::generate_legal(pos);
  if(moves.size() == 0) {
    const auto score = in_check(pos) ? -inf + ply : 0;
    ctx.tt->store(key, no_move, store_value(score, ply), depth, tt_bound::exact);
    return score;
  }
  if(depth == 0) {
    if(ctx.use_quiescence) {
      return quiescence(pos, ply, alpha, beta, ctx, prev_move);
    }
    return side_factor(pos) * eval::evaluate(pos);
  }

  if(!ctx.in_nmp_verification
     && depth >= iir_min_depth
     && tt_move == no_move
     && !in_check(pos)
     && std::abs(alpha) <= mate_score_threshold
     && std::abs(beta) <= mate_score_threshold
     && (beta - alpha) == 1
     && ply > 0) {
    int new_depth = depth - iir_depth_reduction;
    if(new_depth < 1) new_depth = 1;
    if(new_depth < depth) {
      ++ctx.iir_applied;
      depth = new_depth;
    }
  }

  if(depth < 0) depth = 0;
  else if(depth > 0 && depth < 1) depth = 1;

  if(ctx.use_null_move && !was_null && !ctx.in_nmp_verification
     && depth >= nmp_min_depth
     && (beta - alpha) == 1
     && std::abs(beta) <= mate_score_threshold
     && std::abs(alpha) <= mate_score_threshold
     && non_pawn_material(pos) >= nmp_min_non_pawn
     && !in_check(pos)
     && ply > 0) {
    int static_eval = side_factor(pos) * eval::evaluate(pos);

    bool skip_for_eval = (depth < 5 && static_eval + 100 < beta);
    if(!skip_for_eval) {
      int R = nmp_reduction(depth, static_eval, beta);
      int new_depth = depth - 1 - R;
      if(new_depth < 0) new_depth = 0;
      ++ctx.nmp_tried;
      const auto null_st = pos.make_null_move();
      nnue_after_null(ctx.nnue, pos);
      int null_value = -negamax_ab(pos, new_depth, ply + 1, -beta, -beta + 1, ctx, true, no_move);
      nnue_before_unmake(ctx.nnue);
      pos.unmake_null_move(null_st);
      if(ctx.aborted) {
        return alpha;
      }
      if(null_value >= beta) {
        ++ctx.nmp_cutoff;
        if(depth >= nmp_verification_depth) {

          ++ctx.nmp_verification;
          bool saved_verif = ctx.in_nmp_verification;
          bool saved_use_null = ctx.use_null_move;
          ctx.in_nmp_verification = true;
          ctx.use_null_move = false;
          int verify_value = negamax_ab(pos, depth - 1, ply, beta - 1, beta, ctx, false, prev_move);
          ctx.use_null_move = saved_use_null;
          ctx.in_nmp_verification = saved_verif;
          if(ctx.aborted) {
            return alpha;
          }
          if(verify_value >= beta) {
            return beta;
          }

        } else {
          return beta;
        }
      }
    }
  }
  const auto us = pos.side_to_move();
  const auto checked = in_check(pos);
  const auto alpha_orig = alpha;
  const auto static_eval = side_factor(pos) * eval::evaluate(pos);

  bool improving = false;
  if(ply >= 2 && ply < static_cast<int>(ctx.static_eval_stack.size())) {
    int prev_eval = ctx.static_eval_stack[ply - 2];
    if(prev_eval != std::numeric_limits<int>::max()
       && std::abs(prev_eval) < mate_score_threshold
       && std::abs(static_eval) < mate_score_threshold) {
      improving = static_eval > prev_eval;
      if(improving) ++ctx.improving_count;
    }
  }
  if(ply >= 0 && ply < static_cast<int>(ctx.static_eval_stack.size())) {
    ctx.static_eval_stack[ply] = static_eval;
  }

  if(depth >= 1 && depth <= rfp_max_depth && !checked
     && (beta - alpha) == 1
     && std::abs(beta) <= mate_score_threshold
     && std::abs(alpha) <= mate_score_threshold
     && ply > 0) {
    ++ctx.rfp_considered;

    int hist = 0;
    if(prev_move != no_move) {
      hist = ctx.order.history_score(us, prev_move.from, prev_move.to);
    }
    int margin = rfp_margin(depth, hist);
    if(improving) margin += improving_rfp_bonus;
    if(static_eval - margin >= beta) {
      ++ctx.rfp_pruned;
      return static_eval;
    }
  }

  if(depth >= 1 && depth <= razor_max_depth && !checked
     && (beta - alpha) == 1
     && std::abs(beta) <= mate_score_threshold
     && std::abs(alpha) <= mate_score_threshold
     && ply > 0) {
    ++ctx.razor_considered;
    int hist_r = 0;
    if(prev_move != no_move) {
      hist_r = ctx.order.history_score(us, prev_move.from, prev_move.to);
    }
    int r_margin = razor_margin(depth, hist_r);
    if(improving) r_margin += improving_razor_bonus;
    if(static_eval + r_margin <= alpha) {
      int qscore = quiescence(pos, ply, alpha, beta, ctx, prev_move);
      if(ctx.aborted) {
        return alpha;
      }
      if(qscore <= alpha) {
        ++ctx.razor_pruned;
        return qscore;
      }
    }
  }
  const bool lmr_eligible_material = non_pawn_material(pos) >= lmr_min_non_pawn_material;
  std::array<scored_move, 256> scored{};
  size_t n = moves.size();
  int best_cap = 0; bool have_cap = false;
  for(size_t i = 0; i < n; ++i) {
    scored[i].mv = moves[i];
    scored[i].score = ctx.order.score(pos, moves[i], ply, tt_move, prev_move);
    if(is_capture(moves[i]) && (!have_cap || scored[i].score > best_cap)) { best_cap = scored[i].score; have_cap = true; }
  }
  if(have_cap) {
    constexpr int refine_window = 32;
    size_t refined = 0;
    for(size_t i = 0; i < n; ++i) if(is_capture(scored[i].mv) && best_cap - scored[i].score <= refine_window) ++refined;
    if(refined >= 2) {
      for(size_t i = 0; i < n; ++i) if(is_capture(scored[i].mv) && best_cap - scored[i].score <= refine_window) {
        int see = static_exchange_eval(pos, scored[i].mv);
        scored[i].see = see;
        ++ctx.order.see_ordered_captures;
        if(see >= 0) ++ctx.order.see_winning_captures; else ++ctx.order.see_losing_captures;
        int bonus = 0;
        if(see >= 300) bonus = 2000;
        else if(see >= 0) bonus = 500;
        else if(see >= -100) bonus = -200;
        else bonus = -1500;
        scored[i].score += bonus;
      }
    }
  }

  std::array<move, 256> searched_captures{};
  size_t searched_capture_count = 0;
  auto best = -inf;
  for(size_t i = 0; i < n; ++i) {
    size_t best_idx = i;
    for(size_t j = i + 1; j < n; ++j) if(scored[j].score > scored[best_idx].score) best_idx = j;
    if(best_idx != i) std::swap(scored[i], scored[best_idx]);
    const auto mv = scored[i].mv;
    if(ctx.use_futility && ctx.use_quiescence && depth == 1 && !checked && (beta - alpha) == 1
       && is_quiet(mv) && static_eval + futility_margin <= alpha) {
      if(std::abs(alpha) > mate_score_threshold || std::abs(beta) > mate_score_threshold) {
      } else {
        auto tmp = pos;
        const auto st_tmp = tmp.make_move(mv);
        const bool gives_check = in_check(tmp);
        tmp.unmake_move(mv, st_tmp);
        if(!gives_check) {
          continue;
        }
      }
    }
    if(depth <= 3 && !checked && (beta - alpha) == 1 && is_capture(mv) && !is_promotion(mv)) {
      if(std::abs(alpha) <= mate_score_threshold && std::abs(beta) <= mate_score_threshold) {
        auto tmp = pos;
        const auto st_tmp = tmp.make_move(mv);
        const bool gives_check = in_check(tmp);
        tmp.unmake_move(mv, st_tmp);
        if(!gives_check) {
          int see = scored[i].see;
          if(see == std::numeric_limits<int>::max()) see = static_exchange_eval(pos, mv);
          if(see < 0 && static_eval + see + 100 <= alpha) {
            continue;
          }
        }
      }
    }
    if(depth >= 2 && depth <= 3 && !checked && is_quiet(mv) && mv != tt_move && !is_promotion(mv) && !ctx.order.is_killer(mv, ply)
       && std::abs(alpha) <= mate_score_threshold && std::abs(beta) <= mate_score_threshold && (beta - alpha) == 1) {
      auto tmp = pos;
      const auto st_tmp = tmp.make_move(mv);
      const bool gives_check = in_check(tmp);
      tmp.unmake_move(mv, st_tmp);
      if(!gives_check) {
        const int h = ctx.order.history_score(us, mv.from, mv.to);
        if(h < -1024) {
          continue;
        }
      }
    }

    if(depth <= lmp_max_depth && !checked && (beta - alpha) == 1
       && is_quiet(mv) && !is_promotion(mv)
       && mv != tt_move && !ctx.order.is_killer(mv, ply)
       && std::abs(alpha) <= mate_score_threshold
       && std::abs(beta) <= mate_score_threshold) {
      ++ctx.lmp_considered;
      int hist = ctx.order.history_score(us, mv.from, mv.to);
      int thresh = lmp_threshold(depth, hist);
      if(improving) thresh += improving_lmp_bonus;
      if(static_cast<int>(i) + 1 > thresh) {
        auto tmp = pos;
        auto st_tmp = tmp.make_move(mv);
        bool gives_check = in_check(tmp);
        if(!gives_check) {
          ++ctx.lmp_pruned;
          continue;
        }
      }
    }

    bool is_cap_for_lmr = is_capture(mv);
    int cap_hist_for_lmr = 0;
    if(is_cap_for_lmr) {
      auto moving_piece = pos.piece_on(mv.from);
      piece_type mov_pt = (moving_piece == piece::none ? piece_type::pawn : piece_type_of(moving_piece));
      piece_type cap_pt = piece_type::pawn;
      if(mv.flag == move_flag::en_passant) cap_pt = piece_type::pawn;
      else {
        auto cap = pos.piece_on(mv.to);
        if(cap != piece::none) cap_pt = piece_type_of(cap);
      }
      cap_hist_for_lmr = ctx.order.capture_history_score(us, mov_pt, mv.to, cap_pt);

      searched_captures[searched_capture_count++] = mv;
    }
    const auto st = pos.make_move(mv);
    nnue_after_move(ctx.nnue, pos, mv, st);
    auto new_depth = depth - 1;
    int reduction = 0;

    if(ctx.use_lmr && depth >= lmr_min_depth
       && static_cast<int>(i) + 1 >= lmr_min_move_count
       && mv != tt_move
       && !ctx.order.is_killer(mv, ply)
       && !checked
       && !is_promotion(mv)) {

      bool gives_check = in_check(pos);
      if(!gives_check) {
        bool is_cap = is_capture(mv);
        int hist = 0;
        if(is_cap) hist = cap_hist_for_lmr;
        else hist = ctx.order.history_score(us, mv.from, mv.to);
        int mc = std::min<int>(63, static_cast<int>(i) + 1);
        reduction = lmr_reduction(depth, mc, is_cap, hist);

        if(reduction > depth - 1) reduction = depth - 1;
        if(reduction < 0) reduction = 0;
        if(reduction > lmr_max_reduction) reduction = lmr_max_reduction;
        if(lmr_eligible_material && reduction > 0 && depth - 1 - reduction >= 1) {
          new_depth = depth - 1 - reduction;
          ++ctx.lmr_applied;
        } else {
          reduction = 0;
        }
      }
    }
    auto value = 0;
    if(i == 0) {
      value = -negamax_ab(pos, new_depth, ply + 1, -beta, -alpha, ctx, false, mv);
    } else {
      value = -negamax_ab(pos, new_depth, ply + 1, -alpha - 1, -alpha, ctx, false, mv);
      if(value > alpha && value < beta && !ctx.aborted) {
        value = -negamax_ab(pos, new_depth, ply + 1, -beta, -alpha, ctx, false, mv);
      }
    }
    if(new_depth < depth - 1 && value > alpha && !ctx.aborted) {
      ++ctx.lmr_research;
      value = -negamax_ab(pos, depth - 1, ply + 1, -beta, -alpha, ctx, false, mv);
    }
    nnue_before_unmake(ctx.nnue);
    pos.unmake_move(mv, st);
    if(ctx.aborted) {
      return best;
    }
    if(value > best) {
      best = value;
      tt_move = mv;
    }
    if(best > alpha) {
      alpha = best;
    }
    if(alpha >= beta) {
      if(is_quiet(mv)) {
        ctx.order.update_killers(mv, ply);
        ctx.order.update_history(us, mv, depth);
        ctx.order.update_continuation(prev_move, mv, depth);
      }
      ctx.order.update_counter(prev_move, mv);
      break;
    }
  }

  if(!ctx.aborted && best != -inf && searched_capture_count > 0) {
    bool best_is_cap = (tt_move != no_move && is_capture(tt_move));
    if(best_is_cap) {
      ctx.order.update_capture_history(us, tt_move, pos, capture_history_bonus);
      ++ctx.capture_history_updates;
      for(size_t k = 0; k < searched_capture_count; ++k) {
        if(searched_captures[k] == tt_move) continue;
        ctx.order.update_capture_history(us, searched_captures[k], pos, -capture_history_malus);
        ++ctx.capture_history_updates;
      }
    } else {

      int small_malus = capture_history_malus / 4;
      for(size_t k = 0; k < searched_capture_count; ++k) {
        ctx.order.update_capture_history(us, searched_captures[k], pos, -small_malus);
        ++ctx.capture_history_updates;
      }
    }
  }
  auto bound = tt_bound::upper;
  if(best >= beta) {
    bound = tt_bound::lower;
  } else if(best > alpha_orig) {
    bound = tt_bound::exact;
  }
  ctx.tt->store(key, tt_move, store_value(best, ply), depth, bound);
  return best;
}

auto root_search(position& pos, int depth, search_context& ctx, int alpha, int beta) -> search_result
{
  auto result = search_result{};
  if(depth < 1) {
    return result;
  }
  result.depth = depth;
  const auto before = ctx.nodes;
  inc_nodes(ctx);
  const auto moves = movegen::generate_legal(pos);
  if(moves.size() == 0) {
    result.score = in_check(pos) ? -inf : 0;
    result.nodes = ctx.nodes - before;
    return result;
  }
  auto best = -inf;
  for(const auto& mv : moves) {
    const auto st = pos.make_move(mv);
    nnue_after_move(ctx.nnue, pos, mv, st);
    auto value = 0;
    if(best == -inf) {
      value = -negamax_ab(pos, depth - 1, 1, -beta, -alpha, ctx, false, mv);
    } else {
      value = -negamax_ab(pos, depth - 1, 1, -alpha - 1, -alpha, ctx, false, mv);
      if(value > alpha && value < beta && !ctx.aborted) {
        value = -negamax_ab(pos, depth - 1, 1, -beta, -alpha, ctx, false, mv);
      }
    }
    nnue_before_unmake(ctx.nnue);
    pos.unmake_move(mv, st);
    if(ctx.aborted) {
      break;
    }
    if(value > best) {
      best = value;
      result.best_move = mv;
      result.has_move = true;
    }
    if(best > alpha) {
      alpha = best;
    }
    if(alpha >= beta) {
      break;
    }
  }
  result.score = best;
  result.nodes = ctx.nodes - before;
  return result;
}

auto aspiration_search(position& pos, int depth, search_context& ctx, int previous_score,
                       bool have_previous) -> search_result
{
  auto start_nodes = ctx.nodes;
  int window = aspiration_initial;
  int alpha = -inf;
  int beta = inf;
  bool use_narrow = have_previous && depth >= 2
                    && std::abs(previous_score) < mate_score_threshold;
  if(use_narrow) {
    alpha = std::max(-inf + 1, previous_score - window);
    beta = std::min(inf - 1, previous_score + window);
  }
  int attempts = 0;
  int fail_low = 0;
  int fail_high = 0;
  auto result = root_search(pos, depth, ctx, alpha, beta);
  ++attempts;

  while(!ctx.aborted && attempts < aspiration_max_retries) {
    if(result.score <= alpha) {
      if(ctx.aborted) break;
      ++fail_low;
      if(alpha <= -inf + 1) break;

      alpha = std::max(-inf + 1, alpha - window);
      beta = std::min(inf - 1, beta + window / 4);
      window = std::min(aspiration_max, window * aspiration_growth);
      if(window >= aspiration_max) window = aspiration_max;
      if(alpha <= -inf + 1 && beta >= inf - 1) {
        auto retry = root_search(pos, depth, ctx, alpha, beta);
        ++attempts;
        if(!ctx.aborted) result = retry;
        break;
      }
      auto retry = root_search(pos, depth, ctx, alpha, beta);
      ++attempts;
      if(ctx.aborted) break;
      result = retry;
    } else if(result.score >= beta) {
      if(ctx.aborted) break;
      ++fail_high;
      if(beta >= inf - 1) break;
      beta = std::min(inf - 1, beta + window);
      alpha = std::max(-inf + 1, alpha - window / 4);
      window = std::min(aspiration_max, window * aspiration_growth);
      if(window >= aspiration_max) window = aspiration_max;
      if(alpha <= -inf + 1 && beta >= inf - 1) {
        auto retry = root_search(pos, depth, ctx, alpha, beta);
        ++attempts;
        if(!ctx.aborted) result = retry;
        break;
      }
      auto retry = root_search(pos, depth, ctx, alpha, beta);
      ++attempts;
      if(ctx.aborted) break;
      result = retry;
    } else {
      break;
    }
    if(alpha <= -inf + 1 && beta >= inf - 1) break;
  }

  if(!ctx.aborted && attempts < aspiration_max_retries
     && (result.score <= alpha || result.score >= beta)
     && !(alpha <= -inf + 1 && beta >= inf - 1)) {

    alpha = -inf;
    beta = inf;
    auto retry = root_search(pos, depth, ctx, alpha, beta);
    ++attempts;
    if(!ctx.aborted) result = retry;
  }
  ctx.aspiration_attempts += attempts;
  ctx.aspiration_fail_low += fail_low;
  ctx.aspiration_fail_high += fail_high;
  if(attempts > 0) ctx.aspiration_researches += (attempts - 1);
  result.nodes = ctx.nodes - start_nodes;
  return result;
}

}

auto lmr_base_reduction(int depth, int move_count) noexcept -> int
{
  int d = depth;
  int m = move_count;
  if(d < 1) d = 1;
  if(d >= 64) d = 63;
  if(m < 1) m = 1;
  if(m >= 64) m = 63;
  return lmr_base_table[d][m];
}

auto lmr_reduction(int depth, int history) noexcept -> int
{

  int mc = lmr_min_move_count;
  int base = lmr_base_reduction(depth, mc);
  int adj = history / lmr_history_scale;
  if(adj > lmr_history_max_adjust) adj = lmr_history_max_adjust;
  if(adj < -lmr_history_max_adjust) adj = -lmr_history_max_adjust;
  int r = base - adj;
  if(r < 0) r = 0;
  if(r > lmr_max_reduction) r = lmr_max_reduction;

  if(r < 1 && depth >= lmr_min_depth) r = 1;
  return r;
}

auto lmr_reduction(int depth, int move_count, bool is_capture, int history) noexcept -> int
{
  int base = lmr_base_reduction(depth, move_count);

  if(is_capture) {
    base = std::max(0, base - lmr_capture_adjust);

    int adj = history / (lmr_history_scale * 2);
    if(adj > lmr_history_max_adjust) adj = lmr_history_max_adjust;
    if(adj < -lmr_history_max_adjust) adj = -lmr_history_max_adjust;
    int r = base - adj;
    if(r < 0) r = 0;
    if(r > lmr_max_reduction) r = lmr_max_reduction;
    return r;
  }

  int adj = history / lmr_history_scale;
  if(adj > lmr_history_max_adjust) adj = lmr_history_max_adjust;
  if(adj < -lmr_history_max_adjust) adj = -lmr_history_max_adjust;
  int r = base - adj;

  if(r < 0) r = 0;
  if(r > lmr_max_reduction) r = lmr_max_reduction;
  return r;
}

auto minimax(position& pos, int depth) -> search_result
{
  auto result = search_result{};
  if(depth < 1) {
    return result;
  }
  const auto nnue = nnue_begin(pos);
  auto nodes = std::uint64_t{1};
  const auto moves = movegen::generate_legal(pos);
  if(moves.size() == 0) {
    result.score = in_check(pos) ? -inf : 0;
    result.nodes = nodes;
    return result;
  }
  auto best = -inf;
  for(const auto mv : moves) {
    const auto st = pos.make_move(mv);
    nnue_after_move(nnue, pos, mv, st);
    const auto value = -negamax_plain(pos, depth - 1, 1, nodes, nnue);
    nnue_before_unmake(nnue);
    pos.unmake_move(mv, st);
    if(value > best) {
      best = value;
      result.best_move = mv;
      result.has_move = true;
    }
  }
  result.score = best;
  result.nodes = nodes;
  return result;
}

auto alpha_beta(position& pos, int depth, bool use_null_move, bool use_lmr, bool use_quiescence,
                bool use_futility, bool use_check_extension) -> search_result
{
  auto stopper = search_stopper{};
  transposition_table tt;
  tt.new_generation();
  auto ctx = search_context{};
  ctx.static_eval_stack.fill(std::numeric_limits<int>::max());
  ctx.stopper = &stopper;
  ctx.tt = &tt;
  ctx.nnue = nnue_begin(pos);
  ctx.use_null_move = use_null_move;
  ctx.use_lmr = use_lmr;
  ctx.use_quiescence = use_quiescence;
  ctx.use_futility = use_futility;
  ctx.use_check_extension = use_check_extension;
  auto result = root_search(pos, depth, ctx, -inf, inf);
  result.lmr_applied = ctx.lmr_applied;
  result.lmr_research = ctx.lmr_research;
  result.lmp_pruned = ctx.lmp_pruned;
  result.rfp_pruned = ctx.rfp_pruned;
  result.razor_pruned = ctx.razor_pruned;
  result.improving_count = ctx.improving_count;
  result.capture_history_updates = ctx.capture_history_updates;
  result.see_ordered_captures = ctx.order.see_ordered_captures;
  result.see_winning_captures = ctx.order.see_winning_captures;
  result.see_losing_captures = ctx.order.see_losing_captures;
  result.iir_applied = ctx.iir_applied;
  result.extensions_applied = ctx.extensions_applied;
  result.check_extensions = ctx.check_extensions;
  result.aspiration_attempts = ctx.aspiration_attempts;
  result.aspiration_fail_low = ctx.aspiration_fail_low;
  result.aspiration_fail_high = ctx.aspiration_fail_high;
  result.aspiration_researches = ctx.aspiration_researches;
  result.time_soft_stops = ctx.time_soft_stops;
  result.time_hard_stops = ctx.time_hard_stops;
  result.time_extensions = ctx.time_extensions;
  return result;
}

auto iterative_deepening(position& pos, int max_depth) -> search_result
{
  auto result = search_result{};
  if(max_depth < 1) {
    return result;
  }
  auto stopper = search_stopper{};
  transposition_table tt;
  tt.new_generation();
  auto ctx = search_context{};
  ctx.static_eval_stack.fill(std::numeric_limits<int>::max());
  ctx.stopper = &stopper;
  ctx.tt = &tt;
  ctx.nnue = nnue_begin(pos);
  auto nodes = std::uint64_t{0};
  auto previous_score = 0;
  auto have_previous = false;
  for(auto depth = 1; depth <= max_depth; ++depth) {
    if(depth == max_depth) {

      result = root_search(pos, depth, ctx, -inf, inf);
    } else {
      result = aspiration_search(pos, depth, ctx, previous_score, have_previous);
    }
    nodes += result.nodes;
    previous_score = result.score;
    have_previous = true;
  }
  result.nodes = nodes;
  result.lmp_pruned = ctx.lmp_pruned;
  result.rfp_pruned = ctx.rfp_pruned;
  result.razor_pruned = ctx.razor_pruned;
  result.improving_count = ctx.improving_count;
  result.capture_history_updates = ctx.capture_history_updates;
  result.see_ordered_captures = ctx.order.see_ordered_captures;
  result.see_winning_captures = ctx.order.see_winning_captures;
  result.see_losing_captures = ctx.order.see_losing_captures;
  result.iir_applied = ctx.iir_applied;
  result.extensions_applied = ctx.extensions_applied;
  result.check_extensions = ctx.check_extensions;
  result.aspiration_attempts = ctx.aspiration_attempts;
  result.aspiration_fail_low = ctx.aspiration_fail_low;
  result.aspiration_fail_high = ctx.aspiration_fail_high;
  result.aspiration_researches = ctx.aspiration_researches;
  result.time_soft_stops = ctx.time_soft_stops;
  result.time_hard_stops = ctx.time_hard_stops;
  result.time_extensions = ctx.time_extensions;
  return result;
}

auto search_impl(position& pos, const search_limits& limits, const search_stopper& stopper,
                 const std::function<void(const search_result&)>& on_iteration,
                 transposition_table& tt,
                 std::atomic<std::uint64_t>* shared_nodes_ptr = nullptr) -> search_result
{
  auto ctx = search_context{};
  ctx.static_eval_stack.fill(std::numeric_limits<int>::max());
  ctx.stopper = &stopper;
  ctx.tt = &tt;
  ctx.nnue = nnue_begin(pos);
  ctx.start = std::chrono::steady_clock::now();
  ctx.hard_time_ms = limits.hard_time_ms;
  ctx.depth_limit = limits.depth;
  ctx.nodes_limit = limits.nodes;
  ctx.infinite = limits.infinite;

  std::atomic<std::uint64_t> local_shared_nodes{0};
  if(shared_nodes_ptr != nullptr) {
    ctx.shared_nodes = shared_nodes_ptr;
  } else if(limits.nodes > 0) {
    ctx.shared_nodes = &local_shared_nodes;
  }
  search_result last_completed{};
  bool has_completed = false;
  search_result prev_completed{};
  bool have_prev_completed = false;
  auto previous_score = 0;
  auto have_previous = false;
  for(auto depth = 1; ; ++depth) {
    if(limits.depth > 0 && depth > limits.depth) {
      break;
    }

    if(limits.nodes > 0 && nodes_exceeded(ctx)) {
      break;
    }

    if(limits.hard_time_ms > 0 && !limits.infinite && limits.depth == 0 && limits.nodes == 0) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - ctx.start)
                               .count();
      if(elapsed >= limits.hard_time_ms) {
        ++ctx.time_hard_stops;
        break;
      }
    }

    if(limits.soft_time_ms > 0 && has_completed && !limits.infinite && limits.depth == 0
       && limits.nodes == 0) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - ctx.start)
                               .count();
      int effective_soft = limits.soft_time_ms;
      bool stable = true;
      if(have_prev_completed) {
        int delta = std::abs(last_completed.score - prev_completed.score);
        bool move_same = (last_completed.best_move == prev_completed.best_move);
        stable = move_same && delta <= tm_score_stability;
        if(!stable) {
          int extended = static_cast<int>(limits.soft_time_ms * tm_unstable_factor);
          if(limits.hard_time_ms > 0 && extended > limits.hard_time_ms) extended = limits.hard_time_ms;
          effective_soft = extended;
        }
      }
      if(elapsed >= effective_soft) {
        ++ctx.time_soft_stops;
        break;
      }
      if(!stable && elapsed >= limits.soft_time_ms && elapsed < effective_soft) {
        ++ctx.time_extensions;
      }
    }
    if(stopper.stop.load(std::memory_order_relaxed)) {
      break;
    }
    auto iteration = aspiration_search(pos, depth, ctx, previous_score, have_previous);
    if(ctx.aborted) {
      break;
    }
    if(!iteration.has_move) {
      continue;
    }
    {
      auto legal_copy = pos;
      const auto legal = movegen::generate_legal(legal_copy);
      bool legal_found = false;
      for(auto m : legal) if(m == iteration.best_move) { legal_found = true; break; }
      if(!legal_found) {
        break;
      }
    }
    if(has_completed) {
      prev_completed = last_completed;
      have_prev_completed = true;
    }
    last_completed = iteration;
    has_completed = true;
    if(on_iteration) {
      on_iteration(iteration);
    }
    previous_score = iteration.score;
    have_previous = true;
  }
  if(has_completed) {
    last_completed.nodes = ctx.nodes;
    last_completed.lmp_pruned = ctx.lmp_pruned;
    last_completed.rfp_pruned = ctx.rfp_pruned;
    last_completed.razor_pruned = ctx.razor_pruned;
    last_completed.improving_count = ctx.improving_count;
    last_completed.capture_history_updates = ctx.capture_history_updates;
    last_completed.see_ordered_captures = ctx.order.see_ordered_captures;
    last_completed.see_winning_captures = ctx.order.see_winning_captures;
    last_completed.see_losing_captures = ctx.order.see_losing_captures;
    last_completed.iir_applied = ctx.iir_applied;
    last_completed.extensions_applied = ctx.extensions_applied;
    last_completed.check_extensions = ctx.check_extensions;
    last_completed.aspiration_attempts = ctx.aspiration_attempts;
    last_completed.aspiration_fail_low = ctx.aspiration_fail_low;
    last_completed.aspiration_fail_high = ctx.aspiration_fail_high;
    last_completed.aspiration_researches = ctx.aspiration_researches;
    last_completed.time_soft_stops = ctx.time_soft_stops;
    last_completed.time_hard_stops = ctx.time_hard_stops;
    last_completed.time_extensions = ctx.time_extensions;
    last_completed.lmr_applied = ctx.lmr_applied;
    last_completed.lmr_research = ctx.lmr_research;
    return last_completed;
  }
  search_result emergency{};
  emergency.nodes = ctx.nodes;
  emergency.lmp_pruned = ctx.lmp_pruned;
  emergency.rfp_pruned = ctx.rfp_pruned;
  emergency.razor_pruned = ctx.razor_pruned;
  emergency.improving_count = ctx.improving_count;
  emergency.capture_history_updates = ctx.capture_history_updates;
  emergency.see_ordered_captures = ctx.order.see_ordered_captures;
  emergency.see_winning_captures = ctx.order.see_winning_captures;
  emergency.see_losing_captures = ctx.order.see_losing_captures;
  emergency.iir_applied = ctx.iir_applied;
  emergency.extensions_applied = ctx.extensions_applied;
  emergency.check_extensions = ctx.check_extensions;
  emergency.aspiration_attempts = ctx.aspiration_attempts;
  emergency.aspiration_fail_low = ctx.aspiration_fail_low;
  emergency.aspiration_fail_high = ctx.aspiration_fail_high;
  emergency.aspiration_researches = ctx.aspiration_researches;
  emergency.time_soft_stops = ctx.time_soft_stops;
  emergency.time_hard_stops = ctx.time_hard_stops;
  emergency.time_extensions = ctx.time_extensions;
  emergency.depth = 0;
  emergency.score = 0;
  auto copy = pos;
  const auto legal = movegen::generate_legal(copy);
  if(legal.size() == 0) {
    emergency.score = in_check(copy) ? -mate_value : 0;
    emergency.has_move = false;
  } else {
    emergency.best_move = legal[0];
    emergency.has_move = true;
  }
  return emergency;
}

static auto probe_tablebase_root(const position& pos,
                                 const std::function<void(const search_result&)>& on_iteration)
    -> std::optional<search_result>
{
  tb::probe_result probe;
  if(!tb::probe_root(pos, probe)) {
    return std::nullopt;
  }
  auto result = search_result{};
  result.depth = 1;
  result.score = probe.score;
  result.best_move = probe.best_move;
  result.has_move = probe.has_move;
  if(on_iteration) {
    on_iteration(result);
  }
  return result;
}

auto search(position& pos, const search_limits& limits, const search_stopper& stopper,
            const std::function<void(const search_result&)>& on_iteration) -> search_result
{
  transposition_table tt;
  return search(pos, limits, stopper, tt, on_iteration);
}

auto search(position& pos, const search_limits& limits, const search_stopper& stopper,
            transposition_table& tt,
            const std::function<void(const search_result&)>& on_iteration) -> search_result
{
  if(auto tb_result = probe_tablebase_root(pos, on_iteration); tb_result.has_value()) {
    return *tb_result;
  }
  tt.new_generation();
  return search_impl(pos, limits, stopper, on_iteration, tt);
}

auto parallel_search(position& pos, const search_limits& limits, search_stopper& stopper, int threads,
                     const std::function<void(const search_result&)>& on_iteration) -> search_result
{
  transposition_table tt;
  return parallel_search(pos, limits, stopper, threads, tt, on_iteration);
}

auto parallel_search(position& pos, const search_limits& limits, search_stopper& stopper, int threads,
                     transposition_table& tt,
                     const std::function<void(const search_result&)>& on_iteration) -> search_result
{
  if(threads < 1) {
    threads = 1;
  }
  if(threads == 1) {
    return search(pos, limits, stopper, tt, on_iteration);
  }

  if(auto tb_result = probe_tablebase_root(pos, on_iteration); tb_result.has_value()) {
    return *tb_result;
  }
  tt.new_generation();

  std::atomic<std::uint64_t> global_nodes{0};
  std::atomic<std::uint64_t>* shared_ptr = limits.nodes > 0 ? &global_nodes : nullptr;
  auto workers = std::vector<std::thread>{};
  workers.reserve(static_cast<std::size_t>(threads - 1));
  for(auto i = 0; i < threads - 1; ++i) {
    workers.emplace_back([&tt, &stopper, &limits, pos, shared_ptr]() mutable {
      static_cast<void>(search_impl(pos, limits, stopper, {}, tt, shared_ptr));
    });
  }
  auto result = search_impl(pos, limits, stopper, on_iteration, tt, shared_ptr);
  stopper.stop.store(true, std::memory_order_relaxed);
  for(auto& worker : workers) {
    worker.join();
  }
  return result;
}

}