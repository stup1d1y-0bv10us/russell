#include "movegen/movegen.hpp"

#include "movegen/attacks.hpp"

#include <bit>

namespace tuna::movegen {

namespace {

auto push_promotions(move_list& moves, int from, int to, move_flag flag) noexcept -> void
{
  moves.push(move{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to), piece_type::queen, flag});
  moves.push(move{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to), piece_type::rook, flag});
  moves.push(move{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to), piece_type::bishop, flag});
  moves.push(move{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to), piece_type::knight, flag});
}

auto push_move(move_list& moves, int from, int to, move_flag flag = move_flag::quiet) noexcept -> void
{
  moves.push(move{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to), piece_type::queen, flag});
}

auto generate_pawns(const position& pos, move_list& moves, color us) noexcept -> void
{
  const auto them = opposite(us);
  auto pawns = pos.pieces(us, piece_type::pawn);
  const auto empty = ~pos.occupancy();
  const auto their_pieces = pos.occupancy(them);
  while(pawns != 0) {
    const auto from = static_cast<int>(std::countr_zero(pawns));
    pawns &= pawns - 1;
    const auto rank = rank_of(from);
    const auto direction = us == color::white ? 8 : -8;
    const auto start_rank = us == color::white ? 1 : 6;
    const auto promotion_rank = us == color::white ? 6 : 1;
    const auto one = from + direction;
    if(one >= 0 && one < square_count && (empty & bit(one)) != 0) {
      if(rank == promotion_rank) {
        push_promotions(moves, from, one, move_flag::promotion);
      } else {
        push_move(moves, from, one);
        const auto two = from + direction + direction;
        if(rank == start_rank && (empty & bit(two)) != 0) {
          push_move(moves, from, two, move_flag::double_push);
        }
      }
    }
    auto captures = pawn_attacks(us, from) & their_pieces;
    while(captures != 0) {
      const auto to = static_cast<int>(std::countr_zero(captures));
      captures &= captures - 1;
      if(rank == promotion_rank) {
        push_promotions(moves, from, to, move_flag::promotion_capture);
      } else {
        push_move(moves, from, to, move_flag::capture);
      }
    }
    const auto ep = pos.en_passant_square();
    if(ep != no_square && (pawn_attacks(us, from) & bit(ep)) != 0) {
      push_move(moves, from, ep, move_flag::en_passant);
    }
  }
}

auto generate_knights(const position& pos, move_list& moves, color us) noexcept -> void
{
  auto knights = pos.pieces(us, piece_type::knight);
  const auto own = pos.occupancy(us);
  const auto enemy = pos.occupancy(opposite(us));
  while(knights != 0) {
    const auto from = static_cast<int>(std::countr_zero(knights));
    knights &= knights - 1;
    auto targets = knight_attacks(from) & ~own;
    while(targets != 0) {
      const auto to = static_cast<int>(std::countr_zero(targets));
      targets &= targets - 1;
      push_move(moves, from, to, (enemy & bit(to)) != 0 ? move_flag::capture : move_flag::quiet);
    }
  }
}

auto generate_castling(const position& pos, move_list& moves, color us) noexcept -> void
{
  const auto rights = pos.castling_rights();
  const auto occ = pos.occupancy();
  const auto king_rank = us == color::white ? 0 : 7;
  const auto king_sq = make_square(4, king_rank);
  if((pos.pieces(us, piece_type::king) & bit(king_sq)) == 0) {
    return;
  }
  const auto king_side_right = us == color::white ? white_king_side : black_king_side;
  const auto queen_side_right = us == color::white ? white_queen_side : black_queen_side;
  if((rights & king_side_right) != 0) {
    const auto rook_sq = make_square(7, king_rank);
    const auto f_sq = make_square(5, king_rank);
    const auto g_sq = make_square(6, king_rank);
    if((pos.pieces(us, piece_type::rook) & bit(rook_sq)) != 0 && (occ & (bit(f_sq) | bit(g_sq))) == 0) {
      push_move(moves, king_sq, g_sq, move_flag::castling);
    }
  }
  if((rights & queen_side_right) != 0) {
    const auto rook_sq = make_square(0, king_rank);
    const auto b_sq = make_square(1, king_rank);
    const auto c_sq = make_square(2, king_rank);
    const auto d_sq = make_square(3, king_rank);
    if((pos.pieces(us, piece_type::rook) & bit(rook_sq)) != 0
       && (occ & (bit(b_sq) | bit(c_sq) | bit(d_sq))) == 0) {
      push_move(moves, king_sq, c_sq, move_flag::castling);
    }
  }
}

