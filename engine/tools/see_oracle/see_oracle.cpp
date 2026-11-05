#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/ordering.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

auto show_sq(int sq) -> std::string
{
  return std::string(1, static_cast<char>('a' + (sq & 7))) + std::to_string(1 + (sq >> 3));
}

}

auto main() -> int
{
  const std::vector<std::string> fens = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/RNBQK2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",

    "4rk1/8/8/8/8/8/R2P1PPP/4K2R w K - 0 1",

    "4r1k1/8/8/8/8/2q5/3Q1PPP/4K2R w - - 0 1",

    "3rk3/8/8/8/8/8/R2R1PPP/4K3 w - - 0 1",

    "5rk1/8/8/8/1b6/8/3B1PPP/4K2R w - - 0 1",

    "8/8/8/8/8/5K2/6p1/7k w - - 0 1",

    "4r1k1/8/8/8/8/3p1p2/4P1PP/4K2R w K - 0 1",

    "4r1k1/8/8/3pP3/8/8/5PPP/4K2R w - d6 0 1",
    "6k1/8/8/3pP3/8/8/5PPP/4K2R w - d6 0 1",

    "R5k1/1P6/8/8/8/8/8/1K6 w - - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",

    "4r1k1/8/8/8/8/5n2/4N1PP/4K2R w - - 0 1",

    "3qk3/8/8/8/8/8/3B1PPP/4K2R w K - 0 1",

    "4r1k1/8/8/8/1b6/8/5PPP/4K2R w K - 0 1",

    "r2q1rk1/pp1bppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 0 10",
    "2r3k1/p5pp/8/8/8/8/P4PPP/2R3K1 w - - 0 1",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1",
    "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 1",
  };
  for(const auto& fen : fens) {
    auto pos = russell::position::from_fen(fen);
    auto copy = pos;
    const auto pseudo = russell::movegen::generate_pseudo_legal(copy);
    for(const auto mv : pseudo) {
      if(!russell::search::is_capture(mv)) continue;
      auto probe = pos;
      const auto see = russell::search::static_exchange_eval(probe, mv);
      std::printf("%s %s-%s p%d f%d see=%d\n", fen.c_str(), show_sq(mv.from).c_str(),
                  show_sq(mv.to).c_str(), static_cast<int>(mv.promotion),
                  static_cast<int>(mv.flag), see);
    }
  }
  return 0;
}
