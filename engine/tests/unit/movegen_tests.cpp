#include "core/position.hpp"
#include "movegen/attacks.hpp"
#include "movegen/movegen.hpp"

#include <bit>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

auto require(bool value, const char* message) -> void
{
  if(!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

auto has_move(const tuna::move_list& moves, int from, int to, tuna::move_flag flag) -> bool
{
  for(const auto mv : moves) {
    if(mv.from == from && mv.to == to && mv.flag == flag) {
      return true;
    }
  }
  return false;
}

auto count_move(const tuna::move_list& moves, int from, int to) -> int
{
  auto count = 0;
  for(const auto mv : moves) {
    if(mv.from == from && mv.to == to) {
      ++count;
    }
  }
  return count;
}

auto is_capture(const tuna::move& mv) -> bool
{
  return mv.flag == tuna::move_flag::capture || mv.flag == tuna::move_flag::en_passant
         || mv.flag == tuna::move_flag::promotion_capture;
}

auto is_promotion(const tuna::move& mv) -> bool
{
  return mv.flag == tuna::move_flag::promotion || mv.flag == tuna::move_flag::promotion_capture;
}

auto contains(const tuna::move_list& moves, tuna::move mv) -> bool
{
  for(const auto candidate : moves) {
    if(candidate == mv) {
      return true;
    }
  }
  return false;
}

auto in_check(const tuna::position& pos) -> bool
{
  const auto king_bb = pos.pieces(pos.side_to_move(), tuna::piece_type::king);
  return king_bb != 0
         && tuna::movegen::is_square_attacked(
                pos, static_cast<int>(std::countr_zero(king_bb)),
                tuna::opposite(pos.side_to_move()));
}

auto is_quiet(const tuna::move& mv) -> bool
{
  return !is_capture(mv) && !is_promotion(mv);
}

auto old_quiescence_moves(tuna::position& pos) -> tuna::move_list
{
  auto out = tuna::move_list{};
  const auto checked = in_check(pos);
  for(const auto mv : tuna::movegen::generate_legal(pos)) {
    if(checked || is_capture(mv) || is_promotion(mv)) {
      out.push(mv);
    }
  }
  return out;
}

auto new_quiescence_moves(tuna::position& pos) -> tuna::move_list
{
  return in_check(pos) ? tuna::movegen::generate_evasions(pos)
                       : tuna::movegen::generate_captures_promotions(pos);
}

auto same_move_set(const tuna::move_list& a, const tuna::move_list& b) -> bool
{
  if(a.size() != b.size()) {
    return false;
  }
  for(const auto mv : a) {
    if(!contains(b, mv)) {
      return false;
    }
  }
  return true;
}

auto test_attack_tables() -> void
{
  require(tuna::movegen::knight_attacks(tuna::make_square(0, 0)) == (tuna::bit(tuna::make_square(1, 2)) | tuna::bit(tuna::make_square(2, 1))), "knight a1 attacks");
  require(tuna::movegen::knight_attacks(tuna::make_square(3, 3)) == 0x0000142200221400ull, "knight d4 attacks");
  require(tuna::movegen::king_attacks(tuna::make_square(7, 7)) == (tuna::bit(tuna::make_square(6, 7)) | tuna::bit(tuna::make_square(6, 6)) | tuna::bit(tuna::make_square(7, 6))), "king h8 attacks");
  require(tuna::movegen::king_attacks(tuna::make_square(4, 3)) == 0x0000003828380000ull, "king e4 attacks");
  require(tuna::movegen::pawn_attacks(tuna::color::white, tuna::make_square(0, 1)) == tuna::bit(tuna::make_square(1, 2)), "white pawn a2 attacks");
  require(tuna::movegen::pawn_attacks(tuna::color::black, tuna::make_square(7, 6)) == tuna::bit(tuna::make_square(6, 5)), "black pawn h7 attacks");
}

auto test_start_position() -> void
{
  const auto moves = tuna::movegen::generate_non_sliding_pseudo_legal(tuna::position::start());
  require(moves.size() == 20, "start non-sliding move count");
  require(has_move(moves, tuna::make_square(4, 1), tuna::make_square(4, 2), tuna::move_flag::quiet), "start e2e3");
  require(has_move(moves, tuna::make_square(4, 1), tuna::make_square(4, 3), tuna::move_flag::double_push), "start e2e4");
  require(has_move(moves, tuna::make_square(6, 0), tuna::make_square(5, 2), tuna::move_flag::quiet), "start g1f3");
  require(has_move(moves, tuna::make_square(6, 0), tuna::make_square(7, 2), tuna::move_flag::quiet), "start g1h3");
}

auto test_blocked_pawn_position() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(4, 7));
  pos.set_piece(tuna::color::white, tuna::piece_type::pawn, tuna::make_square(4, 1));
  pos.set_piece(tuna::color::black, tuna::piece_type::pawn, tuna::make_square(4, 2));
  const auto moves = tuna::movegen::generate_non_sliding_pseudo_legal(pos);
  require(!has_move(moves, tuna::make_square(4, 1), tuna::make_square(4, 2), tuna::move_flag::quiet), "blocked pawn single");
  require(!has_move(moves, tuna::make_square(4, 1), tuna::make_square(4, 3), tuna::move_flag::double_push), "blocked pawn double");
}

