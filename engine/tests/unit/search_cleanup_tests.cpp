#include "core/position.hpp"
#include "search/search.hpp"
#include "search/transposition_table.hpp"
#include <cstdlib>
#include <iostream>

namespace {
auto require(bool v, const char* m){ if(!v){ std::cerr<<m<<"\n"; std::exit(1);} }
}

int main(){

  auto pos = tuna::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  tuna::search::search_limits limits; limits.depth = 4;
  tuna::search::search_stopper stopper;
  tuna::search::transposition_table tt;
  auto p1 = pos;
  const auto r1 = tuna::search::search(p1, limits, stopper, tt, {});
  auto p2 = pos;
  tuna::search::search_stopper stopper2;
  tuna::search::transposition_table tt2;
  const auto r2 = tuna::search::search(p2, limits, stopper2, tt2, {});
  require(r1.has_move && r2.has_move, "both have move");
  require(r1.best_move == r2.best_move, "production search same best move after cleanup");
  require(r1.score == r2.score, "production search same score after cleanup");

  auto p3 = pos;
  tuna::search::search_stopper s3;
  tuna::search::transposition_table tt3;
  const auto r3 = tuna::search::parallel_search(p3, limits, s3, 2, tt3, {});
  require(r3.has_move, "parallel has move");
  require(r3.best_move == r1.best_move || r3.score == r1.score, "parallel consistent with single");
  std::cout<<"search cleanup regression passed\n";
  return 0;
}