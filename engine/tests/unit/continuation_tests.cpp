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
  const int depth = 4;
  tuna::search::search_limits lim; lim.depth = depth;

  tuna::search::search_stopper s1;
  tuna::search::transposition_table tt1;
  auto p1 = pos;
  const auto r1 = tuna::search::search(p1, lim, s1, tt1, {});
  require(r1.has_move, "continuation first has move");

  tuna::search::search_stopper s2;
  tuna::search::transposition_table tt2;
  auto p2 = pos;
  const auto r2 = tuna::search::search(p2, lim, s2, tt2, {});
  require(r2.best_move == r1.best_move, "continuation same best move");
  require(r2.score == r1.score, "continuation same score");
  require(r2.nodes <= r1.nodes * 1.2, "continuation does not increase nodes significantly");

  auto quiet = tuna::position::from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
  tuna::search::search_limits lim2; lim2.depth = 3;
  tuna::search::search_stopper s3;
  tuna::search::transposition_table tt3;
  auto p3 = quiet;
  const auto r3 = tuna::search::search(p3, lim2, s3, tt3, {});
  require(r3.has_move, "continuation quiet has move");
  std::cout<<"continuation regression passed: same best move/score, nodes not increased\n";
  return 0;
}