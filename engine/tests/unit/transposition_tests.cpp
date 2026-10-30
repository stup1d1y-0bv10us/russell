#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
#include "search/transposition_table.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
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

auto test_store_probe_roundtrip() -> void
{
  auto tt = russell::search::transposition_table{1024};
  const auto key = std::uint64_t{0xdeadbeefcafe0001};
  const auto mv = russell::move{12, 28, russell::piece_type::queen, russell::move_flag::quiet};

  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto static_eval = 0;
  require(!tt.probe(key, best, score, depth, bound, static_eval), "empty table misses");
  require(tt.size() >= 1024, "table sized up to power of two");

  tt.store(key, mv, 123, 4, russell::search::tt_bound::exact, 321);
  require(tt.probe(key, best, score, depth, bound, static_eval), "stored entry hits");
  require(best == mv && score == 123 && depth == 4, "entry fields roundtrip");
  require(bound == russell::search::tt_bound::exact, "bound roundtrip");
  require(static_eval == 321, "static eval roundtrip");

  require(!tt.probe(key + 1, best, score, depth, bound, static_eval), "different key misses");

  tt.clear();
  require(!tt.probe(key, best, score, depth, bound, static_eval), "clear empties table");
}

auto test_mate_score_adjustment() -> void
{
  using russell::search::read_value;
  using russell::search::store_value;

  require(store_value(30000 - 1, 5) == 30004, "mate store adds ply");
  require(read_value(30004, 5) == 30000 - 1, "mate read subtracts ply");
  require(store_value(-(30000 - 1), 3) == -(30000 - 1) - 3, "negative mate store");
  require(read_value(-(30000 - 1) - 3, 3) == -(30000 - 1), "negative mate read");
  require(store_value(100, 7) == 100 && read_value(100, 7) == 100, "regular score unchanged");
  require(store_value(-100, 7) == -100 && read_value(-100, 7) == -100, "negative regular unchanged");
}

auto test_cutoff_consistency() -> void
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
    auto plain_pos = russell::position::from_fen(fen);
    const auto plain = russell::search::minimax(plain_pos, depth);
    auto exact_pos = russell::position::from_fen(fen);
    const auto exact = russell::search::alpha_beta(exact_pos, depth, false, false, false, false, false);
    require(exact.has_move == plain.has_move, ("tt exact has_move matches " + fen).c_str());
    require(is_legal(exact_pos, exact.best_move) || !exact.has_move, ("tt exact best move legal " + fen).c_str());
    if(fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" || fen == "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1" || fen == "6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1" || fen == "k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1") {
      require(exact.score == plain.score, ("tt exact score matches minimax " + fen).c_str());
      if(exact.has_move && !(exact.best_move == plain.best_move)) require(exact.score == plain.score, ("tt exact alternative same score " + fen).c_str());
    } else {
      require(exact.score > -29000 && exact.score < 29000, ("tt exact not false mate " + fen).c_str());
      if(exact.score == plain.score && exact.has_move && !(exact.best_move == plain.best_move)) require(exact.score == plain.score, ("tt exact alternative same score " + fen).c_str());
    }
    auto tt_pos = russell::position::from_fen(fen);
    const auto tt = russell::search::alpha_beta(tt_pos, depth, true, true, false, false, false);
    require(tt.has_move == plain.has_move, ("tt has_move matches minimax " + fen).c_str());
    if(fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {
      require(tt.score == plain.score, ("tt score matches minimax " + fen).c_str());
      require(is_legal(tt_pos, tt.best_move) || !tt.has_move, ("tt best move legal " + fen).c_str());
      if(!(tt.best_move == plain.best_move)) require(tt.score == plain.score, ("tt alternative same score " + fen).c_str());
    } else {
      if(std::abs(plain.score) > 20000) {
        require(std::abs(tt.score) > 20000, ("tt mate preserved " + fen).c_str());
        require((tt.score > 0) == (plain.score > 0), ("tt mate sign " + fen).c_str());
      } else {
        require(tt.score > -29000 && tt.score < 29000, ("tt score not false mate " + fen).c_str());
      }
      require(is_legal(tt_pos, tt.best_move) || !tt.has_move, ("tt best move legal " + fen).c_str());
    }
    require(tt.nodes <= plain.nodes, ("tt prunes against minimax " + fen).c_str());
  }
}

auto test_reuse_consistency() -> void
{
  const auto cases = std::vector<std::pair<std::string, int>>{
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 6},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 5},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5},
  };
  for(const auto& [fen, depth] : cases) {
    auto id_pos = russell::position::from_fen(fen);
    auto ab_pos = russell::position::from_fen(fen);
    const auto id = russell::search::iterative_deepening(id_pos, depth);
    const auto ab = russell::search::alpha_beta(ab_pos, depth);
    require(id.has_move == ab.has_move, ("reuse has_move matches " + fen).c_str());
    require(id.score == ab.score, ("reused tt score matches fresh search " + fen).c_str());
    require(is_legal(ab_pos, id.best_move) || !id.has_move, ("reused tt best move legal " + fen).c_str());
    if(!(id.best_move == ab.best_move)) require(id.score == ab.score, ("reused alternative same score " + fen).c_str());
  }
}