auto test_pawn_capture_position() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(0, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(7, 7));
  pos.set_piece(tuna::color::white, tuna::piece_type::pawn, tuna::make_square(3, 3));
  pos.set_piece(tuna::color::black, tuna::piece_type::knight, tuna::make_square(2, 4));
  pos.set_piece(tuna::color::black, tuna::piece_type::bishop, tuna::make_square(4, 4));
  const auto moves = tuna::movegen::generate_non_sliding_pseudo_legal(pos);
  require(has_move(moves, tuna::make_square(3, 3), tuna::make_square(3, 4), tuna::move_flag::quiet), "pawn capture position push");
  require(has_move(moves, tuna::make_square(3, 3), tuna::make_square(2, 4), tuna::move_flag::capture), "pawn capture left");
  require(has_move(moves, tuna::make_square(3, 3), tuna::make_square(4, 4), tuna::move_flag::capture), "pawn capture right");
}

auto test_en_passant_position() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(4, 7));
  pos.set_piece(tuna::color::white, tuna::piece_type::pawn, tuna::make_square(4, 4));
  pos.set_piece(tuna::color::black, tuna::piece_type::pawn, tuna::make_square(3, 4));
  pos.set_en_passant_square(tuna::make_square(3, 5));
  const auto moves = tuna::movegen::generate_non_sliding_pseudo_legal(pos);
  require(has_move(moves, tuna::make_square(4, 4), tuna::make_square(3, 5), tuna::move_flag::en_passant), "white en passant");
}

auto test_white_promotion_position() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(7, 7));
  pos.set_piece(tuna::color::white, tuna::piece_type::pawn, tuna::make_square(0, 6));
  pos.set_piece(tuna::color::black, tuna::piece_type::rook, tuna::make_square(1, 7));
  const auto moves = tuna::movegen::generate_non_sliding_pseudo_legal(pos);
  require(count_move(moves, tuna::make_square(0, 6), tuna::make_square(0, 7)) == 4, "white promotion count");
  require(count_move(moves, tuna::make_square(0, 6), tuna::make_square(1, 7)) == 4, "white promotion capture count");
  require(has_move(moves, tuna::make_square(0, 6), tuna::make_square(0, 7), tuna::move_flag::promotion), "white promotion move");
  require(has_move(moves, tuna::make_square(0, 6), tuna::make_square(1, 7), tuna::move_flag::promotion_capture), "white promotion capture");
}

auto test_black_promotion_position() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(0, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(4, 7));
  pos.set_piece(tuna::color::black, tuna::piece_type::pawn, tuna::make_square(7, 1));
  pos.set_piece(tuna::color::white, tuna::piece_type::rook, tuna::make_square(6, 0));
  pos.set_side_to_move(tuna::color::black);
  const auto moves = tuna::movegen::generate_non_sliding_pseudo_legal(pos);
  require(count_move(moves, tuna::make_square(7, 1), tuna::make_square(7, 0)) == 4, "black promotion count");
  require(count_move(moves, tuna::make_square(7, 1), tuna::make_square(6, 0)) == 4, "black promotion capture count");
}

