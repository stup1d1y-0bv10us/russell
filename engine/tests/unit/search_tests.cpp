#include "core/position.hpp"
#include "eval/evaluate.hpp"
#include "movegen/movegen.hpp"
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

auto flip(const tuna::position& pos) -> tuna::position
{
  auto out = tuna::position::empty();
  for(auto c = 0; c < tuna::color_count; ++c) {
    for(auto pt = 0; pt < tuna::piece_type_count; ++pt) {
      auto bb = pos.pieces(static_cast<tuna::color>(c), static_cast<tuna::piece_type>(pt));
      while(bb != 0) {
        const auto sq = static_cast<int>(std::countr_zero(bb));
        bb &= bb - 1;
        out.set_piece(tuna::opposite(static_cast<tuna::color>(c)),
                      static_cast<tuna::piece_type>(pt), sq ^ 56);
      }
    }
  }
  out.set_side_to_move(tuna::opposite(pos.side_to_move()));
  return out;
}

auto is_legal(const tuna::position& pos, tuna::move mv) -> bool
{
  auto copy = pos;
  for(const auto candidate : tuna::movegen::generate_legal(copy)) {
    if(candidate == mv) {
      return true;
    }
  }
  return false;
}

auto test_eval_start() -> void
{
  require(tuna::eval::evaluate(tuna::position::start()) == 0, "start eval zero");
}

auto test_eval_material() -> void
{
  auto base = tuna::position::empty();
  base.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  base.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(4, 7));
  require(tuna::eval::evaluate(base) == 0, "kings only zero");

  auto with_knight = base;
  auto with_queen = base;
  with_knight.set_piece(tuna::color::white, tuna::piece_type::knight, tuna::make_square(3, 3));
  with_queen.set_piece(tuna::color::white, tuna::piece_type::queen, tuna::make_square(3, 3));
  require(tuna::eval::evaluate(with_knight) > tuna::eval::evaluate(base),
          "extra white knight improves");
  require(tuna::eval::evaluate(with_queen) > tuna::eval::evaluate(with_knight),
          "extra white queen improves more");

  auto with_black_rook = base;
  with_black_rook.set_piece(tuna::color::black, tuna::piece_type::rook, tuna::make_square(0, 6));
  require(tuna::eval::evaluate(with_black_rook) < tuna::eval::evaluate(base),
          "black rook hurts white");
}

