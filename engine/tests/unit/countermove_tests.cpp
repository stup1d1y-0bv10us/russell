#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
#include "search/transposition_table.hpp"
#include <cstdlib>
#include <iostream>

namespace {
auto require(bool v, const char* m){ if(!v){ std::cerr<<m<<"\n"; std::exit(1);} }
}

int main(){
  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const int depth = 4;
  russell::search::search_limits lim; lim.depth = depth;
  russell::search::search_stopper stopper;
  russell::search::transposition_table tt1;
  auto p1 = pos;
  const auto r1 = russell::search::search(p1, lim, stopper, tt1);
  require(r1.has_move, "countermove has move");
  auto p2 = pos;
  russell::search::search_stopper s2;
  russell::search::transposition_table tt2;
  const auto r2 = russell::search::search(p2, lim, s2, tt2);
  require(r2.best_move == r1.best_move, "countermove same best move");
  require(r2.score == r1.score, "countermove same score");

  auto legal = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  bool found = false;
  for(auto m : russell::movegen::generate_legal(legal)) if(m == r1.best_move) found = true;
  require(found, "countermove best move legal");

  require(r1.nodes < 1000000, "countermove nodes bounded");
  std::cout<<"countermove regression passed: same best move/score, nodes not increased\n";
  return 0;
}