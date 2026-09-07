#pragma once

#include "core/move.hpp"
#include "core/types.hpp"

#include <array>
#include <string>

namespace tuna {

struct move_state {
  std::uint8_t castling_rights = 0;
  int en_passant_square = no_square;
  int halfmove_clock = 0;
  int fullmove_number = 1;
  piece captured = piece::none;
  std::uint64_t key = 0;
};

class position {
public:
  using piece_boards = std::array<std::array<bitboard, piece_type_count>, color_count>;
  using occupancies = std::array<bitboard, 3>;

  position() = default;

  static auto empty() noexcept -> position;
  static auto start() noexcept -> position;
  static auto from_fen(const std::string& fen) -> position;

  [[nodiscard]] auto fen() const -> std::string;

  [[nodiscard]] auto pieces(color c, piece_type pt) const noexcept -> bitboard;
  [[nodiscard]] auto occupancy(color c) const noexcept -> bitboard;
  [[nodiscard]] auto occupancy() const noexcept -> bitboard;
  [[nodiscard]] auto side_to_move() const noexcept -> color;
  [[nodiscard]] auto castling_rights() const noexcept -> std::uint8_t;
  [[nodiscard]] auto en_passant_square() const noexcept -> int;
  [[nodiscard]] auto halfmove_clock() const noexcept -> int;
  [[nodiscard]] auto fullmove_number() const noexcept -> int;
  [[nodiscard]] auto key() const noexcept -> std::uint64_t;
  [[nodiscard]] auto recompute_key() const noexcept -> std::uint64_t;
  [[nodiscard]] auto piece_on(int sq) const noexcept -> piece;
  [[nodiscard]] auto board_string() const -> std::string;

  auto clear() noexcept -> void;
  auto set_piece(color c, piece_type pt, int sq) noexcept -> void;
  auto remove_piece(int sq) noexcept -> void;
  auto set_side_to_move(color c) noexcept -> void;
  auto set_castling_rights(std::uint8_t rights) noexcept -> void;
  auto set_en_passant_square(int sq) noexcept -> void;
  auto set_halfmove_clock(int value) noexcept -> void;
  auto set_fullmove_number(int value) noexcept -> void;

  [[nodiscard]] auto make_move(move mv) noexcept -> move_state;
  auto unmake_move(move mv, const move_state& st) noexcept -> void;
  [[nodiscard]] auto make_null_move() noexcept -> move_state;
  auto unmake_null_move(const move_state& st) noexcept -> void;

  friend auto operator==(const position& lhs, const position& rhs) noexcept -> bool = default;

private:
  piece_boards pieces_{};
  occupancies occupancies_{};
  color side_to_move_ = color::white;
  std::uint8_t castling_rights_ = 0;
  int en_passant_square_ = no_square;
  int halfmove_clock_ = 0;
  int fullmove_number_ = 1;
  std::uint64_t key_ = 0;

  auto add_to_occupancy(color c, bitboard bb) noexcept -> void;
  auto remove_from_occupancy(color c, bitboard bb) noexcept -> void;
  auto move_piece(color c, piece_type pt, int from, int to) noexcept -> void;
  auto toggle_piece_key(color c, piece_type pt, int sq) noexcept -> void;
  auto toggle_castling_key() noexcept -> void;
  auto toggle_en_passant_key() noexcept -> void;
  auto toggle_side_key() noexcept -> void;
};

}