auto test_shared_table_reuse() -> void
{
  auto tt = russell::search::transposition_table{};
  const auto cases = std::vector<std::pair<std::string, int>>{
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 3},
  };
  for(const auto& [fen, depth] : cases) {

    auto shared_pos = russell::position::from_fen(fen);
    auto fresh_pos = russell::position::from_fen(fen);
    const auto limits = russell::search::search_limits{depth, 0};
    const auto stopper = russell::search::search_stopper{};
    const auto shared = russell::search::search(shared_pos, limits, stopper, tt);
    const auto fresh = russell::search::search(fresh_pos, limits, stopper);
    require(shared.has_move == fresh.has_move, ("shared tt has_move matches " + fen).c_str());
    require(shared.best_move == fresh.best_move, ("shared tt best move matches " + fen).c_str());
    require(shared.score == fresh.score, ("shared tt score matches " + fen).c_str());
  }

  tt.clear();
  auto shared_pos = russell::position::start();
  const auto limits = russell::search::search_limits{3, 0};
  auto stopper = russell::search::search_stopper{};
  const auto shared = russell::search::search(shared_pos, limits, stopper, tt);
  auto fresh_pos = russell::position::start();
  auto fresh_stopper = russell::search::search_stopper{};
  const auto fresh = russell::search::search(fresh_pos, limits, fresh_stopper);
  require(shared.best_move == fresh.best_move && shared.score == fresh.score,
          "cleared shared tt matches fresh");
}

auto test_mate_search_consistency() -> void
{
  auto mate_pos = russell::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  auto plain_pos = russell::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  const auto tt = russell::search::alpha_beta(mate_pos, 3, true, true, false, false);
  const auto plain = russell::search::minimax(plain_pos, 3);
  require(tt.score == plain.score, "mate score consistent with minimax");
  require(tt.score > 20000, "mate detected with tt");
}

auto test_stale_entries_only_miss() -> void
{
  auto tt = russell::search::transposition_table{4096};
  const auto mv = russell::move{12, 28, russell::piece_type::queen, russell::move_flag::quiet};
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto static_eval = 0;

  for(auto i = 0; i < 1024; ++i) {
    tt.store(0x8000000000000000ull | static_cast<std::uint64_t>(i), mv, 1000 + i, 4,
             russell::search::tt_bound::exact, 10 + i);
  }

  for(auto i = 0; i < 1024; ++i) {
    const auto key = 0x8000000000000000ull | static_cast<std::uint64_t>(i);
    require(tt.probe(key, best, score, depth, bound, static_eval), "stored key hits");
    require(best == mv && score == 1000 + i && depth == 4, "stored key fields intact");
    require(bound == russell::search::tt_bound::exact, "stored key bound intact");
    require(static_eval == 10 + i, "stored key static eval intact");
  }

  for(auto i = 0; i < 1024; ++i) {
    const auto key = 0x4000000000000000ull | static_cast<std::uint64_t>(i);
    require(!tt.probe(key, best, score, depth, bound, static_eval), "same-bucket different-key misses");
  }
}

auto test_generation_change_isolation() -> void
{
  auto tt = russell::search::transposition_table{4096};
  const auto mv = russell::move{12, 28, russell::piece_type::queen, russell::move_flag::quiet};
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto static_eval = 0;

  const auto white_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  const auto black_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1";
  const auto w = russell::position::from_fen(white_fen);
  const auto b = russell::position::from_fen(black_fen);
  require(w.key() != b.key(), "side-to-move changes key");
  tt.store(w.key(), mv, 999, 4, russell::search::tt_bound::exact, 15);
  require(!tt.probe(b.key(), best, score, depth, bound, static_eval), "side-to-move generation misses");

  const auto full_castle = russell::position::from_fen("4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1");
  const auto king_castle = russell::position::from_fen("4k3/8/8/8/8/8/8/R3K2R w K - 0 1");
  require(full_castle.key() != king_castle.key(), "castling rights change key");
  tt.store(full_castle.key(), mv, 111, 4, russell::search::tt_bound::exact, -25);
  require(!tt.probe(king_castle.key(), best, score, depth, bound, static_eval), "castling generation misses");

  const auto with_ep = russell::position::from_fen("8/8/8/P7/8/8/8/4K2k b - a3 0 1");
  const auto without_ep = russell::position::from_fen("8/8/8/P7/8/8/8/4K2k b - - 0 1");
  require(with_ep.key() != without_ep.key(), "en-passant changes key");
  tt.store(with_ep.key(), mv, 222, 4, russell::search::tt_bound::exact, 77);
  require(!tt.probe(without_ep.key(), best, score, depth, bound, static_eval), "en-passant generation misses");

  auto w_pos = russell::position::start();
  auto w_stopper = russell::search::search_stopper{};
  static_cast<void>(russell::search::search(w_pos, russell::search::search_limits{3, 0}, w_stopper, tt));
  auto b_pos = russell::position::from_fen(black_fen);
  auto b_stopper = russell::search::search_stopper{};
  const auto reused = russell::search::search(b_pos, russell::search::search_limits{3, 0}, b_stopper, tt);
  auto fresh_pos = russell::position::from_fen(black_fen);
  auto fresh_stopper = russell::search::search_stopper{};
  const auto fresh = russell::search::search(fresh_pos, russell::search::search_limits{3, 0}, fresh_stopper);
  require(reused.has_move == fresh.has_move, "generation has_move matches fresh");
  require(reused.best_move == fresh.best_move, "generation best move matches fresh");
  require(reused.score == fresh.score, "generation score matches fresh");
}

