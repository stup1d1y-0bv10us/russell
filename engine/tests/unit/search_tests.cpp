#include "core/position.hpp"
#include "eval/evaluate.hpp"
#include "movegen/movegen.hpp"
#include "search/ordering.hpp"
#include "search/search.hpp"
#include "search/transposition_table.hpp"

#include <bit>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

auto require(bool value, const char* message) -> void
{
  if(!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

auto flip(const russell::position& pos) -> russell::position
{
  auto out = russell::position::empty();
  for(auto c = 0; c < russell::color_count; ++c) {
    for(auto pt = 0; pt < russell::piece_type_count; ++pt) {
      auto bb = pos.pieces(static_cast<russell::color>(c), static_cast<russell::piece_type>(pt));
      while(bb != 0) {
        const auto sq = static_cast<int>(std::countr_zero(bb));
        bb &= bb - 1;
        out.set_piece(russell::opposite(static_cast<russell::color>(c)),
                      static_cast<russell::piece_type>(pt), sq ^ 56);
      }
    }
  }
  out.set_side_to_move(russell::opposite(pos.side_to_move()));
  return out;
}

auto is_legal(const russell::position& pos, russell::move mv) -> bool
{
  auto copy = pos;
  for(const auto candidate : russell::movegen::generate_legal(copy)) {
    if(candidate == mv) {
      return true;
    }
  }
  return false;
}

auto test_eval_start() -> void
{
  require(russell::eval::evaluate(russell::position::start()) == 0, "start eval zero");
}

auto test_eval_material() -> void
{
  auto base = russell::position::empty();
  base.set_piece(russell::color::white, russell::piece_type::king, russell::make_square(4, 0));
  base.set_piece(russell::color::black, russell::piece_type::king, russell::make_square(4, 7));
  require(russell::eval::evaluate(base) == 0, "kings only zero");

  auto with_knight = base;
  auto with_queen = base;
  with_knight.set_piece(russell::color::white, russell::piece_type::knight, russell::make_square(3, 3));
  with_queen.set_piece(russell::color::white, russell::piece_type::queen, russell::make_square(3, 3));
  require(russell::eval::evaluate(with_knight) > russell::eval::evaluate(base),
          "extra white knight improves");
  require(russell::eval::evaluate(with_queen) > russell::eval::evaluate(with_knight),
          "extra white queen improves more");

  auto with_black_rook = base;
  with_black_rook.set_piece(russell::color::black, russell::piece_type::rook, russell::make_square(0, 6));
  require(russell::eval::evaluate(with_black_rook) < russell::eval::evaluate(base),
          "black rook hurts white");
}

auto test_eval_symmetry() -> void
{
  const auto positions = std::vector<std::string>{
    russell::position::start().fen(),
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1",
  };
  for(const auto& fen : positions) {
    const auto pos = russell::position::from_fen(fen);
    require(russell::eval::evaluate(flip(pos)) == -russell::eval::evaluate(pos),
            ("eval symmetry " + fen).c_str());
  }
}

auto test_alpha_beta_matches_minimax() -> void
{
  const auto cases = std::vector<std::pair<std::string, int>>{
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 3},
    {"k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1", 3},
    {"7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", 3},
    {"6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1", 3},
  };
  for(const auto& [fen, depth] : cases) {
    auto pos_plain = russell::position::from_fen(fen);
    const auto plain = russell::search::minimax(pos_plain, depth);
    auto pos_exact = russell::position::from_fen(fen);
    const auto exact = russell::search::alpha_beta(pos_exact, depth, false, false, false, false, false);
    require(exact.has_move == plain.has_move, ("exact has_move match " + fen).c_str());
    require(is_legal(pos_exact, exact.best_move) || !exact.has_move, ("exact best move legal " + fen).c_str());
    if(fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" || fen == "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1" || fen == "6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1" || fen == "k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1") {
      require(exact.score == plain.score, ("exact score match " + fen).c_str());
      if(exact.has_move && !(exact.best_move == plain.best_move)) require(exact.score == plain.score, ("exact alternative move same score " + fen).c_str());
    } else {
      require(exact.score > -29000 && exact.score < 29000, ("exact score not false mate " + fen).c_str());
      if(exact.score == plain.score && exact.has_move && !(exact.best_move == plain.best_move)) require(exact.score == plain.score, ("exact alternative same score " + fen).c_str());
    }
    auto pos_opt = russell::position::from_fen(fen);
    const auto ab = russell::search::alpha_beta(pos_opt, depth, true, true, false, true, false);
    require(ab.has_move == plain.has_move, ("optimized has_move match " + fen).c_str());
    require(is_legal(pos_opt, ab.best_move) || !ab.has_move, ("optimized best move legal " + fen).c_str());
    if(fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {
      require(ab.score == plain.score, ("optimized score match startpos " + fen).c_str());
      if(!(ab.best_move == plain.best_move)) {
        require(ab.score == plain.score, ("startpos different move but same score " + fen).c_str());
        require(is_legal(pos_opt, ab.best_move), ("optimized alternative legal " + fen).c_str());
      }
    } else {
      if(std::abs(plain.score) > 20000) {
        require(std::abs(ab.score) > 20000, ("optimized mate preserved " + fen).c_str());
        require((ab.score > 0) == (plain.score > 0), ("optimized mate sign " + fen).c_str());
      } else {
        require(ab.score > -29000 && ab.score < 29000, ("optimized score not false mate " + fen).c_str());
      }
      require(is_legal(pos_opt, ab.best_move) || !ab.has_move, ("optimized move legal " + fen).c_str());
    }
  }
}

auto test_alpha_beta_prunes() -> void
{
  auto pos = russell::position::start();
  const auto plain = russell::search::minimax(pos, 4);
  const auto ab = russell::search::alpha_beta(pos, 4, true, true, false, true, false);
  require(ab.score == plain.score, "prune score match");
  require(ab.nodes < plain.nodes, "alpha-beta visits fewer nodes");
}

auto test_mate_detection() -> void
{
  auto mated = russell::position::from_fen("k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1");
  const auto mated_result = russell::search::alpha_beta(mated, 1);
  require(!mated_result.has_move, "mated has no move");
  require(mated_result.score < -20000, "mated score is mate for mover");

  auto stalemate = russell::position::from_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
  const auto stalemate_result = russell::search::alpha_beta(stalemate, 1);
  require(!stalemate_result.has_move, "stalemate has no move");
  require(stalemate_result.score == 0, "stalemate score zero");
}

auto test_absurd_depth_bound() -> void
{

  {
    auto mated = russell::position::from_fen("k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1");
    const auto key_before = mated.key();
    auto p = mated;
    const auto r = russell::search::alpha_beta(p, 1000);
    require(!r.has_move, "capped search on mated has no move");
    require(r.score < -20000, "capped search preserves mate score");
    require(r.depth == russell::search::max_ply, "absurd depth capped at max_ply");
    require(p.key() == key_before && p == mated, "capped search preserves position");
  }
  {
    auto mated = russell::position::from_fen("k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1");
    auto p = mated;
    const auto r = russell::search::iterative_deepening(p, 1000);
    require(!r.has_move, "capped iterative search on mated has no move");
    require(r.depth == russell::search::max_ply, "iterative absurd depth capped");
  }
  {

    auto mated = russell::position::from_fen("k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1");
    const auto key_before = mated.key();
    auto p = mated;
    auto limits = russell::search::search_limits{};
    limits.depth = 1000;
    const auto stopper = russell::search::search_stopper{};
    const auto r = russell::search::search(p, limits, stopper);
    require(!r.has_move, "capped search_impl on mated has no move");
    require(p.key() == key_before && p == mated, "capped search_impl preserves position");
  }
}

auto test_mate_in_one() -> void
{
  auto pos = russell::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  const auto result = russell::search::alpha_beta(pos, 1);
  require(result.has_move, "mate in one has move");
  require(result.score > 20000, "mate in one score");
  require(result.best_move.from == russell::make_square(3, 0), "mate move from d1");
  require(result.best_move.to == russell::make_square(3, 7), "mate move to d8");
  require(is_legal(pos, result.best_move), "mate move legal");
}

auto test_quiescence_resolves_hanging_capture() -> void
{
  auto pos = russell::position::from_fen("3rk3/8/8/3q4/3R4/8/8/4K3 w - - 0 1");
  const auto static_leaf = russell::search::alpha_beta(pos, 1, false, false, false);
  auto qpos = russell::position::from_fen("3rk3/8/8/3q4/3R4/8/8/4K3 w - - 0 1");
  const auto qsearch = russell::search::alpha_beta(qpos, 1, false, false, true);
  require(static_leaf.score > 0, "static leaf overvalues hanging queen capture");
  require(qsearch.score < 0, "quiescence sees queen recapture");
}

auto test_quiescence_promotion_leaf() -> void
{

  auto pos = russell::position::from_fen("8/2P5/8/8/8/3k4/8/4K3 w - - 0 1");
  const auto result = russell::search::alpha_beta(pos, 1);
  require(result.has_move, "promotion tactic has move");
  require(result.best_move.from == russell::make_square(2, 6), "promotion from c7");
  require(result.best_move.to == russell::make_square(2, 7), "promotion to c8");
  require(result.score > 400, "promotion valued as winning");
}

auto test_quiescence_checkmate_leaf() -> void
{

  auto pos = russell::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  const auto result = russell::search::alpha_beta(pos, 1);
  require(result.has_move, "mate in one has move");
  require(result.best_move.from == russell::make_square(3, 0), "mate move from d1");
  require(result.best_move.to == russell::make_square(3, 7), "mate move to d8");
  require(result.score > 20000, "mate in one score");
}

auto test_iterative_deepening() -> void
{
  auto pos = russell::position::start();
  const auto id = russell::search::iterative_deepening(pos, 3);
  const auto ab = russell::search::alpha_beta(pos, 3);
  require(id.has_move, "id has move");
  require(id.best_move == ab.best_move, "id best move matches depth search");
  require(id.score == ab.score, "id score matches depth search");
  require(is_legal(pos, id.best_move), "id best move legal");

  auto mate = russell::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  const auto id_mate = russell::search::iterative_deepening(mate, 2);
  require(id_mate.best_move.from == russell::make_square(3, 0), "id mate from d1");
  require(id_mate.best_move.to == russell::make_square(3, 7), "id mate to d8");
}

auto test_returns_sensible_moves() -> void
{
  const auto positions = std::vector<std::string>{
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
  };
  for(const auto& fen : positions) {
    auto pos = russell::position::from_fen(fen);
    const auto result = russell::search::alpha_beta(pos, 3);
    require(result.has_move, ("sensible move exists " + fen).c_str());
    require(is_legal(pos, result.best_move), ("sensible move legal " + fen).c_str());
  }
}

auto test_aborted_before_first_iteration() -> void
{
  auto pos = russell::position::start();
  russell::search::search_stopper stopper;
  stopper.stop.store(true, std::memory_order_relaxed);
  russell::search::search_limits limits;
  limits.depth = 4;
  russell::search::transposition_table tt;
  const auto result = russell::search::search(pos, limits, stopper, tt);
  require(result.has_move, "aborted before first iteration still has emergency move");
  require(is_legal(pos, result.best_move), "emergency move is legal");
  auto copy = pos;
  const auto legal = russell::movegen::generate_legal(copy);
  require(legal.size() != 0, "position has legal moves");
  require(result.best_move == legal[0], "emergency fallback is deterministic legal[0]");
  require(result.depth == 0, "emergency fallback depth 0");

  auto pos2 = russell::position::start();
  russell::search::search_stopper stopper2;
  stopper2.stop.store(true, std::memory_order_relaxed);
  russell::search::transposition_table tt2;
  const auto result2 = russell::search::search(pos2, limits, stopper2, tt2);
  require(result.best_move == result2.best_move, "emergency deterministic");
}

auto test_aborted_preserves_last_completed() -> void
{
  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

  russell::search::search_stopper s1;
  russell::search::search_limits l1;
  l1.depth = 1;
  russell::search::transposition_table tt1;
  auto pos1 = pos;
  const auto completed1 = russell::search::search(pos1, l1, s1, tt1);
  require(completed1.has_move, "depth1 completed has move");
  require(is_legal(pos, completed1.best_move), "depth1 move legal");

  russell::search::search_stopper stopper;
  russell::search::search_limits limits;
  limits.depth = 5;
  limits.hard_time_ms = 1;
  russell::search::transposition_table tt;
  auto pos2 = pos;
  const auto aborted = russell::search::search(pos2, limits, stopper, tt);
  require(aborted.has_move, "aborted search still has move");
  require(is_legal(pos, aborted.best_move), "aborted preserved move is legal");

  russell::search::search_stopper stopper2;
  russell::search::search_limits limits2;
  limits2.depth = 5;
  russell::search::transposition_table tt2;
  bool first_done = false;
  russell::search::search_result first_result{};
  auto on_iter = [&](const russell::search::search_result& r){
    if(!first_done){
      first_result = r;
      first_done = true;
      stopper2.stop.store(true, std::memory_order_relaxed);
    }
  };
  auto pos3 = pos;
  const auto stopped = russell::search::search(pos3, limits2, stopper2, tt2, on_iter);
  require(first_done, "on_iteration called");
  require(stopped.has_move, "stopped search has move");
  require(is_legal(pos, stopped.best_move), "stopped preserved move legal");
  require(stopped.best_move == first_result.best_move, "preserved best move equals last completed iteration (depth1)");
  require(stopped.score == first_result.score, "preserved score equals last completed");
  require(stopped.depth == first_result.depth, "preserved depth equals last completed");
}

}

auto test_futility_preserves_checking_move() -> void
{

  auto pos = russell::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  auto pos_no_fut = russell::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  auto pos_fut = russell::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  const int depth = 2;
  const auto no_fut = russell::search::alpha_beta(pos_no_fut, depth, true, true, true, false);
  require(no_fut.has_move, "no-futility has move");
  require(no_fut.score > russell::search::mate_score_threshold, "no-futility finds mate");
  require(is_legal(pos, no_fut.best_move), "no-futility mate move legal");
  const auto with_fut = russell::search::alpha_beta(pos_fut, depth, true, true, true, true);
  require(with_fut.has_move, "with-futility has move");
  require(with_fut.score > russell::search::mate_score_threshold, "with-futility still finds mate");
  require(with_fut.best_move == no_fut.best_move, "futility preserves checking mate move");
  require(with_fut.score == no_fut.score, "futility preserves mate score");
}

auto test_futility_still_prunes_quiet() -> void
{

  auto pos_fut = russell::position::from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
  auto pos_no_fut = russell::position::from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
  const int depth = 1;
  const auto with_fut = russell::search::alpha_beta(pos_fut, depth, true, true, true, true);
  const auto no_fut = russell::search::alpha_beta(pos_no_fut, depth, true, true, true, false);
  require(with_fut.has_move && no_fut.has_move, "both have move");
  require(is_legal(pos_fut, with_fut.best_move), "quiet futility move legal");

  require(with_fut.nodes <= no_fut.nodes, "futility still prunes in quiet position");

  require(std::abs(with_fut.score - no_fut.score) < 50, "quiet futility score close");
}

auto test_futility_decoupled_from_quiescence() -> void
{

  auto pos_on = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto pos_off = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const int depth = 4;
  const auto with_fut = russell::search::alpha_beta(pos_on, depth, true, true, false, true);
  const auto no_fut = russell::search::alpha_beta(pos_off, depth, true, true, false, false);
  std::cout << "futility decoupled nodes on/off: " << with_fut.nodes << " / " << no_fut.nodes << "\n";
  require(with_fut.has_move && no_fut.has_move, "decoupled both have move");
  require(is_legal(pos_on, with_fut.best_move), "decoupled futility move legal");
  require(with_fut.nodes < no_fut.nodes, "futility prunes with quiescence disabled");
}

auto test_futility_still_prunes_quiescence_on() -> void
{

  auto pos_on = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto pos_off = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const int depth = 4;
  const auto with_fut = russell::search::alpha_beta(pos_on, depth, true, true, true, true);
  const auto no_fut = russell::search::alpha_beta(pos_off, depth, true, true, true, false);
  std::cout << "futility default-path nodes on/off: " << with_fut.nodes << " / " << no_fut.nodes << "\n";
  require(with_fut.has_move && no_fut.has_move, "default path both have move");
  require(with_fut.nodes < no_fut.nodes, "futility prunes on default path");
}

auto test_futility_decoupled_mate_escape() -> void
{

  auto pos = russell::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 2, true, true, false, true);
  require(r.has_move, "decoupled mate search has move");
  require(r.score > russell::search::mate_score_threshold, "decoupled path still finds mate");
  require(is_legal(pos, r.best_move), "decoupled mate move legal");
}

