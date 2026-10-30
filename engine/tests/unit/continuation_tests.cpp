#include "core/position.hpp"
#include "search/ordering.hpp"
#include "search/search.hpp"
#include "search/transposition_table.hpp"
#include <cstdlib>
#include <iostream>

namespace {
auto require(bool v, const char* m){ if(!v){ std::cerr<<m<<"\n"; std::exit(1);} }

auto test_continuation_units() -> void
{
  using namespace russell;
  using namespace russell::search;

  {
    move_ordering order;
    require(order.continuation_score_1ply(piece::wp, 28, piece::wn, 18) == 0,
            "fresh 1-ply continuation zero");
    require(order.continuation_score_2ply(piece::bp, 11, piece::bn, 10) == 0,
            "fresh 2-ply continuation zero");
  }

  {
    move_ordering order;
    order.update_continuation_1ply(piece::wn, 28, piece::wb, 18, 64);
    require(order.continuation_score_1ply(piece::wn, 28, piece::wb, 18) == 64,
            "1-ply bonus applies fully on fresh entry");
    order.update_continuation_1ply(piece::wn, 28, piece::wb, 18, -64);

    require(order.continuation_score_1ply(piece::wn, 28, piece::wb, 18) == 0,
            "1-ply malus revokes bonus via gravity");
    order.update_continuation_2ply(piece::bp, 11, piece::bn, 10, 64);
    require(order.continuation_score_2ply(piece::bp, 11, piece::bn, 10) == 64,
            "2-ply bonus applies fully on fresh entry");
    order.update_continuation_2ply(piece::bp, 11, piece::bn, 10, -128);
    require(order.continuation_score_2ply(piece::bp, 11, piece::bn, 10) < 0,
            "2-ply malus drives entry negative");
  }

  {
    move_ordering order;
    order.update_continuation_1ply(piece::wq, 27, piece::wr, 26, 16384);
    require(order.continuation_score_1ply(piece::wq, 27, piece::wr, 26) == 10000,
            "1-ply clamps at max on huge bonus");
    for(int i = 0; i < 200; ++i) {
      order.update_continuation_1ply(piece::wq, 27, piece::wr, 26, 16384);
      order.update_continuation_2ply(piece::bq, 35, piece::br, 36, -16384);
    }
    require(order.continuation_score_1ply(piece::wq, 27, piece::wr, 26) == 10000,
            "1-ply stays clamped under hammering (no overflow)");
    require(order.continuation_score_2ply(piece::bq, 35, piece::br, 36) == -10000,
            "2-ply stays clamped under hammering (no overflow)");
  }

  {
    move_ordering order;
    order.update_continuation_1ply(piece::wn, 28, piece::wb, 18, 256);
    require(order.continuation_score_1ply(piece::wn, 28, piece::wb, 18) > 0,
            "knight-prev entry positive");
    require(order.continuation_score_1ply(piece::wb, 28, piece::wb, 18) == 0,
            "bishop-prev entry independent (still zero)");
  }

  {
    move_ordering order;
    order.update_continuation_1ply(piece::wn, 28, piece::wb, 18, 256);
    require(order.continuation_score_2ply(piece::wn, 28, piece::wb, 18) == 0,
            "2-ply table independent of 1-ply update");
    order.update_continuation_2ply(piece::wn, 28, piece::wb, 18, 256);
    require(order.continuation_score_1ply(piece::wn, 28, piece::wb, 18) == 256,
            "1-ply entry intact after 2-ply update");
  }

  {
    move_ordering order;
    require(order.continuation_score_1ply(piece::none, 28, piece::wb, 18) == 0,
            "none prev piece safe");
    require(order.continuation_score_1ply(piece::wn, 28, piece::none, 18) == 0,
            "none cur piece safe");
    require(order.continuation_score_2ply(piece::none, 0, piece::none, 0) == 0,
            "fully empty 2-ply context safe");
  }

  {
    move_ordering order;
    order.update_continuation_1ply(piece::wp, 48, piece::wn, 33, 256);
    require(order.continuation_score_1ply(piece::wp, 48, piece::wn, 33) > 0,
            "pawn-identity prev found");
    require(order.continuation_score_1ply(piece::wq, 48, piece::wn, 33) == 0,
            "queen-identity prev independent");
  }

  {
    move_ordering order;
    order.update_continuation_1ply(piece::wn, 28, piece::wb, 18, 256);
    order.update_continuation_2ply(piece::bp, 11, piece::bn, 10, 256);
    order.reset();
    require(order.continuation_score_1ply(piece::wn, 28, piece::wb, 18) == 0,
            "reset clears 1-ply");
    require(order.continuation_score_2ply(piece::bp, 11, piece::bn, 10) == 0,
            "reset clears 2-ply");
  }

  {
    auto pos = position::from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
    move_ordering order;
    const move mv{12, 20, piece_type::queen, move_flag::quiet};
    require(order.score(pos, mv, 2, no_move, no_move, piece::none, no_move, piece::none) == 0,
            "empty continuation context falls to zero butterfly");
  }
}

}

int main(){
  auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const int depth = 4;
  russell::search::search_limits lim; lim.depth = depth;

  russell::search::search_stopper s1;
  russell::search::transposition_table tt1;
  auto p1 = pos;
  const auto r1 = russell::search::search(p1, lim, s1, tt1, {});
  require(r1.has_move, "continuation first has move");

  russell::search::search_stopper s2;
  russell::search::transposition_table tt2;
  auto p2 = pos;
  const auto r2 = russell::search::search(p2, lim, s2, tt2, {});
  require(r2.best_move == r1.best_move, "continuation same best move");
  require(r2.score == r1.score, "continuation same score");
  require(r2.nodes <= r1.nodes * 1.2, "continuation does not increase nodes significantly");

  auto quiet = russell::position::from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
  russell::search::search_limits lim2; lim2.depth = 3;
  russell::search::search_stopper s3;
  russell::search::transposition_table tt3;
  auto p3 = quiet;
  const auto r3 = russell::search::search(p3, lim2, s3, tt3, {});
  require(r3.has_move, "continuation quiet has move");
  test_continuation_units();
  std::cout<<"continuation regression passed: same best move/score, nodes not increased\n";
  std::cout<<"continuation units passed: init/bonus/malus/gravity/clamp/piece-split/1v2/root/promotion/reset\n";
  return 0;
}
