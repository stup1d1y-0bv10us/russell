#include "movegen/attacks.hpp"

#include <bit>
#include <vector>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <immintrin.h>
#include <intrin.h>
#endif

#if defined(__BMI2__) && (defined(__x86_64__) || defined(__i386__))
#include <immintrin.h>
#endif

namespace tuna::movegen {

namespace {

constexpr auto on_board(int file, int rank) noexcept -> bool
{
  return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

constexpr auto make_knight_attacks() noexcept -> std::array<bitboard, square_count>
{
  auto attacks = std::array<bitboard, square_count>{};
  constexpr auto df = std::array<int, 8>{1, 2, 2, 1, -1, -2, -2, -1};
  constexpr auto dr = std::array<int, 8>{2, 1, -1, -2, -2, -1, 1, 2};
  for(auto sq = 0; sq < square_count; ++sq) {
    const auto file = file_of(sq);
    const auto rank = rank_of(sq);
    for(auto i = 0; i < 8; ++i) {
      const auto next_file = file + df[i];
      const auto next_rank = rank + dr[i];
      if(on_board(next_file, next_rank)) {
        attacks[sq] |= bit(make_square(next_file, next_rank));
      }
    }
  }
  return attacks;
}

constexpr auto make_king_attacks() noexcept -> std::array<bitboard, square_count>
{
  auto attacks = std::array<bitboard, square_count>{};
  for(auto sq = 0; sq < square_count; ++sq) {
    const auto file = file_of(sq);
    const auto rank = rank_of(sq);
    for(auto df = -1; df <= 1; ++df) {
      for(auto dr = -1; dr <= 1; ++dr) {
        if(df != 0 || dr != 0) {
          const auto next_file = file + df;
          const auto next_rank = rank + dr;
          if(on_board(next_file, next_rank)) {
            attacks[sq] |= bit(make_square(next_file, next_rank));
          }
        }
      }
    }
  }
  return attacks;
}

constexpr auto make_pawn_attacks() noexcept -> std::array<std::array<bitboard, square_count>, color_count>
{
  auto attacks = std::array<std::array<bitboard, square_count>, color_count>{};
  for(auto sq = 0; sq < square_count; ++sq) {
    const auto file = file_of(sq);
    const auto rank = rank_of(sq);
    if(on_board(file - 1, rank + 1)) {
      attacks[color_index(color::white)][sq] |= bit(make_square(file - 1, rank + 1));
    }
    if(on_board(file + 1, rank + 1)) {
      attacks[color_index(color::white)][sq] |= bit(make_square(file + 1, rank + 1));
    }
    if(on_board(file - 1, rank - 1)) {
      attacks[color_index(color::black)][sq] |= bit(make_square(file - 1, rank - 1));
    }
    if(on_board(file + 1, rank - 1)) {
      attacks[color_index(color::black)][sq] |= bit(make_square(file + 1, rank - 1));
    }
  }
  return attacks;
}

constexpr auto knight_attacks_value = make_knight_attacks();
constexpr auto king_attacks_value = make_king_attacks();
constexpr auto pawn_attacks_value = make_pawn_attacks();

struct slider_entry {
  bitboard mask = 0;
  std::uint32_t offset = 0;
  std::uint8_t bits = 0;
};

struct slider_tables {
  std::array<slider_entry, square_count> bishops{};
  std::array<slider_entry, square_count> rooks{};
  std::vector<bitboard> bishop_attacks{};
  std::vector<bitboard> rook_attacks{};
};

using index_fn = std::uint32_t (*)(bitboard, bitboard) noexcept;

auto ray_mask(int sq, const std::array<int, 4>& df, const std::array<int, 4>& dr) noexcept -> bitboard
{
  auto result = bitboard{0};
  const auto file = file_of(sq);
  const auto rank = rank_of(sq);
  for(auto i = 0; i < 4; ++i) {
    auto next_file = file + df[i];
    auto next_rank = rank + dr[i];
    while(on_board(next_file + df[i], next_rank + dr[i])) {
      result |= bit(make_square(next_file, next_rank));
      next_file += df[i];
      next_rank += dr[i];
    }
  }
  return result;
}

auto ray_attacks(int sq, bitboard occupancy, const std::array<int, 4>& df, const std::array<int, 4>& dr) noexcept -> bitboard
{
  auto result = bitboard{0};
  const auto file = file_of(sq);
  const auto rank = rank_of(sq);
  for(auto i = 0; i < 4; ++i) {
    auto next_file = file + df[i];
    auto next_rank = rank + dr[i];
    while(on_board(next_file, next_rank)) {
      const auto sq_next = make_square(next_file, next_rank);
      result |= bit(sq_next);
      if((occupancy & bit(sq_next)) != 0) {
        break;
      }
      next_file += df[i];
      next_rank += dr[i];
    }
  }
  return result;
}

auto deposit_bits(std::uint32_t index, bitboard mask) noexcept -> bitboard
{
  auto result = bitboard{0};
  auto bit_index = 0;
  while(mask != 0) {
    const auto sq = std::countr_zero(mask);
    if((index & (std::uint32_t{1} << bit_index)) != 0) {
      result |= bit(static_cast<int>(sq));
    }
    mask &= mask - 1;
    ++bit_index;
  }
  return result;
}

auto masked_index(bitboard occupancy, bitboard mask) noexcept -> std::uint32_t
{
  auto result = std::uint32_t{0};
  auto bit_index = 0;
  occupancy &= mask;
  while(mask != 0) {
    const auto sq = std::countr_zero(mask);
    if((occupancy & bit(static_cast<int>(sq))) != 0) {
      result |= std::uint32_t{1} << bit_index;
    }
    mask &= mask - 1;
    ++bit_index;
  }
  return result;
}

#if defined(__BMI2__) && (defined(__x86_64__) || defined(__i386__))
auto pext_index(bitboard occupancy, bitboard mask) noexcept -> std::uint32_t
{
  return static_cast<std::uint32_t>(_pext_u64(occupancy, mask));
}
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
auto pext_index(bitboard occupancy, bitboard mask) noexcept -> std::uint32_t
{
  return static_cast<std::uint32_t>(_pext_u64(occupancy, mask));
}
#endif

auto has_bmi2() noexcept -> bool
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  int regs[4]{};
  __cpuidex(regs, 7, 0);
  return (regs[1] & (1 << 8)) != 0;
#elif defined(__BMI2__) && (defined(__x86_64__) || defined(__i386__))
  return true;
#else
  return false;
#endif
}

auto indexer() noexcept -> index_fn
{
#if (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))) || (defined(__BMI2__) && (defined(__x86_64__) || defined(__i386__)))
  if(has_bmi2()) {
    return pext_index;
  }
#endif
  return masked_index;
}

auto make_slider_tables() -> slider_tables
{
  constexpr auto bishop_df = std::array<int, 4>{1, 1, -1, -1};
  constexpr auto bishop_dr = std::array<int, 4>{1, -1, 1, -1};
  constexpr auto rook_df = std::array<int, 4>{1, -1, 0, 0};
  constexpr auto rook_dr = std::array<int, 4>{0, 0, 1, -1};
  auto tables = slider_tables{};
  for(auto sq = 0; sq < square_count; ++sq) {
    auto& entry = tables.bishops[sq];
    entry.mask = ray_mask(sq, bishop_df, bishop_dr);
    entry.offset = static_cast<std::uint32_t>(tables.bishop_attacks.size());
    entry.bits = static_cast<std::uint8_t>(std::popcount(entry.mask));
    const auto count = std::uint32_t{1} << entry.bits;
    tables.bishop_attacks.resize(tables.bishop_attacks.size() + count);
    for(auto index = std::uint32_t{0}; index < count; ++index) {
      const auto occupancy = deposit_bits(index, entry.mask);
      tables.bishop_attacks[entry.offset + index] = ray_attacks(sq, occupancy, bishop_df, bishop_dr);
    }
  }
  for(auto sq = 0; sq < square_count; ++sq) {
    auto& entry = tables.rooks[sq];
    entry.mask = ray_mask(sq, rook_df, rook_dr);
    entry.offset = static_cast<std::uint32_t>(tables.rook_attacks.size());
    entry.bits = static_cast<std::uint8_t>(std::popcount(entry.mask));
    const auto count = std::uint32_t{1} << entry.bits;
    tables.rook_attacks.resize(tables.rook_attacks.size() + count);
    for(auto index = std::uint32_t{0}; index < count; ++index) {
      const auto occupancy = deposit_bits(index, entry.mask);
      tables.rook_attacks[entry.offset + index] = ray_attacks(sq, occupancy, rook_df, rook_dr);
    }
  }
  return tables;
}

auto tables() -> const slider_tables&
{
  static const auto value = make_slider_tables();
  return value;
}

auto slider_index(bitboard occupancy, const slider_entry& entry) noexcept -> std::uint32_t
{
  static const auto fn = indexer();
  return entry.offset + fn(occupancy, entry.mask);
}

}

auto pawn_attacks(color c, int sq) noexcept -> bitboard
{
  return pawn_attacks_value[color_index(c)][sq];
}

auto knight_attacks(int sq) noexcept -> bitboard
{
  return knight_attacks_value[sq];
}

auto king_attacks(int sq) noexcept -> bitboard
{
  return king_attacks_value[sq];
}

auto bishop_attacks(int sq, bitboard occupancy) noexcept -> bitboard
{
  const auto& data = tables();
  const auto& entry = data.bishops[sq];
  return data.bishop_attacks[slider_index(occupancy, entry)];
}

auto rook_attacks(int sq, bitboard occupancy) noexcept -> bitboard
{
  const auto& data = tables();
  const auto& entry = data.rooks[sq];
  return data.rook_attacks[slider_index(occupancy, entry)];
}

auto queen_attacks(int sq, bitboard occupancy) noexcept -> bitboard
{
  return bishop_attacks(sq, occupancy) | rook_attacks(sq, occupancy);
}

}