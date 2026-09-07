#include "core/position.hpp"

#include "core/zobrist.hpp"

#include <bit>
#include <cctype>
#include <sstream>
#include <string>

namespace tuna {

namespace {

constexpr auto white_pawns = bitboard{0x000000000000ff00};
constexpr auto white_rooks = bitboard{0x0000000000000081};
constexpr auto white_knights = bitboard{0x0000000000000042};
constexpr auto white_bishops = bitboard{0x0000000000000024};
constexpr auto white_queens = bitboard{0x0000000000000008};
constexpr auto white_kings = bitboard{0x0000000000000010};
constexpr auto black_pawns = bitboard{0x00ff000000000000};
constexpr auto black_rooks = bitboard{0x8100000000000000};
constexpr auto black_knights = bitboard{0x4200000000000000};
constexpr auto black_bishops = bitboard{0x2400000000000000};
constexpr auto black_queens = bitboard{0x0800000000000000};
constexpr auto black_kings = bitboard{0x1000000000000000};

auto piece_char(piece p) noexcept -> char
{
  switch(p) {
  case piece::wp: return 'P';
  case piece::wn: return 'N';
  case piece::wb: return 'B';
  case piece::wr: return 'R';
  case piece::wq: return 'Q';
  case piece::wk: return 'K';
  case piece::bp: return 'p';
  case piece::bn: return 'n';
  case piece::bb: return 'b';
  case piece::br: return 'r';
  case piece::bq: return 'q';
  case piece::bk: return 'k';
  case piece::none: return '.';
  }
  return '.';
}

auto make_piece(color c, piece_type pt) noexcept -> piece
{
  const auto offset = c == color::white ? 1 : 7;
  return static_cast<piece>(offset + piece_type_index(pt));
}

}

auto position::empty() noexcept -> position
{
  auto pos = position{};
  pos.key_ = pos.recompute_key();
  return pos;
}

auto position::start() noexcept -> position
{
  auto pos = position{};
  pos.pieces_[color_index(color::white)][piece_type_index(piece_type::pawn)] = white_pawns;
  pos.pieces_[color_index(color::white)][piece_type_index(piece_type::knight)] = white_knights;
  pos.pieces_[color_index(color::white)][piece_type_index(piece_type::bishop)] = white_bishops;
  pos.pieces_[color_index(color::white)][piece_type_index(piece_type::rook)] = white_rooks;
  pos.pieces_[color_index(color::white)][piece_type_index(piece_type::queen)] = white_queens;
  pos.pieces_[color_index(color::white)][piece_type_index(piece_type::king)] = white_kings;
  pos.pieces_[color_index(color::black)][piece_type_index(piece_type::pawn)] = black_pawns;
  pos.pieces_[color_index(color::black)][piece_type_index(piece_type::knight)] = black_knights;
  pos.pieces_[color_index(color::black)][piece_type_index(piece_type::bishop)] = black_bishops;
  pos.pieces_[color_index(color::black)][piece_type_index(piece_type::rook)] = black_rooks;
  pos.pieces_[color_index(color::black)][piece_type_index(piece_type::queen)] = black_queens;
  pos.pieces_[color_index(color::black)][piece_type_index(piece_type::king)] = black_kings;
  pos.occupancies_[color_index(color::white)] = white_pawns | white_knights | white_bishops | white_rooks | white_queens | white_kings;
  pos.occupancies_[color_index(color::black)] = black_pawns | black_knights | black_bishops | black_rooks | black_queens | black_kings;
  pos.occupancies_[2] = pos.occupancies_[0] | pos.occupancies_[1];
  pos.side_to_move_ = color::white;
  pos.castling_rights_ = all_castling;
  pos.en_passant_square_ = no_square;
  pos.halfmove_clock_ = 0;
  pos.fullmove_number_ = 1;
  pos.key_ = pos.recompute_key();
  return pos;
}

auto position::from_fen(const std::string& fen) -> position
{
  auto pos = position::empty();
  std::istringstream ss(fen);
  auto placement = std::string{};
  auto side = std::string{};
  auto castling = std::string{};
  auto ep = std::string{};
  auto halfmove = std::string{};
  auto fullmove = std::string{};
  ss >> placement >> side >> castling >> ep >> halfmove >> fullmove;

  auto rank = 7;
  auto file = 0;
  for(const auto ch : placement) {
    if(ch == '/') {
      --rank;
      file = 0;
    } else if(ch >= '1' && ch <= '8') {
      file += ch - '0';
    } else {
      const auto c = std::isupper(static_cast<unsigned char>(ch)) != 0 ? color::white : color::black;
      auto pt = piece_type::king;
      switch(std::tolower(static_cast<unsigned char>(ch))) {
      case 'p': pt = piece_type::pawn; break;
      case 'n': pt = piece_type::knight; break;
      case 'b': pt = piece_type::bishop; break;
      case 'r': pt = piece_type::rook; break;
      case 'q': pt = piece_type::queen; break;
      default: break;
      }
      pos.set_piece(c, pt, make_square(file, rank));
      ++file;
    }
  }

  if(side == "b") {
    pos.set_side_to_move(color::black);
  }

  auto rights = std::uint8_t{0};
  if(castling != "-") {
    for(const auto ch : castling) {
      switch(ch) {
      case 'K': rights |= white_king_side; break;
      case 'Q': rights |= white_queen_side; break;
      case 'k': rights |= black_king_side; break;
      case 'q': rights |= black_queen_side; break;
      default: break;
      }
    }
  }
  pos.set_castling_rights(rights);

  if(ep != "-" && ep.size() >= 2) {
    pos.set_en_passant_square(make_square(ep[0] - 'a', ep[1] - '1'));
  }

  if(!halfmove.empty()) {
    pos.set_halfmove_clock(std::stoi(halfmove));
  }
  if(!fullmove.empty()) {
    pos.set_fullmove_number(std::stoi(fullmove));
  }
  return pos;
}

auto position::fen() const -> std::string
{
  auto out = std::string{};
  out.reserve(90);
  for(auto rank = 7; rank >= 0; --rank) {
    auto empty = 0;
    for(auto file = 0; file < 8; ++file) {
      const auto p = piece_on(make_square(file, rank));
      if(p == piece::none) {
        ++empty;
      } else {
        if(empty != 0) {
          out.push_back(static_cast<char>('0' + empty));
          empty = 0;
        }
        out.push_back(piece_char(p));
      }
    }
    if(empty != 0) {
      out.push_back(static_cast<char>('0' + empty));
    }
    if(rank != 0) {
      out.push_back('/');
    }
  }

  out.push_back(' ');
  out.push_back(side_to_move_ == color::white ? 'w' : 'b');

  out.push_back(' ');
  if(castling_rights_ == 0) {
    out.push_back('-');
  } else {
    if((castling_rights_ & white_king_side) != 0) { out.push_back('K'); }
    if((castling_rights_ & white_queen_side) != 0) { out.push_back('Q'); }
    if((castling_rights_ & black_king_side) != 0) { out.push_back('k'); }
    if((castling_rights_ & black_queen_side) != 0) { out.push_back('q'); }
  }

  out.push_back(' ');
  if(en_passant_square_ == no_square) {
    out.push_back('-');
  } else {
    out.push_back(static_cast<char>('a' + file_of(en_passant_square_)));
    out.push_back(static_cast<char>('1' + rank_of(en_passant_square_)));
  }

  out.push_back(' ');
  out += std::to_string(halfmove_clock_);

  out.push_back(' ');
  out += std::to_string(fullmove_number_);

  return out;
}

auto position::pieces(color c, piece_type pt) const noexcept -> bitboard
{
  return pieces_[color_index(c)][piece_type_index(pt)];
}

auto position::occupancy(color c) const noexcept -> bitboard
{
  return occupancies_[color_index(c)];
}

auto position::occupancy() const noexcept -> bitboard
{
  return occupancies_[2];
}

auto position::side_to_move() const noexcept -> color
{
  return side_to_move_;
}

auto position::castling_rights() const noexcept -> std::uint8_t
{
  return castling_rights_;
}

auto position::en_passant_square() const noexcept -> int
{
  return en_passant_square_;
}

auto position::halfmove_clock() const noexcept -> int
{
  return halfmove_clock_;
}

auto position::fullmove_number() const noexcept -> int
{
  return fullmove_number_;
}

auto position::key() const noexcept -> std::uint64_t
{
  return key_;
}

auto position::recompute_key() const noexcept -> std::uint64_t
{
  auto result = std::uint64_t{0};
  for(auto c = 0; c < color_count; ++c) {
    for(auto pt = 0; pt < piece_type_count; ++pt) {
      auto bb = pieces_[c][pt];
      while(bb != 0) {
        const auto sq = std::countr_zero(bb);
        result ^= zobrist::piece_square(static_cast<color>(c), static_cast<piece_type>(pt), static_cast<int>(sq));
        bb &= bb - 1;
      }
    }
  }
  result ^= zobrist::castling(castling_rights_);
  if(en_passant_square_ != no_square) {
    result ^= zobrist::en_passant(file_of(en_passant_square_));
  }
  if(side_to_move_ == color::black) {
    result ^= zobrist::side();
  }
  return result;
}

auto position::piece_on(int sq) const noexcept -> piece
{
  const auto bb = bit(sq);
  for(auto c = 0; c < color_count; ++c) {
    for(auto pt = 0; pt < piece_type_count; ++pt) {
      if((pieces_[c][pt] & bb) != 0) {
        return make_piece(static_cast<color>(c), static_cast<piece_type>(pt));
      }
    }
  }
  return piece::none;
}

auto position::board_string() const -> std::string
{
  auto out = std::string{};
  out.reserve(71);
  for(auto rank = 7; rank >= 0; --rank) {
    for(auto file = 0; file < 8; ++file) {
      out.push_back(piece_char(piece_on(make_square(file, rank))));
    }
    if(rank != 0) {
      out.push_back('\n');
    }
  }
  return out;
}

auto position::clear() noexcept -> void
{
  pieces_ = {};
  occupancies_ = {};
  side_to_move_ = color::white;
  castling_rights_ = 0;
  en_passant_square_ = no_square;
  halfmove_clock_ = 0;
  fullmove_number_ = 1;
  key_ = recompute_key();
}

auto position::set_piece(color c, piece_type pt, int sq) noexcept -> void
{
  remove_piece(sq);
  const auto bb = bit(sq);
  pieces_[color_index(c)][piece_type_index(pt)] |= bb;
  add_to_occupancy(c, bb);
  toggle_piece_key(c, pt, sq);
}

auto position::remove_piece(int sq) noexcept -> void
{
  const auto bb = bit(sq);
  for(auto c = 0; c < color_count; ++c) {
    for(auto pt = 0; pt < piece_type_count; ++pt) {
      if((pieces_[c][pt] & bb) != 0) {
        pieces_[c][pt] &= ~bb;
        remove_from_occupancy(static_cast<color>(c), bb);
        toggle_piece_key(static_cast<color>(c), static_cast<piece_type>(pt), sq);
        return;
      }
    }
  }
}

auto position::set_side_to_move(color c) noexcept -> void
{
  if(side_to_move_ != c) {
    toggle_side_key();
  }
  side_to_move_ = c;
}

auto position::set_castling_rights(std::uint8_t rights) noexcept -> void
{
  toggle_castling_key();
  castling_rights_ = rights & all_castling;
  toggle_castling_key();
}

auto position::set_en_passant_square(int sq) noexcept -> void
{
  toggle_en_passant_key();
  en_passant_square_ = sq;
  toggle_en_passant_key();
}

auto position::set_halfmove_clock(int value) noexcept -> void
{
  halfmove_clock_ = value;
}

auto position::set_fullmove_number(int value) noexcept -> void
{
  fullmove_number_ = value;
}

auto position::make_move(move mv) noexcept -> move_state
{
  auto st = move_state{};
  st.castling_rights = castling_rights_;
  st.en_passant_square = en_passant_square_;
  st.halfmove_clock = halfmove_clock_;
  st.fullmove_number = fullmove_number_;
  st.key = key_;

  const auto us = side_to_move_;
  const auto them = opposite(us);
  const auto from = static_cast<int>(mv.from);
  const auto to = static_cast<int>(mv.to);
  const auto pt = piece_type_of(piece_on(from));
  st.captured = piece_on(to);

  switch(mv.flag) {
  case move_flag::quiet:
  case move_flag::double_push:
    move_piece(us, pt, from, to);
    break;
  case move_flag::capture:
    remove_piece(to);
    move_piece(us, pt, from, to);
    break;
  case move_flag::en_passant:
    remove_piece(to + (us == color::white ? -8 : 8));
    move_piece(us, piece_type::pawn, from, to);
    break;
  case move_flag::promotion:
    remove_piece(from);
    set_piece(us, mv.promotion, to);
    break;
  case move_flag::promotion_capture:
    remove_piece(to);
    remove_piece(from);
    set_piece(us, mv.promotion, to);
    break;
  case move_flag::castling:
    move_piece(us, piece_type::king, from, to);
    if(to > from) {
      move_piece(us, piece_type::rook, to + 1, to - 1);
    } else {
      move_piece(us, piece_type::rook, to - 2, to + 1);
    }
    break;
  }

  if(mv.flag == move_flag::double_push) {
    set_en_passant_square(from + (us == color::white ? 8 : -8));
  } else {
    set_en_passant_square(no_square);
  }

  if(pt == piece_type::pawn
     || mv.flag == move_flag::capture
     || mv.flag == move_flag::en_passant
     || mv.flag == move_flag::promotion_capture) {
    halfmove_clock_ = 0;
  } else {
    ++halfmove_clock_;
  }

  if(us == color::black) {
    ++fullmove_number_;
  }

  auto rights = castling_rights_;
  if(pt == piece_type::king) {
    rights &= us == color::white
      ? static_cast<std::uint8_t>(~(white_king_side | white_queen_side))
      : static_cast<std::uint8_t>(~(black_king_side | black_queen_side));
  }
  if(from == make_square(0, 0)) {
    rights &= ~white_queen_side;
  } else if(from == make_square(7, 0)) {
    rights &= ~white_king_side;
  } else if(from == make_square(0, 7)) {
    rights &= ~black_queen_side;
  } else if(from == make_square(7, 7)) {
    rights &= ~black_king_side;
  }
  if(st.captured == piece::wr) {
    if(to == make_square(0, 0)) {
      rights &= ~white_queen_side;
    } else if(to == make_square(7, 0)) {
      rights &= ~white_king_side;
    }
  } else if(st.captured == piece::br) {
    if(to == make_square(0, 7)) {
      rights &= ~black_queen_side;
    } else if(to == make_square(7, 7)) {
      rights &= ~black_king_side;
    }
  }
  set_castling_rights(rights);

  set_side_to_move(them);
  return st;
}

auto position::unmake_move(move mv, const move_state& st) noexcept -> void
{
  const auto us = opposite(side_to_move_);
  const auto from = static_cast<int>(mv.from);
  const auto to = static_cast<int>(mv.to);

  switch(mv.flag) {
  case move_flag::quiet:
  case move_flag::double_push:
    move_piece(us, piece_type_of(piece_on(to)), to, from);
    break;
  case move_flag::capture:
    move_piece(us, piece_type_of(piece_on(to)), to, from);
    set_piece(opposite(us), piece_type_of(st.captured), to);
    break;
  case move_flag::en_passant:
    remove_piece(to);
    set_piece(us, piece_type::pawn, from);
    set_piece(opposite(us), piece_type::pawn, to + (us == color::white ? -8 : 8));
    break;
  case move_flag::promotion:
    remove_piece(to);
    set_piece(us, piece_type::pawn, from);
    break;
  case move_flag::promotion_capture:
    remove_piece(to);
    set_piece(us, piece_type::pawn, from);
    set_piece(opposite(us), piece_type_of(st.captured), to);
    break;
  case move_flag::castling:
    move_piece(us, piece_type::king, to, from);
    if(to > from) {
      move_piece(us, piece_type::rook, to - 1, to + 1);
    } else {
      move_piece(us, piece_type::rook, to + 1, to - 2);
    }
    break;
  }

  set_castling_rights(st.castling_rights);
  set_en_passant_square(st.en_passant_square);
  halfmove_clock_ = st.halfmove_clock;
  fullmove_number_ = st.fullmove_number;
  set_side_to_move(opposite(side_to_move_));
  key_ = st.key;
}

auto position::make_null_move() noexcept -> move_state
{
  auto st = move_state{};
  st.castling_rights = castling_rights_;
  st.en_passant_square = en_passant_square_;
  st.halfmove_clock = halfmove_clock_;
  st.fullmove_number = fullmove_number_;
  st.key = key_;
  set_en_passant_square(no_square);
  set_side_to_move(opposite(side_to_move_));
  return st;
}

auto position::unmake_null_move(const move_state& st) noexcept -> void
{
  set_side_to_move(opposite(side_to_move_));
  set_en_passant_square(st.en_passant_square);
  halfmove_clock_ = st.halfmove_clock;
  fullmove_number_ = st.fullmove_number;
  key_ = st.key;
}

auto position::add_to_occupancy(color c, bitboard bb) noexcept -> void
{
  occupancies_[color_index(c)] |= bb;
  occupancies_[2] |= bb;
}

auto position::remove_from_occupancy(color c, bitboard bb) noexcept -> void
{
  occupancies_[color_index(c)] &= ~bb;
  occupancies_[2] &= ~bb;
}

auto position::move_piece(color c, piece_type pt, int from, int to) noexcept -> void
{
  const auto from_bb = bit(from);
  const auto to_bb = bit(to);
  auto& bb = pieces_[color_index(c)][piece_type_index(pt)];
  bb ^= from_bb | to_bb;
  occupancies_[color_index(c)] ^= from_bb | to_bb;
  occupancies_[2] ^= from_bb | to_bb;
  key_ ^= zobrist::piece_square(c, pt, from);
  key_ ^= zobrist::piece_square(c, pt, to);
}

auto position::toggle_piece_key(color c, piece_type pt, int sq) noexcept -> void
{
  key_ ^= zobrist::piece_square(c, pt, sq);
}

auto position::toggle_castling_key() noexcept -> void
{
  key_ ^= zobrist::castling(castling_rights_);
}

auto position::toggle_en_passant_key() noexcept -> void
{
  if(en_passant_square_ != no_square) {
    key_ ^= zobrist::en_passant(file_of(en_passant_square_));
  }
}

auto position::toggle_side_key() noexcept -> void
{
  key_ ^= zobrist::side();
}

}