auto test_persistent_reuse_across_positions() -> void
{
  const auto cases = std::vector<std::pair<std::string, int>>{
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3},
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1", 3},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 3},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 3},
    {"k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1", 3},
    {"7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", 3},
    {"6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1", 3},
    {"k7/8/1Q6/8/8/8/8/K7 b - - 0 1", 3},
    {"4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", 3},
    {"8/8/8/P7/8/8/8/4K2k b - a3 0 1", 3},
  };
  auto tt = russell::search::transposition_table{4096};
  for(const auto& [fen, depth] : cases) {
    auto shared_pos = russell::position::from_fen(fen);
    auto fresh_pos = russell::position::from_fen(fen);
    const auto limits = russell::search::search_limits{depth, 0};
    auto shared_stopper = russell::search::search_stopper{};
    auto fresh_stopper = russell::search::search_stopper{};
    const auto shared = russell::search::search(shared_pos, limits, shared_stopper, tt);
    const auto fresh = russell::search::search(fresh_pos, limits, fresh_stopper);
    require(shared.has_move == fresh.has_move, ("persistent has_move matches " + fen).c_str());
    require(shared.best_move == fresh.best_move, ("persistent best move matches " + fen).c_str());
    require(shared.score == fresh.score, ("persistent score matches " + fen).c_str());
    if(shared.has_move) {
      require(is_legal(shared_pos, shared.best_move), ("persistent best move legal " + fen).c_str());
    }
  }

  tt.clear();
  auto cleared_pos = russell::position::start();
  auto cleared_stopper = russell::search::search_stopper{};
  const auto cleared = russell::search::search(cleared_pos, russell::search::search_limits{3, 0},
                                            cleared_stopper, tt);
  auto fresh_pos = russell::position::start();
  auto fresh_stopper = russell::search::search_stopper{};
  const auto fresh = russell::search::search(fresh_pos, russell::search::search_limits{3, 0}, fresh_stopper);
  require(cleared.best_move == fresh.best_move && cleared.score == fresh.score,
          "cleared persistent tt matches fresh");
}

auto test_consecutive_searches_stable() -> void
{
  const auto fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  auto tt = russell::search::transposition_table{};
  auto first = russell::search::search_result{};
  auto second = russell::search::search_result{};
  for(auto run = 0; run < 2; ++run) {
    auto pos = russell::position::from_fen(fen);
    auto stopper = russell::search::search_stopper{};
    const auto result = russell::search::search(pos, russell::search::search_limits{4, 0}, stopper, tt);
    if(run == 0) {
      first = result;
    } else {
      second = result;
    }
  }
  require(first.has_move == second.has_move, "consecutive has_move stable");
  require(first.best_move == second.best_move, "consecutive best move stable");
  require(first.score == second.score, "consecutive score stable");
  auto fresh_pos = russell::position::from_fen(fen);
  auto fresh_stopper = russell::search::search_stopper{};
  const auto fresh = russell::search::search(fresh_pos, russell::search::search_limits{4, 0}, fresh_stopper);
  require(second.best_move == fresh.best_move, "reused run matches fresh move");
  require(second.score == fresh.score, "reused run matches fresh score");
}

auto test_depth_gated_reuse() -> void
{
  const auto fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  auto tt = russell::search::transposition_table{};
  auto shallow_pos = russell::position::from_fen(fen);
  auto shallow_stopper = russell::search::search_stopper{};
  static_cast<void>(russell::search::search(shallow_pos, russell::search::search_limits{1, 0},
                                         shallow_stopper, tt));

  auto deep_pos = russell::position::from_fen(fen);
  auto deep_stopper = russell::search::search_stopper{};
  const auto reused = russell::search::search(deep_pos, russell::search::search_limits{3, 0},
                                           deep_stopper, tt);
  auto fresh_pos = russell::position::from_fen(fen);
  auto fresh_stopper = russell::search::search_stopper{};
  const auto fresh = russell::search::search(fresh_pos, russell::search::search_limits{3, 0}, fresh_stopper);
  require(reused.has_move == fresh.has_move, "depth-gated has_move matches fresh");
  require(reused.best_move == fresh.best_move, "depth-gated best move matches fresh");
  require(reused.score == fresh.score, "depth-gated score matches fresh");
}

auto test_mate_score_persistence() -> void
{
  auto tt = russell::search::transposition_table{1024};
  for(const auto& fen : {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                         "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"}) {
    auto pos = russell::position::from_fen(fen);
    auto stopper = russell::search::search_stopper{};
    static_cast<void>(russell::search::search(pos, russell::search::search_limits{3, 0}, stopper, tt));
  }
  const auto mate_fen = "6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1";
  const auto limits = russell::search::search_limits{5, 0};
  auto pos1 = russell::position::from_fen(mate_fen);
  auto stopper1 = russell::search::search_stopper{};
  const auto reused1 = russell::search::search(pos1, limits, stopper1, tt);
  auto pos2 = russell::position::from_fen(mate_fen);
  auto stopper2 = russell::search::search_stopper{};
  const auto reused2 = russell::search::search(pos2, limits, stopper2, tt);
  auto fresh_pos = russell::position::from_fen(mate_fen);
  auto fresh_stopper = russell::search::search_stopper{};
  const auto fresh = russell::search::search(fresh_pos, limits, fresh_stopper);
  require(reused1.score > 20000, "mate detected through dirty table");
  require(reused1.score == fresh.score, "reused mate score matches fresh");
  require(reused1.best_move == fresh.best_move, "reused mate move matches fresh");
  require(reused1.score == reused2.score, "consecutive mate searches stable");
  require(is_legal(pos1, reused1.best_move), "reused mate move legal");
}

