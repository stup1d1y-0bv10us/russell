#include "core/move.hpp"
#include "core/position.hpp"
#include "core/types.hpp"
#include "movegen/movegen.hpp"

#include <cstdlib>
#include <iostream>
#include <random>
#include <utility>
#include <vector>

namespace {

auto require(bool value, const char* message) -> void
{
  if(!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

auto test_start_round_trip() -> void
{
  auto pos = tuna::position::start();
  for(const auto mv : tuna::movegen::generate_legal(pos)) {
    const auto before = pos;
    const auto st = pos.make_move(mv);
    require(pos.key() == pos.recompute_key(), "start make key");
    pos.unmake_move(mv, st);
    require(pos == before, "start round trip");
  }
}

auto test_double_push_ep_square() -> void
{
  auto pos = tuna::position::start();
  const auto mv = tuna::move{
    static_cast<std::uint8_t>(tuna::make_square(4, 1)),
    static_cast<std::uint8_t>(tuna::make_square(4, 3)),
    tuna::piece_type::queen,
    tuna::move_flag::double_push};
  const auto st = pos.make_move(mv);
  require(pos.en_passant_square() == tuna::make_square(4, 2), "double push ep e3");
  require(pos.piece_on(tuna::make_square(4, 3)) == tuna::piece::wp, "double push pawn e4");
  require(pos.side_to_move() == tuna::color::black, "double push side");
  require(pos.key() == pos.recompute_key(), "double push key");
  pos.unmake_move(mv, st);
  require(pos == tuna::position::start(), "double push restored");
}

auto test_capture_round_trip() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(4, 7));
  pos.set_piece(tuna::color::white, tuna::piece_type::knight, tuna::make_square(1, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::bishop, tuna::make_square(2, 2));
  const auto before = pos;
  const auto mv = tuna::move{
    static_cast<std::uint8_t>(tuna::make_square(1, 0)),
    static_cast<std::uint8_t>(tuna::make_square(2, 2)),
    tuna::piece_type::queen,
    tuna::move_flag::capture};
  const auto st = pos.make_move(mv);
  require(pos.piece_on(tuna::make_square(2, 2)) == tuna::piece::wn, "capture knight on c3");
  require(pos.pieces(tuna::color::black, tuna::piece_type::bishop) == 0, "capture bishop gone");
  require(pos.key() == pos.recompute_key(), "capture key");
  pos.unmake_move(mv, st);
  require(pos == before, "capture restored");
}

auto test_en_passant_round_trip() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(4, 7));
  pos.set_piece(tuna::color::white, tuna::piece_type::pawn, tuna::make_square(4, 4));
  pos.set_piece(tuna::color::black, tuna::piece_type::pawn, tuna::make_square(3, 4));
  pos.set_en_passant_square(tuna::make_square(3, 5));
  const auto before = pos;
  const auto mv = tuna::move{
    static_cast<std::uint8_t>(tuna::make_square(4, 4)),
    static_cast<std::uint8_t>(tuna::make_square(3, 5)),
    tuna::piece_type::queen,
    tuna::move_flag::en_passant};
  const auto st = pos.make_move(mv);
  require(pos.piece_on(tuna::make_square(3, 5)) == tuna::piece::wp, "ep pawn on d6");
  require(pos.piece_on(tuna::make_square(3, 4)) == tuna::piece::none, "ep captured pawn cleared");
  require(pos.en_passant_square() == tuna::no_square, "ep cleared after move");
  require(pos.key() == pos.recompute_key(), "ep key");
  pos.unmake_move(mv, st);
  require(pos == before, "ep restored");
}

auto test_castling_round_trip() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  pos.set_piece(tuna::color::white, tuna::piece_type::rook, tuna::make_square(7, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(4, 7));
  pos.set_castling_rights(tuna::white_king_side);
  const auto before = pos;
  const auto mv = tuna::move{
    static_cast<std::uint8_t>(tuna::make_square(4, 0)),
    static_cast<std::uint8_t>(tuna::make_square(6, 0)),
    tuna::piece_type::queen,
    tuna::move_flag::castling};
  const auto st = pos.make_move(mv);
  require(pos.piece_on(tuna::make_square(6, 0)) == tuna::piece::wk, "castle king on g1");
  require(pos.piece_on(tuna::make_square(5, 0)) == tuna::piece::wr, "castle rook on f1");
  require(pos.piece_on(tuna::make_square(7, 0)) == tuna::piece::none, "castle h1 empty");
  require(pos.castling_rights() == 0, "castle rights cleared");
  require(pos.key() == pos.recompute_key(), "castle key");
  pos.unmake_move(mv, st);
  require(pos == before, "castle restored");
}

