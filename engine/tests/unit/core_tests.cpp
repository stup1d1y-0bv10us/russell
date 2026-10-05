#include "core/position.hpp"

#include <cstdlib>
#include <iostream>
#include <random>
#include <string>

namespace {

auto require(bool value, const char* message) -> void
{
  if(!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

auto test_start_position() -> void
{
  const auto pos = russell::position::start();
  require(pos.key() == pos.recompute_key(), "start key");
  require(pos.pieces(russell::color::white, russell::piece_type::pawn) == 0x000000000000ff00ull, "white pawns");
  require(pos.pieces(russell::color::white, russell::piece_type::knight) == 0x0000000000000042ull, "white knights");
  require(pos.pieces(russell::color::white, russell::piece_type::bishop) == 0x0000000000000024ull, "white bishops");
  require(pos.pieces(russell::color::white, russell::piece_type::rook) == 0x0000000000000081ull, "white rooks");
  require(pos.pieces(russell::color::white, russell::piece_type::queen) == 0x0000000000000008ull, "white queen");
  require(pos.pieces(russell::color::white, russell::piece_type::king) == 0x0000000000000010ull, "white king");
  require(pos.pieces(russell::color::black, russell::piece_type::pawn) == 0x00ff000000000000ull, "black pawns");
  require(pos.pieces(russell::color::black, russell::piece_type::knight) == 0x4200000000000000ull, "black knights");
  require(pos.pieces(russell::color::black, russell::piece_type::bishop) == 0x2400000000000000ull, "black bishops");
  require(pos.pieces(russell::color::black, russell::piece_type::rook) == 0x8100000000000000ull, "black rooks");
  require(pos.pieces(russell::color::black, russell::piece_type::queen) == 0x0800000000000000ull, "black queen");
  require(pos.pieces(russell::color::black, russell::piece_type::king) == 0x1000000000000000ull, "black king");
  require(pos.occupancy(russell::color::white) == 0x000000000000ffffull, "white occupancy");
  require(pos.occupancy(russell::color::black) == 0xffff000000000000ull, "black occupancy");
  require(pos.occupancy() == 0xffff00000000ffffull, "all occupancy");
  require(pos.side_to_move() == russell::color::white, "start side");
  require(pos.castling_rights() == russell::all_castling, "start castling");
  require(pos.en_passant_square() == russell::no_square, "start en passant");
  require(pos.halfmove_clock() == 0, "start halfmove");
  require(pos.fullmove_number() == 1, "start fullmove");
  require(pos.board_string() == "rnbqkbnr\npppppppp\n........\n........\n........\n........\nPPPPPPPP\nRNBQKBNR", "start board");
}

auto test_custom_position() -> void
{
  auto pos = russell::position::empty();
  pos.set_piece(russell::color::white, russell::piece_type::king, russell::make_square(6, 0));
  pos.set_piece(russell::color::white, russell::piece_type::rook, russell::make_square(0, 0));
  pos.set_piece(russell::color::white, russell::piece_type::pawn, russell::make_square(4, 4));
  pos.set_piece(russell::color::black, russell::piece_type::king, russell::make_square(6, 7));
  pos.set_piece(russell::color::black, russell::piece_type::queen, russell::make_square(3, 5));
  pos.set_piece(russell::color::black, russell::piece_type::pawn, russell::make_square(5, 4));
  pos.set_side_to_move(russell::color::black);
  pos.set_castling_rights(russell::white_king_side);
  pos.set_en_passant_square(russell::make_square(5, 5));
  pos.set_halfmove_clock(3);
  pos.set_fullmove_number(17);
  require(pos.piece_on(russell::make_square(6, 0)) == russell::piece::wk, "custom white king");
  require(pos.piece_on(russell::make_square(3, 5)) == russell::piece::bq, "custom black queen");
  require(pos.piece_on(russell::make_square(1, 1)) == russell::piece::none, "custom empty square");
  require(pos.occupancy(russell::color::white) == 0x0000001000000041ull, "custom white occupancy");
  require(pos.occupancy(russell::color::black) == 0x4000082000000000ull, "custom black occupancy");
  require(pos.occupancy() == 0x4000083000000041ull, "custom all occupancy");
  require(pos.side_to_move() == russell::color::black, "custom side");
  require(pos.castling_rights() == russell::white_king_side, "custom castling");
  require(pos.en_passant_square() == russell::make_square(5, 5), "custom en passant");
  require(pos.halfmove_clock() == 3, "custom halfmove");
  require(pos.fullmove_number() == 17, "custom fullmove");
  require(pos.board_string() == "......k.\n........\n...q....\n....Pp..\n........\n........\n........\nR.....K.", "custom board");
  require(pos.key() == pos.recompute_key(), "custom key");
}

auto test_kings_only_position() -> void
{
  auto pos = russell::position::empty();
  pos.set_piece(russell::color::white, russell::piece_type::king, russell::make_square(4, 0));
  pos.set_piece(russell::color::black, russell::piece_type::king, russell::make_square(4, 7));
  pos.set_side_to_move(russell::color::black);
  pos.set_halfmove_clock(12);
  pos.set_fullmove_number(88);
  require(pos.pieces(russell::color::white, russell::piece_type::king) == russell::bit(russell::make_square(4, 0)), "kings only white king");
  require(pos.pieces(russell::color::black, russell::piece_type::king) == russell::bit(russell::make_square(4, 7)), "kings only black king");
  require(pos.occupancy() == (russell::bit(russell::make_square(4, 0)) | russell::bit(russell::make_square(4, 7))), "kings only occupancy");
  require(pos.side_to_move() == russell::color::black, "kings only side");
  require(pos.halfmove_clock() == 12, "kings only halfmove");
  require(pos.fullmove_number() == 88, "kings only fullmove");
  require(pos.board_string() == "....k...\n........\n........\n........\n........\n........\n........\n....K...", "kings only board");
  require(pos.key() == pos.recompute_key(), "kings only key");
}

auto test_middlegame_position() -> void
{
  auto pos = russell::position::empty();
  pos.set_piece(russell::color::white, russell::piece_type::king, russell::make_square(2, 0));
  pos.set_piece(russell::color::white, russell::piece_type::queen, russell::make_square(7, 4));
  pos.set_piece(russell::color::white, russell::piece_type::rook, russell::make_square(0, 0));
  pos.set_piece(russell::color::white, russell::piece_type::rook, russell::make_square(5, 0));
  pos.set_piece(russell::color::white, russell::piece_type::bishop, russell::make_square(6, 1));
  pos.set_piece(russell::color::white, russell::piece_type::knight, russell::make_square(5, 2));
  pos.set_piece(russell::color::white, russell::piece_type::pawn, russell::make_square(0, 1));
  pos.set_piece(russell::color::white, russell::piece_type::pawn, russell::make_square(1, 1));
  pos.set_piece(russell::color::white, russell::piece_type::pawn, russell::make_square(2, 3));
  pos.set_piece(russell::color::white, russell::piece_type::pawn, russell::make_square(3, 2));
  pos.set_piece(russell::color::white, russell::piece_type::pawn, russell::make_square(4, 3));
  pos.set_piece(russell::color::white, russell::piece_type::pawn, russell::make_square(5, 1));
  pos.set_piece(russell::color::white, russell::piece_type::pawn, russell::make_square(6, 2));
  pos.set_piece(russell::color::white, russell::piece_type::pawn, russell::make_square(7, 1));
  pos.set_piece(russell::color::black, russell::piece_type::king, russell::make_square(6, 7));
  pos.set_piece(russell::color::black, russell::piece_type::queen, russell::make_square(3, 7));
  pos.set_piece(russell::color::black, russell::piece_type::rook, russell::make_square(0, 7));
  pos.set_piece(russell::color::black, russell::piece_type::rook, russell::make_square(5, 7));
  pos.set_piece(russell::color::black, russell::piece_type::bishop, russell::make_square(2, 7));
  pos.set_piece(russell::color::black, russell::piece_type::bishop, russell::make_square(6, 6));
  pos.set_piece(russell::color::black, russell::piece_type::knight, russell::make_square(5, 5));
  pos.set_piece(russell::color::black, russell::piece_type::pawn, russell::make_square(0, 6));
  pos.set_piece(russell::color::black, russell::piece_type::pawn, russell::make_square(1, 6));
  pos.set_piece(russell::color::black, russell::piece_type::pawn, russell::make_square(2, 5));
  pos.set_piece(russell::color::black, russell::piece_type::pawn, russell::make_square(3, 5));
  pos.set_piece(russell::color::black, russell::piece_type::pawn, russell::make_square(4, 4));
  pos.set_piece(russell::color::black, russell::piece_type::pawn, russell::make_square(5, 6));
  pos.set_piece(russell::color::black, russell::piece_type::pawn, russell::make_square(6, 5));
  pos.set_piece(russell::color::black, russell::piece_type::pawn, russell::make_square(7, 6));
  require(pos.piece_on(russell::make_square(2, 0)) == russell::piece::wk, "middlegame white king");
  require(pos.piece_on(russell::make_square(7, 4)) == russell::piece::wq, "middlegame white queen");
  require(pos.piece_on(russell::make_square(6, 7)) == russell::piece::bk, "middlegame black king");
  require(pos.piece_on(russell::make_square(3, 7)) == russell::piece::bq, "middlegame black queen");
  require(pos.pieces(russell::color::white, russell::piece_type::pawn) == 0x000000001448a300ull, "middlegame white pawns");
  require(pos.pieces(russell::color::black, russell::piece_type::pawn) == 0x00a34c1000000000ull, "middlegame black pawns");
  require(pos.board_string() == "r.bq.rk.\npp...pbp\n..pp.np.\n....p..Q\n..P.P...\n...P.NP.\nPP...PBP\nR.K..R..", "middlegame board");
  require(pos.key() == pos.recompute_key(), "middlegame key");
}

auto test_updates() -> void
{
  auto pos = russell::position::empty();
  pos.set_piece(russell::color::white, russell::piece_type::queen, 0);
  pos.set_piece(russell::color::black, russell::piece_type::rook, 0);
  require(pos.piece_on(0) == russell::piece::br, "replace piece");
  require(pos.occupancy(russell::color::white) == 0, "replace white occupancy");
  require(pos.occupancy(russell::color::black) == 1, "replace black occupancy");
  pos.remove_piece(0);
  require(pos.piece_on(0) == russell::piece::none, "remove piece");
  require(pos.occupancy() == 0, "remove occupancy");
  pos.set_piece(russell::color::white, russell::piece_type::bishop, 63);
  pos.clear();
  require(pos == russell::position::empty(), "clear position");
  require(pos.key() == pos.recompute_key(), "clear key");
}

auto test_randomized_hash_consistency() -> void
{
  auto rng = std::mt19937_64{0x5eed1234ull};
  auto pos = russell::position::empty();
  for(auto i = 0; i < 10000; ++i) {
    const auto action = static_cast<int>(rng() % 8);
    const auto sq = static_cast<int>(rng() % russell::square_count);
    const auto c = (rng() & 1) == 0 ? russell::color::white : russell::color::black;
    const auto pt = static_cast<russell::piece_type>(rng() % russell::piece_type_count);
    if(action == 0) {
      pos.set_piece(c, pt, sq);
    } else if(action == 1) {
      pos.remove_piece(sq);
    } else if(action == 2) {
      pos.set_side_to_move(c);
    } else if(action == 3) {
      pos.set_castling_rights(static_cast<std::uint8_t>(rng() & russell::all_castling));
    } else if(action == 4) {
      pos.set_en_passant_square((rng() & 1) == 0 ? russell::no_square : static_cast<int>(rng() % russell::square_count));
    } else if(action == 5) {
      pos.set_halfmove_clock(static_cast<int>(rng() % 100));
    } else if(action == 6) {
      pos.set_fullmove_number(static_cast<int>((rng() % 200) + 1));
    } else {
      pos.clear();
    }
    require(pos.key() == pos.recompute_key(), "randomized key");
  }
}

}

auto main() -> int
{
  test_start_position();
  test_custom_position();
  test_kings_only_position();
  test_middlegame_position();
  test_updates();
  test_randomized_hash_consistency();
  return 0;
}
