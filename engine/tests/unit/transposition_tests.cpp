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

auto test_store_probe_roundtrip() -> void
{
  auto tt = tuna::search::transposition_table{1024};
  const auto key = std::uint64_t{0xdeadbeefcafe0001};
  const auto mv = tuna::move{12, 28, tuna::piece_type::queen, tuna::move_flag::quiet};

  auto best = tuna::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = tuna::search::tt_bound::none;
  require(!tt.probe(key, best, score, depth, bound), "empty table misses");
  require(tt.size() >= 1024, "table sized up to power of two");

  tt.store(key, mv, 123, 4, tuna::search::tt_bound::exact);
  require(tt.probe(key, best, score, depth, bound), "stored entry hits");
  require(best == mv && score == 123 && depth == 4, "entry fields roundtrip");
  require(bound == tuna::search::tt_bound::exact, "bound roundtrip");

  require(!tt.probe(key + 1, best, score, depth, bound), "different key misses");

  tt.clear();
  require(!tt.probe(key, best, score, depth, bound), "clear empties table");
}

auto test_mate_score_adjustment() -> void
{
  using tuna::search::read_value;
  using tuna::search::store_value;

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
    auto plain_pos = tuna::position::from_fen(fen);
    const auto plain = tuna::search::minimax(plain_pos, depth);
    auto exact_pos = tuna::position::from_fen(fen);
    const auto exact = tuna::search::alpha_beta(exact_pos, depth, false, false, false, false, false);
    require(exact.has_move == plain.has_move, ("tt exact has_move matches " + fen).c_str());
    require(is_legal(exact_pos, exact.best_move) || !exact.has_move, ("tt exact best move legal " + fen).c_str());
    if(fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" || fen == "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1" || fen == "6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1" || fen == "k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1") {
      require(exact.score == plain.score, ("tt exact score matches minimax " + fen).c_str());
      if(exact.has_move && !(exact.best_move == plain.best_move)) require(exact.score == plain.score, ("tt exact alternative same score " + fen).c_str());
    } else {
      require(exact.score > -29000 && exact.score < 29000, ("tt exact not false mate " + fen).c_str());
      if(exact.score == plain.score && exact.has_move && !(exact.best_move == plain.best_move)) require(exact.score == plain.score, ("tt exact alternative same score " + fen).c_str());
    }
    auto tt_pos = tuna::position::from_fen(fen);
    const auto tt = tuna::search::alpha_beta(tt_pos, depth, true, true, false, false, false);
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
    auto id_pos = tuna::position::from_fen(fen);
    auto ab_pos = tuna::position::from_fen(fen);
    const auto id = tuna::search::iterative_deepening(id_pos, depth);
    const auto ab = tuna::search::alpha_beta(ab_pos, depth);
    require(id.has_move == ab.has_move, ("reuse has_move matches " + fen).c_str());
    require(id.score == ab.score, ("reused tt score matches fresh search " + fen).c_str());
    require(is_legal(ab_pos, id.best_move) || !id.has_move, ("reused tt best move legal " + fen).c_str());
    if(!(id.best_move == ab.best_move)) require(id.score == ab.score, ("reused alternative same score " + fen).c_str());
  }
}

auto test_shared_table_reuse() -> void
{
  auto tt = tuna::search::transposition_table{};
  const auto cases = std::vector<std::pair<std::string, int>>{
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 3},
  };
  for(const auto& [fen, depth] : cases) {

    auto shared_pos = tuna::position::from_fen(fen);
    auto fresh_pos = tuna::position::from_fen(fen);
    const auto limits = tuna::search::search_limits{depth, 0};
    const auto stopper = tuna::search::search_stopper{};
    const auto shared = tuna::search::search(shared_pos, limits, stopper, tt);
    const auto fresh = tuna::search::search(fresh_pos, limits, stopper);
    require(shared.has_move == fresh.has_move, ("shared tt has_move matches " + fen).c_str());
    require(shared.best_move == fresh.best_move, ("shared tt best move matches " + fen).c_str());
    require(shared.score == fresh.score, ("shared tt score matches " + fen).c_str());
  }

  tt.clear();
  auto shared_pos = tuna::position::start();
  const auto limits = tuna::search::search_limits{3, 0};
  auto stopper = tuna::search::search_stopper{};
  const auto shared = tuna::search::search(shared_pos, limits, stopper, tt);
  auto fresh_pos = tuna::position::start();
  auto fresh_stopper = tuna::search::search_stopper{};
  const auto fresh = tuna::search::search(fresh_pos, limits, fresh_stopper);
  require(shared.best_move == fresh.best_move && shared.score == fresh.score,
          "cleared shared tt matches fresh");
}

