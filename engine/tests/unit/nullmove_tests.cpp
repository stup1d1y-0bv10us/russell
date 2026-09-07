#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
#include "search/transposition_table.hpp"

#include <cstdlib>
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

auto test_null_move_roundtrip() -> void
{
  auto pos = tuna::position::from_fen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq e3 0 1");
  const auto original = pos;
  const auto before_key = pos.key();
  const auto st = pos.make_null_move();
  require(pos.side_to_move() == tuna::color::black, "null move flips side to move");
  require(pos.en_passant_square() == tuna::no_square, "null move clears en passant");
  require(pos.key() != before_key, "null move changes the zobrist key");
  require(!(pos == original), "null move produces a different position");
  pos.unmake_null_move(st);
  require(pos == original, "unmake null move restores the position");
  require(pos.key() == before_key, "unmake null move restores the key");
}

auto test_tactical_exactness() -> void
{
  const auto cases = std::vector<std::pair<std::string, int>>{
    {"7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", 4},
    {"6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1", 4},
    {"k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1", 4},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4},
  };
  for(const auto& [fen, depth] : cases) {
    auto plain_pos = tuna::position::from_fen(fen);
    const auto plain = tuna::search::minimax(plain_pos, depth);
    auto exact_pos = tuna::position::from_fen(fen);
    const auto exact = tuna::search::alpha_beta(exact_pos, depth, false, false, false, false, false);
    require(exact.score == plain.score, ("exact score matches minimax " + fen).c_str());
    require(exact.has_move == plain.has_move, ("exact has_move matches " + fen).c_str());
    if(exact.has_move) {
      require(is_legal(exact_pos, exact.best_move), ("exact best move legal " + fen).c_str());
      if(!(exact.best_move == plain.best_move)) require(exact.score == plain.score, ("exact alternative same score " + fen).c_str());
    }
    require(exact.nodes <= plain.nodes, ("exact prunes vs minimax " + fen).c_str());
    auto null_pos = tuna::position::from_fen(fen);
    auto ab_pos = tuna::position::from_fen(fen);
    const auto null = tuna::search::alpha_beta(null_pos, depth, true, true, false, false, false);
    const auto ab = tuna::search::alpha_beta(ab_pos, depth, false, true, false, false, false);
    require(null.has_move == plain.has_move, ("null has_move matches minimax " + fen).c_str());
    require(ab.has_move == plain.has_move, ("no-null has_move matches minimax " + fen).c_str());
    if(null.has_move) require(is_legal(null_pos, null.best_move), ("null best move legal " + fen).c_str());
    if(ab.has_move) require(is_legal(ab_pos, ab.best_move), ("no-null best move legal " + fen).c_str());
    if(std::abs(plain.score) > 20000) {
      require(std::abs(null.score) > 20000, ("null preserves mate " + fen).c_str());
      require((null.score > 0) == (plain.score > 0), ("null mate sign " + fen).c_str());
      require(std::abs(ab.score) > 20000, ("no-null preserves mate " + fen).c_str());
    } else {
      require(null.score > -29000 && null.score < 29000, ("null not false mate " + fen).c_str());
      require(ab.score > -29000 && ab.score < 29000, ("no-null not false mate " + fen).c_str());
    }
    require(null.nodes <= plain.nodes, ("null search prunes against minimax " + fen).c_str());
    if(null.has_move && ab.has_move && null.score == ab.score && null.score == plain.score) {
      if(!(null.best_move == plain.best_move)) require(is_legal(null_pos, null.best_move), ("null alternative same score " + fen).c_str());
    }
  }
}

auto test_quiet_reduction() -> void
{

  const auto cases = std::vector<std::pair<std::string, int>>{
    {"4k3/8/8/8/8/8/4R3/4K3 w - - 0 1", 6},
    {"8/8/8/4k3/8/8/8/3R2K1 b - - 0 1", 5},
  };
  for(const auto& [fen, depth] : cases) {
    auto null_pos = tuna::position::from_fen(fen);
    auto ab_pos = tuna::position::from_fen(fen);
    const auto null = tuna::search::alpha_beta(null_pos, depth, true);
    const auto ab = tuna::search::alpha_beta(ab_pos, depth, false);
    require(null.nodes < ab.nodes, ("null move reduces nodes on quiet position " + fen).c_str());
    require((null.score > 0) == (ab.score > 0),
            ("null move preserves win/loss sign on " + fen).c_str());
    auto legal_check = tuna::position::from_fen(fen);
    require(is_legal(legal_check, null.best_move), ("null best move legal " + fen).c_str());
  }
}

