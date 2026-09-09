#include "core/types.hpp"
#include "movegen/attacks.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>

namespace {

auto require(bool value, const char* message) -> void
{
  if(!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

auto on_board(int file, int rank) noexcept -> bool
{
  return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

constexpr auto bishop_df = std::array<int, 4>{1, 1, -1, -1};
constexpr auto bishop_dr = std::array<int, 4>{1, -1, 1, -1};
constexpr auto rook_df = std::array<int, 4>{1, -1, 0, 0};
constexpr auto rook_dr = std::array<int, 4>{0, 0, 1, -1};

auto reference_attack(int sq, russell::bitboard occupancy, const std::array<int, 4>& df, const std::array<int, 4>& dr) noexcept -> russell::bitboard
{
  auto result = russell::bitboard{0};
  const auto file = russell::file_of(sq);
  const auto rank = russell::rank_of(sq);
  for(auto i = 0; i < 4; ++i) {
    auto next_file = file + df[i];
    auto next_rank = rank + dr[i];
    while(on_board(next_file, next_rank)) {
      const auto next = russell::make_square(next_file, next_rank);
      result |= russell::bit(next);
      if((occupancy & russell::bit(next)) != 0) {
        break;
      }
      next_file += df[i];
      next_rank += dr[i];
    }
  }
  return result;
}

auto reference_bishop(int sq, russell::bitboard occupancy) noexcept -> russell::bitboard
{
  return reference_attack(sq, occupancy, bishop_df, bishop_dr);
}

auto reference_rook(int sq, russell::bitboard occupancy) noexcept -> russell::bitboard
{
  return reference_attack(sq, occupancy, rook_df, rook_dr);
}

auto reference_queen(int sq, russell::bitboard occupancy) noexcept -> russell::bitboard
{
  return reference_bishop(sq, occupancy) | reference_rook(sq, occupancy);
}

auto test_hand_verified() -> void
{
  require(russell::movegen::bishop_attacks(russell::make_square(0, 0), 0) ==
    (russell::bit(russell::make_square(1, 1)) | russell::bit(russell::make_square(2, 2)) | russell::bit(russell::make_square(3, 3))
      | russell::bit(russell::make_square(4, 4)) | russell::bit(russell::make_square(5, 5)) | russell::bit(russell::make_square(6, 6))
      | russell::bit(russell::make_square(7, 7))), "bishop a1 empty");

  require(russell::movegen::bishop_attacks(russell::make_square(0, 0), russell::bit(russell::make_square(3, 3))) ==
    (russell::bit(russell::make_square(1, 1)) | russell::bit(russell::make_square(2, 2)) | russell::bit(russell::make_square(3, 3))),
    "bishop a1 blocked at d4");

  require(russell::movegen::rook_attacks(russell::make_square(0, 0), 0) ==
    (russell::bit(russell::make_square(1, 0)) | russell::bit(russell::make_square(2, 0)) | russell::bit(russell::make_square(3, 0))
      | russell::bit(russell::make_square(4, 0)) | russell::bit(russell::make_square(5, 0)) | russell::bit(russell::make_square(6, 0))
      | russell::bit(russell::make_square(7, 0)) | russell::bit(russell::make_square(0, 1)) | russell::bit(russell::make_square(0, 2))
      | russell::bit(russell::make_square(0, 3)) | russell::bit(russell::make_square(0, 4)) | russell::bit(russell::make_square(0, 5))
      | russell::bit(russell::make_square(0, 6)) | russell::bit(russell::make_square(0, 7))), "rook a1 empty");

  require(russell::movegen::rook_attacks(russell::make_square(0, 0), russell::bit(russell::make_square(1, 0))) ==
    (russell::bit(russell::make_square(1, 0)) | russell::bit(russell::make_square(0, 1)) | russell::bit(russell::make_square(0, 2))
      | russell::bit(russell::make_square(0, 3)) | russell::bit(russell::make_square(0, 4)) | russell::bit(russell::make_square(0, 5))
      | russell::bit(russell::make_square(0, 6)) | russell::bit(russell::make_square(0, 7))), "rook a1 blocked at b1");

  require(russell::movegen::rook_attacks(russell::make_square(7, 0), 0) ==
    (russell::bit(russell::make_square(6, 0)) | russell::bit(russell::make_square(5, 0)) | russell::bit(russell::make_square(4, 0))
      | russell::bit(russell::make_square(3, 0)) | russell::bit(russell::make_square(2, 0)) | russell::bit(russell::make_square(1, 0))
      | russell::bit(russell::make_square(0, 0)) | russell::bit(russell::make_square(7, 1)) | russell::bit(russell::make_square(7, 2))
      | russell::bit(russell::make_square(7, 3)) | russell::bit(russell::make_square(7, 4)) | russell::bit(russell::make_square(7, 5))
      | russell::bit(russell::make_square(7, 6)) | russell::bit(russell::make_square(7, 7))), "rook h1 empty");

  require(russell::movegen::queen_attacks(russell::make_square(3, 3), 0) ==
    (russell::movegen::bishop_attacks(russell::make_square(3, 3), 0) | russell::movegen::rook_attacks(russell::make_square(3, 3), 0)),
    "queen equals bishop|rook");
}

auto test_random_reference() -> void
{
  auto rng = std::mt19937_64{0x9e3779b97f4a7c15ull};
  auto total = std::uint64_t{0};
  for(auto sq = 0; sq < russell::square_count; ++sq) {
    for(auto i = 0; i < 16000; ++i) {
      const auto occupancy = rng() & ~russell::bit(sq);
      const auto expected_bishop = reference_bishop(sq, occupancy);
      const auto actual_bishop = russell::movegen::bishop_attacks(sq, occupancy);
      if(actual_bishop != expected_bishop) {
        std::cerr << "bishop mismatch sq=" << sq << " occ=0x" << std::hex << occupancy << std::dec << '\n';
        require(false, "random bishop mismatch");
      }
      const auto expected_rook = reference_rook(sq, occupancy);
      const auto actual_rook = russell::movegen::rook_attacks(sq, occupancy);
      if(actual_rook != expected_rook) {
        std::cerr << "rook mismatch sq=" << sq << " occ=0x" << std::hex << occupancy << std::dec << '\n';
        require(false, "random rook mismatch");
      }
      const auto expected_queen = reference_queen(sq, occupancy);
      const auto actual_queen = russell::movegen::queen_attacks(sq, occupancy);
      if(actual_queen != expected_queen) {
        std::cerr << "queen mismatch sq=" << sq << " occ=0x" << std::hex << occupancy << std::dec << '\n';
        require(false, "random queen mismatch");
      }
      ++total;
    }
  }
  require(total >= 1000000, "at least 1 million randomized configurations per piece type");
}

}

auto main() -> int
{
  test_hand_verified();
  test_random_reference();
  return 0;
}