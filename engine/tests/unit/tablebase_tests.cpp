#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
#include "tb/tablebase.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#ifndef TUN_TB_TEST_DIR
#define TUN_TB_TEST_DIR "engine/tests/tablebases"
#endif

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

auto search_depth(const tuna::position& pos, int depth) -> tuna::search::search_result
{
  auto copy = pos;
  auto stopper = tuna::search::search_stopper{};
  auto limits = tuna::search::search_limits{};
  limits.depth = depth;
  return tuna::search::search(copy, limits, stopper, {});
}

auto test_init_largest() -> void
{
  tuna::tb::unload();
  require(tuna::tb::init(TUN_TB_TEST_DIR), "tablebase init succeeds");
  require(tuna::tb::largest() >= 3, "tablebase largest covers 3-piece positions");
  require(tuna::tb::is_loaded(), "tablebase reports loaded");
}

auto test_probe_wdl() -> void
{
  struct case_t {
    const char* fen;
    int expected;
  };
  const auto cases = std::initializer_list<case_t>{

    {"k7/2Q5/1K6/8/8/8/8/8 w - - 0 1", tuna::tb::win},

    {"7k/8/8/8/8/8/8/KR6 w - - 0 1", tuna::tb::win},

    {"7k/8/8/8/8/8/7P/7K w - - 0 1", tuna::tb::draw},

    {"8/8/8/8/8/8/4P3/K6k w - - 0 1", tuna::tb::win},

    {"k7/1QK5/8/8/8/8/8/8 b - - 0 1", tuna::tb::loss},

    {"7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", tuna::tb::draw},
  };
  for(const auto& c : cases) {
    auto pos = tuna::position::from_fen(c.fen);
    auto success = 0;
    const auto wdl = tuna::tb::probe_wdl(pos, &success);
    if(success != 1 || wdl != c.expected) {
      std::cerr << "fen=" << c.fen << " wdl=" << wdl << " success=" << success << "\n";
      require(false, "probe_wdl mismatch");
    }
  }
}

auto test_probe_root_mate_in_one() -> void
{
  auto pos = tuna::position::from_fen("k7/2Q5/1K6/8/8/8/8/8 w - - 0 1");
  auto result = tuna::tb::probe_result{};
  require(tuna::tb::probe_root(pos, result), "root probe succeeds");
  require(result.has_move, "root probe reports a move");
  require(result.wdl == tuna::tb::win, "root probe WDL is a win");
  require(result.score > 20000, "root probe win scores as a mate");
  require(is_legal(pos, result.best_move), "root probe move is legal");

  auto after = pos;
  const auto state = after.make_move(result.best_move);
  (void)state;
  auto copy = after;
  require(tuna::movegen::generate_legal(copy).size() == 0, "root probe move mates");
  const auto defender_king = static_cast<int>(std::countr_zero(
      after.pieces(tuna::opposite(after.side_to_move()), tuna::piece_type::king)));
  require(tuna::movegen::is_square_attacked(after, defender_king,
                                            tuna::opposite(after.side_to_move())),
          "root probe move checks the king");
}

auto test_probe_root_draw() -> void
{
  auto pos = tuna::position::from_fen("7k/8/8/8/8/8/7P/7K w - - 0 1");
  auto result = tuna::tb::probe_result{};
  require(tuna::tb::probe_root(pos, result), "root probe succeeds on draw");
  require(result.wdl == tuna::tb::draw, "root probe WDL is a draw");
  require(result.score == 0, "root probe draw scores zero");
  require(result.has_move, "root probe draw still has a move");
}

auto test_probe_root_no_moves() -> void
{
  auto mated = tuna::position::from_fen("k7/1QK5/8/8/8/8/8/8 b - - 0 1");
  auto result = tuna::tb::probe_result{};
  require(tuna::tb::probe_root(mated, result), "root probe succeeds on checkmate");
  require(!result.has_move, "checkmate root has no move");
  require(result.score == -tuna::search::mate_value, "checkmate root scores as mate");

  auto stalemated = tuna::position::from_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
  result = tuna::tb::probe_result{};
  require(tuna::tb::probe_root(stalemated, result), "root probe succeeds on stalemate");
  require(!result.has_move, "stalemate root has no move");
  require(result.score == 0, "stalemate root scores zero");
}

auto test_search_returns_tablebase() -> void
{
  auto mate = tuna::position::from_fen("k7/2Q5/1K6/8/8/8/8/8 w - - 0 1");
  auto mate_result = search_depth(mate, 3);
  require(mate_result.has_move, "search returns a tablebase move");
  require(is_legal(mate, mate_result.best_move), "search tablebase move is legal");
  require(mate_result.score > 20000, "search reports the tablebase mate score");

  auto draw = tuna::position::from_fen("7k/8/8/8/8/8/7P/7K w - - 0 1");
  auto draw_result = search_depth(draw, 3);
  require(draw_result.has_move, "search returns a move for the draw position");
  require(draw_result.score == 0, "search reports the tablebase draw score");

  auto mated = tuna::position::from_fen("k7/1QK5/8/8/8/8/8/8 b - - 0 1");
  auto mated_result = search_depth(mated, 3);
  require(!mated_result.has_move, "search reports a checkmated root without a move");
  require(mated_result.score < -20000, "search reports the checkmate score");
}