auto generate_kings(const position& pos, move_list& moves, color us) noexcept -> void
{
  auto kings = pos.pieces(us, piece_type::king);
  const auto own = pos.occupancy(us);
  const auto enemy = pos.occupancy(opposite(us));
  while(kings != 0) {
    const auto from = static_cast<int>(std::countr_zero(kings));
    kings &= kings - 1;
    auto targets = king_attacks(from) & ~own;
    while(targets != 0) {
      const auto to = static_cast<int>(std::countr_zero(targets));
      targets &= targets - 1;
      push_move(moves, from, to, (enemy & bit(to)) != 0 ? move_flag::capture : move_flag::quiet);
    }
  }
  generate_castling(pos, moves, us);
}

auto generate_slider_moves(move_list& moves, int from, bitboard targets, bitboard enemy) noexcept -> void
{
  while(targets != 0) {
    const auto to = static_cast<int>(std::countr_zero(targets));
    targets &= targets - 1;
    push_move(moves, from, to, (enemy & bit(to)) != 0 ? move_flag::capture : move_flag::quiet);
  }
}

auto generate_sliders(const position& pos, move_list& moves, color us) noexcept -> void
{
  const auto own = pos.occupancy(us);
  const auto enemy = pos.occupancy(opposite(us));
  const auto occupancy = pos.occupancy();
  auto bishops = pos.pieces(us, piece_type::bishop);
  while(bishops != 0) {
    const auto from = static_cast<int>(std::countr_zero(bishops));
    bishops &= bishops - 1;
    generate_slider_moves(moves, from, bishop_attacks(from, occupancy) & ~own, enemy);
  }
  auto rooks = pos.pieces(us, piece_type::rook);
  while(rooks != 0) {
    const auto from = static_cast<int>(std::countr_zero(rooks));
    rooks &= rooks - 1;
    generate_slider_moves(moves, from, rook_attacks(from, occupancy) & ~own, enemy);
  }
  auto queens = pos.pieces(us, piece_type::queen);
  while(queens != 0) {
    const auto from = static_cast<int>(std::countr_zero(queens));
    queens &= queens - 1;
    generate_slider_moves(moves, from, queen_attacks(from, occupancy) & ~own, enemy);
  }
}

auto castling_legal(const position& pos, move mv, color us) noexcept -> bool
{
  const auto by = opposite(us);
  const auto from = static_cast<int>(mv.from);
  const auto to = static_cast<int>(mv.to);
  if(is_square_attacked(pos, from, by)) {
    return false;
  }
  const auto between = to > from ? from + 1 : from - 1;
  if(is_square_attacked(pos, between, by)) {
    return false;
  }
  if(is_square_attacked(pos, to, by)) {
    return false;
  }
  return true;
}

auto between(int a, int b) noexcept -> bitboard
{
  const auto a_bb = bit(a);
  const auto b_bb = bit(b);
  return (rook_attacks(a, b_bb) & rook_attacks(b, a_bb))
         | (bishop_attacks(a, b_bb) & bishop_attacks(b, a_bb));
}

