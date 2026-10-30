#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"

#include <atomic>
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

auto test_single_thread_matches_search() -> void
{
  auto pos = russell::position::start();
  auto stopper = russell::search::search_stopper{};
  const auto limits = russell::search::search_limits{3, 0};
  const auto single = russell::search::search(pos, limits, stopper);
  const auto parallel = russell::search::parallel_search(pos, limits, stopper, 1);
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
      auto pos = russell::position::from_fen(fen);
      auto stopper = russell::search::search_stopper{};
      const auto limits = russell::search::search_limits{3, 0};
      const auto result = russell::search::parallel_search(pos, limits, stopper, threads);
      require(result.has_move, ("parallel has move " + fen).c_str());
      require(is_legal(pos, result.best_move), ("parallel best move legal " + fen).c_str());
      require(result.depth == 3, ("parallel reaches depth " + fen).c_str());
    }
  }
}

auto test_parallel_respects_stop() -> void
{
  auto pos = russell::position::start();
  auto stopper = russell::search::search_stopper{};
  const auto limits = russell::search::search_limits{0, 0};
  auto result = russell::search::search_result{};
  auto done = std::atomic<bool>{false};
  auto worker = std::thread([&]() {
    result = russell::search::parallel_search(pos, limits, stopper, 2);
    done.store(true, std::memory_order_relaxed);
  });
  while(!done.load(std::memory_order_relaxed)) {
    stopper.stop.store(true, std::memory_order_relaxed);
  }
  worker.join();
  require(result.has_move, "stopped parallel search returns a move");
  require(is_legal(pos, result.best_move), "stopped parallel best move legal");
}

auto completed_result(int depth) -> russell::search::search_result
{
  auto r = russell::search::search_result{};
  r.depth = depth;
  r.has_move = true;
  return r;
}

auto test_selection_completed_beats_incomplete() -> void
{
  using russell::search::select_completed_result;

  auto results = std::vector<russell::search::search_result>{completed_result(6), {}, {}};
  results[1].depth = 7;
  results[2].depth = 0;
  results[2].has_move = true;
  require(select_completed_result(results.data(), results.size(), 0) == 0,
          "completed depth 6 beats incomplete depth 7 and emergency");
  results[1].has_move = true;
  require(select_completed_result(results.data(), results.size(), 0) == 1,
          "completed deeper result wins");
}

auto test_selection_tie_prefers_main_and_deterministic() -> void
{
  using russell::search::select_completed_result;
  auto results = std::vector<russell::search::search_result>{
    completed_result(3), completed_result(4), completed_result(4), completed_result(2)};
  const auto first = select_completed_result(results.data(), results.size(), 0);
  const auto second = select_completed_result(results.data(), results.size(), 0);
  require(first == 1 && second == 1, "tie keeps lowest slot above main; deterministic");
  results[0] = completed_result(4);
  require(select_completed_result(results.data(), results.size(), 0) == 0,
          "main worker wins completed-depth ties");
}

auto test_selection_none_eligible_falls_back_to_main() -> void
{
  using russell::search::select_completed_result;
  auto results = std::vector<russell::search::search_result>{{}, {}, {}};
  require(select_completed_result(results.data(), results.size(), 0) == 0,
          "no completed result falls back to main slot");
  require(select_completed_result(results.data(), 0, 0) == 0, "empty set falls back to main");
}

auto test_workers_publish_valid_completed_results() -> void
{

  const auto fens = std::vector<std::string>{
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
  };
  for(const auto& fen : fens) {
    for(auto threads = 2; threads <= 4; ++threads) {
      auto pos = russell::position::from_fen(fen);
      auto stopper = russell::search::search_stopper{};
      const auto result = russell::search::parallel_search(pos, {3, 0}, stopper, threads);
      require(result.has_move, ("worker result has move " + fen).c_str());
      require(result.depth == 3, ("worker result completed depth " + fen).c_str());
      require(is_legal(pos, result.best_move), ("worker result move legal " + fen).c_str());
    }
  }
}

auto test_stop_terminates_all_workers() -> void
{

  auto pos = russell::position::from_fen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto stopper = russell::search::search_stopper{};
  stopper.stop.store(true, std::memory_order_relaxed);
  const auto result = russell::search::parallel_search(pos, {100, 0}, stopper, 4);
  if(result.has_move) {
    require(is_legal(pos, result.best_move), "post-stop move (if any) is legal");
  }
}

auto test_repeated_parallel_searches_valid() -> void
{

  const auto fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  auto tt = russell::search::transposition_table{};
  for(auto run = 0; run < 3; ++run) {
    auto pos = russell::position::from_fen(fen);
    auto stopper = russell::search::search_stopper{};
    const auto result = russell::search::parallel_search(pos, {3, 0}, stopper, 3, tt);
    require(result.has_move, "repeated parallel search has move");
    require(result.depth == 3, "repeated parallel search completed depth");
    require(is_legal(pos, result.best_move), "repeated parallel best move legal");
  }
}

