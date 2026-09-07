#pragma once

#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace tuna {

enum class move_flag : std::uint8_t {
  quiet,
  double_push,
  capture,
  en_passant,
  promotion,
  promotion_capture,
  castling
};

struct move {
  std::uint8_t from = 0;
  std::uint8_t to = 0;
  piece_type promotion = piece_type::queen;
  move_flag flag = move_flag::quiet;

  friend auto operator==(const move& lhs, const move& rhs) noexcept -> bool = default;
};

class move_list {
public:
  auto push(move mv) noexcept -> void
  {
    moves_[size_] = mv;
    ++size_;
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t
  {
    return size_;
  }

  [[nodiscard]] auto begin() const noexcept -> const move*
  {
    return moves_.data();
  }

  [[nodiscard]] auto end() const noexcept -> const move*
  {
    return moves_.data() + size_;
  }

  [[nodiscard]] auto operator[](std::size_t index) const noexcept -> move
  {
    return moves_[index];
  }

private:
  std::array<move, 256> moves_{};
  std::size_t size_ = 0;
};

}