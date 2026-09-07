#include "core/position.hpp"
#include "core/types.hpp"
#include "movegen/movegen.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

auto require(bool value, const char* message) -> void
{
  if(!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

auto perft(tuna::position& pos, int depth) -> std::uint64_t
{
  if(depth == 0) {
    return 1;
  }
  auto nodes = std::uint64_t{0};
  for(const auto mv : tuna::movegen::generate_legal(pos)) {
    const auto st = pos.make_move(mv);
    nodes += perft(pos, depth - 1);
    pos.unmake_move(mv, st);
  }
  return nodes;
}

auto test_generate_legal_is_stateless() -> void
{
  auto pos = tuna::position::start();
  const auto before = pos;
  const auto moves = tuna::movegen::generate_legal(pos);
  require(moves.size() == 20, "start legal(1) = 20");
  require(pos == before, "generate_legal leaves position unchanged");
}

auto test_start_position() -> void
{
  auto pos = tuna::position::start();
  require(perft(pos, 1) == 20, "start perft 1");
  require(perft(pos, 2) == 400, "start perft 2");
  require(perft(pos, 3) == 8902, "start perft 3");
  require(perft(pos, 4) == 197281, "start perft 4");
}

auto test_kiwipete() -> void
{
  auto pos = tuna::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  require(tuna::movegen::generate_legal(pos).size() == 48, "kiwipete legal(1) = 48");
  require(perft(pos, 1) == 48, "kiwipete perft 1");
  require(perft(pos, 2) == 2039, "kiwipete perft 2");
  require(perft(pos, 3) == 97862, "kiwipete perft 3");
  require(perft(pos, 4) == 4085603, "kiwipete perft 4");
}

auto test_position_4() -> void
{
  auto pos = tuna::position::from_fen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1");
  require(tuna::movegen::generate_legal(pos).size() == 6, "position 4 legal(1) = 6");
  require(perft(pos, 1) == 6, "position 4 perft 1");
  require(perft(pos, 2) == 264, "position 4 perft 2");
  require(perft(pos, 3) == 9467, "position 4 perft 3");
  require(perft(pos, 4) == 422333, "position 4 perft 4");
}

auto test_in_check_counts() -> void
{
  auto pos = tuna::position::from_fen("4r2k/8/8/8/8/8/8/4K3 w - - 0 1");
  const auto moves = tuna::movegen::generate_legal(pos);
  require(moves.size() == 4, "king in check has exactly 4 replies");
  for(const auto mv : moves) {
    require(mv.from == tuna::make_square(4, 0), "all replies are king moves from e1");
  }
}

}

auto main() -> int
{
  test_generate_legal_is_stateless();
  test_start_position();
  test_kiwipete();
  test_position_4();
  test_in_check_counts();
  return 0;
}