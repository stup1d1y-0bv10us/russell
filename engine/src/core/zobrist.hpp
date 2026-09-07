#pragma once

#include "core/types.hpp"

#include <array>
#include <cstdint>

namespace tuna::zobrist {

using piece_keys = std::array<std::array<std::array<std::uint64_t, square_count>, piece_type_count>, color_count>;
using castling_keys = std::array<std::uint64_t, 16>;
using en_passant_keys = std::array<std::uint64_t, 8>;

[[nodiscard]] auto piece_square(color c, piece_type pt, int sq) noexcept -> std::uint64_t;
[[nodiscard]] auto castling(std::uint8_t rights) noexcept -> std::uint64_t;
[[nodiscard]] auto en_passant(int file) noexcept -> std::uint64_t;
[[nodiscard]] auto side() noexcept -> std::uint64_t;

}