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
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace russell::search {

namespace {

constexpr auto inf = mate_value;
constexpr auto lmr_min_non_pawn_material = 6;
constexpr auto futility_margin = 200;
constexpr auto delta_margin = 200;

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

  std::uint64_t tt_eval_reused = 0;
  std::uint64_t capture_history_updates = 0;
  std::uint64_t iir_applied = 0;
  std::uint64_t extensions_applied = 0;
  std::uint64_t check_extensions = 0;
  std::uint64_t singular_considered = 0;
  std::uint64_t singular_extensions = 0;
  std::uint64_t probcut_considered = 0;
  std::uint64_t probcut_cutoff = 0;
  std::uint64_t threat_considered = 0;
  std::uint64_t threat_detected = 0;
  std::uint64_t aspiration_attempts = 0;
  std::uint64_t aspiration_fail_low = 0;
  std::uint64_t aspiration_fail_high = 0;
  std::uint64_t aspiration_researches = 0;
  std::uint64_t time_soft_stops = 0;
  std::uint64_t time_hard_stops = 0;
  std::uint64_t time_extensions = 0;

  std::array<SearchStackEntry, kSearchStackSize> stack{};
  bool aborted = false;
  bool use_null_move = true;
  bool use_lmr = true;
  bool use_quiescence = true;
  bool use_futility = true;
  bool use_check_extension = true;
  bool in_nmp_verification = false;
  bool in_probcut = false;
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

inline auto init_search_stack(search_context& ctx) noexcept -> void
{
  for(int i = 0; i < kSearchStackSize; ++i) {
    ctx.stack[static_cast<std::size_t>(i)].staticEval = std::numeric_limits<int>::max();
    ctx.stack[static_cast<std::size_t>(i)].improving = false;
    ctx.stack[static_cast<std::size_t>(i)].prevMove = no_move;
    ctx.stack[static_cast<std::size_t>(i)].movedPiece = piece::none;
    ctx.stack[static_cast<std::size_t>(i)].ply = i;
  }
}

inline auto publish_child_stack(SearchStackEntry* child, move mv, piece mover) noexcept -> void
{
  child->prevMove = mv;
  child->movedPiece = mover;
  child->improving = false;

}

inline auto publish_null_child(SearchStackEntry* child) noexcept -> void
{
  child->prevMove = no_move;
  child->movedPiece = piece::none;
  child->improving = false;
}

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

auto quiescence(position& pos, int ply, int alpha, int beta, search_context& ctx, SearchStackEntry* ss) -> int
{
  inc_nodes(ctx);

  const move prev_move = ss->prevMove;

  const piece prev_moved = ss->movedPiece;
  const move prev2_move = (ply >= 1) ? (ss - 1)->prevMove : no_move;
  const piece prev2_moved = (ply >= 1) ? (ss - 1)->movedPiece : piece::none;
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
    scored[i].score = ctx.order.score(pos, moves[i], ply, no_move, prev_move, prev_moved,
                                        prev2_move, prev2_moved);
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
        else if(see >= -100) bonus = -120000;
        else bonus = -121500;
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
    const piece qmover = pos.piece_on(mv.from);
    const auto st = pos.make_move(mv);
    nnue_after_move(ctx.nnue, pos, mv, st);

    publish_child_stack(ss + 1, mv, qmover);
    const auto value = -quiescence(pos, ply + 1, -beta, -alpha, ctx, ss + 1);
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
                 SearchStackEntry* ss, bool was_null = false, move excluded_move = no_move) -> int
{
  inc_nodes(ctx);

  const move prev_move = ss->prevMove;

  const piece prev_moved = ss->movedPiece;
  const move prev2_move = (ply >= 1) ? (ss - 1)->prevMove : no_move;
  const piece prev2_moved = (ply >= 1) ? (ss - 1)->movedPiece : piece::none;
  if((ctx.nodes & 0xfff) == 0
     && (ctx.stopper->stop.load(std::memory_order_relaxed) || hard_time_exceeded(ctx)
         || nodes_exceeded(ctx))) {
    ctx.aborted = true;
    return alpha;
  }

  bool node_extended = false;
  if(ctx.use_check_extension && in_check(pos)) {
    const bool tactical = (prev_move == no_move) || is_capture(prev_move) || is_promotion(prev_move);
    if(tactical && ply < max_ply - 1) {
      int new_depth = depth + check_extension_value;
      if(new_depth > max_ply - ply) new_depth = max_ply - ply;
      if(new_depth > depth) {
        ++ctx.extensions_applied;
        ++ctx.check_extensions;
        depth = new_depth;
        node_extended = true;
      }
    }
  }
  const auto key = pos.key();
  auto tt_move = move{};
  auto tt_score = 0;
  auto tt_depth = 0;
  auto probe_bound = tt_bound::none;
  auto tt_static_eval = tt_no_static_eval;

  const bool tt_hit =
      ctx.tt->probe(key, tt_move, tt_score, tt_depth, probe_bound, tt_static_eval);
  if(tt_hit && tt_depth >= depth) {
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
  const auto stage_tt_move = tt_move;
  const auto tt_searchable = stage_tt_move != no_move && !(stage_tt_move == excluded_move)
                             && movegen::is_legal(pos, stage_tt_move);
  auto cap_moves = move_list{};
  auto have_cap_moves = false;
  auto quiet_moves = move_list{};
  auto have_quiet_moves = false;
  const auto has_searchable = [&](const move_list& list) {
    for(const auto candidate : list) {
      if(!(candidate == excluded_move)) {
        return true;
      }
    }
    return false;
  };
  if(!tt_searchable) {
    cap_moves = movegen::generate_captures_promotions(pos);
    have_cap_moves = true;
  }
  const auto caps_searchable = tt_searchable || (have_cap_moves && has_searchable(cap_moves));
  if(!caps_searchable) {
    quiet_moves = movegen::generate_quiet_moves(pos);
    have_quiet_moves = true;
  }
  const auto any_searchable = caps_searchable || (have_quiet_moves && has_searchable(quiet_moves));
  if(!any_searchable) {
    if(have_cap_moves && have_quiet_moves && cap_moves.size() == 0 && quiet_moves.size() == 0) {
      const auto score = in_check(pos) ? -inf + ply : 0;

      if(excluded_move == no_move) {

        ctx.tt->store(key, no_move, store_value(score, ply), depth, tt_bound::exact,
                      tt_no_static_eval);
      }
      return score;
    }
    return -inf;
  }
  if(depth == 0) {
    if(ctx.use_quiescence) {
      return quiescence(pos, ply, alpha, beta, ctx, ss);
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

  int static_eval;
  if(tt_eval_usable(tt_hit, tt_static_eval)) {
    static_eval = tt_static_eval;
    ++ctx.tt_eval_reused;
  } else {
    static_eval = side_factor(pos) * eval::evaluate(pos);
  }

  bool improving = false;
  if(ply >= 2 && ply < 128) {
    int prev_eval = (ss - 2)->staticEval;
    if(prev_eval != std::numeric_limits<int>::max()
       && std::abs(prev_eval) < mate_score_threshold
       && std::abs(static_eval) < mate_score_threshold) {
      improving = static_eval > prev_eval;
      if(improving) ++ctx.improving_count;
    }
  }
  if(ply >= 0 && ply < 128) {
    ss->staticEval = static_eval;
  }
  ss->improving = improving;

  bool threat_now = false;
  if(!ctx.aborted
     && threat_gate(ply, in_check(pos), ctx.in_nmp_verification,
                    (excluded_move != no_move), ctx.in_probcut, node_extended)) {
    ++ctx.threat_considered;
    threat_now = detect_immediate_threat(pos);
    if(threat_now) {
      ++ctx.threat_detected;
    }
  }

  const int nmp_material = non_pawn_material(pos);
  if(ctx.use_null_move && !was_null && !ctx.in_nmp_verification
     && !threat_now
     && depth >= nmp_min_depth
     && (beta - alpha) == 1
     && std::abs(beta) <= mate_score_threshold
     && std::abs(alpha) <= mate_score_threshold
     && nmp_material >= nmp_min_non_pawn
     && !in_check(pos)
     && ply > 0) {
    bool skip_for_eval = (depth < 5 && static_eval + 100 < beta);
    if(!skip_for_eval) {
      int R = nmp_reduction(depth, static_eval, beta, improving, nmp_material);
      int new_depth = depth - 1 - R;
      if(new_depth < 0) new_depth = 0;
      ++ctx.nmp_tried;
      const auto null_st = pos.make_null_move();
      nnue_after_null(ctx.nnue, pos);

      publish_null_child(ss + 1);
      int null_value = -negamax_ab(pos, new_depth, ply + 1, -beta, -beta + 1, ctx, ss + 1, true);
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

          int verify_value = negamax_ab(pos, depth - 1, ply, beta - 1, beta, ctx, ss, false);
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

  if(depth >= 1 && depth <= rfp_max_depth && !checked && !threat_now
     && (beta - alpha) == 1
     && std::abs(beta) <= mate_score_threshold
     && std::abs(alpha) <= mate_score_threshold
     && ply > 0) {
    ++ctx.rfp_considered;

    int hist = 0;
    if(prev_move != no_move) {
      hist = ctx.order.history_score(opposite(us), prev_move.from, prev_move.to);
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
      hist_r = ctx.order.history_score(opposite(us), prev_move.from, prev_move.to);
    }
    int r_margin = razor_margin(depth, hist_r);
    if(improving) r_margin += improving_razor_bonus;
    if(static_eval + r_margin <= alpha) {

      int qscore = quiescence(pos, ply, alpha, beta, ctx, ss);
      if(ctx.aborted) {
        return alpha;
      }
      if(qscore <= alpha) {
        ++ctx.razor_pruned;
        return qscore;
      }
    }
  }

  bool singular_extend = false;
  {
    const bool in_exclusion = (excluded_move != no_move);
    const bool tt_exists = (tt_move != no_move);
    const bool bound_ok =
        (probe_bound == tt_bound::lower || probe_bound == tt_bound::exact);
    const int se_value = read_value(tt_score, ply);
    if(singular_gate(node_extended, checked, ply, in_exclusion, depth,
                     tt_exists, tt_depth, bound_ok, se_value)) {
      const int se_beta = se_value - depth;
      const int verify_depth = depth / 2;
      ++ctx.singular_considered;

      int verify_value = negamax_ab(pos, verify_depth, ply, se_beta - 1, se_beta, ctx,
                                    ss, false, tt_move);
      if(!ctx.aborted && verify_value < se_beta) {
        singular_extend = true;
      }
    }
  }

  if(!ctx.aborted
     && probcut_gate(node_extended, checked, ply, (excluded_move != no_move),
                     ctx.in_nmp_verification, depth, (beta - alpha) == 1,
                     alpha, beta)) {
    const int probcut_beta = beta + probcut_margin;
    const int pc_depth = depth - probcut_reduction;
    struct pc_scored {
      move mv{};
      int score = 0;
    };
    std::array<pc_scored, 256> pc_list{};
    size_t pc_n = 0;
    for(const auto cand : movegen::generate_captures_promotions(pos)) {
      if(cand == excluded_move) continue;
      if(!movegen::is_legal(pos, cand)) continue;
      const bool promo = is_promotion(cand);
      if(!probcut_see_ok(static_exchange_eval(pos, cand), promo)) continue;
      pc_list[pc_n++] = {cand, ctx.order.score(pos, cand, ply, tt_move, prev_move, prev_moved,
                                                 prev2_move, prev2_moved)};
      if(pc_n >= pc_list.size()) break;
    }
    for(size_t i = 0; i < pc_n; ++i) {
      size_t best_i = i;
      for(size_t k = i + 1; k < pc_n; ++k) {
        if(pc_list[k].score > pc_list[best_i].score) best_i = k;
      }
      if(best_i != i) {
        const auto tmp = pc_list[i];
        pc_list[i] = pc_list[best_i];
        pc_list[best_i] = tmp;
      }
    }
    for(size_t i = 0; i < pc_n; ++i) {
      if(ctx.aborted) return alpha;
      const auto mv = pc_list[i].mv;
      const piece pc_mover = pos.piece_on(mv.from);
      const auto st = pos.make_move(mv);
      nnue_after_move(ctx.nnue, pos, mv, st);
      ++ctx.probcut_considered;
      const bool saved_pc = ctx.in_probcut;
      ctx.in_probcut = true;

      publish_child_stack(ss + 1, mv, pc_mover);
      int value = -negamax_ab(pos, pc_depth, ply + 1, -probcut_beta, -probcut_beta + 1,
                              ctx, ss + 1, false);
      ctx.in_probcut = saved_pc;
      nnue_before_unmake(ctx.nnue);
      pos.unmake_move(mv, st);
      if(ctx.aborted) return alpha;
      if(value >= probcut_beta) {
        ++ctx.probcut_cutoff;
        return value;
      }
    }
  }
  const bool lmr_eligible_material = non_pawn_material(pos) >= lmr_min_non_pawn_material;

  const bool pv_node = (beta - alpha) > 1;

  std::array<move, 256> searched_captures{};
  size_t searched_capture_count = 0;

  std::array<move, 256> searched_quiets{};
  size_t searched_quiet_count = 0;
  auto best = -inf;

  auto search_stage_move = [&](move mv, int idx, int see) -> int {

    if(ctx.use_futility && depth == 1
       && !checked && (beta - alpha) == 1
       && is_quiet(mv) && static_eval + futility_margin <= alpha) {
      if(std::abs(alpha) > mate_score_threshold || std::abs(beta) > mate_score_threshold) {
      } else {
        auto tmp = pos;
        const auto st_tmp = tmp.make_move(mv);
        const bool gives_check = in_check(tmp);
        tmp.unmake_move(mv, st_tmp);
        if(!gives_check) {
          return 0;
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
          if(see == std::numeric_limits<int>::max()) see = static_exchange_eval(pos, mv);

          if(see < 0 && static_eval + see + see_prune_margin * depth <= alpha) {
            return 0;
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
        if(h < -history_prune_base * depth) {
          return 0;
        }
      }
    }

    if(depth <= lmp_max_depth && !checked && !threat_now && (beta - alpha) == 1
       && is_quiet(mv) && !is_promotion(mv)
       && mv != tt_move && !ctx.order.is_killer(mv, ply)
       && std::abs(alpha) <= mate_score_threshold
       && std::abs(beta) <= mate_score_threshold) {
      ++ctx.lmp_considered;
      int hist = ctx.order.history_score(us, mv.from, mv.to);
      int thresh = lmp_threshold(depth, hist);
      if(improving) thresh += improving_lmp_bonus;
      if(static_cast<int>(idx) + 1 > thresh) {
        auto tmp = pos;
        auto st_tmp = tmp.make_move(mv);
        bool gives_check = in_check(tmp);
        if(!gives_check) {
          ++ctx.lmp_pruned;
          return 0;
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

    if(is_quiet(mv)) {
      searched_quiets[searched_quiet_count++] = mv;
    }

    const bool recap_candidate = is_cap_for_lmr && prev_move != no_move
                                 && is_capture(prev_move) && mv.to == prev_move.to
                                 && !is_promotion(mv) && !node_extended
                                 && depth <= recap_max_depth;
    if(recap_candidate && see == std::numeric_limits<int>::max()) {
      see = static_exchange_eval(pos, mv);
    }
    if(is_cap_for_lmr && see == std::numeric_limits<int>::max()
       && ctx.use_lmr && depth >= lmr_min_depth
       && idx + 1 >= lmr_min_move_count
       && mv != tt_move
       && !ctx.order.is_killer(mv, ply)
       && !checked
       && !is_promotion(mv)) {
       see = static_exchange_eval(pos, mv);
    }

    const piece child_mover = pos.piece_on(mv.from);
    const auto st = pos.make_move(mv);
    nnue_after_move(ctx.nnue, pos, mv, st);
    publish_child_stack(ss + 1, mv, child_mover);
    auto new_depth = depth - 1;
    int reduction = 0;

    if(ctx.use_lmr && depth >= lmr_min_depth
       && idx + 1 >= lmr_min_move_count
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
        int mc = std::min<int>(63, idx + 1);
        reduction = lmr_reduction(depth, mc, is_cap, hist, see, pv_node, improving,
                                  ctx.order.is_countermove(prev_move, mv));

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

    if(!node_extended && depth < max_ply - ply) {
      if(mv == tt_move && singular_extend) {

        new_depth = depth;
        ++ctx.extensions_applied;
        ++ctx.singular_extensions;
      } else if(is_capture(mv) && !is_promotion(mv) && prev_move != no_move
                && is_capture(prev_move) && mv.to == prev_move.to
                && depth <= recap_max_depth) {

        const int recap_see = see;
        if(recap_see != std::numeric_limits<int>::max() && recap_see >= 0) {

          new_depth = std::min(new_depth + 1, depth);
          ++ctx.extensions_applied;
        }
      }
    }
    auto value = 0;
    if(idx == 0) {
      value = -negamax_ab(pos, new_depth, ply + 1, -beta, -alpha, ctx, ss + 1, false);
    } else {
      value = -negamax_ab(pos, new_depth, ply + 1, -alpha - 1, -alpha, ctx, ss + 1, false);
      if(value > alpha && value < beta && !ctx.aborted) {
        value = -negamax_ab(pos, new_depth, ply + 1, -beta, -alpha, ctx, ss + 1, false);
      }
    }
    if(new_depth < depth - 1 && value > alpha && !ctx.aborted) {
      ++ctx.lmr_research;
      value = -negamax_ab(pos, depth - 1, ply + 1, -beta, -alpha, ctx, ss + 1, false);
    }
    nnue_before_unmake(ctx.nnue);
    pos.unmake_move(mv, st);
    if(ctx.aborted) {
      return 2;
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

        if(depth >= killer_min_depth) {
          ctx.order.update_killers(mv, ply);
        }
        ctx.order.update_history(us, mv, depth);

        if(prev_move != no_move && ss->movedPiece != piece::none) {
          const piece cur_piece = pos.piece_on(mv.from);
          if(cur_piece != piece::none) {
            const bool have_prev2 = ply >= 1 && (ss - 1)->prevMove != no_move
                                     && (ss - 1)->movedPiece != piece::none;
            const int cont_bonus = depth * depth;
            const int cont_malus = -(depth * depth);
            ctx.order.update_continuation_1ply(ss->movedPiece, prev_move.to, cur_piece, mv.to,
                                               cont_bonus);
            if(have_prev2) {
              ctx.order.update_continuation_2ply((ss - 1)->movedPiece, (ss - 1)->prevMove.to,
                                                 cur_piece, mv.to, cont_bonus);
            }
            for(size_t k = 0; k < searched_quiet_count; ++k) {
              if(searched_quiets[k] == mv) continue;
              const piece alt_piece = pos.piece_on(searched_quiets[k].from);
              if(alt_piece == piece::none) continue;
              ctx.order.update_continuation_1ply(ss->movedPiece, prev_move.to, alt_piece,
                                                 searched_quiets[k].to, cont_malus);
              if(have_prev2) {
                ctx.order.update_continuation_2ply((ss - 1)->movedPiece, (ss - 1)->prevMove.to,
                                                    alt_piece, searched_quiets[k].to, cont_malus);
              }
            }
          }
        }

        const int malus = -(depth * depth);
        for(size_t k = 0; k < searched_quiet_count; ++k) {
          if(searched_quiets[k] == mv) continue;
          ctx.order.update_history(us, searched_quiets[k].from, searched_quiets[k].to, malus);
        }

        if(prev_move != no_move) {
          ctx.order.update_countermove_history(prev_move, mv, depth);
          const int cm_malus = -(depth * depth);
          for(size_t k = 0; k < searched_quiet_count; ++k) {
            if(searched_quiets[k] == mv) continue;
            ctx.order.update_countermove_history(prev_move.from, prev_move.to,
                                                 searched_quiets[k].from,
                                                 searched_quiets[k].to, cm_malus);
          }
        }
      }
      return 1;
    }
    return 0;
  };

  auto stage_status = 0;
  auto picker = move_picker(ctx.order, ply, stage_tt_move, tt_searchable,
                            std::move(cap_moves), have_cap_moves,
                            std::move(quiet_moves), have_quiet_moves,
                            prev_move, prev_moved, prev2_move, prev2_moved,
                            excluded_move);
  auto picker_mv = move{};
  auto picker_see = std::numeric_limits<int>::max();
  auto picker_idx = 0;
  while(picker.next(pos, picker_mv, picker_see, picker_idx)) {
    const int status = search_stage_move(picker_mv, picker_idx, picker_see);
    if(status != 0) {
      stage_status = status;
      break;
    }
  }
  if(stage_status == 2) {
    return best;
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

  if(excluded_move == no_move && tt_search_result_valid(best)) {

    ctx.tt->store(key, tt_move, store_value(best, ply), depth, bound, static_eval);
  }
  return best;
}

auto root_search(position& pos, int depth, search_context& ctx, int alpha, int beta,
                 move prev_best) -> search_result
{
  auto result = search_result{};

  ctx.stack[0].prevMove = no_move;
  ctx.stack[0].movedPiece = piece::none;
  ctx.stack[0].improving = false;
  ctx.stack[0].ply = 0;
  ctx.stack[0].staticEval = side_factor(pos) * eval::evaluate(pos);
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

  const auto root_tt_searchable = prev_best != no_move && movegen::is_legal(pos, prev_best);
  auto picker = move_picker(ctx.order, 0, prev_best, root_tt_searchable,
                            move_list{}, false, move_list{}, false,
                            no_move, piece::none, no_move, piece::none, no_move);
  auto best = -inf;
  auto picker_mv = move{};
  auto picker_see = std::numeric_limits<int>::max();
  auto picker_idx = 0;
  while(picker.next(pos, picker_mv, picker_see, picker_idx)) {
    const auto& mv = picker_mv;
    const piece root_mover = pos.piece_on(mv.from);
    const auto st = pos.make_move(mv);
    nnue_after_move(ctx.nnue, pos, mv, st);

    publish_child_stack(&ctx.stack[1], mv, root_mover);
    ctx.stack[1].ply = 1;
    auto value = 0;
    if(best == -inf) {
      value = -negamax_ab(pos, depth - 1, 1, -beta, -alpha, ctx, &ctx.stack[1], false);
    } else {
      value = -negamax_ab(pos, depth - 1, 1, -alpha - 1, -alpha, ctx, &ctx.stack[1], false);
      if(value > alpha && value < beta && !ctx.aborted) {
        value = -negamax_ab(pos, depth - 1, 1, -beta, -alpha, ctx, &ctx.stack[1], false);
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
                       bool have_previous, move prev_best) -> search_result
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
  auto result = root_search(pos, depth, ctx, alpha, beta, prev_best);
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
        auto retry = root_search(pos, depth, ctx, alpha, beta, prev_best);
        ++attempts;
        if(!ctx.aborted) result = retry;
        break;
      }
      auto retry = root_search(pos, depth, ctx, alpha, beta, prev_best);
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
        auto retry = root_search(pos, depth, ctx, alpha, beta, prev_best);
        ++attempts;
        if(!ctx.aborted) result = retry;
        break;
      }
      auto retry = root_search(pos, depth, ctx, alpha, beta, prev_best);
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
    auto retry = root_search(pos, depth, ctx, alpha, beta, prev_best);
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

auto detect_immediate_threat(position& pos) noexcept -> bool
{
  for(const auto cand : movegen::generate_captures_promotions(pos)) {
    if(static_exchange_eval(pos, cand) >= threat_see_margin) {
      return true;
    }

    const auto st = pos.make_move(cand);
    const bool gives_check = in_check(pos);
    pos.unmake_move(cand, st);
    if(gives_check) {
      return true;
    }
  }
  return false;
}

auto rfp_margin(int depth, int history) noexcept -> int
{

  int m = rfp_linear * depth;
  int adj = history / rfp_history_scale;
  if(adj > rfp_history_max_adjust) adj = rfp_history_max_adjust;
  if(adj < -rfp_history_max_adjust) adj = -rfp_history_max_adjust;
  m += adj;
  if(m < 0) m = 0;
  return m;
}

auto nmp_reduction(int depth, int static_eval, int beta) noexcept -> int
{
  int R = nmp_base_reduction + depth / nmp_depth_divisor;

  const int margin = static_eval - beta;
  if(depth > 3 && margin < 64) {
    R -= 1;
  } else if(margin > nmp_eval_margin) {
    R += 1;
  }
  if(R > nmp_max_reduction) R = nmp_max_reduction;
  if(R < 1) R = 1;
  if(R > depth - 1) R = depth - 1;
  return R;
}

auto nmp_reduction(int depth, int static_eval, int beta, bool improving,
                   int non_pawn_material) noexcept -> int
{
  int R = nmp_reduction(depth, static_eval, beta);
  if(improving) R -= 1;
  if(depth > 3 && non_pawn_material <= 6) R -= 1;
  if(R < 1) R = 1;
  if(R > depth - 1) R = depth - 1;
  return R;
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

    int adj = history / capture_history_lmr_scale;
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

auto lmr_reduction(int depth, int move_count, bool is_capture, int history, int see,
                   bool pv_node, bool improving, bool is_countermove) noexcept -> int
{

  if(is_countermove) {
    return 0;
  }

  int r = lmr_reduction(depth, move_count, is_capture, history);

  if(pv_node) r -= 1;

  if(improving) r -= 1;
  if(is_capture && see != std::numeric_limits<int>::max()) {

    if(see >= 0) r -= 1;

    else r += 1;
  }
  if(!is_capture && depth >= 5 && history < -4096) {
    r += 1;
  }
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

  if(depth > max_ply) {
    depth = max_ply;
  }
  auto stopper = search_stopper{};
  transposition_table tt;
  tt.new_generation();
  auto ctx = search_context{};
  init_search_stack(ctx);
  ctx.stopper = &stopper;
  ctx.tt = &tt;
  ctx.nnue = nnue_begin(pos);
  ctx.use_null_move = use_null_move;
  ctx.use_lmr = use_lmr;
  ctx.use_quiescence = use_quiescence;
  ctx.use_futility = use_futility;
  ctx.use_check_extension = use_check_extension;
  auto result = root_search(pos, depth, ctx, -inf, inf, no_move);
  result.lmr_applied = ctx.lmr_applied;
  result.lmr_research = ctx.lmr_research;
  result.lmp_pruned = ctx.lmp_pruned;
  result.rfp_pruned = ctx.rfp_pruned;
  result.razor_pruned = ctx.razor_pruned;
  result.improving_count = ctx.improving_count;
  result.tt_eval_reused = ctx.tt_eval_reused;
  result.capture_history_updates = ctx.capture_history_updates;
  result.see_ordered_captures = ctx.order.see_ordered_captures;
  result.see_winning_captures = ctx.order.see_winning_captures;
  result.see_losing_captures = ctx.order.see_losing_captures;
  result.iir_applied = ctx.iir_applied;
  result.extensions_applied = ctx.extensions_applied;
  result.check_extensions = ctx.check_extensions;
  result.singular_considered = ctx.singular_considered;
  result.singular_extensions = ctx.singular_extensions;
  result.probcut_considered = ctx.probcut_considered;
  result.probcut_cutoff = ctx.probcut_cutoff;
  result.threat_considered = ctx.threat_considered;
  result.threat_detected = ctx.threat_detected;
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

  if(max_depth > max_ply) {
    max_depth = max_ply;
  }
  auto stopper = search_stopper{};
  transposition_table tt;
  tt.new_generation();
  auto ctx = search_context{};
  init_search_stack(ctx);
  ctx.stopper = &stopper;
  ctx.tt = &tt;
  ctx.nnue = nnue_begin(pos);
  auto nodes = std::uint64_t{0};
  auto previous_score = 0;
  auto have_previous = false;
  auto previous_best = no_move;
  for(auto depth = 1; depth <= max_depth; ++depth) {
    if(depth == max_depth) {

      result = root_search(pos, depth, ctx, -inf, inf, previous_best);
    } else {
      result = aspiration_search(pos, depth, ctx, previous_score, have_previous, previous_best);
    }
    nodes += result.nodes;
    previous_score = result.score;
    have_previous = true;
    if(result.has_move) {
      previous_best = result.best_move;
    }
  }
  result.nodes = nodes;
  result.lmp_pruned = ctx.lmp_pruned;
  result.rfp_pruned = ctx.rfp_pruned;
  result.razor_pruned = ctx.razor_pruned;
  result.improving_count = ctx.improving_count;
  result.tt_eval_reused = ctx.tt_eval_reused;
  result.capture_history_updates = ctx.capture_history_updates;
  result.see_ordered_captures = ctx.order.see_ordered_captures;
  result.see_winning_captures = ctx.order.see_winning_captures;
  result.see_losing_captures = ctx.order.see_losing_captures;
  result.iir_applied = ctx.iir_applied;
  result.extensions_applied = ctx.extensions_applied;
  result.check_extensions = ctx.check_extensions;
  result.singular_considered = ctx.singular_considered;
  result.singular_extensions = ctx.singular_extensions;
  result.probcut_considered = ctx.probcut_considered;
  result.probcut_cutoff = ctx.probcut_cutoff;
  result.threat_considered = ctx.threat_considered;
  result.threat_detected = ctx.threat_detected;
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
                 std::atomic<std::uint64_t>* shared_nodes_ptr = nullptr,
                 int start_depth = 1) -> search_result
{
  auto ctx = search_context{};
  init_search_stack(ctx);
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

  std::array<move, tm_hist_size> hist_moves{};
  std::array<int, tm_hist_size> hist_scores{};
  int hist_count = 0;

  bool last_asp_turbulent = false;
  auto previous_score = 0;
  auto have_previous = false;

  if(start_depth < 1) {
    start_depth = 1;
  }
  for(auto depth = start_depth; ; ++depth) {

    if(depth > max_ply) {
      break;
    }
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

      double factor = 1.0;
      if(hist_count >= 2) {
        const bool move_same = (hist_moves[0] == hist_moves[1]);
        const int delta = std::abs(hist_scores[0] - hist_scores[1]);
        const bool score_stable = delta <= tm_score_stability;
        int streak = 1;
        for(int k = 1; k < hist_count; ++k) {
          if(hist_moves[k] == hist_moves[0]) ++streak;
          else break;
        }
        if(move_same && score_stable && streak >= tm_streak_min) {
          factor = std::max(factor, tm_stable_factor);
        }
        if(!move_same || !score_stable) {
          factor = std::max(factor, tm_unstable_factor);
        }
        if(last_asp_turbulent) {
          factor = std::max(factor, tm_aspire_factor);
        }
        bool churn = false;
        if(hist_count >= 3) {
          churn = (hist_moves[0] != hist_moves[1]) && (hist_moves[1] != hist_moves[2]);
        }
        const bool collapse = hist_scores[0] <= hist_scores[1] - tm_panic_drop;
        if(churn || collapse) {
          factor = std::max(factor, tm_panic_factor);
        }
      }
      int effective_soft = static_cast<int>(limits.soft_time_ms * factor);
      if(limits.hard_time_ms > 0) {
        if(effective_soft > limits.hard_time_ms) effective_soft = limits.hard_time_ms;
      } else {
        const int cap = static_cast<int>(limits.soft_time_ms * tm_no_hard_cap_factor);
        if(effective_soft > cap) effective_soft = cap;
      }
      if(elapsed >= effective_soft) {
        ++ctx.time_soft_stops;
        break;
      }
      if(elapsed >= limits.soft_time_ms && elapsed < effective_soft) {
        ++ctx.time_extensions;
      }
    }
    if(stopper.stop.load(std::memory_order_relaxed)) {
      break;
    }

    const auto asp_low_before = ctx.aspiration_fail_low;
    const auto asp_high_before = ctx.aspiration_fail_high;
    const auto asp_res_before = ctx.aspiration_researches;
    const auto prev_best = has_completed ? last_completed.best_move : no_move;
    auto iteration = aspiration_search(pos, depth, ctx, previous_score, have_previous, prev_best);
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
    last_completed = iteration;
    has_completed = true;
    last_asp_turbulent = (ctx.aspiration_fail_low > asp_low_before)
                         || (ctx.aspiration_fail_high > asp_high_before)
                         || (ctx.aspiration_researches > asp_res_before);
    for(int k = tm_hist_size - 1; k > 0; --k) {
      hist_moves[k] = hist_moves[k - 1];
      hist_scores[k] = hist_scores[k - 1];
    }
    hist_moves[0] = iteration.best_move;
    hist_scores[0] = iteration.score;
    if(hist_count < tm_hist_size) ++hist_count;
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
    last_completed.tt_eval_reused = ctx.tt_eval_reused;
    last_completed.capture_history_updates = ctx.capture_history_updates;
    last_completed.see_ordered_captures = ctx.order.see_ordered_captures;
    last_completed.see_winning_captures = ctx.order.see_winning_captures;
    last_completed.see_losing_captures = ctx.order.see_losing_captures;
    last_completed.iir_applied = ctx.iir_applied;
    last_completed.extensions_applied = ctx.extensions_applied;
    last_completed.check_extensions = ctx.check_extensions;
    last_completed.singular_considered = ctx.singular_considered;
    last_completed.singular_extensions = ctx.singular_extensions;
    last_completed.probcut_considered = ctx.probcut_considered;
    last_completed.probcut_cutoff = ctx.probcut_cutoff;
    last_completed.threat_considered = ctx.threat_considered;
    last_completed.threat_detected = ctx.threat_detected;
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
  emergency.tt_eval_reused = ctx.tt_eval_reused;
  emergency.capture_history_updates = ctx.capture_history_updates;
  emergency.see_ordered_captures = ctx.order.see_ordered_captures;
  emergency.see_winning_captures = ctx.order.see_winning_captures;
  emergency.see_losing_captures = ctx.order.see_losing_captures;
  emergency.iir_applied = ctx.iir_applied;
  emergency.extensions_applied = ctx.extensions_applied;
  emergency.check_extensions = ctx.check_extensions;
  emergency.singular_considered = ctx.singular_considered;
  emergency.singular_extensions = ctx.singular_extensions;
  emergency.probcut_considered = ctx.probcut_considered;
  emergency.probcut_cutoff = ctx.probcut_cutoff;
  emergency.threat_considered = ctx.threat_considered;
  emergency.threat_detected = ctx.threat_detected;
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
  auto helper_limits = limits;
  helper_limits.soft_time_ms = 0;
  helper_limits.hard_time_ms = 0;
  auto slots = std::vector<search_result>(static_cast<std::size_t>(threads));
  auto slots_mutex = std::mutex{};
  auto workers = std::vector<std::thread>{};
  workers.reserve(static_cast<std::size_t>(threads - 1));
  for(auto i = 0; i < threads - 1; ++i) {
    const auto slot = static_cast<std::size_t>(i + 1);
    workers.emplace_back([&tt, &stopper, helper_limits, pos, shared_ptr, &slots, &slots_mutex,
                          slot]() mutable {
      auto helper_result =
          search_impl(pos, helper_limits, stopper, {}, tt, shared_ptr, 1 + static_cast<int>(slot));
      const auto lock = std::lock_guard<std::mutex>{slots_mutex};
      slots[slot] = helper_result;
    });
  }
  auto main_result = search_impl(pos, limits, stopper, on_iteration, tt, shared_ptr, 1);
  {
    const auto lock = std::lock_guard<std::mutex>{slots_mutex};
    slots[0] = main_result;
  }
  stopper.stop.store(true, std::memory_order_relaxed);
  for(auto& worker : workers) {
    worker.join();
  }
  const auto winner = select_completed_result(slots.data(), slots.size(), 0);

  auto final_result = slots[winner];
  final_result.nodes = aggregate_worker_nodes(slots.data(), slots.size());
  return final_result;
}

}