auto test_knight_center_position() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(0, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(7, 7));
  pos.set_piece(tuna::color::white, tuna::piece_type::knight, tuna::make_square(3, 3));
  const auto moves = tuna::movegen::generate_non_sliding_pseudo_legal(pos);
  require(count_move(moves, tuna::make_square(3, 3), tuna::make_square(0, 0)) == 0, "knight does not include invalid square");
  require(has_move(moves, tuna::make_square(3, 3), tuna::make_square(1, 2), tuna::move_flag::quiet), "knight d4b3");
  require(has_move(moves, tuna::make_square(3, 3), tuna::make_square(5, 4), tuna::move_flag::quiet), "knight d4f5");
}

auto test_knight_own_piece_block_position() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(4, 7));
  pos.set_piece(tuna::color::white, tuna::piece_type::knight, tuna::make_square(1, 0));
  pos.set_piece(tuna::color::white, tuna::piece_type::pawn, tuna::make_square(2, 2));
  pos.set_piece(tuna::color::black, tuna::piece_type::pawn, tuna::make_square(0, 2));
  const auto moves = tuna::movegen::generate_non_sliding_pseudo_legal(pos);
  require(!has_move(moves, tuna::make_square(1, 0), tuna::make_square(2, 2), tuna::move_flag::quiet), "knight own occupied target");
  require(has_move(moves, tuna::make_square(1, 0), tuna::make_square(0, 2), tuna::move_flag::capture), "knight enemy occupied target");
}

auto test_king_corner_position() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(0, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(7, 7));
  const auto moves = tuna::movegen::generate_non_sliding_pseudo_legal(pos);
  require(has_move(moves, tuna::make_square(0, 0), tuna::make_square(0, 1), tuna::move_flag::quiet), "king a1a2");
  require(has_move(moves, tuna::make_square(0, 0), tuna::make_square(1, 0), tuna::move_flag::quiet), "king a1b1");
  require(has_move(moves, tuna::make_square(0, 0), tuna::make_square(1, 1), tuna::move_flag::quiet), "king a1b2");
}

auto test_black_side_position() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(0, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(7, 7));
  pos.set_piece(tuna::color::black, tuna::piece_type::pawn, tuna::make_square(4, 6));
  pos.set_piece(tuna::color::black, tuna::piece_type::knight, tuna::make_square(6, 7));
  pos.set_side_to_move(tuna::color::black);
  const auto moves = tuna::movegen::generate_non_sliding_pseudo_legal(pos);
  require(has_move(moves, tuna::make_square(4, 6), tuna::make_square(4, 5), tuna::move_flag::quiet), "black e7e6");
  require(has_move(moves, tuna::make_square(4, 6), tuna::make_square(4, 4), tuna::move_flag::double_push), "black e7e5");
  require(has_move(moves, tuna::make_square(6, 7), tuna::make_square(5, 5), tuna::move_flag::quiet), "black g8f6");
}

auto test_captures_promotions_parity() -> void
{
  const auto fens = std::vector<std::string>{
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "1k6/1P6/8/8/8/8/8/1K6 w - - 0 1",
  };
  for(const auto& fen : fens) {
    auto pos = tuna::position::from_fen(fen);
    const auto all = tuna::movegen::generate_legal(pos);
    const auto caps = tuna::movegen::generate_captures_promotions(pos);
    auto expected = std::size_t{0};
    for(const auto mv : all) {
      if(is_capture(mv) || is_promotion(mv)) {
        ++expected;
      }
    }
    require(caps.size() == expected, ("captures/promotions count match " + fen).c_str());
    for(const auto mv : caps) {
      require(is_capture(mv) || is_promotion(mv), "generated move is capture or promotion");
      require(contains(all, mv), "generated capture/promotion is legal");
    }
    for(const auto mv : all) {
      if(is_capture(mv) || is_promotion(mv)) {
        require(contains(caps, mv), ("every legal capture/promotion generated " + fen).c_str());
      }
    }
  }
}

