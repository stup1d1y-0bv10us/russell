#include "core/position.hpp"
#include "movegen/movegen.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace {

struct perft_case {
  const char* name;
  const char* fen;
  std::uint64_t expected[7];
};

constexpr auto suite = std::to_array<perft_case>({
  {"startpos",
   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
   {0, 20, 400, 8902, 197281, 4865609, 119060324}},
  {"kiwipete",
   "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
   {0, 48, 2039, 97862, 4085603, 193690690, 8031647685}},
  {"position 3",
   "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
   {0, 14, 191, 2812, 43238, 674624, 11030083}},
  {"position 4",
   "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
   {0, 6, 264, 9467, 422333, 15833292, 706045033}},
  {"position 5",
   "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
   {0, 44, 1486, 62379, 2103487, 89941194, 3048196529}},
  {"position 6",
   "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
   {0, 46, 2079, 89890, 3894594, 164075551, 6923051137}},
});

auto perft(tuna::position& pos, int depth) -> std::uint64_t
{
  if(depth == 0) {
    return 1;
  }
  const auto moves = tuna::movegen::generate_legal(pos);
  if(depth == 1) {
    return static_cast<std::uint64_t>(moves.size());
  }
  auto nodes = std::uint64_t{0};
  for(const auto mv : moves) {
    const auto st = pos.make_move(mv);
    nodes += perft(pos, depth - 1);
    pos.unmake_move(mv, st);
  }
  return nodes;
}

auto run_single(const std::string& fen, int depth) -> bool
{
  auto pos = tuna::position::from_fen(fen);
  std::printf("perft(%d) = %llu\n", depth,
              static_cast<unsigned long long>(perft(pos, depth)));
  return true;
}

auto run_case(const perft_case& test, int depth) -> bool
{
  auto pos = tuna::position::from_fen(test.fen);
  auto ok = true;
  std::printf("%-11s", test.name);
  const auto start = std::chrono::steady_clock::now();
  for(auto d = 1; d <= depth; ++d) {
    const auto nodes = perft(pos, d);
    const auto pass = d <= 6 && nodes == test.expected[d];
    ok = ok && pass;
    std::printf("  d%d=%llu%s", d, static_cast<unsigned long long>(nodes),
                pass ? "" : " *MISMATCH*");
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start)
                      .count();
  std::printf("  [%llu ms] %s\n", static_cast<unsigned long long>(ms),
              ok ? "PASS" : "FAIL");
  return ok;
}

}

auto main(int argc, char** argv) -> int
{
  if(argc < 2) {
    std::fprintf(stderr, "usage: tuna_perft --suite <depth> | \"<fen>\" <depth>\n");
    return 2;
  }
  const auto arg = std::string{argv[1]};
  if(arg == "--suite") {
    if(argc < 3) {
      std::fprintf(stderr, "usage: tuna_perft --suite <depth>\n");
      return 2;
    }
    const auto depth = std::atoi(argv[2]);
    if(depth < 1 || depth > 6) {
      std::fprintf(stderr, "suite depth must be 1..6\n");
      return 2;
    }
    auto ok = true;
    for(const auto& test : suite) {
      ok = run_case(test, depth) && ok;
    }
    return ok ? 0 : 1;
  }
  if(argc < 3) {
    std::fprintf(stderr, "usage: tuna_perft \"<fen>\" <depth>\n");
    return 2;
  }
  const auto depth = std::atoi(argv[2]);
  if(depth < 1) {
    std::fprintf(stderr, "depth must be >= 1\n");
    return 2;
  }
  run_single(arg, depth);
  return 0;
}