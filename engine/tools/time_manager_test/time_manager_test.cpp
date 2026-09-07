#include "core/position.hpp"
#include "search/search.hpp"
#include "uci/uci.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

struct time_control {
  const char* name;
  int base_ms;
  int inc_ms;
};

constexpr auto controls = std::to_array<time_control>({
  {"1+0", 1000, 0},
  {"1+0.1", 1000, 100},
  {"3+0", 3000, 0},
  {"3+2", 3000, 2000},
  {"10+0.1", 10000, 100},
  {"10+5", 10000, 5000},
});

constexpr auto minimum_budget_ms = 10;

struct per_tc_stats {
  std::int64_t moves = 0;
  std::int64_t below_floor = 0;
  std::int64_t hard_above_available = 0;
  std::int64_t search_over_hard = 0;
  std::int64_t search_over_available = 0;
  std::int64_t flagged = 0;
  std::int64_t min_actual = 1LL << 60;
  std::int64_t max_actual = 0;
  std::int64_t sum_actual = 0;
  int min_depth = 1 << 20;
  int max_depth = 0;
  std::int64_t sum_depth = 0;
};

auto move_string(tuna::move mv) -> std::string
{
  auto out = std::string{};
  out.reserve(5);
  out.push_back(static_cast<char>('a' + tuna::file_of(mv.from)));
  out.push_back(static_cast<char>('1' + tuna::rank_of(mv.from)));
  out.push_back(static_cast<char>('a' + tuna::file_of(mv.to)));
  out.push_back(static_cast<char>('1' + tuna::rank_of(mv.to)));
  if(mv.flag == tuna::move_flag::promotion || mv.flag == tuna::move_flag::promotion_capture) {
    switch(mv.promotion) {
    case tuna::piece_type::queen: out.push_back('q'); break;
    case tuna::piece_type::rook: out.push_back('r'); break;
    case tuna::piece_type::bishop: out.push_back('b'); break;
    case tuna::piece_type::knight: out.push_back('n'); break;
    default: break;
    }
  }
  return out;
}

auto play_game(const time_control& tc, int moves_cap) -> per_tc_stats
{
  auto pos = tuna::position::start();
  auto stopper = tuna::search::search_stopper{};
  auto tt = tuna::search::transposition_table{};
  auto wtime = tc.base_ms;
  auto btime = tc.base_ms;
  auto stats = per_tc_stats{};

  std::printf("=== %-7s (wtime %d btime %d winc %d binc %d) ===\n", tc.name, tc.base_ms,
              tc.base_ms, tc.inc_ms, tc.inc_ms);
  std::printf("  %3s %4s %8s %5s %5s %7s %6s %s\n", "n", "side", "remain", "soft", "hard",
              "actual", "depth", "move");

  for(auto n = 1; n <= moves_cap; ++n) {
    const auto us = pos.side_to_move();
    const auto remaining = us == tuna::color::white ? wtime : btime;
    const auto inc = us == tuna::color::white ? tc.inc_ms : tc.inc_ms;
    const auto budget = tuna::uci::time_budget_ms(remaining, inc, tuna::uci::default_moves_to_go,
                                                  tuna::uci::default_move_overhead_ms);

    auto limits = tuna::search::search_limits{};
    limits.soft_time_ms = budget.soft_ms;
    limits.hard_time_ms = budget.hard_ms;

    const auto available = remaining - tuna::uci::default_move_overhead_ms;
    if(std::min(budget.soft_ms, budget.hard_ms) < minimum_budget_ms) {
      ++stats.below_floor;
    }
    if(budget.hard_ms > available) {
      ++stats.hard_above_available;
    }

    const auto search_start = std::chrono::steady_clock::now();
    const auto result = tuna::search::search(pos, limits, stopper, tt);
    const auto actual_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - search_start)
                               .count();

    if(actual_ms > budget.hard_ms) {
      ++stats.search_over_hard;
    }
    if(actual_ms > available) {
      ++stats.search_over_available;
    }

    std::printf("  %3d %4s %8d %5d %5d %7lld %6d %s\n", n, us == tuna::color::white ? "w" : "b",
                remaining, budget.soft_ms, budget.hard_ms,
                static_cast<long long>(actual_ms), result.depth,
                result.has_move ? move_string(result.best_move).c_str() : "-");

    stats.moves += 1;
    stats.min_actual = std::min(stats.min_actual, actual_ms);
    stats.max_actual = std::max(stats.max_actual, actual_ms);
    stats.sum_actual += actual_ms;
    stats.min_depth = std::min(stats.min_depth, result.depth);
    stats.max_depth = std::max(stats.max_depth, result.depth);
    stats.sum_depth += result.depth;

    if(us == tuna::color::white) {
      wtime = wtime - static_cast<int>(actual_ms) + tc.inc_ms;
    } else {
      btime = btime - static_cast<int>(actual_ms) + tc.inc_ms;
    }
    if((us == tuna::color::white ? wtime : btime) < 0) {
      ++stats.flagged;
      std::printf("  FLAG: %s ran out of time on move %d\n", us == tuna::color::white ? "white" : "black", n);
      break;
    }
    if(!result.has_move) {
      break;
    }
    static_cast<void>(pos.make_move(result.best_move));
  }

  const auto avg_actual = stats.moves == 0 ? 0 : stats.sum_actual / stats.moves;
  const auto avg_depth = stats.moves == 0 ? 0 : static_cast<int>(stats.sum_depth / stats.moves);
  std::printf("moves=%lld  actual(min/avg/max)=%lld/%lld/%lldms  depth(min/avg/max)=%d/%d/%d\n",
              static_cast<long long>(stats.moves), static_cast<long long>(stats.min_actual),
              static_cast<long long>(avg_actual), static_cast<long long>(stats.max_actual),
              stats.min_depth, avg_depth, stats.max_depth);
  std::printf("  allocations below %dms floor: %lld\n", minimum_budget_ms,
              static_cast<long long>(stats.below_floor));
  std::printf("  hard budget above clock-after-overhead: %lld\n",
              static_cast<long long>(stats.hard_above_available));
  std::printf("  searches over hard budget: %lld\n", static_cast<long long>(stats.search_over_hard));
  std::printf("  searches over clock-after-overhead: %lld\n",
              static_cast<long long>(stats.search_over_available));
  std::printf("  flags (clock went negative): %lld\n", static_cast<long long>(stats.flagged));
  std::printf("\n");
  return stats;
}

}

auto main(int argc, char** argv) -> int
{
  const auto moves_cap = argc >= 2 ? std::atoi(argv[1]) : 30;
  if(moves_cap < 1 || moves_cap > 200) {
    std::fprintf(stderr, "usage: tuna_time_manager_test [moves per game 1..200]\n");
    return 2;
  }

  auto pass = true;
  for(const auto& tc : controls) {
    const auto stats = play_game(tc, moves_cap);
    if(stats.below_floor != 0 || stats.hard_above_available != 0 || stats.flagged != 0) {
      pass = false;
    }
  }

  if(pass) {
    std::printf("RESULT: PASS - no 1 ms allocations, hard budget always below clock-after-overhead, no time losses\n");
    return 0;
  }
  std::printf("RESULT: FAIL - time manager misallocated time\n");
  return 1;
}