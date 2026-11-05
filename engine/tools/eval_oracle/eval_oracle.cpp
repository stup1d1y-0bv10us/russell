#include "core/position.hpp"
#include "eval/evaluate.hpp"

#include <array>
#include <chrono>
#include <cstdio>

namespace {

constexpr auto kFens = std::to_array<const char*>({
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1",
  "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
  "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1",
  "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
  "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 b - - 0 1",
  "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
  "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
  "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
  "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
  "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2",
  "r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 3 9",
  "5rk1/5ppp/8/8/8/8/5PPP/5RK1 w - - 0 1",
  "8/8/4k3/8/8/3KN3/8/8 w - - 0 1",
  "8/1p6/8/8/8/8/1P6/7K w - - 0 1",
  "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
  "rnb1kbnr/pp1ppppp/8/q1p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 2 3",
  "r2q1rk1/pp1bppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 0 10",
  "8/2P5/8/1K6/8/8/6k1/8 w - - 0 1",
  "k7/8/8/3q4/3Q4/8/8/K7 w - - 0 1",
  "r1b2rk1/pp1p1ppp/2n1pn2/2p5/3P4/2NBPN2/PP3PPP/R2Q1RK1 w - - 0 9",
  "6k1/5ppp/8/8/8/8/5PPP/4R1K1 w - - 0 1",
  "rnbqk2r/ppp1ppbp/3p1np1/8/3P4/2N1PN2/PP3PPP/R1BQKB1R w KQkq - 2 5",
  "2r3k1/5ppp/8/8/8/8/5PPP/2R3K1 b - - 0 1",
});

}

auto main() -> int
{
  for(const auto* fen : kFens) {
    const auto pos = russell::position::from_fen(fen);
    const auto score = russell::eval::evaluate(pos);
    std::printf("%d %s\n", score, fen);
  }
  constexpr int kReps = 2000;
  auto positions = std::array<russell::position, kFens.size()>{};
  for(auto i = std::size_t{0}; i < kFens.size(); ++i) {
    positions[i] = russell::position::from_fen(kFens[i]);
  }
  auto checksum = 0;
  const auto start = std::chrono::steady_clock::now();
  for(auto r = 0; r < kReps; ++r) {
    for(const auto& pos : positions) {
      checksum += russell::eval::evaluate(pos);
    }
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start)
                      .count();
  std::printf("eval_micro %d reps x %llu fens, %lld ms, checksum %d\n",
              kReps, static_cast<unsigned long long>(kFens.size()),
              static_cast<long long>(ms), checksum);
  return 0;
}
