#pragma once

#include "core/position.hpp"

#include <memory>

namespace tuna::eval {

namespace nnue {
class network;
class evaluator;
}

struct weights {
  double material = 1.0;
  double pst = 1.0;
  double mobility = 1.0;
  double pawn_structure = 1.0;
  double king_safety = 1.0;
  double rook_bonuses = 1.0;
};

[[nodiscard]] auto default_weights() noexcept -> weights;

[[nodiscard]] auto evaluate(const position& pos) noexcept -> int;
[[nodiscard]] auto evaluate(const position& pos, const weights& w) noexcept -> int;

[[nodiscard]] auto nnue_accumulator() noexcept -> nnue::evaluator*;

auto set_nnue(std::shared_ptr<const eval::nnue::network> net) noexcept -> void;

}