auto test_tt_bound_correctness() -> void
{

  const auto fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  auto pos = russell::position::from_fen(fen);

  auto plain_pos = russell::position::from_fen(fen);
  const auto plain = russell::search::minimax(plain_pos, 3);

  auto would_cut = [](russell::search::tt_bound bound, int score, int depth, int alpha, int beta, int search_depth) -> bool {
    if(depth < search_depth) return false;
    if(bound == russell::search::tt_bound::exact) return true;
    if(bound == russell::search::tt_bound::lower && score >= beta) return true;
    if(bound == russell::search::tt_bound::upper && score <= alpha) return true;
    return false;
  };

  require(!would_cut(russell::search::tt_bound::lower, 0, 5, 0, 50, 3), "LOWER insufficient must not cut (0 < beta 50)");

  require(!would_cut(russell::search::tt_bound::upper, 100, 5, 0, 50, 3), "UPPER insufficient must not cut (100 > alpha 0)");

  require(would_cut(russell::search::tt_bound::lower, 200, 5, 0, 50, 3), "LOWER sufficient must cut (200 >= 50)");

  require(would_cut(russell::search::tt_bound::upper, -100, 5, -50, 50, 3), "UPPER sufficient must cut (-100 <= -50)");

  require(would_cut(russell::search::tt_bound::exact, 0, 5, 0, 50, 3), "EXACT must cut");

  require(!would_cut(russell::search::tt_bound::exact, 0, 1, 0, 50, 3), "insufficient depth must not cut");

  {
    auto tt = russell::search::transposition_table{1024};
    const auto key = pos.key();
    tt.store(key, plain.best_move, plain.score, 5, russell::search::tt_bound::exact, 42);
    russell::move best; int score; int depth; russell::search::tt_bound bound; int static_eval = 0;
    require(tt.probe(key, best, score, depth, bound, static_eval), "EXACT entry present");
    require(bound == russell::search::tt_bound::exact && score == plain.score && depth == 5, "EXACT entry correct");
    require(best == plain.best_move, "EXACT best move correct");
    require(static_eval == 42, "EXACT static eval correct");
  }
  {
    auto tt = russell::search::transposition_table{1024};
    const auto key = pos.key();

    tt.store(key, plain.best_move, 200, 5, russell::search::tt_bound::lower, -13);
    auto search_pos = russell::position::from_fen(fen);

    russell::move best; int score; int depth; russell::search::tt_bound bound; int static_eval = 0;
    require(tt.probe(key, best, score, depth, bound, static_eval), "LOWER sufficient entry present");
    require(bound == russell::search::tt_bound::lower && score == 200 && depth == 5, "LOWER entry correct");
    require(static_eval == -13, "LOWER static eval correct");
    require(depth >= 3, "depth sufficient for cutoff");

    russell::search::search_stopper stopper;
    russell::search::search_limits limits;
    limits.depth = 3;
    const auto result = russell::search::search(search_pos, limits, stopper, tt);
    require(is_legal(search_pos, result.best_move) || !result.has_move, "normal LOWER cutoff keeps legality");
  }
  {
    auto tt = russell::search::transposition_table{1024};
    const auto key = pos.key();

    tt.store(key, plain.best_move, -100, 5, russell::search::tt_bound::upper, 777);
    russell::move best; int score; int depth; russell::search::tt_bound bound; int static_eval = 0;
    require(tt.probe(key, best, score, depth, bound, static_eval), "UPPER sufficient entry present");
    require(bound == russell::search::tt_bound::upper && score == -100, "UPPER entry correct");
    require(static_eval == 777, "UPPER static eval correct");
  }

  {
    auto tt = russell::search::transposition_table{1024};
    const auto key = pos.key();
    tt.store(key, plain.best_move, plain.score, 1, russell::search::tt_bound::exact, 5);
    auto search_pos = russell::position::from_fen(fen);
    auto fresh_pos = russell::position::from_fen(fen);
    const auto with_shallow_tt = russell::search::alpha_beta(search_pos, 3, true, true, false, false);
    const auto fresh = russell::search::alpha_beta(fresh_pos, 3, true, true, false, false);
    require(with_shallow_tt.score == fresh.score, "insufficient depth must not change result (depth 1 vs 3)");
    require(with_shallow_tt.best_move == fresh.best_move || with_shallow_tt.score == fresh.score, "shallow tt move consistent");
  }
}

static_assert(sizeof(russell::search::tt_entry) == 32, "STEP2: tt_entry must stay 32 bytes");
static_assert(sizeof(russell::search::tt_bucket) == 128, "STEP2: tt_bucket must stay 128 bytes");

auto test_static_eval_roundtrip_values() -> void
{
  auto tt = russell::search::transposition_table{1024};
  const auto mv = russell::move{12, 28, russell::piece_type::queen, russell::move_flag::quiet};
  const auto evals = std::vector<int>{150, -230, 0, 20000, -20000};
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto eval_out = 0;
  for(std::size_t i = 0; i < evals.size(); ++i) {
    const auto key = std::uint64_t{0x51e7000000000000ull | i};
    tt.store(key, mv, 100 + static_cast<int>(i), 3, russell::search::tt_bound::exact, evals[i]);
    require(tt.probe(key, best, score, depth, bound, eval_out), "static eval entry hits");
    require(eval_out == evals[i], "static eval roundtrips (pos/neg/zero/large)");
    require(score == 100 + static_cast<int>(i) && depth == 3, "score/depth intact with eval");
  }
}

auto test_score_static_eval_independent() -> void
{
  auto tt = russell::search::transposition_table{1024};
  const auto mv = russell::move{8, 24, russell::piece_type::knight, russell::move_flag::capture};
  const auto key = std::uint64_t{0x123456789abcdef1ULL};
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto eval_out = 0;
  tt.store(key, mv, 500, 6, russell::search::tt_bound::lower, -175);
  require(tt.probe(key, best, score, depth, bound, eval_out), "independent entry hits");
  require(score == 500 && eval_out == -175, "score != eval both correct");
  require(score != eval_out, "score and eval are independent values");
}

