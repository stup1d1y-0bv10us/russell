#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
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

auto test_single_thread_matches_search() -> void
{
  auto pos = tuna::position::start();
  auto stopper = tuna::search::search_stopper{};
  const auto limits = tuna::search::search_limits{3, 0};
  const auto single = tuna::search::search(pos, limits, stopper);
  const auto parallel = tuna::search::parallel_search(pos, limits, stopper, 1);
  require(parallel.has_move == single.has_move, "single-thread has_move matches search");
  require(parallel.best_move == single.best_move, "single-thread best move matches search");
  require(parallel.score == single.score, "single-thread score matches search");
  require(parallel.depth == single.depth, "single-thread depth matches search");
}

auto test_parallel_returns_legal_moves() -> void
{
  const auto positions = std::vector<std::string>{
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1",
  };
  for(const auto& fen : positions) {
    for(auto threads = 2; threads <= 4; ++threads) {
      auto pos = tuna::position::from_fen(fen);
      auto stopper = tuna::search::search_stopper{};
      const auto limits = tuna::search::search_limits{3, 0};
      const auto result = tuna::search::parallel_search(pos, limits, stopper, threads);
      require(result.has_move, ("parallel has move " + fen).c_str());
      require(is_legal(pos, result.best_move), ("parallel best move legal " + fen).c_str());
      require(result.depth == 3, ("parallel reaches depth " + fen).c_str());
    }
  }
}

auto test_parallel_respects_stop() -> void
{
  auto pos = tuna::position::start();
  auto stopper = tuna::search::search_stopper{};
  const auto limits = tuna::search::search_limits{0, 0};
  auto result = tuna::search::search_result{};
  auto done = std::atomic<bool>{false};
  auto worker = std::thread([&]() {
    result = tuna::search::parallel_search(pos, limits, stopper, 2);
    done.store(true, std::memory_order_relaxed);
  });
  while(!done.load(std::memory_order_relaxed)) {
    stopper.stop.store(true, std::memory_order_relaxed);
  }
  worker.join();
  require(result.has_move, "stopped parallel search returns a move");
  require(is_legal(pos, result.best_move), "stopped parallel best move legal");
}

}

auto main() -> int
{
  test_single_thread_matches_search();
  test_parallel_returns_legal_moves();
  test_parallel_respects_stop();
  return 0;
}