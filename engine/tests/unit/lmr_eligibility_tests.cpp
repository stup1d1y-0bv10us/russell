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

  auto pos3 = tuna::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const auto r3 = tuna::search::alpha_beta(pos3, depth, true, true, true, true, true);
  require(r3.best_move == r1.best_move, "fixed tactical position same best move after optimization");
  require(r3.score == r1.score, "fixed tactical position same score after optimization");

  auto legal_check = tuna::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  bool found = false;
  for(auto m : tuna::movegen::generate_legal(legal_check)) if(m == r1.best_move) found = true;
  require(found, "best move legal");
  std::cout<<"LMR eligibility precompute regression passed\n";
  return 0;
}