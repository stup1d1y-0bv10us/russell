#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
#include "search/transposition_table.hpp"
#include <cstdlib>
#include <iostream>

namespace {
auto require(bool v, const char* m){ if(!v){ std::cerr<<m<<"\n"; std::exit(1);} }
auto is_legal(const tuna::position& pos, tuna::move mv)->bool{
  auto c = pos;
  for(auto m: tuna::movegen::generate_legal(c)) if(m==mv) return true;
  return false;
}
}

int main(){
  auto pos = tuna::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const int depth = 4;

  tuna::search::search_limits lim; lim.depth = depth;
  tuna::search::search_stopper stopper;
  tuna::search::transposition_table tt1;
  auto p1 = pos;
  const auto r1 = tuna::search::search(p1, lim, stopper, tt1);
  require(r1.has_move, "IIR has move");
  require(is_legal(pos, r1.best_move), "IIR move legal");

  tuna::search::transposition_table tt2;
  auto p2 = pos;
  tuna::search::search_stopper s2;
  const auto r2 = tuna::search::search(p2, lim, s2, tt2);
  require(r2.best_move == r1.best_move, "IIR same best move");
  require(r2.score == r1.score, "IIR same score");

  tuna::search::search_limits lim3; lim3.depth = 3;
  tuna::search::transposition_table tt3;
  auto p3 = pos;
  tuna::search::search_stopper s3;
  const auto r3 = tuna::search::search(p3, lim3, s3, tt3);
  require(r1.nodes >= r3.nodes, "IIR depth4 nodes >= depth3 nodes");
  require(r1.nodes < 1000000, "IIR nodes bounded");
  std::cout<<"IIR regression passed: same best move/score, nodes not increased\n";
  return 0;
}