auto test_mate_search_consistency() -> void
{
  auto mate_pos = tuna::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  auto plain_pos = tuna::position::from_fen("6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1");
  const auto tt = tuna::search::alpha_beta(mate_pos, 3, true, true, false, false);
  const auto plain = tuna::search::minimax(plain_pos, 3);
  require(tt.score == plain.score, "mate score consistent with minimax");
  require(tt.score > 20000, "mate detected with tt");
}

auto test_stale_entries_only_miss() -> void
{
  auto tt = tuna::search::transposition_table{4096};
  const auto mv = tuna::move{12, 28, tuna::piece_type::queen, tuna::move_flag::quiet};
  auto best = tuna::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = tuna::search::tt_bound::none;

  for(auto i = 0; i < 1024; ++i) {
    tt.store(0x8000000000000000ull | static_cast<std::uint64_t>(i), mv, 1000 + i, 4,
             tuna::search::tt_bound::exact);
  }

  for(auto i = 0; i < 1024; ++i) {
    const auto key = 0x8000000000000000ull | static_cast<std::uint64_t>(i);
    require(tt.probe(key, best, score, depth, bound), "stored key hits");
    require(best == mv && score == 1000 + i && depth == 4, "stored key fields intact");
    require(bound == tuna::search::tt_bound::exact, "stored key bound intact");
  }

  for(auto i = 0; i < 1024; ++i) {
    const auto key = 0x4000000000000000ull | static_cast<std::uint64_t>(i);
    require(!tt.probe(key, best, score, depth, bound), "same-bucket different-key misses");
  }
}

auto test_generation_change_isolation() -> void
{
  auto tt = tuna::search::transposition_table{4096};
  const auto mv = tuna::move{12, 28, tuna::piece_type::queen, tuna::move_flag::quiet};
  auto best = tuna::move{};
  auto score = 0;
  auto depth = 0;
  auto bound = tuna::search::tt_bound::none;

  const auto white_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  const auto black_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1";
  const auto w = tuna::position::from_fen(white_fen);
  const auto b = tuna::position::from_fen(black_fen);
  require(w.key() != b.key(), "side-to-move changes key");
  tt.store(w.key(), mv, 999, 4, tuna::search::tt_bound::exact);
  require(!tt.probe(b.key(), best, score, depth, bound), "side-to-move generation misses");

  const auto full_castle = tuna::position::from_fen("4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1");
  const auto king_castle = tuna::position::from_fen("4k3/8/8/8/8/8/8/R3K2R w K - 0 1");
  require(full_castle.key() != king_castle.key(), "castling rights change key");
  tt.store(full_castle.key(), mv, 111, 4, tuna::search::tt_bound::exact);
  require(!tt.probe(king_castle.key(), best, score, depth, bound), "castling generation misses");

  const auto with_ep = tuna::position::from_fen("8/8/8/P7/8/8/8/4K2k b - a3 0 1");
  const auto without_ep = tuna::position::from_fen("8/8/8/P7/8/8/8/4K2k b - - 0 1");
  require(with_ep.key() != without_ep.key(), "en-passant changes key");
  tt.store(with_ep.key(), mv, 222, 4, tuna::search::tt_bound::exact);
  require(!tt.probe(without_ep.key(), best, score, depth, bound), "en-passant generation misses");

  auto w_pos = tuna::position::start();
  auto w_stopper = tuna::search::search_stopper{};
  static_cast<void>(tuna::search::search(w_pos, tuna::search::search_limits{3, 0}, w_stopper, tt));
  auto b_pos = tuna::position::from_fen(black_fen);
  auto b_stopper = tuna::search::search_stopper{};
  const auto reused = tuna::search::search(b_pos, tuna::search::search_limits{3, 0}, b_stopper, tt);
  auto fresh_pos = tuna::position::from_fen(black_fen);
  auto fresh_stopper = tuna::search::search_stopper{};
  const auto fresh = tuna::search::search(fresh_pos, tuna::search::search_limits{3, 0}, fresh_stopper);
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
  auto tt = tuna::search::transposition_table{4096};
  for(const auto& [fen, depth] : cases) {
    auto shared_pos = tuna::position::from_fen(fen);
    auto fresh_pos = tuna::position::from_fen(fen);
    const auto limits = tuna::search::search_limits{depth, 0};
    auto shared_stopper = tuna::search::search_stopper{};
    auto fresh_stopper = tuna::search::search_stopper{};
    const auto shared = tuna::search::search(shared_pos, limits, shared_stopper, tt);
    const auto fresh = tuna::search::search(fresh_pos, limits, fresh_stopper);
    require(shared.has_move == fresh.has_move, ("persistent has_move matches " + fen).c_str());
    require(shared.best_move == fresh.best_move, ("persistent best move matches " + fen).c_str());
    require(shared.score == fresh.score, ("persistent score matches " + fen).c_str());
    if(shared.has_move) {
      require(is_legal(shared_pos, shared.best_move), ("persistent best move legal " + fen).c_str());
    }
  }

  tt.clear();
  auto cleared_pos = tuna::position::start();
  auto cleared_stopper = tuna::search::search_stopper{};
  const auto cleared = tuna::search::search(cleared_pos, tuna::search::search_limits{3, 0},
                                            cleared_stopper, tt);
  auto fresh_pos = tuna::position::start();
  auto fresh_stopper = tuna::search::search_stopper{};
  const auto fresh = tuna::search::search(fresh_pos, tuna::search::search_limits{3, 0}, fresh_stopper);
  require(cleared.best_move == fresh.best_move && cleared.score == fresh.score,
          "cleared persistent tt matches fresh");
}