auto test_null_move_mate_preservation() -> void
{

  const auto mate_fen = std::string{"6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1"};
  auto pos_null = tuna::position::from_fen(mate_fen);
  auto pos_plain = tuna::position::from_fen(mate_fen);
  auto pos_no_null = tuna::position::from_fen(mate_fen);
  const int depth = 3;
  const auto plain = tuna::search::minimax(pos_plain, depth);
  const auto with_null = tuna::search::alpha_beta(pos_null, depth, true, true, false);
  const auto no_null = tuna::search::alpha_beta(pos_no_null, depth, false, true, false);
  require(plain.score > tuna::search::mate_score_threshold, "plain minimax finds mate");
  require(with_null.score > tuna::search::mate_score_threshold, "null-enabled still finds mate");
  require(with_null.score == plain.score, "null-enabled mate score equals minimax");
  require(no_null.score == plain.score, "no-null mate score equals minimax");
  require(with_null.best_move == plain.best_move, "null-enabled best move equals minimax");
  require(is_legal(pos_null, with_null.best_move), "mate move legal");

  const auto quiet_fen = std::string{"4k3/8/8/8/8/8/4R3/4K3 w - - 0 1"};
  auto q_null = tuna::position::from_fen(quiet_fen);
  auto q_no_null = tuna::position::from_fen(quiet_fen);
  const auto q_with = tuna::search::alpha_beta(q_null, 5, true);
  const auto q_without = tuna::search::alpha_beta(q_no_null, 5, false);
  require(q_with.nodes < q_without.nodes, "normal null-move still reduces nodes on quiet position");
  require(is_legal(q_null, q_with.best_move), "quiet null best move legal");
}

auto test_null_move_zugzwang_preservation() -> void
{

  const auto sparse_cases = std::vector<std::string>{
    "4k3/8/8/8/8/4P3/4K3/8 w - - 0 1",
    "8/8/4k3/3P4/4K3/8/8/8 w - - 0 1",
    "4k3/8/8/8/3P4/8/4K3/8 w - - 0 1"
  };
  for(const auto& fen : sparse_cases) {
    auto pos_null = tuna::position::from_fen(fen);
    auto pos_plain = tuna::position::from_fen(fen);
    const int depth = 4;
    const auto with_null = tuna::search::alpha_beta(pos_null, depth, true, true, false);
    const auto no_null = tuna::search::alpha_beta(pos_plain, depth, false, true, false);
    auto plain_pos2 = tuna::position::from_fen(fen);
    const auto plain = tuna::search::minimax(plain_pos2, depth);
    auto exact_pos = tuna::position::from_fen(fen);
    const auto exact = tuna::search::alpha_beta(exact_pos, depth, false, false, false, false, false);
    require(exact.score == plain.score, ("zugzwang exact matches minimax " + fen).c_str());
    require(with_null.score == no_null.score, ("zugzwang sparse score consistent " + fen).c_str());
    if(!(with_null.best_move == no_null.best_move)) require(with_null.score == no_null.score, ("zugzwang alternative same score " + fen).c_str());
    if(std::abs(plain.score) > 20000) {
      require(std::abs(with_null.score) > 20000, ("zugzwang mate preserved " + fen).c_str());
    } else if(exact.score == plain.score) {
      if(std::abs(exact.score) > 20000) require(std::abs(with_null.score) > 20000, ("zugzwang mate " + fen).c_str());
    }
    require(is_legal(pos_null, with_null.best_move) || !with_null.has_move, ("zugzwang move legal " + fen).c_str());
  }

  const auto mid_fen = std::string{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"};
  auto mid_null = tuna::position::from_fen(mid_fen);
  auto mid_no_null = tuna::position::from_fen(mid_fen);
  const auto mid_with = tuna::search::alpha_beta(mid_null, 4, true);
  const auto mid_without = tuna::search::alpha_beta(mid_no_null, 4, false);
  require(mid_with.nodes < mid_without.nodes, "null still reduces nodes in middlegame");
  require(is_legal(mid_null, mid_with.best_move), "middlegame null best move legal");
}

}

auto main() -> int
{
  test_null_move_roundtrip();
  test_tactical_exactness();
  test_quiet_reduction();
  test_null_move_mate_preservation();
  test_null_move_zugzwang_preservation();
  return 0;
}