auto test_promotion_round_trip() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(4, 7));
  pos.set_piece(tuna::color::white, tuna::piece_type::pawn, tuna::make_square(0, 6));
  const auto before = pos;
  const auto mv = tuna::move{
    static_cast<std::uint8_t>(tuna::make_square(0, 6)),
    static_cast<std::uint8_t>(tuna::make_square(0, 7)),
    tuna::piece_type::queen,
    tuna::move_flag::promotion};
  const auto st = pos.make_move(mv);
  require(pos.piece_on(tuna::make_square(0, 7)) == tuna::piece::wq, "promotion queen on a8");
  require(pos.pieces(tuna::color::white, tuna::piece_type::pawn) == 0, "promotion pawn gone");
  require(pos.key() == pos.recompute_key(), "promotion key");
  pos.unmake_move(mv, st);
  require(pos == before, "promotion restored");
}

auto test_promotion_capture_round_trip() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(0, 0));
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(0, 7));
  pos.set_piece(tuna::color::white, tuna::piece_type::pawn, tuna::make_square(6, 6));
  pos.set_piece(tuna::color::black, tuna::piece_type::rook, tuna::make_square(7, 7));
  const auto before = pos;
  const auto mv = tuna::move{
    static_cast<std::uint8_t>(tuna::make_square(6, 6)),
    static_cast<std::uint8_t>(tuna::make_square(7, 7)),
    tuna::piece_type::knight,
    tuna::move_flag::promotion_capture};
  const auto st = pos.make_move(mv);
  require(pos.piece_on(tuna::make_square(7, 7)) == tuna::piece::wn, "promo capture knight on h8");
  require(pos.piece_on(tuna::make_square(6, 6)) == tuna::piece::none, "promo capture g7 cleared");
  require(pos.key() == pos.recompute_key(), "promo capture key");
  pos.unmake_move(mv, st);
  require(pos == before, "promo capture restored");
}

auto test_black_castling_round_trip() -> void
{
  auto pos = tuna::position::empty();
  pos.set_piece(tuna::color::black, tuna::piece_type::king, tuna::make_square(4, 7));
  pos.set_piece(tuna::color::black, tuna::piece_type::rook, tuna::make_square(0, 7));
  pos.set_piece(tuna::color::white, tuna::piece_type::king, tuna::make_square(4, 0));
  pos.set_castling_rights(tuna::black_queen_side);
  pos.set_side_to_move(tuna::color::black);
  const auto before = pos;
  const auto mv = tuna::move{
    static_cast<std::uint8_t>(tuna::make_square(4, 7)),
    static_cast<std::uint8_t>(tuna::make_square(2, 7)),
    tuna::piece_type::queen,
    tuna::move_flag::castling};
  const auto st = pos.make_move(mv);
  require(pos.piece_on(tuna::make_square(2, 7)) == tuna::piece::bk, "castle king on c8");
  require(pos.piece_on(tuna::make_square(3, 7)) == tuna::piece::br, "castle rook on d8");
  require(pos.piece_on(tuna::make_square(0, 7)) == tuna::piece::none, "castle a8 empty");
  require(pos.castling_rights() == 0, "castle rights cleared");
  require(pos.key() == pos.recompute_key(), "castle key");
  pos.unmake_move(mv, st);
  require(pos == before, "castle restored");
}

auto test_random_sequences() -> void
{
  auto rng = std::mt19937_64{0xabcdef1234567890ull};
  for(auto game = 0; game < 30; ++game) {
    auto pos = tuna::position::start();
    auto history = std::vector<std::pair<tuna::move, tuna::move_state>>{};
    auto snapshots = std::vector<tuna::position>{};
    snapshots.push_back(pos);
    for(auto ply = 0; ply < 300; ++ply) {
      auto moves = tuna::movegen::generate_legal(pos);
      if(moves.size() == 0) {
        break;
      }
      const auto mv = moves[rng() % moves.size()];
      history.push_back({mv, pos.make_move(mv)});
      require(pos.key() == pos.recompute_key(), "random sequence key");
      snapshots.push_back(pos);
    }
    for(auto i = static_cast<int>(history.size()) - 1; i >= 0; --i) {
      pos.unmake_move(history[i].first, history[i].second);
      require(pos == snapshots[i], "random sequence restore");
    }
  }
}

}

auto main() -> int
{
  test_start_round_trip();
  test_double_push_ep_square();
  test_capture_round_trip();
  test_en_passant_round_trip();
  test_castling_round_trip();
  test_promotion_round_trip();
  test_promotion_capture_round_trip();
  test_black_castling_round_trip();
  test_random_sequences();
  return 0;
}