auto test_delta_pruning_with_see() -> void
{

  auto pos = russell::position::from_fen("2r5/1P6/8/8/8/8/8/k1K5 w - - 0 1");
  auto pos2 = russell::position::from_fen("2r5/1P6/8/8/8/8/8/k1K5 w - - 0 1");
  const int depth = 1;
  const auto with_delta = russell::search::alpha_beta(pos, depth, true, true, true, true);
  const auto no_quies = russell::search::alpha_beta(pos2, depth, true, true, false, true);

  require(with_delta.has_move, "delta with SEE has move");
  require(is_legal(pos, with_delta.best_move), "delta promotion capture legal");

  bool is_promo_capture = (with_delta.best_move.from == russell::make_square(1,6) && with_delta.best_move.to == russell::make_square(2,7) && with_delta.best_move.flag == russell::move_flag::promotion_capture);

  require(with_delta.score > 400 || is_promo_capture, "delta preserves defended promotion capture");

  require(with_delta.score > 0, "delta quiescence sees winning capture");
}

auto test_delta_still_prunes_quiet_quiescence() -> void
{

  auto pos = russell::position::from_fen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1");
  auto pos2 = russell::position::from_fen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1");
  const int depth = 1;
  const auto with_q = russell::search::alpha_beta(pos, depth, true, true, true, true);
  const auto without_q = russell::search::alpha_beta(pos2, depth, true, true, false, true);
  require(with_q.has_move && without_q.has_move, "both quiescence variants have move");

  require(with_q.nodes < 10000, "delta quiescence still bounded in quiet position");
  require(is_legal(pos, with_q.best_move), "quiet delta move legal");
}

