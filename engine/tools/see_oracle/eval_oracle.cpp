#include "core/position.hpp"
#include "eval/evaluate.hpp"
#include "movegen/movegen.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

auto splitmix64(std::uint64_t& state) noexcept -> std::uint64_t
{
  state += 0x9E3779B97F4A7C15ULL;
  auto z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

}

auto main() -> int
{
  const std::vector<std::string> fixed = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/RNBQK2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "8/8/8/4k3/8/8/5PPP/5RK1 w - - 0 1",
    "6k1/5ppp/8/8/8/8/5PPP/6K1 w - - 0 1",
    "R5k1/1P6/8/8/8/8/8/1K6 w - - 0 1",
    "k7/8/8/8/8/8/8/K7 w - - 0 1",
  };
  auto n = 0;
  for(const auto& fen : fixed) {
    auto pos = russell::position::from_fen(fen);
    std::printf("%s eval=%d\n", fen.c_str(), russell::eval::evaluate(pos));
    ++n;
  }
  auto rng = std::uint64_t{0x9E3779B97F4A7C15ULL};
  for(auto g = 0; g < 60; ++g) {
    auto pos = russell::position::start();
    for(auto p = 0; p < 120; ++p) {
      auto copy = pos;
      const auto legal = russell::movegen::generate_legal(copy);
      if(legal.size() == 0) break;
      std::printf("%s eval=%d\n", pos.fen().c_str(), russell::eval::evaluate(pos));
      ++n;
      const auto pick = static_cast<std::size_t>(splitmix64(rng) % legal.size());
      static_cast<void>(pos.make_move(legal[pick]));
      if(pos.halfmove_clock() >= 100) break;
    }
  }
  std::printf("eval_oracle: %d positions\n", n);
  return 0;
}
