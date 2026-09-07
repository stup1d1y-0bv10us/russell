#include "core/position.hpp"
#include "core/types.hpp"
#include "movegen/movegen.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

inline constexpr const char* fens[] = {

  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
  "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
  "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
  "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
  "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",

  "4k3/8/8/3p4/4P3/8/8/4K3 w - d6 0 1",
  "4k3/8/8/2p5/3P4/8/8/4K3 w - c6 0 1",
  "4k3/8/8/5p2/4P3/8/8/4K3 w - f6 0 1",

  "4k3/8/8/8/3p4/8/2P5/4K3 b - c3 0 1",
  "4k3/8/8/8/4p3/8/3P4/4K3 b - d3 0 1",
  "4k3/8/8/8/p7/8/1P6/4K3 b - b3 0 1",

  "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
  "r3k2r/8/8/8/8/8/8/R3K2R w K - 0 1",
  "r3k2r/8/8/8/8/8/8/R3K2R b q - 0 1",
  "r3k2r/8/8/8/8/8/8/R3K2R b kq - 0 1",
  "4k3/8/8/8/8/8/8/R3K2R w Q - 0 1",
  "r3k2r/8/8/8/8/8/8/4K3 b q - 0 1",
  "r3k2r/8/8/8/8/8/8/R3K2R w KQ - 0 1",
  "r3k2r/8/8/8/8/8/8/R3K2R b k - 0 1",
  "4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1",
  "r3k2r/8/8/8/8/8/8/4K3 b kq - 0 1",

  "8/8/8/8/8/8/8/K6k w - - 0 1",
  "8/8/8/8/8/8/8/K6k w - - 100 200",
  "8/8/8/8/8/8/8/K6k b - - 49 77",
  "8/8/8/8/8/8/8/K6k w - - 1 50",

  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1",

  "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 2 3",
  "r1bqkbnr/pppp1Qpp/2n5/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4",
  "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3",
  "1rbq1rk1/5ppp/p1npbn2/1p2N3/4P3/2N2Q1P/PPP2PP1/2KR1B1R b - - 3 13",
  "r1bq1r2/pp2ppkp/2np1np1/8/2B1P1B1/2NP1N2/PPP2PPP/R2Q1RK1 w - - 1 9",

  "8/8/8/8/8/8/4k3/4K3 w - - 0 1",
  "8/8/8/4k3/8/8/8/4K3 w - - 0 1",
  "6k1/5ppp/8/8/8/8/8/1K6 w - - 0 1",
  "K7/8/8/8/8/8/8/k7 w - - 0 1",
  "k7/8/8/8/8/8/8/K7 b - - 0 1",

  "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1",
  "4k3/8/8/8/8/8/1p6/4K3 b - - 0 1",
  "4k3/P7/8/8/8/8/8/4K3 w - - 0 1",

  "r3k2r/pp1q1ppp/2p1pn2/3p4/1P1P4/P1N1P3/3PBPPP/R2Q1RK1 w kq - 0 10",
  "r1bq1rk1/pp2ppbp/2np1np1/2p5/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 8",
  "rnbqk2r/pppp1ppp/5n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R b KQkq - 3 5",
  "r1bqkb1r/ppp2ppp/2np1n2/8/4P3/2NP1N2/PPP2PPP/R1BQKB1R w KQkq - 0 5",
  "4k3/1p6/8/8/8/8/6P1/4K3 w - - 0 1",
  "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
  "r4rk1/1pqb1ppp/p2ppn2/8/2BNP1B1/2N1QP2/PPP3PP/R4RK1 w - - 0 13",
  "r1bq1r1k/pp1n1ppp/2pb1n2/3p2B1/3P4/2NBPN2/PPQ2PPP/2KR3R b - - 2 10",
  "8/8/8/2k5/8/8/8/2K5 w - - 0 1",
  "8/8/8/8/8/3K4/8/3k4 w - - 0 1",
  "5k2/8/8/8/8/8/8/5K2 w - - 0 1",
  "2r3k1/5ppp/8/8/8/8/5PPP/2R3K1 w - - 0 1",
};

auto require(bool value, const char* message) -> void
{
  if(!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

auto test_round_trip() -> void
{
  for(const auto* fen : fens) {
    const auto pos = tuna::position::from_fen(fen);
    const auto serialized = pos.fen();
    require(serialized == fen, "fen round-trip failed");
    const auto reparsed = tuna::position::from_fen(serialized);
    require(reparsed == pos, "fen reparse diverged from original position");
  }
}

auto test_field_parsing() -> void
{
  const auto start = tuna::position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  require(start.side_to_move() == tuna::color::white, "start side to move");
  require(start.castling_rights() == tuna::all_castling, "start castling rights");
  require(start.en_passant_square() == tuna::no_square, "start en passant");
  require(start.halfmove_clock() == 0, "start halfmove clock");
  require(start.fullmove_number() == 1, "start fullmove number");

  const auto kiwi = tuna::position::from_fen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
  require(kiwi.castling_rights() == (tuna::white_king_side | tuna::white_queen_side), "pos5 castling rights KQ");
  require(kiwi.halfmove_clock() == 1, "pos5 halfmove clock");
  require(kiwi.fullmove_number() == 8, "pos5 fullmove number");

  const auto ep = tuna::position::from_fen("4k3/8/8/3p4/4P3/8/8/4K3 w - d6 0 1");
  require(ep.en_passant_square() == tuna::make_square(3, 5), "ep square d6 parsed");

  const auto black = tuna::position::from_fen("4k3/8/8/8/p7/8/1P6/4K3 b - b3 0 1");
  require(black.side_to_move() == tuna::color::black, "side to move b parsed");
  require(black.en_passant_square() == tuna::make_square(1, 2), "ep square b3 parsed");

  const auto no_castling = tuna::position::from_fen("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1");
  require(no_castling.castling_rights() == 0, "no castling rights parsed");
}

auto test_round_trip_after_moves() -> void
{
  auto pos = tuna::position::start();
  for(auto ply = 0; ply < 200; ++ply) {
    const auto moves = tuna::movegen::generate_legal(pos);
    if(moves.size() == 0) {
      break;
    }
    const auto mv = moves[static_cast<std::size_t>(ply) % moves.size()];
    static_cast<void>(pos.make_move(mv));
    const auto serialized = pos.fen();
    require(tuna::position::from_fen(serialized) == pos, "round-trip after random move diverged");
    require(tuna::position::from_fen(serialized).fen() == serialized, "round-trip string after random move diverged");
  }
}

auto test_start_fen_output() -> void
{
  const auto pos = tuna::position::start();
  require(pos.fen() == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "start fen output canonical");
}

}

auto main() -> int
{
  test_round_trip();
  test_field_parsing();
  test_round_trip_after_moves();
  test_start_fen_output();
  return 0;
}