auto attackers_to(const position& pos, int sq, color by) noexcept -> bitboard
{
  const auto occupancy = pos.occupancy();
  auto attackers = (pawn_attacks(opposite(by), sq) & pos.pieces(by, piece_type::pawn))
                   | (knight_attacks(sq) & pos.pieces(by, piece_type::knight))
                   | (king_attacks(sq) & pos.pieces(by, piece_type::king));
  attackers |= bishop_attacks(sq, occupancy)
               & (pos.pieces(by, piece_type::bishop) | pos.pieces(by, piece_type::queen));
  attackers |= rook_attacks(sq, occupancy)
               & (pos.pieces(by, piece_type::rook) | pos.pieces(by, piece_type::queen));
  return attackers;
}

auto generate_captures_pseudo_legal(const position& pos, move_list& moves) noexcept -> void
{
  const auto us = pos.side_to_move();
  const auto them = opposite(us);
  const auto enemy = pos.occupancy(them);
  const auto occupancy = pos.occupancy();

  auto pawns = pos.pieces(us, piece_type::pawn);
  while(pawns != 0) {
    const auto from = static_cast<int>(std::countr_zero(pawns));
    pawns &= pawns - 1;
    const auto rank = rank_of(from);
    const auto direction = us == color::white ? 8 : -8;
    const auto promotion_rank = us == color::white ? 6 : 1;
    const auto one = from + direction;
    if(one >= 0 && one < square_count && (occupancy & bit(one)) == 0 && rank == promotion_rank) {
      push_promotions(moves, from, one, move_flag::promotion);
    }
    auto captures = pawn_attacks(us, from) & enemy;
    while(captures != 0) {
      const auto to = static_cast<int>(std::countr_zero(captures));
      captures &= captures - 1;
      if(rank == promotion_rank) {
        push_promotions(moves, from, to, move_flag::promotion_capture);
      } else {
        push_move(moves, from, to, move_flag::capture);
      }
    }
    const auto ep = pos.en_passant_square();
    if(ep != no_square && (pawn_attacks(us, from) & bit(ep)) != 0) {
      push_move(moves, from, ep, move_flag::en_passant);
    }
  }

  auto knights = pos.pieces(us, piece_type::knight);
  while(knights != 0) {
    const auto from = static_cast<int>(std::countr_zero(knights));
    knights &= knights - 1;
    auto targets = knight_attacks(from) & enemy;
    while(targets != 0) {
      const auto to = static_cast<int>(std::countr_zero(targets));
      targets &= targets - 1;
      push_move(moves, from, to, move_flag::capture);
    }
  }

  const auto kings = pos.pieces(us, piece_type::king);
  if(kings != 0) {
    const auto from = static_cast<int>(std::countr_zero(kings));
    auto targets = king_attacks(from) & enemy;
    while(targets != 0) {
      const auto to = static_cast<int>(std::countr_zero(targets));
      targets &= targets - 1;
      push_move(moves, from, to, move_flag::capture);
    }
  }

  auto bishops = pos.pieces(us, piece_type::bishop);
  while(bishops != 0) {
    const auto from = static_cast<int>(std::countr_zero(bishops));
    bishops &= bishops - 1;
    generate_slider_moves(moves, from, bishop_attacks(from, occupancy) & enemy, enemy);
  }
  auto rooks = pos.pieces(us, piece_type::rook);
  while(rooks != 0) {
    const auto from = static_cast<int>(std::countr_zero(rooks));
    rooks &= rooks - 1;
    generate_slider_moves(moves, from, rook_attacks(from, occupancy) & enemy, enemy);
  }
  auto queens = pos.pieces(us, piece_type::queen);
  while(queens != 0) {
    const auto from = static_cast<int>(std::countr_zero(queens));
    queens &= queens - 1;
    generate_slider_moves(moves, from, queen_attacks(from, occupancy) & enemy, enemy);
  }
}

}

auto generate_non_sliding_pseudo_legal(const position& pos) noexcept -> move_list
{
  auto moves = move_list{};
  const auto us = pos.side_to_move();
  generate_pawns(pos, moves, us);
  generate_knights(pos, moves, us);
  generate_kings(pos, moves, us);
  return moves;
}