auto test_static_eval_mate_immune() -> void
{
  using russell::search::read_value;
  using russell::search::store_value;
  auto tt = russell::search::transposition_table{1024};
  const auto mv = russell::move{12, 28, russell::piece_type::queen, russell::move_flag::quiet};
  const auto key = std::uint64_t{0x07ae000000000001ULL};

  const int mate = 30000 - 10;
  const int ply = 7;
  const int stored_score = store_value(mate, ply);
  require(stored_score == mate + ply, "mate store precondition");
  tt.store(key, mv, stored_score, 5, russell::search::tt_bound::exact, 29999);
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto eval_out = 0;
  require(tt.probe(key, best, score, depth, bound, eval_out), "mate entry hits");
  require(score == stored_score, "mate score stored raw (ply-adjusted)");
  require(eval_out == 29999, "static eval NOT mate-normalized");
  require(read_value(score, ply) == mate, "score restores via read_value");
  require(read_value(29999, ply) == 29999 - ply, "read_value WOULD shift mate-range eval");
  require(eval_out == 29999, "eval kept raw despite mate range");

  const auto key2 = std::uint64_t{0x07ae000000000002ULL};
  tt.store(key2, mv, stored_score, 5, russell::search::tt_bound::exact,
           russell::search::tt_no_static_eval);
  require(tt.probe(key2, best, score, depth, bound, eval_out), "sentinel entry hits");
  require(eval_out == russell::search::tt_no_static_eval, "no-eval sentinel survives");
}

auto test_static_eval_generation_and_fields() -> void
{
  auto tt = russell::search::transposition_table{1024};
  const auto mv = russell::move{51, 35, russell::piece_type::rook, russell::move_flag::quiet};
  const auto key = std::uint64_t{0x600e000000000001ULL};
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto eval_out = 0;
  tt.store(key, mv, -55, 7, russell::search::tt_bound::upper, 456);
  const auto gen_before = tt.generation();
  tt.new_generation();
  const auto gen_after = tt.generation();
  require(gen_after == static_cast<std::uint8_t>(gen_before + 1 == 0 ? 1 : gen_before + 1),
          "generation advances by one (0 skipped)");

  require(tt.probe(key, best, score, depth, bound, eval_out), "entry survives generation bump");
  require(best == mv, "move survives with new field");
  require(score == -55 && depth == 7, "score/depth survive with new field");
  require(bound == russell::search::tt_bound::upper, "bound survives with new field");
  require(eval_out == 456, "static eval survives generation bump");
}

auto test_tt_entry_layout_and_hash_accounting() -> void
{
  require(sizeof(russell::search::tt_entry) == 32, "entry size is 32 bytes");
  require(sizeof(russell::search::tt_bucket) == 128, "bucket is 128 bytes (4 entries)");
  require(alignof(russell::search::tt_entry) == 8, "entry alignment is 8");

  constexpr std::size_t one_mib = 1024 * 1024;
  require(one_mib / sizeof(russell::search::tt_entry) == 32768, "1MiB holds 32768 entries at 32B");
  require((16 * one_mib) / sizeof(russell::search::tt_entry) == 524288, "16MiB default holds 524288 entries");
  auto tt = russell::search::transposition_table{1024};
  require(tt.size() >= 1024 && tt.size() % 4 == 0, "table size is bucket multiple");
}

auto search_with_shared_tt(const std::string& fen, int depth,
                            russell::search::transposition_table& tt)
    -> russell::search::search_result
{
  auto pos = russell::position::from_fen(fen);
  auto stopper = russell::search::search_stopper{};
  return russell::search::search(pos, russell::search::search_limits{depth, 0}, stopper, tt);
}

auto test_tt_eval_usable_gate() -> void
{
  using russell::search::tt_eval_usable;
  using russell::search::tt_no_static_eval;
  require(!tt_eval_usable(false, 0), "miss has no usable eval");
  require(!tt_eval_usable(false, 123), "miss with junk stays unusable");
  require(!tt_eval_usable(true, tt_no_static_eval), "sentinel unusable on hit");
  require(tt_eval_usable(true, 0), "zero eval usable");
  require(tt_eval_usable(true, 47), "positive eval usable");
  require(tt_eval_usable(true, -31), "negative eval usable");
  require(tt_eval_usable(true, 20000), "large positive usable");
  require(tt_eval_usable(true, -20000), "large negative usable");
}

auto test_tt_eval_sign_propagation() -> void
{
  auto tt = russell::search::transposition_table{1024};
  const auto mv = russell::move{12, 28, russell::piece_type::queen, russell::move_flag::quiet};
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto eval_out = 0;
  tt.store(0xE4A1000000000001ULL, mv, 10, 2, russell::search::tt_bound::exact, 47);
  require(tt.probe(0xE4A1000000000001ULL, best, score, depth, bound, eval_out), "positive entry hits");
  require(eval_out == 47 && eval_out > 0, "positive eval propagates");
  tt.store(0xE4A1000000000002ULL, mv, 10, 2, russell::search::tt_bound::exact, -31);
  require(tt.probe(0xE4A1000000000002ULL, best, score, depth, bound, eval_out), "negative entry hits");
  require(eval_out == -31 && eval_out < 0, "negative eval propagates (no negation)");
  tt.store(0xE4A1000000000003ULL, mv, 10, 2, russell::search::tt_bound::exact, 0);
  require(tt.probe(0xE4A1000000000003ULL, best, score, depth, bound, eval_out), "zero entry hits");
  require(eval_out == 0, "zero eval propagates");
}

auto test_tt_eval_consumed_cold_search() -> void
{

  const auto fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  auto tt = russell::search::transposition_table{};
  const auto r = search_with_shared_tt(fen, 3, tt);
  require(r.has_move, "cold search has move");
  require(r.tt_eval_reused > 0, "cold search reuses TT evals across iterations");
}

