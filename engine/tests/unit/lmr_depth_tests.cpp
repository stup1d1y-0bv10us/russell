#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
#include <cstdlib>
#include <iostream>

namespace {
auto require(bool v, const char* m){ if(!v){ std::cerr<<m<<"\n"; std::exit(1);} }
}

int main(){

  auto pos = tuna::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const int depth = 4;
  auto pos1 = pos;
  auto pos2 = pos;
  const auto r1 = tuna::search::alpha_beta(pos1, depth, true, true, true, true, true);
  const auto r2 = tuna::search::alpha_beta(pos2, depth, true, true, true, true, true);
  require(r1.has_move && r2.has_move, "both have move");
  require(r1.best_move == r2.best_move, "best move deterministic");
  require(r1.score == r2.score, "score deterministic");

  auto legal_check = tuna::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  bool found = false;
  for(auto m : tuna::movegen::generate_legal(legal_check)) if(m == r1.best_move) found = true;
  require(found, "best move legal");
  require(r1.score > -1000 && r1.score < 1000, "score reasonable for tactical position");
  std::cout<<"LMR depth condition regression passed: best move consistent\n";
  return 0;
}