auto test_parallel_shares_tt_safely() -> void
{

  auto pos = russell::position::from_fen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto stopper = russell::search::search_stopper{};
  const auto result = russell::search::parallel_search(pos, {3, 0}, stopper, 6);
  require(result.has_move, "shared-TT parallel search has move");
  require(result.depth == 3, "shared-TT parallel search completed depth");
  require(is_legal(pos, result.best_move), "shared-TT parallel best move legal");
}

auto test_aggregate_worker_nodes_sums_and_saturates() -> void
{
  using russell::search::aggregate_worker_nodes;
  auto mk = [](std::uint64_t n) {
    auto r = russell::search::search_result{};
    r.nodes = n;
    return r;
  };
  const auto trio = std::vector<russell::search::search_result>{mk(10), mk(20), mk(30)};
  require(aggregate_worker_nodes(trio.data(), trio.size()) == 60, "node sum aggregates");
  const auto with_zero = std::vector<russell::search::search_result>{mk(0), mk(5)};
  require(aggregate_worker_nodes(with_zero.data(), with_zero.size()) == 5,
          "zero-node workers contribute nothing but count");
  constexpr auto max = std::numeric_limits<std::uint64_t>::max();
  const auto saturating = std::vector<russell::search::search_result>{mk(max), mk(1)};
  require(aggregate_worker_nodes(saturating.data(), saturating.size()) == max,
          "aggregation saturates instead of wrapping");
  require(aggregate_worker_nodes(nullptr, 0) == 0, "empty aggregation is zero");
}

auto test_threads_one_nodes_match() -> void
{

  auto pos = russell::position::start();
  auto stopper = russell::search::search_stopper{};
  const auto single = russell::search::search(pos, {3, 0}, stopper);
  const auto parallel = russell::search::parallel_search(pos, {3, 0}, stopper, 1);
  require(parallel.nodes == single.nodes, "threads=1 node count matches search");
}

auto test_node_limit_total_semantics() -> void
{

  const auto fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  auto limits = russell::search::search_limits{};
  limits.nodes = 20000;
  {
    auto pos = russell::position::from_fen(fen);
    auto stopper = russell::search::search_stopper{};
    const auto r = russell::search::parallel_search(pos, limits, stopper, 3);
    require(r.nodes >= 20000, "parallel total reaches node limit");
    require(r.nodes <= 40000, "parallel total stops near node limit");
  }
  {
    auto pos = russell::position::from_fen(fen);
    auto stopper = russell::search::search_stopper{};
    const auto r = russell::search::parallel_search(pos, limits, stopper, 1);
    require(r.nodes >= 20000, "single total reaches node limit");
    require(r.nodes <= 36384, "single total stops near node limit");
  }
}

auto test_movetime_authority_parallel() -> void
{

  auto pos = russell::position::from_fen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto stopper = russell::search::search_stopper{};
  auto limits = russell::search::search_limits{};
  limits.soft_time_ms = 25;
  limits.hard_time_ms = 60;
  const auto result = russell::search::parallel_search(pos, limits, stopper, 3);
  require(result.has_move, "movetime parallel search has move");
  require(result.depth >= 1, "movetime parallel search completed depth");
  require(is_legal(pos, result.best_move), "movetime parallel best move legal");
}

auto test_infinite_preset_stop_parallel() -> void
{

  auto pos = russell::position::from_fen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto stopper = russell::search::search_stopper{};
  stopper.stop.store(true, std::memory_order_relaxed);
  auto limits = russell::search::search_limits{};
  limits.infinite = true;
  const auto result = russell::search::parallel_search(pos, limits, stopper, 3);
  if(result.has_move) {
    require(is_legal(pos, result.best_move), "post-stop infinite move (if any) is legal");
  }
}

auto test_external_stop_parallel_three() -> void
{

  auto pos = russell::position::from_fen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto stopper = russell::search::search_stopper{};
  auto result = russell::search::search_result{};
  auto done = std::atomic<bool>{false};
  auto worker = std::thread([&]() {
    auto limits = russell::search::search_limits{};
    limits.depth = 30;
    result = russell::search::parallel_search(pos, limits, stopper, 3);
    done.store(true, std::memory_order_relaxed);
  });
  while(!done.load(std::memory_order_relaxed)) {
    stopper.stop.store(true, std::memory_order_relaxed);
  }
  worker.join();
  if(result.has_move) {
    require(is_legal(pos, result.best_move), "externally stopped move (if any) is legal");
  }
}

}

auto main() -> int
{
  test_single_thread_matches_search();
  test_parallel_returns_legal_moves();
  test_parallel_respects_stop();
  test_selection_completed_beats_incomplete();
  test_selection_tie_prefers_main_and_deterministic();
  test_selection_none_eligible_falls_back_to_main();
  test_workers_publish_valid_completed_results();
  test_stop_terminates_all_workers();
  test_repeated_parallel_searches_valid();
  test_parallel_shares_tt_safely();
  test_aggregate_worker_nodes_sums_and_saturates();
  test_threads_one_nodes_match();
  test_node_limit_total_semantics();
  test_movetime_authority_parallel();
  test_infinite_preset_stop_parallel();
  test_external_stop_parallel_three();
  return 0;
}