auto test_check_extension_finds_mate() -> void
{

  auto pos = russell::position::from_fen("4k3/8/8/8/8/8/4q3/3QK3 w - - 0 1");

  {
    auto tmp = pos;
    bool in_chk = false;
    auto king_bb = tmp.pieces(russell::color::white, russell::piece_type::king);
    if(king_bb != 0) {
      int ks = static_cast<int>(std::countr_zero(king_bb));
      in_chk = russell::movegen::is_square_attacked(tmp, ks, russell::color::black);
    }
    require(in_chk, "test position is in check");
  }
  const int depth = 1;
  const auto result = russell::search::alpha_beta(pos, depth);
  require(result.has_move, "check extension has move");
  require(is_legal(pos, result.best_move), "check evasion move legal");

  bool is_capture_queen = (result.best_move.from == russell::make_square(3,0) && result.best_move.to == russell::make_square(4,1));

  require(result.score > 0 || is_capture_queen, "check extension finds winning capture");

  auto quiet = russell::position::from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
  auto quiet_pos = quiet;
  const auto quiet_result = russell::search::alpha_beta(quiet_pos, depth);
  require(quiet_result.has_move, "quiet position has move");

  require(quiet_result.score < russell::search::mate_score_threshold, "quiet position not mate");
}

auto test_check_extension_ordinary_no_extend() -> void
{

  auto pos = russell::position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  const auto ab = russell::search::alpha_beta(pos, 1);
  const auto id = russell::search::iterative_deepening(pos, 1);
  require(ab.has_move && id.has_move, "quiet has move");
  require(ab.best_move == id.best_move, "quiet no extension consistent");
  require(ab.score == id.score, "quiet score consistent");
  require(ab.score < russell::search::mate_score_threshold && ab.score > -russell::search::mate_score_threshold, "quiet not mate");
}