auto test_eval_symmetry() -> void
{
  const auto positions = std::vector<std::string>{
    tuna::position::start().fen(),
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1",
  };
  for(const auto& fen : positions) {
    const auto pos = tuna::position::from_fen(fen);
    require(tuna::eval::evaluate(flip(pos)) == -tuna::eval::evaluate(pos),
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
    auto pos_plain = tuna::position::from_fen(fen);
    const auto plain = tuna::search::minimax(pos_plain, depth);
    auto pos_exact = tuna::position::from_fen(fen);
    const auto exact = tuna::search::alpha_beta(pos_exact, depth, false, false, false, false, false);
    require(exact.has_move == plain.has_move, ("exact has_move match " + fen).c_str());
    require(is_legal(pos_exact, exact.best_move) || !exact.has_move, ("exact best move legal " + fen).c_str());
    if(fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" || fen == "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1" || fen == "6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1" || fen == "k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1") {
      require(exact.score == plain.score, ("exact score match " + fen).c_str());
      if(exact.has_move && !(exact.best_move == plain.best_move)) require(exact.score == plain.score, ("exact alternative move same score " + fen).c_str());
    } else {
      require(exact.score > -29000 && exact.score < 29000, ("exact score not false mate " + fen).c_str());
      if(exact.score == plain.score && exact.has_move && !(exact.best_move == plain.best_move)) require(exact.score == plain.score, ("exact alternative same score " + fen).c_str());
    }
    auto pos_opt = tuna::position::from_fen(fen);
    const auto ab = tuna::search::alpha_beta(pos_opt, depth, true, true, false, true, false);
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
  auto pos = tuna::position::start();
  const auto plain = tuna::search::minimax(pos, 4);
  const auto ab = tuna::search::alpha_beta(pos, 4, true, true, false, true, false);
  require(ab.score == plain.score, "prune score match");
  require(ab.nodes < plain.nodes, "alpha-beta visits fewer nodes");
}

auto test_mate_detection() -> void
{
  auto mated = tuna::position::from_fen("k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1");
  const auto mated_result = tuna::search::alpha_beta(mated, 1);
  require(!mated_result.has_move, "mated has no move");
  require(mated_result.score < -20000, "mated score is mate for mover");

  auto stalemate = tuna::position::from_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
  const auto stalemate_result = tuna::search::alpha_beta(stalemate, 1);
  require(!stalemate_result.has_move, "stalemate has no move");
  require(stalemate_result.score == 0, "stalemate score zero");
}

auto test_mate_in_one() -> void
{
  auto pos = tuna::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  const auto result = tuna::search::alpha_beta(pos, 1);
  require(result.has_move, "mate in one has move");
  require(result.score > 20000, "mate in one score");
  require(result.best_move.from == tuna::make_square(3, 0), "mate move from d1");
  require(result.best_move.to == tuna::make_square(3, 7), "mate move to d8");
  require(is_legal(pos, result.best_move), "mate move legal");
}

auto test_quiescence_resolves_hanging_capture() -> void
{
  auto pos = tuna::position::from_fen("3rk3/8/8/3q4/3R4/8/8/4K3 w - - 0 1");
  const auto static_leaf = tuna::search::alpha_beta(pos, 1, false, false, false);
  auto qpos = tuna::position::from_fen("3rk3/8/8/3q4/3R4/8/8/4K3 w - - 0 1");
  const auto qsearch = tuna::search::alpha_beta(qpos, 1, false, false, true);
  require(static_leaf.score > 0, "static leaf overvalues hanging queen capture");
  require(qsearch.score < 0, "quiescence sees queen recapture");
}

auto test_quiescence_promotion_leaf() -> void
{

  auto pos = tuna::position::from_fen("8/2P5/8/8/8/3k4/8/4K3 w - - 0 1");
  const auto result = tuna::search::alpha_beta(pos, 1);
  require(result.has_move, "promotion tactic has move");
  require(result.best_move.from == tuna::make_square(2, 6), "promotion from c7");
  require(result.best_move.to == tuna::make_square(2, 7), "promotion to c8");
  require(result.score > 400, "promotion valued as winning");
}

auto test_quiescence_checkmate_leaf() -> void
{

  auto pos = tuna::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  const auto result = tuna::search::alpha_beta(pos, 1);
  require(result.has_move, "mate in one has move");
  require(result.best_move.from == tuna::make_square(3, 0), "mate move from d1");
  require(result.best_move.to == tuna::make_square(3, 7), "mate move to d8");
  require(result.score > 20000, "mate in one score");
}

auto test_iterative_deepening() -> void
{
  auto pos = tuna::position::start();
  const auto id = tuna::search::iterative_deepening(pos, 3);
  const auto ab = tuna::search::alpha_beta(pos, 3);
  require(id.has_move, "id has move");
  require(id.best_move == ab.best_move, "id best move matches depth search");
  require(id.score == ab.score, "id score matches depth search");
  require(is_legal(pos, id.best_move), "id best move legal");

  auto mate = tuna::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  const auto id_mate = tuna::search::iterative_deepening(mate, 2);
  require(id_mate.best_move.from == tuna::make_square(3, 0), "id mate from d1");
  require(id_mate.best_move.to == tuna::make_square(3, 7), "id mate to d8");
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
    auto pos = tuna::position::from_fen(fen);
    const auto result = tuna::search::alpha_beta(pos, 3);
    require(result.has_move, ("sensible move exists " + fen).c_str());
    require(is_legal(pos, result.best_move), ("sensible move legal " + fen).c_str());
  }
}

auto test_aborted_before_first_iteration() -> void
{
  auto pos = tuna::position::start();
  tuna::search::search_stopper stopper;
  stopper.stop.store(true, std::memory_order_relaxed);
  tuna::search::search_limits limits;
  limits.depth = 4;
  tuna::search::transposition_table tt;
  const auto result = tuna::search::search(pos, limits, stopper, tt);
  require(result.has_move, "aborted before first iteration still has emergency move");
  require(is_legal(pos, result.best_move), "emergency move is legal");
  auto copy = pos;
  const auto legal = tuna::movegen::generate_legal(copy);
  require(legal.size() != 0, "position has legal moves");
  require(result.best_move == legal[0], "emergency fallback is deterministic legal[0]");
  require(result.depth == 0, "emergency fallback depth 0");

  auto pos2 = tuna::position::start();
  tuna::search::search_stopper stopper2;
  stopper2.stop.store(true, std::memory_order_relaxed);
  tuna::search::transposition_table tt2;
  const auto result2 = tuna::search::search(pos2, limits, stopper2, tt2);
  require(result.best_move == result2.best_move, "emergency deterministic");
}

auto test_aborted_preserves_last_completed() -> void
{
  auto pos = tuna::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

  tuna::search::search_stopper s1;
  tuna::search::search_limits l1;
  l1.depth = 1;
  tuna::search::transposition_table tt1;
  auto pos1 = pos;
  const auto completed1 = tuna::search::search(pos1, l1, s1, tt1);
  require(completed1.has_move, "depth1 completed has move");
  require(is_legal(pos, completed1.best_move), "depth1 move legal");

  tuna::search::search_stopper stopper;
  tuna::search::search_limits limits;
  limits.depth = 5;
  limits.hard_time_ms = 1;
  tuna::search::transposition_table tt;
  auto pos2 = pos;
  const auto aborted = tuna::search::search(pos2, limits, stopper, tt);
  require(aborted.has_move, "aborted search still has move");
  require(is_legal(pos, aborted.best_move), "aborted preserved move is legal");

  tuna::search::search_stopper stopper2;
  tuna::search::search_limits limits2;
  limits2.depth = 5;
  tuna::search::transposition_table tt2;
  bool first_done = false;
  tuna::search::search_result first_result{};
  auto on_iter = [&](const tuna::search::search_result& r){
    if(!first_done){
      first_result = r;
      first_done = true;
      stopper2.stop.store(true, std::memory_order_relaxed);
    }
  };
  auto pos3 = pos;
  const auto stopped = tuna::search::search(pos3, limits2, stopper2, tt2, on_iter);
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

  auto pos = tuna::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  auto pos_no_fut = tuna::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  auto pos_fut = tuna::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  const int depth = 2;
  const auto no_fut = tuna::search::alpha_beta(pos_no_fut, depth, true, true, true, false);
  require(no_fut.has_move, "no-futility has move");
  require(no_fut.score > tuna::search::mate_score_threshold, "no-futility finds mate");
  require(is_legal(pos, no_fut.best_move), "no-futility mate move legal");
  const auto with_fut = tuna::search::alpha_beta(pos_fut, depth, true, true, true, true);
  require(with_fut.has_move, "with-futility has move");
  require(with_fut.score > tuna::search::mate_score_threshold, "with-futility still finds mate");
  require(with_fut.best_move == no_fut.best_move, "futility preserves checking mate move");
  require(with_fut.score == no_fut.score, "futility preserves mate score");
}

auto test_futility_still_prunes_quiet() -> void
{

  auto pos_fut = tuna::position::from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
  auto pos_no_fut = tuna::position::from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
  const int depth = 1;
  const auto with_fut = tuna::search::alpha_beta(pos_fut, depth, true, true, true, true);
  const auto no_fut = tuna::search::alpha_beta(pos_no_fut, depth, true, true, true, false);
  require(with_fut.has_move && no_fut.has_move, "both have move");
  require(is_legal(pos_fut, with_fut.best_move), "quiet futility move legal");

  require(with_fut.nodes <= no_fut.nodes, "futility still prunes in quiet position");

  require(std::abs(with_fut.score - no_fut.score) < 50, "quiet futility score close");
}

auto test_delta_pruning_with_see() -> void
{

  auto pos = tuna::position::from_fen("2r5/1P6/8/8/8/8/8/k1K5 w - - 0 1");
  auto pos2 = tuna::position::from_fen("2r5/1P6/8/8/8/8/8/k1K5 w - - 0 1");
  const int depth = 1;
  const auto with_delta = tuna::search::alpha_beta(pos, depth, true, true, true, true);
  const auto no_quies = tuna::search::alpha_beta(pos2, depth, true, true, false, true);

  require(with_delta.has_move, "delta with SEE has move");
  require(is_legal(pos, with_delta.best_move), "delta promotion capture legal");

  bool is_promo_capture = (with_delta.best_move.from == tuna::make_square(1,6) && with_delta.best_move.to == tuna::make_square(2,7) && with_delta.best_move.flag == tuna::move_flag::promotion_capture);

  require(with_delta.score > 400 || is_promo_capture, "delta preserves defended promotion capture");

  require(with_delta.score > 0, "delta quiescence sees winning capture");
}

auto test_delta_still_prunes_quiet_quiescence() -> void
{

  auto pos = tuna::position::from_fen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1");
  auto pos2 = tuna::position::from_fen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1");
  const int depth = 1;
  const auto with_q = tuna::search::alpha_beta(pos, depth, true, true, true, true);
  const auto without_q = tuna::search::alpha_beta(pos2, depth, true, true, false, true);
  require(with_q.has_move && without_q.has_move, "both quiescence variants have move");

  require(with_q.nodes < 10000, "delta quiescence still bounded in quiet position");
  require(is_legal(pos, with_q.best_move), "quiet delta move legal");
}

auto test_check_extension_finds_mate() -> void
{

  auto pos = tuna::position::from_fen("4k3/8/8/8/8/8/4q3/3QK3 w - - 0 1");

  {
    auto tmp = pos;
    bool in_chk = false;
    auto king_bb = tmp.pieces(tuna::color::white, tuna::piece_type::king);
    if(king_bb != 0) {
      int ks = static_cast<int>(std::countr_zero(king_bb));
      in_chk = tuna::movegen::is_square_attacked(tmp, ks, tuna::color::black);
    }
    require(in_chk, "test position is in check");
  }
  const int depth = 1;
  const auto result = tuna::search::alpha_beta(pos, depth);
  require(result.has_move, "check extension has move");
  require(is_legal(pos, result.best_move), "check evasion move legal");

  bool is_capture_queen = (result.best_move.from == tuna::make_square(3,0) && result.best_move.to == tuna::make_square(4,1));

  require(result.score > 0 || is_capture_queen, "check extension finds winning capture");

  auto quiet = tuna::position::from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
  auto quiet_pos = quiet;
  const auto quiet_result = tuna::search::alpha_beta(quiet_pos, depth);
  require(quiet_result.has_move, "quiet position has move");

  require(quiet_result.score < tuna::search::mate_score_threshold, "quiet position not mate");
}

auto test_check_extension_ordinary_no_extend() -> void
{

  auto pos = tuna::position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  const auto ab = tuna::search::alpha_beta(pos, 1);
  const auto id = tuna::search::iterative_deepening(pos, 1);
  require(ab.has_move && id.has_move, "quiet has move");
  require(ab.best_move == id.best_move, "quiet no extension consistent");
  require(ab.score == id.score, "quiet score consistent");
  require(ab.score < tuna::search::mate_score_threshold && ab.score > -tuna::search::mate_score_threshold, "quiet not mate");
}

auto main() -> int
{
  test_eval_start();
  test_eval_material();
  test_eval_symmetry();
  test_alpha_beta_matches_minimax();
  test_alpha_beta_prunes();
  test_mate_detection();
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
  test_delta_pruning_with_see();
  test_delta_still_prunes_quiet_quiescence();
  test_check_extension_finds_mate();
  test_check_extension_ordinary_no_extend();
  return 0;
}