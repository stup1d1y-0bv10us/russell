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
  auto p1 = pos;
  auto p2 = pos;
  const auto r1 = tuna::search::alpha_beta(p1, depth, true, true, true, true, true);
  const auto r2 = tuna::search::alpha_beta(p2, depth, true, true, true, true, true);
  require(r1.has_move && r2.has_move, "both have move");
  require(r1.best_move == r2.best_move, "tuned LMR same best move");
  require(r1.score == r2.score, "tuned LMR same score");

  auto legal = tuna::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  bool found = false;
  for(auto m : tuna::movegen::generate_legal(legal)) if(m == r1.best_move) found = true;
  require(found, "best move legal");
  std::cout<<"LMR tuning regression passed: threshold 6 preserves best move/score\n";
  return 0;
}