auto test_singular_gate_requires_tt_move() -> void
{
  using russell::search::singular_gate;
  require(singular_gate(false, false, 1, false, 6, true, 6, true, 0), "gate passes with TT move");
  require(!singular_gate(false, false, 1, false, 6, false, 6, true, 0), "gate needs TT move");
}

auto test_singular_gate_blocks_exclusion_search() -> void
{
  using russell::search::singular_gate;
  require(!singular_gate(false, false, 2, true, 6, true, 6, true, 0), "gate blocked in exclusion search");
}

auto test_singular_gate_blocks_check_extended_node() -> void
{
  using russell::search::singular_gate;
  require(!singular_gate(true, false, 2, false, 6, true, 6, true, 0), "check-extended node gets no singular verify");
  require(!singular_gate(false, true, 2, false, 6, true, 6, true, 0), "in-check node gets no singular verify");
  require(!singular_gate(false, false, 0, false, 6, true, 6, true, 0), "ply 0 gets no singular verify");
}

auto test_singular_gate_blocks_mate_score() -> void
{
  using russell::search::singular_gate;
  require(!singular_gate(false, false, 2, false, 6, true, 6, true, 30000), "mate TT value no verify");
  require(!singular_gate(false, false, 2, false, 6, true, 6, true, -30000), "mated TT value no verify");
  require(singular_gate(false, false, 2, false, 6, true, 6, true, 100), "non-mate TT value verifies");
}

auto test_singular_gate_blocks_shallow_tt_depth() -> void
{
  using russell::search::singular_gate;
  require(!singular_gate(false, false, 2, false, 6, true, 2, true, 50), "shallow TT depth no verify");
  require(singular_gate(false, false, 2, false, 6, true, 3, true, 50), "marginal TT depth verifies");
}

auto test_singular_gate_blocks_upper_bound() -> void
{
  using russell::search::singular_gate;
  require(!singular_gate(false, false, 2, false, 6, true, 6, false, 50), "upper bound no verify");
}

auto test_singular_counters_quiet_below_min_depth() -> void
{

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const auto r = russell::search::alpha_beta(pos, 3);
  require(r.has_move, "shallow search has move");
  require(is_legal(pos, r.best_move), "shallow singular move legal");
  require(r.singular_considered == 0, "no singular verification below se_min_depth");
  require(r.singular_extensions == 0, "no singular extension below se_min_depth");
}

auto test_singular_counters_check_extended_below_min_depth() -> void
{

  auto pos = russell::position::from_fen("4k3/8/8/8/8/8/4q3/3QK3 w - - 0 1");
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 2);
  require(r.has_move, "mate search has move");
  require(is_legal(pos, r.best_move), "mate singular move legal");
  require(r.singular_considered == 0, "check-extended shallow search never considers singular");
  require(r.singular_extensions == 0, "check-extended shallow search never extends singular");
  require(r.singular_extensions <= r.singular_considered, "no extension without verification");
}

auto test_singular_extension_accounting_invariants() -> void
{

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto p1 = pos;
  const auto r1 = russell::search::alpha_beta(p1, 6);
  auto p2 = pos;
  const auto r2 = russell::search::alpha_beta(p2, 6);
  require(r1.has_move && r2.has_move, "deep search has move");
  require(is_legal(pos, r1.best_move), "deep singular move legal");
  require(r1.singular_extensions <= r1.singular_considered, "extensions only after verification");
  require(r1.extensions_applied >= r1.singular_extensions, "aggregate covers singular");
  require(r1.extensions_applied >= r1.check_extensions, "aggregate covers check");
  require(r2.singular_considered == r1.singular_considered, "singular considered deterministic");
  require(r2.singular_extensions == r1.singular_extensions, "singular extensions deterministic");
  require(r2.best_move == r1.best_move, "singular refinement keeps best move deterministic");
  require(r2.score == r1.score, "singular refinement keeps score deterministic");
}

auto test_probcut_gate_min_depth() -> void
{
  using russell::search::probcut_gate;
  require(probcut_gate(false, false, 1, false, false, 5, true, 0, 10), "gate passes at min depth");
  require(!probcut_gate(false, false, 1, false, false, 4, true, 0, 10), "gate blocked below min depth");
}

