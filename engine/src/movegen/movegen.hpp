#pragma once

#include "core/move.hpp"
#include "core/position.hpp"

namespace tuna::movegen {

[[nodiscard]] auto generate_non_sliding_pseudo_legal(const position& pos) noexcept -> move_list;
[[nodiscard]] auto generate_pseudo_legal(const position& pos) noexcept -> move_list;
[[nodiscard]] auto generate_legal(position& pos) noexcept -> move_list;

[[nodiscard]] auto generate_captures_promotions(position& pos) noexcept -> move_list;

[[nodiscard]] auto generate_evasions(position& pos) noexcept -> move_list;

[[nodiscard]] auto is_square_attacked(const position& pos, int sq, color by) noexcept -> bool;

}