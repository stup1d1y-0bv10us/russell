#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
#include <cstdlib>
#include <iostream>

namespace {
auto require(bool v, const char* m){ if(!v){ std::cerr<<m<<"\n"; std::exit(1);} }
auto is_legal(const russell::position& pos, russell::move mv)->bool{
  auto c = pos;
  for(auto m: russell::movegen::generate_legal(c)) if(m==mv) return true;
  return false;
}
}

int main(){

  {

    using russell::search::rfp_margin;
    require(rfp_margin(3, 0) == 360, "base 120*depth preserved");

    require(rfp_margin(3, 4096) == 361, "positive history increases margin");
    require(rfp_margin(3, 8192) == 362, "larger history increases more");

    require(rfp_margin(3, -4096) == 359, "negative history decreases margin");
    require(rfp_margin(3, -8192) == 358, "larger negative decreases more");

    require(rfp_margin(3, 1000000) == 380, "saturates at +20");
    require(rfp_margin(3, -1000000) == 340, "saturates at -20");
    require(rfp_margin(3, 81920) == 380, "saturation boundary +20");
    require(rfp_margin(3, 81919) == 379, "just below saturation +19");
    require(rfp_margin(3, -81920) == 340, "saturation boundary -20");

    require(rfp_margin(4, 8192) + russell::search::improving_rfp_bonus == 480 + 2 + 40,
            "improving+history combined margin");
  }

  {

    auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    auto p = pos;
    const auto r = russell::search::alpha_beta(p, 5, true, true, true, true, true);
    require(r.has_move, "RFP/improving search has move");
    require(is_legal(pos, r.best_move), "RFP/improving move legal");
    require(r.improving_count > 0, "improving active alongside RFP");
    require(r.rfp_pruned > 0, "RFP prunes with history adjustment");
  }

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const int depth = 3;

  auto p1 = pos;
  const auto r1 = russell::search::alpha_beta(p1, depth, true, true, true, true, true);
  require(r1.has_move, "RFP quiet has move");
  require(is_legal(pos, r1.best_move), "RFP quiet move legal");

  auto p2 = pos;
  const auto r2 = russell::search::alpha_beta(p2, depth, true, true, true, true, true);
  require(r2.best_move == r1.best_move, "RFP deterministic best move");
  require(r2.score == r1.score, "RFP deterministic score");

  require(r1.score > -500 && r1.score < 500, "RFP tactical score reasonable");

  auto p3 = russell::position::from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
  const auto r3 = russell::search::alpha_beta(p3, 2, true, true, true, true, true);
  require(r3.has_move, "RFP shallow quiet has move");
  require(r3.nodes < 5000, "RFP shallow still bounded");
  std::cout<<"RFP regression passed: preserves correct move/score while reducing nodes\n";
  return 0;
}
