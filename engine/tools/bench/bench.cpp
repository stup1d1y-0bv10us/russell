#include "core/position.hpp"
#include "search/search.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct bench_case {
  const char* name;
  const char* fen;
};

constexpr auto suite = std::to_array<bench_case>({
  {"startpos",
   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
  {"kiwipete",
   "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
  {"position 3",
   "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"},
  {"position 4",
   "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"},
  {"position 5",
   "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"},
  {"position 6",
   "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"},
});

}

auto main(int argc, char** argv) -> int
{
  const auto depth = argc >= 2 ? std::atoi(argv[1]) : 8;
  if(depth < 1 || depth > 16) {
    std::fprintf(stderr, "usage: tuna_bench [depth 1..16]\n");
    return 2;
  }

  auto total_nodes = std::uint64_t{0};
  const auto start = std::chrono::steady_clock::now();
  for(const auto& test : suite) {
    auto pos = tuna::position::from_fen(test.fen);
    const auto result = tuna::search::iterative_deepening(pos, depth);
    total_nodes += result.nodes;
    std::printf("%-11s depth %d %8llu nodes\n", test.name, depth,
                static_cast<unsigned long long>(result.nodes));
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start)
                      .count();
  const auto nps = ms == 0 ? 0 : static_cast<std::uint64_t>(total_nodes) * 1000 / ms;
  std::printf("total %llu nodes, %llu ms, %llu nps\n",
              static_cast<unsigned long long>(total_nodes),
              static_cast<unsigned long long>(ms),
              static_cast<unsigned long long>(nps));
  return 0;
}