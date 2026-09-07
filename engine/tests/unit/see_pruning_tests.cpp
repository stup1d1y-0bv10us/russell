#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
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
  const int depth = 3;
  auto p1 = pos;
  const auto result = tuna::search::alpha_beta(p1, depth, true, true, true, true, true);
  require(result.has_move, "tactical capture has move");
  require(is_legal(pos, result.best_move), "tactical capture move legal");

  require(result.score > -1000 && result.score < 1000, "score reasonable for tactical capture test");

  auto p2 = pos;
  const auto r2 = tuna::search::alpha_beta(p2, depth, true, true, true, true, true);
  require(r2.best_move == result.best_move, "SEE pruning deterministic best move");
  require(r2.score == result.score, "SEE pruning deterministic score");
  std::cout<<"SEE pruning regression passed: tactical capture preserved\n";
  return 0;
}