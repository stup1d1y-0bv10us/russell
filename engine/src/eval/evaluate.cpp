#include "eval/evaluate.hpp"

#include "core/position.hpp"
#include "core/types.hpp"
#include "eval/nnue.hpp"
#include "movegen/attacks.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>

namespace tuna::eval {

namespace {

constexpr auto material_value = std::to_array<int>({
  100,
  320,
  330,
  500,
  900,
  20000,
});

constexpr auto pawn_table = std::to_array<int>({
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0,
});

constexpr auto knight_table = std::to_array<int>({
  -50,-40,-30,-30,-30,-30,-40,-50,
  -40,-20,  0,  0,  0,  0,-20,-40,
  -30,  0, 10, 15, 15, 10,  0,-30,
  -30,  5, 15, 20, 20, 15,  5,-30,
  -30,  0, 15, 20, 20, 15,  0,-30,
  -30,  5, 10, 15, 15, 10,  5,-30,
  -40,-20,  0,  5,  5,  0,-20,-40,
  -50,-40,-30,-30,-30,-30,-40,-50,
});

constexpr auto bishop_table = std::to_array<int>({
  -20,-10,-10,-10,-10,-10,-10,-20,
  -10,  0,  0,  0,  0,  0,  0,-10,
  -10,  0,  5, 10, 10,  5,  0,-10,
  -10,  5,  5, 10, 10,  5,  5,-10,
  -10,  0, 10, 10, 10, 10,  0,-10,
  -10, 10, 10, 10, 10, 10, 10,-10,
  -10,  5,  0,  0,  0,  0,  5,-10,
  -20,-10,-10,-10,-10,-10,-10,-20,
});

constexpr auto rook_table = std::to_array<int>({
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10, 10, 10, 10, 10,  5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     0,  0,  0,  5,  5,  0,  0,  0,
});

constexpr auto queen_table = std::to_array<int>({
  -20,-10,-10, -5, -5,-10,-10,-20,
  -10,  0,  0,  0,  0,  0,  0,-10,
  -10,  0,  5,  5,  5,  5,  0,-10,
   -5,  0,  5,  5,  5,  5,  0, -5,
    0,  0,  5,  5,  5,  5,  0, -5,
  -10,  5,  5,  5,  5,  5,  0,-10,
  -10,  0,  5,  0,  0,  0,  0,-10,
  -20,-10,-10, -5, -5,-10,-10,-20,
});

constexpr auto king_table = std::to_array<int>({
  -30,-40,-40,-50,-50,-40,-40,-30,
  -30,-40,-40,-50,-50,-40,-40,-30,
  -30,-40,-40,-50,-50,-40,-40,-30,
  -30,-40,-40,-50,-50,-40,-40,-30,
  -20,-30,-30,-40,-40,-30,-30,-20,
  -10,-20,-20,-20,-20,-20,-20,-10,
   20, 20,  0,  0,  0,  0, 20, 20,
   20, 30, 10,  0,  0, 10, 30, 20,
});

constexpr auto king_eg_table = std::to_array<int>({
  -50,-30,-30,-30,-30,-30,-30,-50,
  -30,-20,  0,  0,  0,  0,-20,-30,
  -30,-10, 20, 20, 20, 20,-10,-30,
  -30,-10, 20, 30, 30, 20,-10,-30,
  -30,-10, 20, 30, 30, 20,-10,-30,
  -30,-10, 20, 20, 20, 20,-10,-30,
  -30,-20,  0,  0,  0,  0,-20,-30,
  -50,-30,-30,-30,-30,-30,-30,-50,
});

constexpr auto pst_table = [](piece_type pt) -> const std::array<int, 64>& {
  switch(pt) {
    case piece_type::pawn:   return pawn_table;
    case piece_type::knight: return knight_table;
    case piece_type::bishop: return bishop_table;
    case piece_type::rook:   return rook_table;
    case piece_type::queen:  return queen_table;
    default:                 return king_table;
  }
};

constexpr auto phase_weights = std::to_array<int>({0, 1, 1, 2, 4, 0});
constexpr auto phase_max = 24;

constexpr auto passed_mg = std::to_array<int>({0, 10, 25, 45, 75, 115, 165, 230});
constexpr auto passed_eg = std::to_array<int>({0, 15, 30, 55, 85, 130, 190, 260});

constexpr auto file_masks = [] {
  auto masks = std::array<bitboard, 8>{};
  for(auto f = 0; f < 8; ++f) {
    masks[f] = 0x0101010101010101ULL << f;
  }
  return masks;
}();

constexpr auto rank_above = [] {
  auto masks = std::array<bitboard, 8>{};
  for(auto r = 0; r < 8; ++r) {
    auto bb = bitboard{0};
    for(auto rr = r + 1; rr < 8; ++rr) {
      bb |= 0xFFULL << (8 * rr);
    }
    masks[r] = bb;
  }
  return masks;
}();

struct term {
  int mg = 0;
  int eg = 0;
};

struct terms {
  term material;
  term pst;
  term mobility;
  term pawn_structure;
  term king_safety;
  term rook_bonuses;
};

auto flip_vertical(bitboard b) noexcept -> bitboard
{
  b = ((b & 0x00FF00FF00FF00FFULL) << 8) | ((b & 0xFF00FF00FF00FF00ULL) >> 8);
  b = ((b & 0x0000FFFF0000FFFFULL) << 16) | ((b & 0xFFFF0000FFFF0000ULL) >> 16);
  b = (b << 32) | (b >> 32);
  return b;
}

auto pst_index(int sq) noexcept -> int
{
  return (7 - rank_of(sq)) * 8 + file_of(sq);
}

auto add_material_pst(const position& pos, color c, int mirror, terms& out) -> void
{
  for(auto i = 0; i < piece_type_count; ++i) {
    const auto pt = static_cast<piece_type>(i);
    const auto& table = pst_table(pt);
    auto bb = pos.pieces(c, pt);
    while(bb != 0) {
      const auto sq = static_cast<int>(std::countr_zero(bb)) ^ mirror;
      bb &= bb - 1;
      const auto value = material_value[i];
      const auto pst = table[pst_index(sq)];
      out.material.mg += value;
      out.material.eg += value;
      out.pst.mg += pst;
      out.pst.eg += pt == piece_type::king ? king_eg_table[pst_index(sq)] : pst;
    }
  }
}

auto pawn_structure(const position& pos, color c, int mirror, term& out) -> void
{
  auto own = pos.pieces(c, piece_type::pawn);
  auto enemy = pos.pieces(opposite(c), piece_type::pawn);
  if(mirror != 0) {
    own = flip_vertical(own);
    enemy = flip_vertical(enemy);
  }

  auto processed_files = bitboard{0};
  auto scan = own;
  while(scan != 0) {
    const auto sq = static_cast<int>(std::countr_zero(scan));
    scan &= scan - 1;
    const auto f = file_of(sq);
    const auto r = rank_of(sq);
    const auto file = file_masks[f];

    if((processed_files & file) == 0) {
      processed_files |= file;
      if(std::popcount(own & file) > 1) {
        out.mg -= 15;
        out.eg -= 30;
      }
    }

    const auto adjacent = [&]() -> bitboard {
      auto bb = bitboard{0};
      if(f > 0) bb |= file_masks[f - 1];
      if(f < 7) bb |= file_masks[f + 1];
      return bb;
    }();
    if((own & adjacent) == 0) {
      out.mg -= 20;
      out.eg -= 25;
    }

    const auto ahead = (adjacent | file) & rank_above[r];
    if((enemy & ahead) == 0) {
      out.mg += passed_mg[r];
      out.eg += passed_eg[r];
    }
  }
}

auto king_safety(const position& pos, color c, int mirror, bitboard occupied_frame,
                 term& out) -> void
{
  auto kings = pos.pieces(c, piece_type::king);
  if(kings == 0) {
    return;
  }
  const auto ksq = (static_cast<int>(std::countr_zero(kings)) ^ mirror);
  const auto kf = file_of(ksq);
  const auto kr = rank_of(ksq);

  auto own = pos.pieces(c, piece_type::pawn);
  if(mirror != 0) {
    own = flip_vertical(own);
  }

  auto shield = bitboard{0};
  for(auto df = -1; df <= 1; ++df) {
    const auto f = kf + df;
    if(f < 0 || f > 7) {
      continue;
    }
    for(const auto dr : {1, 2}) {
      const auto r = kr + dr;
      if(r < 8) {
        shield |= bit(make_square(f, r));
      }
    }
  }
  out.mg += std::popcount(own & shield) * 12;

  auto attack = 0;
  const auto ring = movegen::king_attacks(ksq);
  const auto weights = std::to_array<int>({1, 2, 2, 3, 4, 0});
  for(auto i = 0; i < piece_type_count - 1; ++i) {
    const auto pt = static_cast<piece_type>(i);
    auto bb = pos.pieces(opposite(c), pt);
    if(mirror != 0) {
      bb = flip_vertical(bb);
    }
    while(bb != 0) {
      const auto sq = static_cast<int>(std::countr_zero(bb));
      bb &= bb - 1;
      auto att = bitboard{0};
      switch(pt) {
        case piece_type::pawn:   att = movegen::pawn_attacks(color::black, sq); break;
        case piece_type::knight: att = movegen::knight_attacks(sq); break;
        case piece_type::bishop: att = movegen::bishop_attacks(sq, occupied_frame); break;
        case piece_type::rook:   att = movegen::rook_attacks(sq, occupied_frame); break;
        case piece_type::queen:  att = movegen::queen_attacks(sq, occupied_frame); break;
        default: break;
      }
      if((att & ring) != 0) {
        attack += weights[i];
      }
    }
  }
  const auto has_queen = pos.pieces(opposite(c), piece_type::queen) != 0;
  const auto safe = has_queen ? 24 : 12;
  if(attack > safe) {
    const auto danger = (attack - safe) * 20;
    out.mg -= danger > 480 ? 480 : danger;
  }
}

auto mobility(const position& pos, color c, bitboard occupied, term& out) -> void
{
  const auto own = pos.occupancy(c);
  const auto add = [&](piece_type pt, int base_mg, int base_eg, int w_mg, int w_eg) {
    auto bb = pos.pieces(c, pt);
    while(bb != 0) {
      const auto sq = static_cast<int>(std::countr_zero(bb));
      bb &= bb - 1;
      auto att = bitboard{0};
      switch(pt) {
        case piece_type::knight: att = movegen::knight_attacks(sq) & ~own; break;
        case piece_type::bishop: att = movegen::bishop_attacks(sq, occupied) & ~own; break;
        case piece_type::rook:   att = movegen::rook_attacks(sq, occupied) & ~own; break;
        case piece_type::queen:  att = movegen::queen_attacks(sq, occupied) & ~own; break;
        default: break;
      }
      const auto count = static_cast<int>(std::popcount(att));
      out.mg += (count - base_mg) * w_mg;
      out.eg += (count - base_eg) * w_eg;
    }
  };
  add(piece_type::knight, 4, 4, 4, 4);
  add(piece_type::bishop, 6, 6, 4, 3);
  add(piece_type::rook, 7, 7, 2, 2);
  add(piece_type::queen, 13, 13, 2, 2);
}

auto rook_files(const position& pos, color c, int mirror, term& out) -> void
{
  const auto own_pawns = pos.pieces(c, piece_type::pawn);
  const auto enemy_pawns = pos.pieces(opposite(c), piece_type::pawn);
  auto rooks = pos.pieces(c, piece_type::rook);
  while(rooks != 0) {
    const auto sq = static_cast<int>(std::countr_zero(rooks));
    rooks &= rooks - 1;
    const auto f = file_of(sq);
    const auto file = file_masks[f];
    if((own_pawns & file) == 0) {
      if((enemy_pawns & file) == 0) {
        out.mg += 25;
        out.eg += 20;
      } else {
        out.mg += 12;
        out.eg += 10;
      }
    }
    if(rank_of(sq ^ mirror) == 6) {
      out.mg += 20;
      out.eg += 40;
    }
  }
}

auto side_eval(const position& pos, color c) noexcept -> terms
{
  const auto mirror = c == color::white ? 0 : 56;
  auto out = terms{};
  add_material_pst(pos, c, mirror, out);
  pawn_structure(pos, c, mirror, out.pawn_structure);
  mobility(pos, c, pos.occupancy(), out.mobility);
  rook_files(pos, c, mirror, out.rook_bonuses);
  auto occupied = pos.occupancy();
  if(mirror != 0) {
    occupied = flip_vertical(occupied);
  }
  king_safety(pos, c, mirror, occupied, out.king_safety);
  return out;
}

auto blend(term white, term black, int ph) noexcept -> int
{
  const auto mg = white.mg - black.mg;
  const auto eg = white.eg - black.eg;
  return (mg * ph + eg * (phase_max - ph)) / phase_max;
}

auto phase(const position& pos) noexcept -> int
{
  auto sum = 0;
  for(auto c = 0; c < color_count; ++c) {
    for(auto i = 0; i < piece_type_count; ++i) {
      const auto w = phase_weights[i];
      if(w == 0) {
        continue;
      }
      sum += std::popcount(pos.pieces(static_cast<color>(c), static_cast<piece_type>(i))) * w;
    }
  }
  return sum > phase_max ? phase_max : sum;
}

}

auto default_weights() noexcept -> weights
{
  return {};
}

auto set_nnue(std::shared_ptr<const nnue::network> net) noexcept -> void
{
  nnue::set_active(std::move(net));
}

auto nnue_accumulator() noexcept -> nnue::evaluator*
{

  auto net = nnue::active();
  if(!net) {
    return nullptr;
  }
  thread_local std::shared_ptr<const nnue::network> cached_net;
  thread_local std::unique_ptr<nnue::evaluator> evaluator;
  if(cached_net != net) {
    cached_net = net;
    evaluator = std::make_unique<nnue::evaluator>(*net);
  }
  return evaluator.get();
}

auto evaluate(const position& pos) noexcept -> int
{

  if(auto* ev = nnue_accumulator()) {
    const auto score = ev->evaluate(pos);
    return pos.side_to_move() == color::white ? score : -score;
  }
  return evaluate(pos, default_weights());
}

auto evaluate(const position& pos, const weights& w) noexcept -> int
{
  const auto white = side_eval(pos, color::white);
  const auto black = side_eval(pos, color::black);
  const auto ph = phase(pos);
  auto score = 0.0;
  score += w.material * blend(white.material, black.material, ph);
  score += w.pst * blend(white.pst, black.pst, ph);
  score += w.mobility * blend(white.mobility, black.mobility, ph);
  score += w.pawn_structure * blend(white.pawn_structure, black.pawn_structure, ph);
  score += w.king_safety * blend(white.king_safety, black.king_safety, ph);
  score += w.rook_bonuses * blend(white.rook_bonuses, black.rook_bonuses, ph);
  return static_cast<int>(std::lround(score));
}

}