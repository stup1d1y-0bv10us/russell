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
  const int depth = 3;
  tuna::search::search_limits limits; limits.depth = depth;

  {
    auto p = pos;
    tuna::search::search_stopper stopper;
    tuna::search::transposition_table tt;
    const auto r1 = tuna::search::parallel_search(p, limits, stopper, 1, tt, {});
    auto p2 = pos;
    tuna::search::search_stopper stopper2;
    tuna::search::transposition_table tt2;
    const auto r2 = tuna::search::parallel_search(p2, limits, stopper2, 2, tt2, {});
    require(r1.has_move && r2.has_move, "both have move");
    require(r1.best_move == r2.best_move, "parallel 1 vs 2 threads same best move (no change from optimization)");
    require(r1.score == r2.score, "parallel 1 vs 2 same score");
  }

  {
    auto p1 = pos;
    tuna::search::search_stopper s1;
    tuna::search::transposition_table tt1;
    const auto r1 = tuna::search::parallel_search(p1, limits, s1, 2, tt1, {});
    auto p2 = pos;
    tuna::search::search_stopper s2;
    tuna::search::transposition_table tt2;
    const auto r2 = tuna::search::parallel_search(p2, limits, s2, 2, tt2, {});
    require(r1.best_move == r2.best_move, "parallel deterministic best move");
    require(r1.score == r2.score, "parallel deterministic score");
  }
  std::cout<<"parallel search opt regression passed\n";
  return 0;
}