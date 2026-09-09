#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
#include "search/transposition_table.hpp"
#include <cstdlib>
#include <iostream>

namespace {
auto require(bool v, const char* m){ if(!v){ std::cerr<<m<<"\n"; std::exit(1);} }
auto is_legal(const russell::position& pos, russell::move mv)->bool{
  auto c = pos;
  for(auto m: russell::movegen::generate_legal(c)) if(m==mv) return true;
  return false;
}
}

int main(){

  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

  russell::search::search_stopper s1;
  russell::search::search_limits l1; l1.depth = 1;
  russell::search::transposition_table tt1;
  auto p1 = pos;
  const auto completed1 = russell::search::search(p1, l1, s1, tt1);
  require(completed1.has_move, "depth1 completed");
  require(is_legal(pos, completed1.best_move), "depth1 move legal");

  russell::search::search_stopper stopper;
  russell::search::search_limits limits;
  limits.depth = 5;
  limits.hard_time_ms = 1;
  russell::search::transposition_table tt;
  auto p2 = pos;
  const auto aborted = russell::search::search(p2, limits, stopper, tt);
  require(aborted.has_move, "hard timeout still has move");
  require(is_legal(pos, aborted.best_move), "hard timeout preserved move is legal");

  require(aborted.depth >= 1, "aborted search at least depth1 completed");
  require(is_legal(pos, aborted.best_move), "aborted best move still legal (not partial)");

  std::cout<<"hard timeout preserves last completed legal best move\n";
  return 0;
}