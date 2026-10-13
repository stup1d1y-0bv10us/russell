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
    auto pos = russell::position::from_fen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
    auto p = pos;
    const auto r = russell::search::alpha_beta(p, 3, true, true, true, true, true);
    require(r.has_move, "improving ply2 has move");
    require(is_legal(pos, r.best_move), "improving ply2 move legal");
    std::cout<<"improving_count depth3 quiet: "<<r.improving_count<<"\n";
    require(r.improving_count > 0, "improving fires at ply 2 once root eval seeds stack[0]");
  }

  {
    auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    auto p = pos;
    const auto r = russell::search::alpha_beta(p, 5, true, true, true, true, true);
    require(r.has_move, "rfp/razor history side has move");
    require(is_legal(pos, r.best_move), "rfp/razor history side move legal");
    require(r.rfp_pruned > 0, "RFP gate reached and prunes");
    require(r.razor_pruned > 0, "razor gate reached and prunes");
    std::cout<<"rfp_pruned depth5 kiwipete: "<<r.rfp_pruned<<"\n";
    std::cout<<"razor_pruned depth5 kiwipete: "<<r.razor_pruned<<"\n";
    std::cout<<"nodes depth5 kiwipete: "<<r.nodes<<"\n";
    require(r.rfp_pruned == 3416, "RFP prune count with previous-mover history side");
    require(r.razor_pruned == 1022, "razor prune count with previous-mover history side");
  }

  std::cout<<"improving regression passed\n";
  return 0;
}
