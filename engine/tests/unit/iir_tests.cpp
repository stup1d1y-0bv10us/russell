#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
#include "search/transposition_table.hpp"
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
  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const int depth = 4;

  russell::search::search_limits lim; lim.depth = depth;
  russell::search::search_stopper stopper;
  russell::search::transposition_table tt1;
  auto p1 = pos;
  const auto r1 = russell::search::search(p1, lim, stopper, tt1);
  require(r1.has_move, "IIR has move");
  require(is_legal(pos, r1.best_move), "IIR move legal");

  russell::search::transposition_table tt2;
  auto p2 = pos;
  russell::search::search_stopper s2;
  const auto r2 = russell::search::search(p2, lim, s2, tt2);
  require(r2.best_move == r1.best_move, "IIR same best move");
  require(r2.score == r1.score, "IIR same score");

  russell::search::search_limits lim3; lim3.depth = 3;
  russell::search::transposition_table tt3;
  auto p3 = pos;
  russell::search::search_stopper s3;
  const auto r3 = russell::search::search(p3, lim3, s3, tt3);
  require(r1.nodes >= r3.nodes, "IIR depth4 nodes >= depth3 nodes");
  require(r1.nodes < 1000000, "IIR nodes bounded");
  std::cout<<"IIR regression passed: same best move/score, nodes not increased\n";
  return 0;
}