auto test_evasions_parity() -> void
{
  const auto fens = std::vector<std::string>{
    "k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1",
    "4k3/8/8/8/8/8/8/4R1K1 b - - 0 1",
  };
  for(const auto& fen : fens) {
    auto pos = tuna::position::from_fen(fen);
    require(in_check(pos), ("position is in check " + fen).c_str());
    const auto all = tuna::movegen::generate_legal(pos);
    const auto evasions = tuna::movegen::generate_evasions(pos);
    require(evasions.size() == all.size(), ("evasions count match " + fen).c_str());
    for(const auto mv : evasions) {
      require(contains(all, mv), "generated evasion is legal");
    }
    for(const auto mv : all) {
      require(contains(evasions, mv), ("every legal move is an evasion " + fen).c_str());
    }
  }
}

auto test_evasion_blocks_and_captures() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  pos.set_piece(tuna::color::white, tuna::piece_type::knight, tuna::make_square(6, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(0, 7));
  pos.set_piece(tuna::color::black, tuna::piece_type::rook, tuna::make_square(4, 7));
  require(in_check(pos), "rook e-file check");
  const auto evasions = tuna::movegen::generate_evasions(pos);
  require(has_move(evasions, tuna::make_square(6, 0), tuna::make_square(4, 1), tuna::move_flag::quiet),
          "knight blocks on e2");
  require(has_move(evasions, tuna::make_square(4, 0), tuna::make_square(3, 0), tuna::move_flag::quiet),
          "king d1 escape");
  require(has_move(evasions, tuna::make_square(4, 0), tuna::make_square(5, 1), tuna::move_flag::quiet),
          "king f2 escape");
  require(!has_move(evasions, tuna::make_square(6, 0), tuna::make_square(5, 2), tuna::move_flag::quiet),
          "knight f3 does not block");
  require(evasions.size() == 5, "five evasions against single rook check");
}

auto test_evasion_pinned_block_is_rejected() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  pos.set_piece(tuna::color::white, tuna::piece_type::knight, tuna::make_square(4, 1));
  pos.set_piece(tuna::color::white, tuna::piece_type::bishop, tuna::make_square(6, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(0, 7));
  pos.set_piece(tuna::color::black, tuna::piece_type::rook, tuna::make_square(4, 7));
  pos.set_piece(tuna::color::black, tuna::piece_type::bishop, tuna::make_square(7, 3));
  require(in_check(pos), "bishop diagonal check");
  const auto evasions = tuna::movegen::generate_evasions(pos);
  require(!has_move(evasions, tuna::make_square(4, 1), tuna::make_square(6, 2), tuna::move_flag::quiet),
          "pinned knight cannot block on g3");
  require(has_move(evasions, tuna::make_square(6, 0), tuna::make_square(5, 1), tuna::move_flag::quiet),
          "bishop blocks on f2");
  require(has_move(evasions, tuna::make_square(4, 0), tuna::make_square(3, 0), tuna::move_flag::quiet),
          "king d1 escape");
  require(evasions.size() == 4, "four evasions against pinned block");
}

auto test_evasion_double_check() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(0, 7));
  pos.set_piece(tuna::color::black, tuna::piece_type::rook, tuna::make_square(4, 7));
  pos.set_piece(tuna::color::black, tuna::piece_type::bishop, tuna::make_square(7, 3));
  require(in_check(pos), "double check");
  const auto evasions = tuna::movegen::generate_evasions(pos);
  require(has_move(evasions, tuna::make_square(4, 0), tuna::make_square(3, 0), tuna::move_flag::quiet),
          "double check king d1");
  require(has_move(evasions, tuna::make_square(4, 0), tuna::make_square(3, 1), tuna::move_flag::quiet),
          "double check king d2");
  require(has_move(evasions, tuna::make_square(4, 0), tuna::make_square(5, 0), tuna::move_flag::quiet),
          "double check king f1");
  require(!has_move(evasions, tuna::make_square(4, 0), tuna::make_square(5, 1), tuna::move_flag::quiet),
          "king cannot move to attacked f2");
  require(evasions.size() == 3, "only king moves against double check");
}

