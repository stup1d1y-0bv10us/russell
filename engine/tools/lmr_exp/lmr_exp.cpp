#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

auto main() -> int
{
  using namespace tuna;
  using namespace tuna::search;

  std::cout << "=== aspiration window: iterative deepening vs re-running full-window depths ===\n";
  {
    const auto cases = std::vector<std::pair<std::string, int>>{
      {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 6},
      {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 3", 6},
    };
    for(const auto& [fen, depth] : cases) {
      auto id_pos = position::from_fen(fen);
      const auto id = iterative_deepening(id_pos, depth);
      auto sum = std::uint64_t{0};
      for(auto d = 1; d <= depth; ++d) {
        auto p = position::from_fen(fen);
        sum += alpha_beta(p, d).nodes;
      }
      std::cout << fen.substr(0, 12) << " d=" << depth
                << " id_total=" << id.nodes << " sum_full_window=" << sum
                << " ratio=" << (sum == 0 ? 0.0 : static_cast<double>(id.nodes) / sum) << '\n';
    }
  }

  std::cout << "\n=== quiet position node reduction (null move ON, real config) ===\n";
  const auto quiet_cases = std::vector<std::pair<std::string, int>>{
    {"8/8/8/8/8/4k3/8/4R2K w - - 0 1", 7},
    {"8/8/8/8/8/4k3/8/4R2K w - - 0 1", 8},
    {"4k3/8/8/8/8/8/4R3/4K3 w - - 0 1", 8},
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 7},
    {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 3", 7},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 6},
  };
  for(const auto& [fen, depth] : quiet_cases) {
    auto a = position::from_fen(fen);
    auto b = position::from_fen(fen);
    const auto lmr = alpha_beta(a, depth, true, true);
    const auto no_lmr = alpha_beta(b, depth, true, false);
    const auto pct = no_lmr.nodes == 0 ? 0 : static_cast<double>(lmr.nodes) / no_lmr.nodes * 100.0;
    std::cout << fen.substr(0, 24) << " d=" << depth << " no_lmr=" << no_lmr.nodes
              << " lmr=" << lmr.nodes << " (" << pct << "% of baseline)"
              << " applied=" << lmr.lmr_applied << " research=" << lmr.lmr_research << '\n';
  }

  std::cout << "\n=== same quiet cases, null move OFF (isolate LMR effect) ===\n";
  for(const auto& [fen, depth] : quiet_cases) {
    auto a = position::from_fen(fen);
    auto b = position::from_fen(fen);
    const auto lmr = alpha_beta(a, depth, false, true);
    const auto no_lmr = alpha_beta(b, depth, false, false);
    const auto pct = no_lmr.nodes == 0 ? 0 : static_cast<double>(lmr.nodes) / no_lmr.nodes * 100.0;
    std::cout << fen.substr(0, 24) << " d=" << depth << " no_lmr=" << no_lmr.nodes
              << " lmr=" << lmr.nodes << " (" << pct << "% of baseline)"
              << " applied=" << lmr.lmr_applied << " research=" << lmr.lmr_research << '\n';
  }

  std::cout << "\n=== tactical suite: LMR vs minimax match ===\n";
  const auto tactical_cases = std::vector<std::pair<std::string, int>>{
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
  for(const auto& [fen, depth] : tactical_cases) {
    auto a = position::from_fen(fen);
    auto b = position::from_fen(fen);
    const auto lmr = alpha_beta(a, depth, true, true);
    const auto plain = minimax(b, depth);
    const auto ok = lmr.score == plain.score && lmr.best_move == plain.best_move
                    && lmr.has_move == plain.has_move;
    std::cout << (ok ? "PASS " : "FAIL ") << fen.substr(0, 24) << " d=" << depth
              << " score=" << lmr.score << "/" << plain.score << '\n';
  }
  return 0;
}