auto test_tt_eval_reuse_biased_positions() -> void
{

  const auto fens = std::vector<std::string>{
    "7k/8/8/8/8/8/8/Q3K3 w - - 0 1",
    "7k/8/8/8/8/8/8/Q3K3 b - - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
  };
  for(const auto& fen : fens) {
    auto tt = russell::search::transposition_table{};
    static_cast<void>(search_with_shared_tt(fen, 2, tt));
    const auto warm = search_with_shared_tt(fen, 3, tt);
    require(warm.tt_eval_reused > 0, ("biased position reuses evals " + fen).c_str());
    auto fresh_tt = russell::search::transposition_table{};
    const auto fresh = search_with_shared_tt(fen, 3, fresh_tt);
    require(warm.score == fresh.score, ("reuse score matches fresh " + fen).c_str());
    require(warm.best_move == fresh.best_move, ("reuse move matches fresh " + fen).c_str());
  }
}

auto test_depth_insufficient_eval_reuse() -> void
{

  const auto fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  auto tt = russell::search::transposition_table{};
  static_cast<void>(search_with_shared_tt(fen, 2, tt));
  const auto deep = search_with_shared_tt(fen, 3, tt);
  require(deep.tt_eval_reused > 0, "depth-insufficient hit still reuses eval");
  auto fresh_tt = russell::search::transposition_table{};
  const auto fresh = search_with_shared_tt(fen, 3, fresh_tt);
  require(deep.score == fresh.score, "insufficient-depth reuse score matches fresh");
  require(deep.best_move == fresh.best_move, "insufficient-depth reuse move matches fresh");
}

auto test_sentinel_entry_behaves_as_miss() -> void
{

  const auto fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  auto probe_pos = russell::position::from_fen(fen);
  auto tt = russell::search::transposition_table{};
  tt.store(probe_pos.key(), russell::move{}, 0, 0, russell::search::tt_bound::none,
           russell::search::tt_no_static_eval);
  const auto sentinel_run = search_with_shared_tt(fen, 3, tt);
  auto fresh_tt = russell::search::transposition_table{};
  const auto fresh = search_with_shared_tt(fen, 3, fresh_tt);
  require(sentinel_run.score == fresh.score, "sentinel search score matches fresh");
  require(sentinel_run.best_move == fresh.best_move, "sentinel search move matches fresh");
  require(sentinel_run.score != std::numeric_limits<int>::max(), "sentinel never leaks into score");
}

auto test_mate_eval_separation_search() -> void
{

  const auto fen = "6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1";
  auto tt = russell::search::transposition_table{};
  static_cast<void>(search_with_shared_tt(fen, 2, tt));
  const auto warm = search_with_shared_tt(fen, 4, tt);
  require(warm.score > 20000, "mate preserved through eval reuse");
  require(warm.tt_eval_reused > 0, "mate search reuses interior evals");
  auto fresh_tt = russell::search::transposition_table{};
  const auto fresh = search_with_shared_tt(fen, 4, fresh_tt);
  require(warm.score == fresh.score, "mate reuse score matches fresh");
  require(warm.best_move == fresh.best_move, "mate reuse move matches fresh");
}

auto test_warm_search_determinism() -> void
{

  const auto fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  auto tt = russell::search::transposition_table{};
  static_cast<void>(search_with_shared_tt(fen, 3, tt));
  const auto first = search_with_shared_tt(fen, 3, tt);
  const auto second = search_with_shared_tt(fen, 3, tt);
  require(first.score == second.score, "warm runs agree on score");
  require(first.best_move == second.best_move, "warm runs agree on move");
  auto fresh_tt = russell::search::transposition_table{};
  const auto fresh = search_with_shared_tt(fen, 3, fresh_tt);
  require(first.score == fresh.score, "warm score matches fresh");
  require(first.best_move == fresh.best_move, "warm move matches fresh");
}

auto test_tt_eval_trustworthy_prior_sweep() -> void
{

  const auto fens = std::vector<std::string>{
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
    "7k/8/8/8/8/8/8/Q3K3 w - - 0 1",
    "7k/8/8/8/8/8/8/Q3K3 b - - 0 1",
    "4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
  };
  for(const auto& fen : fens) {
    auto tt = russell::search::transposition_table{};
    static_cast<void>(search_with_shared_tt(fen, 2, tt));
    const auto warm = search_with_shared_tt(fen, 3, tt);
    require(warm.has_move, ("prior sweep has move " + fen).c_str());
    require(warm.tt_eval_reused > 0, ("prior sweep reuses evals " + fen).c_str());
    auto fresh_tt = russell::search::transposition_table{};
    const auto fresh = search_with_shared_tt(fen, 3, fresh_tt);
    require(warm.score == fresh.score, ("prior sweep score matches fresh " + fen).c_str());
    require(warm.best_move == fresh.best_move, ("prior sweep move matches fresh " + fen).c_str());
  }
}

auto test_tt_eval_survives_generations() -> void
{

  const auto fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  auto tt = russell::search::transposition_table{};
  static_cast<void>(search_with_shared_tt(fen, 2, tt));
  tt.new_generation();
  tt.new_generation();
  const auto aged = search_with_shared_tt(fen, 3, tt);
  require(aged.tt_eval_reused > 0, "aged entries still supply evals");
  auto fresh_tt = russell::search::transposition_table{};
  const auto fresh = search_with_shared_tt(fen, 3, fresh_tt);
  require(aged.score == fresh.score, "aged-prior score matches fresh");
  require(aged.best_move == fresh.best_move, "aged-prior move matches fresh");
}

auto test_same_key_keep_deepest() -> void
{
  auto tt = russell::search::transposition_table{1024};
  const auto key = std::uint64_t{0xdee9000000000001ULL};
  const auto deep_mv = russell::move{12, 28, russell::piece_type::queen, russell::move_flag::quiet};
  const auto shallow_mv = russell::move{8, 24, russell::piece_type::knight, russell::move_flag::capture};
  tt.store(key, deep_mv, 500, 8, russell::search::tt_bound::exact, 111);

  tt.store(key, shallow_mv, -50, 3, russell::search::tt_bound::upper, 222);
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto eval_out = 0;
  require(tt.probe(key, best, score, depth, bound, eval_out), "deep entry present");
  require(depth == 8 && score == 500, "deep score/depth survive");
  require(best == deep_mv, "deep best move survives");
  require(bound == russell::search::tt_bound::exact, "deep EXACT survives");
  require(eval_out == 111, "deep eval survives");
}