auto test_evasion_en_passant() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 3));
  pos.set_piece(tuna::color::white, tuna::piece_type::pawn, tuna::make_square(4, 4));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(0, 7));
  pos.set_piece(tuna::color::black, tuna::piece_type::pawn, tuna::make_square(3, 4));
  pos.set_en_passant_square(tuna::make_square(3, 5));
  require(in_check(pos), "pawn en-passant check");
  const auto evasions = tuna::movegen::generate_evasions(pos);
  require(has_move(evasions, tuna::make_square(4, 4), tuna::make_square(3, 5), tuna::move_flag::en_passant),
          "en-passant capture resolves check");
  require(has_move(evasions, tuna::make_square(4, 3), tuna::make_square(3, 4), tuna::move_flag::capture),
          "king captures the checking pawn");
  require(evasions.size() == 8, "eight evasions against pawn check");
}

auto test_quiescence_generator_equivalence() -> void
{
  const auto fens = std::vector<std::string>{

    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "1k6/1P6/8/8/8/8/8/1K6 w - - 0 1",
    "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
    "4k3/8/8/8/8/1p6/P7/4K3 w - - 0 1",
    "k3r3/8/8/8/8/6p1/4N3/4K3 w - - 0 1",
    "4k3/8/8/8/8/8/3n4/4K3 w - - 0 1",
    "3rk3/3R4/8/8/8/8/8/4K3 w - - 0 1",
    "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1",

    "k3r3/8/8/8/8/8/8/4K1N1 w - - 0 1",
    "k3r3/8/5N2/8/8/8/8/4K3 w - - 0 1",
    "k3r3/8/8/8/7b/8/8/4K3 w - - 0 1",
    "4k3/8/8/8/8/8/2n5/4K3 w - - 0 1",
    "k7/8/8/3pP3/4K3/8/8/8 w - d6 0 1",
    "k3r3/8/8/8/7b/8/4R3/4K3 w - - 0 1",
    "4k3/8/8/8/8/8/3p4/4K3 w - - 0 1",
    "4q3/8/8/8/8/8/8/4K3 w - - 0 1",
    "k7/1p6/2N5/8/8/8/8/R2K4 b - - 0 1",
    "4k3/8/8/8/8/8/8/4R1K1 b - - 0 1",
  };
  for(const auto& fen : fens) {
    auto pos = tuna::position::from_fen(fen);
    const auto old_set = old_quiescence_moves(pos);
    const auto new_set = new_quiescence_moves(pos);
    require(same_move_set(old_set, new_set), ("generator equivalence " + fen).c_str());
    for(const auto mv : old_set) {
      require(contains(new_set, mv), ("tactical move preserved " + fen).c_str());
    }
  }
}

auto test_capture_gives_check() -> void
{
  auto pos = tuna::position::from_fen("3rk3/3R4/8/8/8/8/8/4K3 w - - 0 1");
  require(!in_check(pos), "checking capture position not in check");
  const auto caps = tuna::movegen::generate_captures_promotions(pos);
  require(has_move(caps, tuna::make_square(3, 6), tuna::make_square(3, 7), tuna::move_flag::capture),
          "capture that gives check is generated");
  const auto mv = tuna::move{static_cast<std::uint8_t>(tuna::make_square(3, 6)),
                             static_cast<std::uint8_t>(tuna::make_square(3, 7)),
                             tuna::piece_type::queen, tuna::move_flag::capture};
  const auto st = pos.make_move(mv);
  require(in_check(pos), "capturing the d8 rook gives check");
  pos.unmake_move(mv, st);
}