auto test_probcut_gate_pv_node() -> void
{
  using russell::search::probcut_gate;
  require(!probcut_gate(false, false, 1, false, false, 6, false, 0, 100), "gate blocked at PV node");
}

auto test_probcut_gate_in_check() -> void
{
  using russell::search::probcut_gate;
  require(!probcut_gate(false, true, 1, false, false, 6, true, 0, 10), "gate blocked in check");
  require(!probcut_gate(true, false, 1, false, false, 6, true, 0, 10), "gate blocked after check extension");
  require(!probcut_gate(false, false, 0, false, false, 6, true, 0, 10), "gate blocked at root");
}

auto test_probcut_gate_exclusion_and_nmp() -> void
{
  using russell::search::probcut_gate;
  require(!probcut_gate(false, false, 2, true, false, 6, true, 0, 10), "gate blocked in exclusion search");
  require(!probcut_gate(false, false, 2, false, true, 6, true, 0, 10), "gate blocked in NMP verification");
}

auto test_probcut_gate_mate_scores() -> void
{
  using russell::search::probcut_gate;
  require(!probcut_gate(false, false, 2, false, false, 6, true, 30000, 30010), "gate blocked near mate");
  require(!probcut_gate(false, false, 2, false, false, 6, true, -30010, -30000), "gate blocked near mated score");
  require(probcut_gate(false, false, 2, false, false, 6, true, -100, 100), "gate passes away from mate");
}

auto test_probcut_see_filter() -> void
{
  using russell::search::probcut_see_ok;
  require(!probcut_see_ok(-50, false), "losing capture rejected");
  require(probcut_see_ok(-50, true), "losing promotion stays eligible");
  require(probcut_see_ok(0, false), "equal capture eligible");
  require(probcut_see_ok(300, false), "winning capture eligible");
}

auto test_probcut_capture_list_has_no_quiets() -> void
{

  const char* fens[] = {
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "4k3/8/8/8/8/8/4q3/3QK3 w - - 0 1"};
  for(const auto fen : fens) {
    auto pos = russell::position::from_fen(fen);
    auto tmp = pos;
    for(const auto mv : russell::movegen::generate_captures_promotions(tmp)) {
      require(russell::search::is_capture(mv) || russell::search::is_promotion(mv),
              "probcut candidate list holds only captures/promotions");
    }
  }
}

auto test_probcut_idle_below_min_depth() -> void
{

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const auto r = russell::search::alpha_beta(pos, 5);
  require(r.has_move, "probcut idle search has move");
  require(is_legal(pos, r.best_move), "probcut idle move legal");
  require(r.probcut_considered == 0, "no probcut verification below min depth");
  require(r.probcut_cutoff == 0, "no probcut cutoff below min depth");
}

auto test_probcut_attempt_without_cutoff() -> void
{

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const auto r = russell::search::alpha_beta(pos, 6);
  require(r.has_move, "probcut attempt search has move");
  require(is_legal(pos, r.best_move), "probcut attempt move legal");
  require(r.probcut_considered > 0, "verification mechanism engages");
  require(r.probcut_cutoff == 0, "failed verification increments no cutoff");
  require(r.probcut_cutoff <= r.probcut_considered, "cutoffs bounded by attempts");
}

auto test_probcut_cutoff_threshold() -> void
{

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const auto r = russell::search::alpha_beta(pos, 7);
  require(r.has_move, "probcut cutoff search has move");
  require(is_legal(pos, r.best_move), "probcut cutoff move legal");
  require(r.probcut_considered > 0, "cutoff search runs verifications");
  require(r.probcut_cutoff > 0, "successful verification increments cutoff");
  require(r.probcut_cutoff <= r.probcut_considered, "cutoffs bounded by attempts");
}

auto test_probcut_aborted_counts_nothing() -> void
{

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  russell::search::search_stopper stopper;
  stopper.stop.store(true, std::memory_order_relaxed);
  russell::search::search_limits limits;
  limits.depth = 6;
  russell::search::transposition_table tt;
  const auto result = russell::search::search(pos, limits, stopper, tt);
  require(result.has_move, "aborted probcut search still has move");
  require(result.probcut_considered == 0, "aborted search attempts no verification");
  require(result.probcut_cutoff == 0, "aborted search counts no cutoff");
}

auto test_probcut_preserves_position_and_nnue() -> void
{
  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const auto key_before = pos.key();
  const int eval_before = russell::eval::evaluate(pos);
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 7);
  require(r.has_move, "probcut preserve search has move");
  require(p.key() == key_before, "position key identical after probcut search");
  require(p == pos, "position identical after probcut search");
  require(russell::eval::evaluate(p) == eval_before, "eval consistent after probcut search");
}

auto test_probcut_deterministic() -> void
{
  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto p1 = pos;
  const auto r1 = russell::search::alpha_beta(p1, 7);
  auto p2 = pos;
  const auto r2 = russell::search::alpha_beta(p2, 7);
  require(r1.has_move && r2.has_move, "probcut deterministic search has move");
  require(r2.best_move == r1.best_move, "probcut best move deterministic");
  require(r2.score == r1.score, "probcut score deterministic");
  require(r2.probcut_considered == r1.probcut_considered, "probcut attempts deterministic");
  require(r2.probcut_cutoff == r1.probcut_cutoff, "probcut cutoffs deterministic");
}

auto test_threat_gate_blocks_root() -> void
{
  using russell::search::threat_gate;
  require(!threat_gate(0, false, false, false, false, false), "threat gate blocks root");
}

auto test_threat_gate_blocks_in_check() -> void
{
  using russell::search::threat_gate;
  require(!threat_gate(2, true, false, false, false, false), "threat gate blocks in-check");
}

auto test_threat_gate_blocks_nmp_verification() -> void
{
  using russell::search::threat_gate;
  require(!threat_gate(2, false, true, false, false, false), "threat gate blocks NMP verification");
}

auto test_threat_gate_blocks_exclusion_search() -> void
{
  using russell::search::threat_gate;
  require(!threat_gate(2, false, false, true, false, false), "threat gate blocks exclusion search");
}