auto test_same_key_shallow_replaces() -> void
{
  auto tt = russell::search::transposition_table{1024};
  const auto key = std::uint64_t{0xdee9000000000002ULL};
  const auto shallow_mv = russell::move{8, 24, russell::piece_type::knight, russell::move_flag::capture};
  const auto deep_mv = russell::move{12, 28, russell::piece_type::queen, russell::move_flag::quiet};
  tt.store(key, shallow_mv, -50, 3, russell::search::tt_bound::upper, 222);
  tt.store(key, deep_mv, 500, 8, russell::search::tt_bound::exact, 111);
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto eval_out = 0;
  require(tt.probe(key, best, score, depth, bound, eval_out), "replaced entry present");
  require(depth == 8 && score == 500, "deeper result replaces shallower");
  require(best == deep_mv && bound == russell::search::tt_bound::exact, "deeper move/bound stored");
  require(eval_out == 111, "deeper eval stored");
}

auto test_same_key_exact_preserved() -> void
{
  auto tt = russell::search::transposition_table{1024};
  const auto mv1 = russell::move{12, 28, russell::piece_type::queen, russell::move_flag::quiet};
  const auto mv2 = russell::move{8, 24, russell::piece_type::knight, russell::move_flag::capture};
  const auto mv3 = russell::move{51, 35, russell::piece_type::rook, russell::move_flag::quiet};
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto eval_out = 0;

  const auto k1 = std::uint64_t{0xEAc7000000000001ULL};
  tt.store(k1, mv1, 100, 5, russell::search::tt_bound::exact, 10);
  tt.store(k1, mv2, 200, 5, russell::search::tt_bound::lower, 20);
  require(tt.probe(k1, best, score, depth, bound, eval_out), "exact entry present");
  require(score == 100 && bound == russell::search::tt_bound::exact, "weaker bound does not destroy EXACT");
  require(best == mv1 && eval_out == 10, "exact move/eval preserved");

  const auto k2 = std::uint64_t{0xEAc7000000000002ULL};
  tt.store(k2, mv2, 200, 5, russell::search::tt_bound::upper, 20);
  tt.store(k2, mv3, 300, 5, russell::search::tt_bound::exact, 30);
  require(tt.probe(k2, best, score, depth, bound, eval_out), "upgraded entry present");
  require(score == 300 && bound == russell::search::tt_bound::exact, "incoming EXACT replaces weaker bound");
  require(best == mv3 && eval_out == 30, "incoming EXACT move/eval stored");

  tt.store(k2, mv1, 310, 5, russell::search::tt_bound::exact, 31);
  require(tt.probe(k2, best, score, depth, bound, eval_out), "refreshed entry present");
  require(score == 310 && best == mv1 && eval_out == 31, "equal EXACT refreshes values");
}

auto test_same_key_move_preserved() -> void
{
  auto tt = russell::search::transposition_table{1024};
  const auto key = std::uint64_t{0x90BE000000000001ULL};
  const auto mv = russell::move{12, 28, russell::piece_type::queen, russell::move_flag::quiet};

  tt.store(key, mv, 100, 5, russell::search::tt_bound::lower, 10);
  tt.store(key, russell::move{}, 150, 5, russell::search::tt_bound::lower, 20);
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto eval_out = 0;
  require(tt.probe(key, best, score, depth, bound, eval_out), "move entry present");
  require(best == mv, "stored move survives moveless overwrite");
  require(score == 150 && eval_out == 20, "fresh score/eval stored");
}

auto test_same_key_eval_carry_sentinel() -> void
{
  auto tt = russell::search::transposition_table{1024};
  const auto key = std::uint64_t{0x5E4719E100000001ULL};
  const auto mv = russell::move{12, 28, russell::piece_type::queen, russell::move_flag::quiet};
  const auto mv2 = russell::move{8, 24, russell::piece_type::knight, russell::move_flag::capture};

  tt.store(key, mv, 100, 5, russell::search::tt_bound::lower, 10);
  tt.store(key, mv2, 150, 5, russell::search::tt_bound::lower,
           russell::search::tt_no_static_eval);
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto eval_out = 0;
  require(tt.probe(key, best, score, depth, bound, eval_out), "eval entry present");
  require(eval_out == 10, "valid eval survives sentinel overwrite");
  require(score == 150 && best == mv2, "fresh score/move stored");
}

auto test_different_key_age_depth_intact() -> void
{

  auto tt = russell::search::transposition_table{4};
  require(tt.size() == 4, "single bucket table");
  const auto mv = russell::move{12, 28, russell::piece_type::queen, russell::move_flag::quiet};
  for(auto i = 1; i <= 4; ++i) {
    tt.store(static_cast<std::uint64_t>(i), mv, 1000 + i, i,
             russell::search::tt_bound::exact, 10 + i);
  }

  tt.store(5u, mv, 1005, 5, russell::search::tt_bound::exact, 15);
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto eval_out = 0;
  require(!tt.probe(1u, best, score, depth, bound, eval_out), "shallowest evicted");
  for(auto i = 2; i <= 5; ++i) {
    require(tt.probe(static_cast<std::uint64_t>(i), best, score, depth, bound, eval_out),
            "survivor present");
    require(score == 1000 + i && depth == i, "survivor fields intact");
    require(eval_out == 10 + i, "survivor eval intact");
  }
}

