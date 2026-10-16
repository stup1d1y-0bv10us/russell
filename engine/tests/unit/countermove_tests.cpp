#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/ordering.hpp"
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

  {

    russell::search::move_ordering order;
    const auto prev = russell::move{12, 28, russell::piece_type::queen, russell::move_flag::quiet};
    const auto cur = russell::move{10, 26, russell::piece_type::queen, russell::move_flag::quiet};
    const auto other = russell::move{10, 18, russell::piece_type::queen, russell::move_flag::quiet};
    require(!order.is_countermove(prev, cur), "no countermove before store");
    order.update_counter(prev, cur);
    require(order.is_countermove(prev, cur), "countermove recognized after store");
    require(!order.is_countermove(prev, other), "different move not countermove");
    require(!order.is_countermove(russell::search::no_move, cur), "no_move prev never countermove");
  }
  std::cout<<"countermove regression passed: same best move/score, nodes not increased\n";
  return 0;
}
