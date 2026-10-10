#include "core/position.hpp"
#include "search/search.hpp"
#include "search/transposition_table.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
auto require(bool v, const char* m){ if(!v){ std::cerr<<m<<"\n"; std::exit(1);} }
}

int main(){

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  russell::search::search_limits limits;
  limits.depth = 4;
  russell::search::search_stopper stopper;
  std::vector<russell::search::search_result> iterations;
  auto on_iter = [&](const russell::search::search_result& r){ iterations.push_back(r); };
  russell::search::transposition_table tt;
  auto p = pos;
  const auto result = russell::search::search(p, limits, stopper, tt, on_iter);

  std::uint64_t sum = 0;
  for(auto &r: iterations) sum += r.nodes;
  require(!iterations.empty(), "at least one iteration");
  require(result.nodes == sum, "reported node count equals actual accumulated nodes across aspiration re-searches (no double count)");

  auto pos2 = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  russell::search::search_stopper s2;
  russell::search::search_limits l2; l2.depth = 4;
  russell::search::transposition_table tt2;
  const auto r2 = russell::search::search(pos2, l2, s2, tt2);
  require(r2.best_move == result.best_move, "best move deterministic");
  require(r2.score == result.score, "score deterministic");
  std::cout<<"aspiration node count regression passed\n";
  return 0;
}