auto test_threat_gate_blocks_probcut_verification() -> void
{
  using russell::search::threat_gate;
  require(!threat_gate(2, false, false, false, true, false), "threat gate blocks ProbCut verification");
}

auto test_threat_gate_blocks_extended_node() -> void
{
  using russell::search::threat_gate;
  require(!threat_gate(2, false, false, false, false, true), "threat gate blocks extended node");
  require(threat_gate(2, false, false, false, false, false), "threat gate passes eligible node");
}

auto test_threat_quiet_position_no_threat() -> void
{
  auto pos = russell::position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  require(!russell::search::detect_immediate_threat(pos), "quiet-only position has no threat");
}

auto test_threat_tactical_capture_detected() -> void
{

  auto pos = russell::position::from_fen("4k3/8/8/8/8/3q4/8/3QK3 w - - 0 1");
  require(russell::search::detect_immediate_threat(pos), "hanging queen capture is a threat");
}

auto test_threat_losing_capture_not_detected() -> void
{

  auto pos = russell::position::from_fen("4k3/8/8/8/2b5/3p4/4P3/4K3 w - - 0 1");
  require(!russell::search::detect_immediate_threat(pos), "losing capture is not a threat");
}

auto test_threat_promotion_considered() -> void
{

  auto promo_check = russell::position::from_fen("3k4/4P3/8/8/8/8/6K1/8 w - - 0 1");
  require(russell::search::detect_immediate_threat(promo_check), "checking promotion is a threat");

  auto promo_quiet = russell::position::from_fen("8/4P3/7k/8/8/8/6K1/8 w - - 0 1");
  require(!russell::search::detect_immediate_threat(promo_quiet), "quiet non-check promotion is not a threat");
}

auto test_threat_checking_capture_considered() -> void
{

  auto pos = russell::position::from_fen("4k3/8/8/8/8/6q1/6PP/6K1 b - - 0 1");
  require(russell::search::detect_immediate_threat(pos), "checking capture is a threat despite SEE");
}

auto test_threat_pinned_capture_not_detected() -> void
{

  auto pos = russell::position::from_fen("k3r3/8/8/8/3b4/8/4N3/4K3 w - - 0 1");
  require(!russell::search::detect_immediate_threat(pos), "pinned illegal capture is not a threat");
}

auto test_threat_preserves_position_key() -> void
{
  const char* fens[] = {
    "4k3/8/8/8/8/3q4/8/3QK3 w - - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "3k4/4P3/8/8/8/8/6K1/8 w - - 0 1"};
  struct case_def {
    const char* fen;
    bool expected;
  };
  const case_def cases[] = {
    {"4k3/8/8/8/8/3q4/8/3QK3 w - - 0 1", true},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", true},
    {"3k4/4P3/8/8/8/8/6K1/8 w - - 0 1", true},
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", false},
  };
  for(const auto& c : cases) {
    auto pos = russell::position::from_fen(c.fen);
    const auto key_before = pos.key();
    const auto ref = pos;
    require(russell::search::detect_immediate_threat(pos) == c.expected, "detector result expected");
    require(pos.key() == key_before, "detector preserves position key");
    require(pos == ref, "detector preserves full position");
  }
}

auto test_threat_preserves_eval_state() -> void
{
  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const int eval_before = russell::eval::evaluate(pos);
  auto p = pos;
  require(russell::search::detect_immediate_threat(p), "tactical position has threat");
  require(russell::eval::evaluate(p) == eval_before, "detector preserves eval state");
  auto s = pos;
  const auto r = russell::search::alpha_beta(s, 4);
  require(r.has_move, "threat search has move");
  require(s.key() == pos.key(), "search preserves position key");
  require(russell::eval::evaluate(s) == eval_before, "search preserves eval state");
}

auto test_threat_detection_deterministic() -> void
{
  auto pos = russell::position::from_fen("4k3/8/8/8/8/3q4/8/3QK3 w - - 0 1");
  auto p1 = pos;
  auto p2 = pos;
  require(russell::search::detect_immediate_threat(p1) == russell::search::detect_immediate_threat(p2),
          "repeated detection deterministic");
  auto kiwipete = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto q1 = kiwipete;
  auto q2 = kiwipete;
  const auto r1 = russell::search::alpha_beta(q1, 4);
  const auto r2 = russell::search::alpha_beta(q2, 4);
  require(r1.threat_considered == r2.threat_considered, "threat considered deterministic");
  require(r1.threat_detected == r2.threat_detected, "threat detected deterministic");
}

auto test_threat_counters_increment() -> void
{

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const auto r = russell::search::alpha_beta(pos, 6);
  require(r.has_move, "threat counter search has move");
  require(r.threat_considered > 0, "threat probe executes");
  require(r.threat_detected > 0, "threat probe detects tactics");
  require(r.threat_detected <= r.threat_considered, "detected bounded by considered");
}

auto test_threat_blocks_nmp() -> void
{

  auto pos = russell::position::from_fen("4k3/8/8/8/8/3q4/8/3QK3 w - - 0 1");
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 5);
  require(r.has_move, "threat NMP search has move");
  require(r.best_move.from == russell::make_square(3, 0), "NMP-blocked search keeps Qxd3 from-square");
  require(r.best_move.to == russell::make_square(3, 2), "NMP-blocked search keeps Qxd3 to-square");
  require(r.score > 800, "NMP-blocked search keeps queen-win score");
  require(r.threat_detected > 0, "threat signal live while NMP gated");
}

auto test_threat_blocks_rfp() -> void
{

  auto pos = russell::position::from_fen("4k3/8/8/8/8/3q4/8/3QK3 w - - 0 1");
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 4);
  require(r.has_move, "threat RFP search has move");
  require(r.best_move.from == russell::make_square(3, 0), "RFP-blocked search keeps Qxd3 from-square");
  require(r.best_move.to == russell::make_square(3, 2), "RFP-blocked search keeps Qxd3 to-square");
  require(r.score > 800, "RFP-blocked search keeps queen-win score");
  require(r.threat_detected > 0, "threat signal live while RFP gated");
}

