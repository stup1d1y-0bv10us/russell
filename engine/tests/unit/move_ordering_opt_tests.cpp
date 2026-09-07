#include "core/position.hpp"
#include "search/search.hpp"
#include <cstdlib>
#include <iostream>

namespace {
auto require(bool v, const char* m){ if(!v){ std::cerr<<m<<"\n"; std::exit(1);} }
}

int main(){
  auto pos = tuna::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto pos2 = pos;
  const auto r1 = tuna::search::alpha_beta(pos, 3, true, true, true, true, true);
  const auto r2 = tuna::search::alpha_beta(pos2, 3, true, true, true, true, true);
  require(r1.has_move && r2.has_move, "both have move");
  require(r1.best_move == r2.best_move, "best move deterministic");
  require(r1.score == r2.score, "score deterministic");

  auto expected = r1.best_move;
  require(expected.from != 0 || expected.to != 0, "expected move non-null");

  auto pos3 = tuna::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const auto r3 = tuna::search::alpha_beta(pos3, 3, true, true, true, true, true);
  require(r3.best_move == expected, "fixed position produces same best move after optimization");
  require(r3.score == r1.score, "fixed position same score after optimization");
  std::cout<<"move ordering opt regression passed\n";
  return 0;
}