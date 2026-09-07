#include "search/search.hpp"

#include "eval/evaluate.hpp"
#include "eval/nnue.hpp"
#include "movegen/movegen.hpp"
#include "search/ordering.hpp"
#include "search/transposition_table.hpp"
#include "tb/tablebase.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

namespace tuna::search {

namespace {

constexpr auto inf = mate_value;
constexpr auto null_move_r = 2;
constexpr auto lmr_move_threshold = 6;
constexpr auto lmr_min_non_pawn_material = 6;
constexpr auto futility_margin = 200;
constexpr auto delta_margin = 200;

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
  bool aborted = false;
  bool use_null_move = true;
  bool use_lmr = true;
  bool use_quiescence = true;
  bool use_futility = true;
  bool use_check_extension = true;
  const search_stopper* stopper = nullptr;
  move_ordering order;

  std::chrono::steady_clock::time_point start{};
  int hard_time_ms = 0;

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
  if(ctx.hard_time_ms <= 0) {
    return false;
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - ctx.start)
                           .count();
  return elapsed >= ctx.hard_time_ms;
}

struct scored_move {
  move mv;
  int score;
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
  ++ctx.nodes;
  if((ctx.nodes & 0xfff) == 0
     && (ctx.stopper->stop.load(std::memory_order_relaxed) || hard_time_exceeded(ctx))) {
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
        scored[i].score += static_exchange_eval(pos, scored[i].mv) * 32;
      }
    }
  }
  for(size_t i = 0; i < n; ++i) {
    size_t best_idx = i;
    for(size_t j = i + 1; j < n; ++j) if(scored[j].score > scored[best_idx].score) best_idx = j;
    if(best_idx != i) std::swap(scored[i], scored[best_idx]);
    const auto mv = scored[i].mv;
    if(!checked && is_capture(mv)) {
      const int see = static_exchange_eval(pos, mv);
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
  ++ctx.nodes;
  if((ctx.nodes & 0xfff) == 0
     && (ctx.stopper->stop.load(std::memory_order_relaxed) || hard_time_exceeded(ctx))) {
    ctx.aborted = true;
    return alpha;
  }
  if(ctx.use_check_extension && in_check(pos)) {
    if(ply < max_ply - 1) {
      ++depth;
      if(depth > max_ply - ply) depth = max_ply - ply;
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
  if(tt_move == no_move && depth >= 4 && !in_check(pos) && std::abs(alpha) <= mate_score_threshold && std::abs(beta) <= mate_score_threshold && (beta - alpha) == 1) {
    --depth;
  }
  if(ctx.use_null_move && !was_null && depth >= 3 && std::abs(beta) <= mate_score_threshold && non_pawn_material(pos) > 0 && !in_check(pos)) {
    const auto null_st = pos.make_null_move();
    nnue_after_null(ctx.nnue, pos);
    const auto null_value = -negamax_ab(pos, depth - 1 - null_move_r, ply + 1, -beta, -beta + 1, ctx, true, no_move);
    nnue_before_unmake(ctx.nnue);
    pos.unmake_null_move(null_st);
    if(ctx.aborted) {
      return alpha;
    }
    if(null_value >= beta) {
      return beta;
    }
  }
  const auto us = pos.side_to_move();
  const auto checked = in_check(pos);
  const auto alpha_orig = alpha;
  const auto static_eval = side_factor(pos) * eval::evaluate(pos);
  if(depth >= 2 && depth <= 3 && !checked && std::abs(beta) <= mate_score_threshold
     && std::abs(alpha) <= mate_score_threshold && (beta - alpha) == 1) {
    const int rfp_margin = 120 * depth;
    if(static_eval - rfp_margin >= beta) {
      return static_eval;
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
        scored[i].score += static_exchange_eval(pos, scored[i].mv) * 32;
      }
    }
  }
  auto best = -inf;
  for(size_t i = 0; i < n; ++i) {
    size_t best_idx = i;
    for(size_t j = i + 1; j < n; ++j) if(scored[j].score > scored[best_idx].score) best_idx = j;
    if(best_idx != i) std::swap(scored[i], scored[best_idx]);
    const auto mv = scored[i].mv;
    if(ctx.use_futility && ctx.use_quiescence && depth == 1 && !checked && is_quiet(mv)
       && static_eval + futility_margin <= alpha) {
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
    if(depth <= 3 && !checked && is_capture(mv) && !is_promotion(mv)) {
      if(std::abs(alpha) <= mate_score_threshold && std::abs(beta) <= mate_score_threshold) {
        auto tmp = pos;
        const auto st_tmp = tmp.make_move(mv);
        const bool gives_check = in_check(tmp);
        tmp.unmake_move(mv, st_tmp);
        if(!gives_check) {
          const int see = static_exchange_eval(pos, mv);
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
    const auto st = pos.make_move(mv);
    nnue_after_move(ctx.nnue, pos, mv, st);
    auto new_depth = depth - 1;
    auto reduction = 0;
    if(ctx.use_lmr && depth >= 3 && i >= lmr_move_threshold && is_quiet(mv) && mv != tt_move
       && !ctx.order.is_killer(mv, ply) && !in_check(pos)) {
      reduction = lmr_reduction(depth, ctx.order.history_score(us, mv.from, mv.to));
      if(lmr_eligible_material && depth - 1 - reduction >= 1) {
        new_depth = depth - 1 - reduction;
        ++ctx.lmr_applied;
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
  ++ctx.nodes;
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
  constexpr auto initial_delta = 50;
  auto alpha = -inf;
  auto beta = inf;
  auto delta = initial_delta;
  if(have_previous && depth >= 2) {
    alpha = std::max(-inf + 1, previous_score - delta);
    beta = std::min(inf - 1, previous_score + delta);
  }
  auto start_nodes = ctx.nodes;
  auto result = root_search(pos, depth, ctx, alpha, beta);
  while(!ctx.aborted) {
    if(result.score <= alpha) {
      if(alpha <= -inf + 1) {
        break;
      }
      alpha = std::max(-inf + 1, result.score - delta);
      delta += delta;
      result = root_search(pos, depth, ctx, alpha, beta);
    } else if(result.score >= beta) {
      if(beta >= inf - 1) {
        break;
      }
      beta = std::min(inf - 1, result.score + delta);
      delta += delta;
      result = root_search(pos, depth, ctx, alpha, beta);
    } else {
      break;
    }
  }
  result.nodes = ctx.nodes - start_nodes;
  return result;
}

}

auto lmr_reduction(int depth, int history) noexcept -> int
{
  static const auto base_table = [] {
    std::array<int, max_ply + 1> t{};
    for(int d = 0; d <= max_ply; ++d) {
      int v = 1;
      if(d > 0) v = 1 + static_cast<int>(std::log(static_cast<double>(d)) / 2.0);
      t[d] = v;
    }
    return t;
  }();
  int d = depth;
  if(d < 0) d = 0;
  if(d > max_ply) d = max_ply;
  auto r = base_table[d];
  const auto bonus = std::max(0, history) / 1024;
  r -= std::min(bonus, r);
  if(r < 1) r = 1;
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
  static thread_local transposition_table tt;
  tt.clear();
  auto ctx = search_context{};
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
  return result;
}

auto iterative_deepening(position& pos, int max_depth) -> search_result
{
  auto result = search_result{};
  if(max_depth < 1) {
    return result;
  }
  auto stopper = search_stopper{};
  static thread_local transposition_table tt;
  tt.clear();
  auto ctx = search_context{};
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
  return result;
}

auto search_impl(position& pos, const search_limits& limits, const search_stopper& stopper,
                 const std::function<void(const search_result&)>& on_iteration,
                 transposition_table& tt) -> search_result
{
  auto ctx = search_context{};
  ctx.stopper = &stopper;
  ctx.tt = &tt;
  ctx.nnue = nnue_begin(pos);
  ctx.start = std::chrono::steady_clock::now();
  ctx.hard_time_ms = limits.hard_time_ms;
  search_result last_completed{};
  bool has_completed = false;
  auto previous_score = 0;
  auto have_previous = false;
  for(auto depth = 1; ; ++depth) {
    if(limits.depth > 0 && depth > limits.depth) {
      break;
    }
    if(limits.soft_time_ms > 0) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - ctx.start)
                               .count();
      if(elapsed >= limits.soft_time_ms) {
        break;
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
    return last_completed;
  }
  search_result emergency{};
  emergency.nodes = ctx.nodes;
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
  static thread_local transposition_table tt;
  tt.clear();
  return search(pos, limits, stopper, tt, on_iteration);
}

auto search(position& pos, const search_limits& limits, const search_stopper& stopper,
            transposition_table& tt,
            const std::function<void(const search_result&)>& on_iteration) -> search_result
{
  if(auto tb_result = probe_tablebase_root(pos, on_iteration); tb_result.has_value()) {
    return *tb_result;
  }
  return search_impl(pos, limits, stopper, on_iteration, tt);
}

auto parallel_search(position& pos, const search_limits& limits, search_stopper& stopper, int threads,
                     const std::function<void(const search_result&)>& on_iteration) -> search_result
{
  static thread_local transposition_table tt;
  tt.clear();
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
  auto workers = std::vector<std::thread>{};
  workers.reserve(static_cast<std::size_t>(threads - 1));
  for(auto i = 0; i < threads - 1; ++i) {
    workers.emplace_back([&tt, &stopper, &limits, pos]() mutable {
      static_cast<void>(search_impl(pos, limits, stopper, {}, tt));
    });
  }
  auto result = search_impl(pos, limits, stopper, on_iteration, tt);
  stopper.stop.store(true, std::memory_order_relaxed);
  for(auto& worker : workers) {
    worker.join();
  }
  return result;
}

}