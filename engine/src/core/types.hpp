#pragma once

#include <array>
#include <cstdint>

namespace tuna {

using bitboard = std::uint64_t;

enum class color : std::uint8_t {
  white,
  black
};

enum class piece_type : std::uint8_t {
  pawn,
  knight,
  bishop,
  rook,
  queen,
  king
};

enum class piece : std::uint8_t {
  none,
  wp,
  wn,
  wb,
  wr,
  wq,
  wk,
  bp,
  bn,
  bb,
  br,
  bq,
  bk
};

constexpr auto color_count = 2;
constexpr auto piece_type_count = 6;
constexpr auto square_count = 64;
constexpr auto no_square = -1;

constexpr auto color_index(color c) noexcept -> int
{
  return static_cast<int>(c);
}

constexpr auto piece_type_index(piece_type pt) noexcept -> int
{
  return static_cast<int>(pt);
}

constexpr auto piece_type_of(piece p) noexcept -> piece_type
{
  return static_cast<piece_type>((static_cast<int>(p) - 1) % piece_type_count);
}

constexpr auto bit(int sq) noexcept -> bitboard
{
  return bitboard{1} << sq;
}

constexpr auto file_of(int sq) noexcept -> int
{
  return sq & 7;
}

constexpr auto rank_of(int sq) noexcept -> int
{
  return sq >> 3;
}

constexpr auto make_square(int file, int rank) noexcept -> int
{
  return rank * 8 + file;
}

constexpr auto opposite(color c) noexcept -> color
{
  return c == color::white ? color::black : color::white;
}

constexpr auto white_king_side = std::uint8_t{1};
constexpr auto white_queen_side = std::uint8_t{2};
constexpr auto black_king_side = std::uint8_t{4};
constexpr auto black_queen_side = std::uint8_t{8};
constexpr auto all_castling = std::uint8_t{15};

}