auto test_capture_checker_evasion() -> void
{
  auto pos = tuna::position::from_fen("k3r3/8/5N2/8/8/8/8/4K3 w - - 0 1");
  require(in_check(pos), "rook check along e-file");
  const auto evasions = tuna::movegen::generate_evasions(pos);
  require(has_move(evasions, tuna::make_square(5, 5), tuna::make_square(4, 7), tuna::move_flag::capture),
          "knight captures the checking rook");
  require(has_move(evasions, tuna::make_square(5, 5), tuna::make_square(4, 3), tuna::move_flag::quiet),
          "knight blocks the check on e4");
  require(evasions.size() == 6, "capture checker, knight block, and four king escapes");
}

auto test_pinned_capture_excluded() -> void
{
  auto pos = tuna::position::from_fen("k3r3/8/8/8/8/6p1/4N3/4K3 w - - 0 1");
  require(!in_check(pos), "pinned knight position not in check");
  const auto caps = tuna::movegen::generate_captures_promotions(pos);
  require(!has_move(caps, tuna::make_square(4, 1), tuna::make_square(6, 2), tuna::move_flag::capture),
          "pinned knight capture is excluded");
  require(caps.size() == 0, "no legal captures while knight pinned");
}

auto test_king_capture() -> void
{
  auto pos = tuna::position::from_fen("4k3/8/8/8/8/8/3n4/4K3 w - - 0 1");
  require(!in_check(pos), "not in check before king capture");
  const auto caps = tuna::movegen::generate_captures_promotions(pos);
  require(has_move(caps, tuna::make_square(4, 0), tuna::make_square(3, 1), tuna::move_flag::capture),
          "king capture is generated");
}

auto test_quiet_evasion_required() -> void
{
  auto pos = tuna::position::from_fen("4k3/8/8/8/8/8/2n5/4K3 w - - 0 1");
  require(in_check(pos), "knight c2 check");
  const auto evasions = tuna::movegen::generate_evasions(pos);
  require(evasions.size() == 5, "five quiet king escapes");
  for(const auto mv : evasions) {
    require(mv.from == static_cast<std::uint8_t>(tuna::make_square(4, 0)), "evasion is a king move");
    require(is_quiet(mv), "no capture resolves a knight check");
  }
  require(has_move(evasions, tuna::make_square(4, 0), tuna::make_square(3, 0), tuna::move_flag::quiet),
          "king d1 escape");
  require(has_move(evasions, tuna::make_square(4, 0), tuna::make_square(4, 1), tuna::move_flag::quiet),
          "king e2 escape");
}

auto test_pinned_slider_cannot_block() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  pos.set_piece(tuna::color::white, tuna::piece_type::rook, tuna::make_square(4, 1));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(0, 7));
  pos.set_piece(tuna::color::black, tuna::piece_type::rook, tuna::make_square(4, 7));
  pos.set_piece(tuna::color::black, tuna::piece_type::bishop, tuna::make_square(7, 3));
  require(in_check(pos), "bishop h4 check");
  const auto evasions = tuna::movegen::generate_evasions(pos);
  require(!has_move(evasions, tuna::make_square(4, 1), tuna::make_square(5, 1), tuna::move_flag::quiet),
          "pinned rook cannot slide to f2");
  require(evasions.size() == 3, "only king escapes when the blocker is pinned");
}

}

auto main() -> int
{
  test_attack_tables();
  test_start_position();
  test_blocked_pawn_position();
  test_pawn_capture_position();
  test_en_passant_position();
  test_white_promotion_position();
  test_black_promotion_position();
  test_knight_center_position();
  test_knight_own_piece_block_position();
  test_king_corner_position();
  test_black_side_position();
  test_captures_promotions_parity();
  test_evasions_parity();
  test_evasion_blocks_and_captures();
  test_evasion_pinned_block_is_rejected();
  test_evasion_double_check();
  test_evasion_en_passant();
  test_quiescence_generator_equivalence();
  test_capture_gives_check();
  test_capture_checker_evasion();
  test_pinned_capture_excluded();
  test_king_capture();
  test_quiet_evasion_required();
  test_pinned_slider_cannot_block();
  return 0;
}