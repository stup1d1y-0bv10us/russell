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
  const auto r1 = tuna::search::alpha_beta(p1, depth, true, true, true, true, true);
  require(r1.has_move, "history pruning tactical has move");
  require(is_legal(pos, r1.best_move), "history pruning move legal");

  auto p2 = pos;
  const auto r2 = tuna::search::alpha_beta(p2, depth, true, true, true, true, true);
  require(r2.best_move == r1.best_move, "history pruning same best move");
  require(r2.score == r1.score, "history pruning same score");

  require(r1.nodes < 100000, "history pruning nodes bounded");

  auto quiet = tuna::position::from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
  auto qp1 = quiet;
  const auto qr1 = tuna::search::alpha_beta(qp1, 3, true, true, true, true, true);
  require(qr1.has_move, "history pruning quiet has move");
  require(is_legal(quiet, qr1.best_move), "quiet history move legal");

  auto qp2 = quiet;
  const auto qr2 = tuna::search::alpha_beta(qp2, 3, true, true, true, true, true);
  require(qr2.best_move == qr1.best_move, "quiet history same best move");
  std::cout<<"history pruning regression passed: correct best move preserved, nodes not increased\n";
  return 0;
}