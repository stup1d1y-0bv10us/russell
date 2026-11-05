#pragma once

#include "core/position.hpp"
#include "movegen/attacks.hpp"

#include <bit>

namespace russell::movegen {

struct pin_state {
  bool usable = false;
  int checker_count = 0;
  int checker_sq = -1;
  bool checker_is_slider = false;
  bitboard check_block = 0;
  int pin_from[8]{};
  bitboard pin_mask[8]{};
  int pin_count = 0;
};

[[nodiscard]] inline auto has_single_bit(bitboard b) noexcept -> bool
{
  return b != 0 && (b & (b - 1)) == 0;
}

[[nodiscard]] inline auto pin_between(int a, int b) noexcept -> bitboard
{
  const auto dr = rank_of(b) - rank_of(a);
  const auto df = file_of(b) - file_of(a);
  const auto adr = dr < 0 ? -dr : dr;
  const auto adf = df < 0 ? -df : df;
  if((dr == 0 && df == 0) || (dr != 0 && df != 0 && adr != adf)) {
    return bitboard{0};
  }
  const auto step = (dr < 0 ? -8 : dr > 0 ? 8 : 0) + (df < 0 ? -1 : df > 0 ? 1 : 0);
  auto out = bitboard{0};
  auto sq = a + step;
  while(sq != b) {
    out |= bit(sq);
    sq += step;
  }
  return out;
}

inline auto compute_pin_state(const position& pos, color us, int king_sq) noexcept -> pin_state
{
  auto st = pin_state{};
  const auto them = opposite(us);
  const auto own = pos.occupancy(us);

  const auto occ_through_own = pos.occupancy() & ~own;
  auto checkers = (pawn_attacks(us, king_sq) & pos.pieces(them, piece_type::pawn))
                  | (knight_attacks(king_sq) & pos.pieces(them, piece_type::knight))
                  | (king_attacks(king_sq) & pos.pieces(them, piece_type::king));
  auto sliders = (bishop_attacks(king_sq, occ_through_own)
                  & (pos.pieces(them, piece_type::bishop) | pos.pieces(them, piece_type::queen)))
                 | (rook_attacks(king_sq, occ_through_own)
                    & (pos.pieces(them, piece_type::rook) | pos.pieces(them, piece_type::queen)));
  while(sliders != 0) {
    const auto s = static_cast<int>(std::countr_zero(sliders));
    sliders &= sliders - 1;

    const auto seg = pin_between(king_sq, s);
    const auto mid = seg & own;
    if(mid == 0) {
      checkers |= bit(s);
    } else if(has_single_bit(mid) && st.pin_count < 8) {
      st.pin_from[st.pin_count] = static_cast<int>(std::countr_zero(mid));
      st.pin_mask[st.pin_count] = seg | bit(s);
      ++st.pin_count;
    }
  }
  st.checker_count = std::popcount(checkers);
  if(st.checker_count == 1) {
    st.checker_sq = static_cast<int>(std::countr_zero(checkers));
    const auto cp = pos.piece_on(st.checker_sq);
    if(cp != piece::none) {
      const auto cpt = piece_type_of(cp);
      st.checker_is_slider = cpt == piece_type::bishop || cpt == piece_type::rook
                             || cpt == piece_type::queen;
      if(st.checker_is_slider) {
        st.check_block = pin_between(king_sq, st.checker_sq);
      }
    }
  }
  st.usable = true;
  return st;
}

[[nodiscard]] inline auto pin_state_legal_ordinary(const pin_state& st, int from, int to) noexcept
    -> bool
{
  const auto to_bit = bit(to);
  for(auto i = 0; i < st.pin_count; ++i) {
    if(st.pin_from[i] == from) {
      if((st.pin_mask[i] & to_bit) == 0) {
        return false;
      }
      break;
    }
  }
  if(st.checker_count == 0) {
    return true;
  }
  if(st.checker_count >= 2) {
    return false;
  }
  if(to == st.checker_sq) {
    return true;
  }
  if(st.checker_is_slider && (st.check_block & to_bit) != 0) {
    return true;
  }
  return false;
}
}