auto test_consecutive_searches_stable() -> void
{
  const auto fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  auto tt = tuna::search::transposition_table{};
  auto first = tuna::search::search_result{};
  auto second = tuna::search::search_result{};
  for(auto run = 0; run < 2; ++run) {
    auto pos = tuna::position::from_fen(fen);
    auto stopper = tuna::search::search_stopper{};
    const auto result = tuna::search::search(pos, tuna::search::search_limits{4, 0}, stopper, tt);
    if(run == 0) {
      first = result;
    } else {
      second = result;
    }
  }
  require(first.has_move == second.has_move, "consecutive has_move stable");
  require(first.best_move == second.best_move, "consecutive best move stable");
  require(first.score == second.score, "consecutive score stable");
  auto fresh_pos = tuna::position::from_fen(fen);
  auto fresh_stopper = tuna::search::search_stopper{};
  const auto fresh = tuna::search::search(fresh_pos, tuna::search::search_limits{4, 0}, fresh_stopper);
  require(second.best_move == fresh.best_move, "reused run matches fresh move");
  require(second.score == fresh.score, "reused run matches fresh score");
}

auto test_depth_gated_reuse() -> void
{
  const auto fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  auto tt = tuna::search::transposition_table{};
  auto shallow_pos = tuna::position::from_fen(fen);
  auto shallow_stopper = tuna::search::search_stopper{};
  static_cast<void>(tuna::search::search(shallow_pos, tuna::search::search_limits{1, 0},
                                         shallow_stopper, tt));

  auto deep_pos = tuna::position::from_fen(fen);
  auto deep_stopper = tuna::search::search_stopper{};
  const auto reused = tuna::search::search(deep_pos, tuna::search::search_limits{3, 0},
                                           deep_stopper, tt);
  auto fresh_pos = tuna::position::from_fen(fen);
  auto fresh_stopper = tuna::search::search_stopper{};
  const auto fresh = tuna::search::search(fresh_pos, tuna::search::search_limits{3, 0}, fresh_stopper);
  require(reused.has_move == fresh.has_move, "depth-gated has_move matches fresh");
  require(reused.best_move == fresh.best_move, "depth-gated best move matches fresh");
  require(reused.score == fresh.score, "depth-gated score matches fresh");
}

auto test_mate_score_persistence() -> void
{
  auto tt = tuna::search::transposition_table{1024};
  for(const auto& fen : {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                         "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"}) {
    auto pos = tuna::position::from_fen(fen);
    auto stopper = tuna::search::search_stopper{};
    static_cast<void>(tuna::search::search(pos, tuna::search::search_limits{3, 0}, stopper, tt));
  }
  const auto mate_fen = "6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1";
  const auto limits = tuna::search::search_limits{5, 0};
  auto pos1 = tuna::position::from_fen(mate_fen);
  auto stopper1 = tuna::search::search_stopper{};
  const auto reused1 = tuna::search::search(pos1, limits, stopper1, tt);
  auto pos2 = tuna::position::from_fen(mate_fen);
  auto stopper2 = tuna::search::search_stopper{};
  const auto reused2 = tuna::search::search(pos2, limits, stopper2, tt);
  auto fresh_pos = tuna::position::from_fen(mate_fen);
  auto fresh_stopper = tuna::search::search_stopper{};
  const auto fresh = tuna::search::search(fresh_pos, limits, fresh_stopper);
  require(reused1.score > 20000, "mate detected through dirty table");
  require(reused1.score == fresh.score, "reused mate score matches fresh");
  require(reused1.best_move == fresh.best_move, "reused mate move matches fresh");
  require(reused1.score == reused2.score, "consecutive mate searches stable");
  require(is_legal(pos1, reused1.best_move), "reused mate move legal");
}