auto test_search_falls_back_when_not_covered() -> void
{

  auto pos = tuna::position::start();
  auto result = search_depth(pos, 3);
  require(result.has_move, "uncovered position runs a normal search");
  require(is_legal(pos, result.best_move), "uncovered search move is legal");
}

auto test_unload_disables_probing() -> void
{
  tuna::tb::unload();
  require(tuna::tb::largest() == 0, "unload clears the tablebase size");
  auto pos = tuna::position::from_fen("k7/2Q5/1K6/8/8/8/8/8 w - - 0 1");
  auto probe = tuna::tb::probe_result{};
  require(!tuna::tb::probe_root(pos, probe), "probing is disabled after unload");
  auto result = search_depth(pos, 3);
  require(result.has_move, "search still returns a move without tablebases");
  require(is_legal(pos, result.best_move), "fallback search move is legal");
}

auto test_root_move_is_wdl_optimal() -> void
{

  require(tuna::tb::init(TUN_TB_TEST_DIR), "tablebase init for optimality test");

  auto seed = 0x5eed7u;
  auto rnd = [&seed]() -> unsigned {
    seed = seed * 1664525u + 1013904223u;
    return seed;
  };

  auto make_fen = [](int wk, int bk, int pc, char ch) -> std::string {
    auto board = std::string(64, '.');
    board[wk] = 'K';
    board[bk] = 'k';
    board[pc] = ch;
    std::string fen;
    for(auto rank = 7; rank >= 0; --rank) {
      auto empty = 0;
      for(auto file = 0; file < 8; ++file) {
        const auto c = board[rank * 8 + file];
        if(c == '.') {
          ++empty;
        } else {
          if(empty > 0) {
            fen += std::to_string(empty);
            empty = 0;
          }
          fen += c;
        }
      }
      if(empty > 0) {
        fen += std::to_string(empty);
      }
      if(rank > 0) {
        fen += '/';
      }
    }
    fen += " w - - 0 1";
    return fen;
  };

  auto random_square = [&rnd](const int* taken, int taken_count) -> int {
    for(;;) {
      const auto sq = static_cast<int>(rnd() & 63u);
      auto used = false;
      for(auto i = 0; i < taken_count; ++i) {
        if(taken[i] == sq) {
          used = true;
          break;
        }
      }
      if(!used) {
        return sq;
      }
    }
  };

  auto adjacent = [](int a, int b) -> bool {
    const auto dr = (a >> 3) - (b >> 3);
    const auto dc = (a & 7) - (b & 7);
    return dr >= -1 && dr <= 1 && dc >= -1 && dc <= 1;
  };

  auto generate = [&](char ch, int min_rank, int max_rank) -> tuna::position {
    for(;;) {
      const auto wk = random_square(nullptr, 0);
      const auto bk = [&]() -> int {
        const int taken[] = {wk};
        for(;;) {
          const auto sq = random_square(taken, 1);
          if(!adjacent(wk, sq)) {
            return sq;
          }
        }
      }();
      const int taken[] = {wk, bk};
      const auto pc = random_square(taken, 2);
      const auto pr = pc >> 3;
      if(pr < min_rank || pr > max_rank) {
        continue;
      }
      auto pos = tuna::position::from_fen(make_fen(wk, bk, pc, ch));

      if(!tuna::movegen::is_square_attacked(pos, bk, tuna::color::white)) {
        return pos;
      }
    }
  };

  auto verified = 0;
  auto verify = [&verified](const tuna::position& pos, const char* label) {
    auto root = tuna::tb::probe_result{};
    require(tuna::tb::probe_root(pos, root), label);

    auto best_outcome = -1;
    auto legal_moves = 0;
    {
      auto copy = pos;
      for(const auto mv : tuna::movegen::generate_legal(copy)) {
        ++legal_moves;
        auto after = pos;
        (void)after.make_move(mv);

        after.set_halfmove_clock(0);
        auto success = 0;
        const auto defender = tuna::tb::probe_wdl(after, &success);
        if(success == 1) {
          best_outcome = std::max(best_outcome, 4 - defender);
        }
      }
    }
    require(legal_moves > 0, label);
    require(root.has_move, label);
    require(is_legal(pos, root.best_move), label);
    require(root.wdl == best_outcome, label);

    auto after = pos;
    (void)after.make_move(root.best_move);
    after.set_halfmove_clock(0);
    auto success = 0;
    const auto defender = tuna::tb::probe_wdl(after, &success);
    require(success == 1, label);
    require(4 - defender == best_outcome, label);
    ++verified;
  };

  for(auto i = 0; i < 12; ++i) {
    verify(generate('Q', 0, 7), "KQvK root move is WDL-optimal");
    verify(generate('R', 0, 7), "KRvK root move is WDL-optimal");
    verify(generate('P', 1, 5), "KPvK root move is WDL-optimal");
  }
  require(verified >= 30, "at least 30 tablebase positions verified");
}

}

auto main() -> int
{
  test_init_largest();
  test_probe_wdl();
  test_probe_root_mate_in_one();
  test_probe_root_draw();
  test_probe_root_no_moves();
  test_search_returns_tablebase();
  test_search_falls_back_when_not_covered();
  test_unload_disables_probing();
  test_root_move_is_wdl_optimal();
  tuna::tb::unload();
  return 0;
}