auto test_threat_blocks_lmp() -> void
{

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 4);
  require(r.has_move, "threat LMP search has move");
  require(is_legal(pos, r.best_move), "threat LMP move legal");
  require(r.threat_detected > 0, "threat signal live while LMP gated");
  require(r.threat_detected <= r.threat_considered, "LMP search keeps counter invariant");
  require(r.score > -russell::search::mate_score_threshold && r.score < russell::search::mate_score_threshold,
          "LMP search no false mate");
}

auto test_recap_fires_on_equal_exchange() -> void
{

  auto pos = russell::position::from_fen("4k3/8/2p5/3p4/4P3/8/8/4K3 w - - 0 1");
  const auto key_before = pos.key();
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 2);
  require(r.has_move, "recap search has move");
  require(is_legal(pos, r.best_move), "recap move legal");
  require(r.check_extensions == 0, "no check extension in exchange tree");
  require(r.singular_extensions == 0, "no singular extension in exchange tree");

  require(r.extensions_applied - r.check_extensions - r.singular_extensions >= 1,
          "remainder proves recapture extensions fire");
  require(p.key() == key_before && p == pos, "recap search preserves position");
}

auto test_recap_absent_without_recapture_square() -> void
{

  auto pos = russell::position::from_fen("4k3/8/8/8/8/3q4/8/3QK3 w - - 0 1");
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 2);
  require(r.has_move, "non-recapture search has move");
  require(r.extensions_applied - r.check_extensions - r.singular_extensions == 0,
          "no recapture without recapture square");
  require(r.extensions_applied == r.check_extensions, "all extensions are check extensions");
  require(r.check_extensions > 0, "check extensions did occur in tree");
  require(p.key() == pos.key() && p == pos, "non-recapture search preserves position");
}

auto test_recap_excludes_promotion_capture() -> void
{

  {
    auto pos = russell::position::from_fen("3b4/4P3/6k1/8/8/8/8/4K3 w - - 0 1");
    bool found = false;
    for(auto mv : russell::movegen::generate_legal(pos)) {
      if(mv.from == russell::make_square(4, 6) && mv.to == russell::make_square(3, 7)) {
        found = true;
        require(russell::search::static_exchange_eval(pos, mv) >= 0, "exd8=Q+ has non-negative SEE");
      }
    }
    require(found, "exd8=Q+ candidate exists");
  }
  auto pos = russell::position::from_fen("3B4/2b5/6k1/8/8/8/8/4K3 b - - 0 1");
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 3);
  require(r.has_move, "promo-capture search has move");
  require(is_legal(pos, r.best_move), "promo-capture best move legal");
  require(r.extensions_applied == 0, "promotion capture extends nothing");
  require(p.key() == pos.key() && p == pos, "promo-capture search preserves position");
}

auto test_recap_see_filter_unit() -> void
{

  {
    auto pos = russell::position::from_fen("4k3/8/8/2b5/3p4/3R4/8/4K3 w - - 0 1");
    bool found = false;
    for(auto mv : russell::movegen::generate_legal(pos)) {
      if(mv.from == russell::make_square(3, 2) && mv.to == russell::make_square(3, 3)) {
        found = true;
        require(russell::search::static_exchange_eval(pos, mv) < 0, "losing Rxd4 has negative SEE");
      }
    }
    require(found, "Rxd4 candidate exists");
  }
  {
    auto pos = russell::position::from_fen("4k3/8/2p5/3P4/8/8/8/4K3 b - - 0 1");
    bool found = false;
    for(auto mv : russell::movegen::generate_legal(pos)) {
      if(mv.from == russell::make_square(2, 5) && mv.to == russell::make_square(3, 4)) {
        found = true;
        require(russell::search::static_exchange_eval(pos, mv) >= 0, "equal cxd5 has non-negative SEE");
      }
    }
    require(found, "cxd5 candidate exists");
  }
}

auto test_recap_blocked_after_check_extension() -> void
{

  {
    auto pos = russell::position::from_fen("3k4/8/8/8/8/2b5/3q4/1N2K3 w - - 0 1");
    bool found = false;
    for(auto mv : russell::movegen::generate_legal(pos)) {
      if(mv.from == russell::make_square(1, 0) && mv.to == russell::make_square(3, 1)) {
        found = true;
        require(russell::search::static_exchange_eval(pos, mv) >= 0, "Nxd2 recapture is genuine");
      }
    }
    require(found, "Nxd2 candidate exists");
  }
  auto pos = russell::position::from_fen("3qk3/8/8/8/8/2b5/3P4/1N2K3 b - - 0 1");
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 2);
  require(r.has_move, "check-blocked search has move");
  require(r.extensions_applied - r.check_extensions - r.singular_extensions == 0,
          "check extension blocks recapture extension");
  require(r.check_extensions > 0, "check extensions did occur in tree");
  require(p.key() == pos.key() && p == pos, "check-blocked search preserves position");
}

auto test_root_applies_no_extension() -> void
{

  {
    auto pos = russell::position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const auto r = russell::search::alpha_beta(pos, 1);
    require(r.has_move, "depth-1 root has move");
    require(r.extensions_applied == 0, "root depth-1 applies no extension");
    require(r.check_extensions == 0, "root depth-1 no check extension");
    require(r.singular_considered == 0 && r.singular_extensions == 0, "root depth-1 no singular");
  }
  {
    auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    const auto r = russell::search::alpha_beta(pos, 1);
    require(r.has_move, "tactical depth-1 root has move");
    require(r.extensions_applied == 0, "tactical root depth-1 applies no extension");
  }
}

auto test_maxply_bounded_deep_search() -> void
{

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const auto key_before = pos.key();
  const int eval_before = russell::eval::evaluate(pos);
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 8);
  require(r.has_move, "deep search terminates with move");
  require(is_legal(pos, r.best_move), "deep search move legal");
  require(r.extensions_applied <= r.nodes, "extension count bounded by nodes");
  require(r.score > -russell::search::mate_score_threshold && r.score < russell::search::mate_score_threshold,
          "deep search no false mate");
  require(p.key() == key_before && p == pos, "deep search preserves position");
  require(russell::eval::evaluate(p) == eval_before, "deep search preserves eval");
}

