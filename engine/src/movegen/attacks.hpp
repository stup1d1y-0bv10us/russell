#pragma once

#include "core/types.hpp"

namespace tuna::movegen {

[[nodiscard]] auto pawn_attacks(color c, int sq) noexcept -> bitboard;
[[nodiscard]] auto knight_attacks(int sq) noexcept -> bitboard;
[[nodiscard]] auto king_attacks(int sq) noexcept -> bitboard;
[[nodiscard]] auto bishop_attacks(int sq, bitboard occupancy) noexcept -> bitboard;
[[nodiscard]] auto rook_attacks(int sq, bitboard occupancy) noexcept -> bitboard;
[[nodiscard]] auto queen_attacks(int sq, bitboard occupancy) noexcept -> bitboard;

}