auto generate_pseudo_legal(const position& pos) noexcept -> move_list
{
  auto moves = generate_non_sliding_pseudo_legal(pos);
  generate_sliders(pos, moves, pos.side_to_move());
  return moves;
}

auto is_square_attacked(const position& pos, int sq, color by) noexcept -> bool
{
  const auto occupancy = pos.occupancy();
  if((pawn_attacks(opposite(by), sq) & pos.pieces(by, piece_type::pawn)) != 0) {
    return true;
  }
  if((knight_attacks(sq) & pos.pieces(by, piece_type::knight)) != 0) {
    return true;
  }
  if((king_attacks(sq) & pos.pieces(by, piece_type::king)) != 0) {
    return true;
  }
  if((bishop_attacks(sq, occupancy) & (pos.pieces(by, piece_type::bishop) | pos.pieces(by, piece_type::queen))) != 0) {
    return true;
  }
  if((rook_attacks(sq, occupancy) & (pos.pieces(by, piece_type::rook) | pos.pieces(by, piece_type::queen))) != 0) {
    return true;
  }
  return false;
}

auto generate_legal(position& pos) noexcept -> move_list
{
  auto legal = move_list{};
  const auto us = pos.side_to_move();
  const auto pseudo = generate_pseudo_legal(pos);
  for(const auto mv : pseudo) {
    if(mv.flag == move_flag::castling) {
      if(castling_legal(pos, mv, us)) {
        legal.push(mv);
      }
      continue;
    }
    const auto st = pos.make_move(mv);
    const auto king_sq = static_cast<int>(std::countr_zero(pos.pieces(us, piece_type::king)));
    const auto ok = !is_square_attacked(pos, king_sq, opposite(us));
    pos.unmake_move(mv, st);
    if(ok) {
      legal.push(mv);
    }
  }
  return legal;
}

auto generate_captures_promotions(position& pos) noexcept -> move_list
{
  auto pseudo = move_list{};
  generate_captures_pseudo_legal(pos, pseudo);
  auto legal = move_list{};
  const auto us = pos.side_to_move();
  for(const auto mv : pseudo) {
    const auto st = pos.make_move(mv);
    const auto king_sq = static_cast<int>(std::countr_zero(pos.pieces(us, piece_type::king)));
    const auto ok = !is_square_attacked(pos, king_sq, opposite(us));
    pos.unmake_move(mv, st);
    if(ok) {
      legal.push(mv);
    }
  }
  return legal;
}

auto generate_evasions(position& pos) noexcept -> move_list
{
  auto moves = move_list{};
  const auto us = pos.side_to_move();
  const auto them = opposite(us);
  const auto king_sq = static_cast<int>(std::countr_zero(pos.pieces(us, piece_type::king)));
  const auto checkers = attackers_to(pos, king_sq, them);

  auto allow_targets = bitboard{0};
  if(std::popcount(checkers) > 1) {
    allow_targets = 0;
  } else {
    const auto checker_sq = static_cast<int>(std::countr_zero(checkers));
    allow_targets = bit(checker_sq);
    const auto checker_type = piece_type_of(pos.piece_on(checker_sq));
    if(checker_type == piece_type::bishop || checker_type == piece_type::rook
       || checker_type == piece_type::queen) {
      allow_targets |= between(king_sq, checker_sq);
    }
  }

  const auto pseudo = generate_pseudo_legal(pos);
  for(const auto mv : pseudo) {
    if(mv.flag == move_flag::castling) {
      continue;
    }
    if(mv.from != king_sq && (allow_targets & bit(mv.to)) == 0
       && mv.flag != move_flag::en_passant) {
      continue;
    }
    const auto st = pos.make_move(mv);
    const auto current_king = static_cast<int>(std::countr_zero(pos.pieces(us, piece_type::king)));
    const auto ok = !is_square_attacked(pos, current_king, them);
    pos.unmake_move(mv, st);
    if(ok) {
      moves.push(mv);
    }
  }
  return moves;
}

}