auto test_verification_searches_stay_consistent() -> void
{

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const auto key_before = pos.key();
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 7);
  require(r.has_move, "verification search has move");
  require(is_legal(pos, r.best_move), "verification move legal");
  require(r.probcut_cutoff <= r.probcut_considered, "probcut cutoffs bounded");
  require(r.singular_extensions <= r.singular_considered, "singular extensions bounded");
  require(r.threat_detected <= r.threat_considered, "threat detections bounded");
  require(r.extensions_applied <= r.nodes, "extensions bounded by nodes");
  require(r.score > -russell::search::mate_score_threshold && r.score < russell::search::mate_score_threshold,
          "verification search no false mate");
  require(p.key() == key_before && p == pos, "verification search preserves position");
}

auto test_extension_counter_invariants() -> void
{
  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 6);
  require(r.has_move, "invariant search has move");
  require(r.check_extensions <= r.extensions_applied, "check bounded by aggregate");
  require(r.singular_extensions <= r.singular_considered, "singular bounded by considered");
  require(r.singular_extensions <= r.extensions_applied, "singular bounded by aggregate");
  require(r.probcut_cutoff <= r.probcut_considered, "probcut bounded");
  require(r.threat_detected <= r.threat_considered, "threat bounded");
}

auto test_extension_counters_deterministic() -> void
{
  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto p1 = pos;
  const auto r1 = russell::search::alpha_beta(p1, 4);
  auto p2 = pos;
  const auto r2 = russell::search::alpha_beta(p2, 4);
  require(r2.best_move == r1.best_move, "extension counters run keeps best move");
  require(r2.score == r1.score, "extension counters run keeps score");
  require(r2.extensions_applied == r1.extensions_applied, "extensions deterministic");
  require(r2.check_extensions == r1.check_extensions, "check extensions deterministic");
  require(r2.singular_considered == r1.singular_considered, "singular considered deterministic");
  require(r2.singular_extensions == r1.singular_extensions, "singular extensions deterministic");
  require(p2.key() == pos.key() && p1.key() == pos.key(), "both runs preserve position");
}

auto test_threat_adds_no_extension() -> void
{

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 6);
  require(r.threat_detected > 0, "threat signal live in search");
  require(r.extensions_applied >= r.check_extensions + r.singular_extensions,
          "threat adds no extension of its own");
}

auto test_probcut_adds_no_extension() -> void
{

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto p = pos;
  const auto r = russell::search::alpha_beta(p, 7);
  require(r.probcut_cutoff > 0, "probcut cutoffs occurred");
  require(r.extensions_applied >= r.check_extensions + r.singular_extensions,
          "probcut adds no extension of its own");
  require(r.singular_extensions <= r.singular_considered, "singular accounting intact under probcut");
}

auto main() -> int
{
  test_eval_start();
  test_eval_material();
  test_eval_symmetry();
  test_alpha_beta_matches_minimax();
  test_alpha_beta_prunes();
  test_mate_detection();
  test_absurd_depth_bound();
  test_mate_in_one();
  test_quiescence_resolves_hanging_capture();
  test_quiescence_promotion_leaf();
  test_quiescence_checkmate_leaf();
  test_iterative_deepening();
  test_returns_sensible_moves();
  test_aborted_before_first_iteration();
  test_aborted_preserves_last_completed();
  test_futility_preserves_checking_move();
  test_futility_still_prunes_quiet();
  test_futility_decoupled_from_quiescence();
  test_futility_still_prunes_quiescence_on();
  test_futility_decoupled_mate_escape();
  test_delta_pruning_with_see();
  test_delta_still_prunes_quiet_quiescence();
  test_check_extension_finds_mate();
  test_check_extension_ordinary_no_extend();
  test_singular_gate_requires_tt_move();
  test_singular_gate_blocks_exclusion_search();
  test_singular_gate_blocks_check_extended_node();
  test_singular_gate_blocks_mate_score();
  test_singular_gate_blocks_shallow_tt_depth();
  test_singular_gate_blocks_upper_bound();
  test_singular_counters_quiet_below_min_depth();
  test_singular_counters_check_extended_below_min_depth();
  test_singular_extension_accounting_invariants();
  test_probcut_gate_min_depth();
  test_probcut_gate_pv_node();
  test_probcut_gate_in_check();
  test_probcut_gate_exclusion_and_nmp();
  test_probcut_gate_mate_scores();
  test_probcut_see_filter();
  test_probcut_capture_list_has_no_quiets();
  test_probcut_idle_below_min_depth();
  test_probcut_attempt_without_cutoff();
  test_probcut_cutoff_threshold();
  test_probcut_aborted_counts_nothing();
  test_probcut_preserves_position_and_nnue();
  test_probcut_deterministic();
  test_threat_gate_blocks_root();
  test_threat_gate_blocks_in_check();
  test_threat_gate_blocks_nmp_verification();
  test_threat_gate_blocks_exclusion_search();
  test_threat_gate_blocks_probcut_verification();
  test_threat_gate_blocks_extended_node();
  test_threat_quiet_position_no_threat();
  test_threat_tactical_capture_detected();
  test_threat_losing_capture_not_detected();
  test_threat_promotion_considered();
  test_threat_checking_capture_considered();
  test_threat_pinned_capture_not_detected();
  test_threat_preserves_position_key();
  test_threat_preserves_eval_state();
  test_threat_detection_deterministic();
  test_threat_counters_increment();
  test_threat_blocks_nmp();
  test_threat_blocks_rfp();
  test_threat_blocks_lmp();
  test_recap_fires_on_equal_exchange();
  test_recap_absent_without_recapture_square();
  test_recap_excludes_promotion_capture();
  test_recap_see_filter_unit();
  test_recap_blocked_after_check_extension();
  test_root_applies_no_extension();
  test_maxply_bounded_deep_search();
  test_verification_searches_stay_consistent();
  test_extension_counter_invariants();
  test_extension_counters_deterministic();
  test_threat_adds_no_extension();
  test_probcut_adds_no_extension();
  return 0;
}