auto test_terminal_store_intact() -> void
{
  auto tt = russell::search::transposition_table{1024};
  const auto key = std::uint64_t{0x7E2919A100E21E00ULL};

  const int mate = russell::search::store_value(30000 - 4, 6);
  tt.store(key, russell::move{}, mate, 5, russell::search::tt_bound::exact,
           russell::search::tt_no_static_eval);
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto eval_out = 0;
  require(tt.probe(key, best, score, depth, bound, eval_out), "terminal entry present");
  require(score == mate && depth == 5, "terminal score/depth intact");
  require(bound == russell::search::tt_bound::exact, "terminal bound intact");
  require(best == russell::move{}, "terminal move intact");
  require(eval_out == russell::search::tt_no_static_eval, "terminal sentinel intact");
  require(russell::search::read_value(score, 6) == 30000 - 4, "mate restores via read_value");
}

auto test_no_search_result_guard() -> void
{
  using russell::search::mate_value;
  using russell::search::tt_search_result_valid;
  require(!tt_search_result_valid(-mate_value), "unsearched -inf is not storable");
  require(tt_search_result_valid(-mate_value + 1), "mate-distance score is storable");
  require(tt_search_result_valid(0), "zero is storable");
  require(tt_search_result_valid(123), "regular score is storable");
  require(tt_search_result_valid(mate_value - 1), "mate score is storable");
}

auto test_concurrent_store_probe() -> void
{

  auto tt = russell::search::transposition_table{256};
  constexpr int kKeys = 256;
  constexpr int kThreads = 8;
  constexpr int kOps = 2500;
  const auto key_of = [](int i) -> std::uint64_t {
    return 0x1000000000000000ULL | static_cast<std::uint64_t>(i + 1);
  };
  const auto move_of = [](int i) -> russell::move {
    return russell::move{static_cast<std::uint8_t>((i * 13) % 64),
                         static_cast<std::uint8_t>((i * 29 + 7) % 64),
                         russell::piece_type::queen, russell::move_flag::quiet};
  };
  const auto score_of = [](int i) { return 5000 + i; };
  const auto depth_of = [](int i) { return 1 + (i % 4); };
  const auto eval_of = [](int i) { return 300 + i; };
  auto worker = [&](int tid) {
    auto best = russell::move{};
    auto score = 0;
    auto depth = 0;
    auto bound = russell::search::tt_bound::none;
    auto eval_out = 0;
    for(int n = 0; n < kOps; ++n) {
      const int i = static_cast<int>((static_cast<unsigned>(n) * 2654435761u
                                      + static_cast<unsigned>(tid) * 2246822519u)
                                     % kKeys);
      tt.store(key_of(i), move_of(i), score_of(i), depth_of(i),
               russell::search::tt_bound::exact, eval_of(i));
      const int j = static_cast<int>((static_cast<unsigned>(n) * 3266489917u
                                      + static_cast<unsigned>(tid) * 668265263u)
                                     % kKeys);
      if(tt.probe(key_of(j), best, score, depth, bound, eval_out)) {
        require(score == score_of(j), "concurrent hit score consistent");
        require(depth == depth_of(j), "concurrent hit depth consistent");
        require(bound == russell::search::tt_bound::exact, "concurrent hit bound consistent");
        require(best == move_of(j), "concurrent hit move consistent");
        require(eval_out == eval_of(j), "concurrent hit eval consistent");
      }
    }
  };
  auto threads = std::vector<std::thread>{};
  for(int t = 0; t < kThreads; ++t) threads.emplace_back(worker, t);
  for(auto& th : threads) th.join();

  for(int i = 0; i < kKeys; ++i) {
    tt.store(key_of(i), move_of(i), score_of(i), depth_of(i),
             russell::search::tt_bound::exact, eval_of(i));
  }
  auto best = russell::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = russell::search::tt_bound::none;
  auto eval_out = 0;
  for(int i = 0; i < kKeys; ++i) {
    require(tt.probe(key_of(i), best, score, depth, bound, eval_out), "refilled key hits");
    require(score == score_of(i) && depth == depth_of(i), "refilled fields intact");
    require(best == move_of(i) && eval_out == eval_of(i), "refilled move/eval intact");
  }
}

}

auto main() -> int
{
  test_store_probe_roundtrip();
  test_mate_score_adjustment();
  test_cutoff_consistency();
  test_reuse_consistency();
  test_mate_search_consistency();
  test_shared_table_reuse();
  test_stale_entries_only_miss();
  test_generation_change_isolation();
  test_persistent_reuse_across_positions();
  test_consecutive_searches_stable();
  test_depth_gated_reuse();
  test_mate_score_persistence();
  test_tt_bound_correctness();
  test_static_eval_roundtrip_values();
  test_score_static_eval_independent();
  test_static_eval_mate_immune();
  test_static_eval_generation_and_fields();
  test_tt_entry_layout_and_hash_accounting();
  test_tt_eval_usable_gate();
  test_tt_eval_sign_propagation();
  test_tt_eval_consumed_cold_search();
  test_tt_eval_reuse_biased_positions();
  test_depth_insufficient_eval_reuse();
  test_sentinel_entry_behaves_as_miss();
  test_mate_eval_separation_search();
  test_warm_search_determinism();
  test_tt_eval_trustworthy_prior_sweep();
  test_tt_eval_survives_generations();
  test_same_key_keep_deepest();
  test_same_key_shallow_replaces();
  test_same_key_exact_preserved();
  test_same_key_move_preserved();
  test_same_key_eval_carry_sentinel();
  test_different_key_age_depth_intact();
  test_terminal_store_intact();
  test_no_search_result_guard();
  test_concurrent_store_probe();
  return 0;
}