auto test_tt_bound_correctness() -> void
{

  const auto fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  auto pos = tuna::position::from_fen(fen);

  auto plain_pos = tuna::position::from_fen(fen);
  const auto plain = tuna::search::minimax(plain_pos, 3);

  auto would_cut = [](tuna::search::tt_bound bound, int score, int depth, int alpha, int beta, int search_depth) -> bool {
    if(depth < search_depth) return false;
    if(bound == tuna::search::tt_bound::exact) return true;
    if(bound == tuna::search::tt_bound::lower && score >= beta) return true;
    if(bound == tuna::search::tt_bound::upper && score <= alpha) return true;
    return false;
  };

  require(!would_cut(tuna::search::tt_bound::lower, 0, 5, 0, 50, 3), "LOWER insufficient must not cut (0 < beta 50)");

  require(!would_cut(tuna::search::tt_bound::upper, 100, 5, 0, 50, 3), "UPPER insufficient must not cut (100 > alpha 0)");

  require(would_cut(tuna::search::tt_bound::lower, 200, 5, 0, 50, 3), "LOWER sufficient must cut (200 >= 50)");

  require(would_cut(tuna::search::tt_bound::upper, -100, 5, -50, 50, 3), "UPPER sufficient must cut (-100 <= -50)");

  require(would_cut(tuna::search::tt_bound::exact, 0, 5, 0, 50, 3), "EXACT must cut");

  require(!would_cut(tuna::search::tt_bound::exact, 0, 1, 0, 50, 3), "insufficient depth must not cut");

  {
    auto tt = tuna::search::transposition_table{1024};
    const auto key = pos.key();
    tt.store(key, plain.best_move, plain.score, 5, tuna::search::tt_bound::exact);
    tuna::move best; int score; int depth; tuna::search::tt_bound bound;
    require(tt.probe(key, best, score, depth, bound), "EXACT entry present");
    require(bound == tuna::search::tt_bound::exact && score == plain.score && depth == 5, "EXACT entry correct");
    require(best == plain.best_move, "EXACT best move correct");
  }
  {
    auto tt = tuna::search::transposition_table{1024};
    const auto key = pos.key();

    tt.store(key, plain.best_move, 200, 5, tuna::search::tt_bound::lower);
    auto search_pos = tuna::position::from_fen(fen);

    tuna::move best; int score; int depth; tuna::search::tt_bound bound;
    require(tt.probe(key, best, score, depth, bound), "LOWER sufficient entry present");
    require(bound == tuna::search::tt_bound::lower && score == 200 && depth == 5, "LOWER entry correct");
    require(depth >= 3, "depth sufficient for cutoff");

    tuna::search::search_stopper stopper;
    tuna::search::search_limits limits;
    limits.depth = 3;
    const auto result = tuna::search::search(search_pos, limits, stopper, tt);
    require(is_legal(search_pos, result.best_move) || !result.has_move, "normal LOWER cutoff keeps legality");
  }
  {
    auto tt = tuna::search::transposition_table{1024};
    const auto key = pos.key();

    tt.store(key, plain.best_move, -100, 5, tuna::search::tt_bound::upper);
    tuna::move best; int score; int depth; tuna::search::tt_bound bound;
    require(tt.probe(key, best, score, depth, bound), "UPPER sufficient entry present");
    require(bound == tuna::search::tt_bound::upper && score == -100, "UPPER entry correct");
  }

  {
    auto tt = tuna::search::transposition_table{1024};
    const auto key = pos.key();
    tt.store(key, plain.best_move, plain.score, 1, tuna::search::tt_bound::exact);
    auto search_pos = tuna::position::from_fen(fen);
    auto fresh_pos = tuna::position::from_fen(fen);
    const auto with_shallow_tt = tuna::search::alpha_beta(search_pos, 3, true, true, false, false);
    const auto fresh = tuna::search::alpha_beta(fresh_pos, 3, true, true, false, false);
    require(with_shallow_tt.score == fresh.score, "insufficient depth must not change result (depth 1 vs 3)");
    require(with_shallow_tt.best_move == fresh.best_move || with_shallow_tt.score == fresh.score, "shallow tt move consistent");
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
  return 0;
}