#include "book/polyglot.hpp"

#include "book/polyglot_random.hpp"
#include "movegen/movegen.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <unordered_map>

namespace tuna::book {

namespace {

constexpr auto builtin_book_weight = std::uint16_t{10};

constexpr const char* const builtin_lines[][16] = {
  {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", nullptr},
  {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "c2c3", nullptr},
  {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", "d2d3", nullptr},
  {"e2e4", "e7e5", "g1f3", "b8c6", "d2d4", "e5d4", nullptr},
  {"e2e4", "c7c5", "g1f3", "d7d6", nullptr},
  {"e2e4", "c7c5", "b1c3", "b8c6", nullptr},
  {"e2e4", "e7e6", "d2d4", "d7d5", nullptr},
  {"e2e4", "c7c6", "d2d4", "d7d5", nullptr},
  {"d2d4", "d7d5", "c2c4", "e7e6", "b1c3", nullptr},
  {"d2d4", "d7d5", "c2c4", "c7c6", "b1c3", nullptr},
  {"d2d4", "g8f6", "c2c4", "g7g6", "b1c3", nullptr},
  {"d2d4", "g8f6", "c2c4", "e7e6", "b1c3", nullptr},
  {"g1f3", "d7d5", "d2d4", nullptr},
  {"g1f3", "g8f6", "c2c4", nullptr},
  {"c2c4", "e7e5", "b1c3", nullptr},
  {"d2d4", "f7f5", "g1f3", nullptr},
  {"e2e4", "e7e5", "b1c3", "b8c6", nullptr},
  {"e2e4", "e7e5", "g1f3", "g8f6", "f3e5", nullptr},
  {"d2d4", "d7d5", "g1f3", "g8f6", nullptr},
  {"c2c4", "g8f6", "b1c3", nullptr},
};

auto next_random(std::uint64_t& state) noexcept -> std::uint64_t
{
  state += 0x9E3779B97F4A7C15ULL;
  auto z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

auto read_be64(const std::array<std::uint8_t, 16>& buf, std::size_t offset) noexcept -> std::uint64_t
{
  auto value = std::uint64_t{0};
  for(auto i = std::size_t{0}; i < 8; ++i) {
    value = (value << 8) | buf[offset + i];
  }
  return value;
}

auto read_be16(const std::array<std::uint8_t, 16>& buf, std::size_t offset) noexcept -> std::uint16_t
{
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(buf[offset]) << 8) | buf[offset + 1]);
}

auto parse_uci_move(position& pos, const std::string& s) -> std::optional<tuna::move>
{
  if(s.size() < 4) {
    return std::nullopt;
  }
  const auto from = make_square(s[0] - 'a', s[1] - '1');
  const auto to = make_square(s[2] - 'a', s[3] - '1');
  auto promotion = piece_type::queen;
  if(s.size() == 5) {
    switch(s[4]) {
    case 'q': promotion = piece_type::queen; break;
    case 'r': promotion = piece_type::rook; break;
    case 'b': promotion = piece_type::bishop; break;
    case 'n': promotion = piece_type::knight; break;
    default: return std::nullopt;
    }
  }
  for(const auto mv : movegen::generate_legal(pos)) {
    if(mv.from == static_cast<std::uint8_t>(from)
       && mv.to == static_cast<std::uint8_t>(to)
       && mv.promotion == promotion) {
      return mv;
    }
  }
  return std::nullopt;
}

auto matches(const tuna::move& legal, const tuna::move& raw) noexcept -> bool
{
  if(raw.from != legal.from) {
    return false;
  }
  if(legal.flag == move_flag::castling) {

    const auto rook_sq = legal.to > legal.from ? legal.to + 1 : legal.to - 2;
    return raw.to == legal.to || raw.to == rook_sq;
  }
  if(raw.to != legal.to) {
    return false;
  }
  const auto raw_promotes = raw.flag == move_flag::promotion || raw.flag == move_flag::promotion_capture;
  const auto legal_promotes = legal.flag == move_flag::promotion || legal.flag == move_flag::promotion_capture;
  if(raw_promotes != legal_promotes) {
    return false;
  }
  return !legal_promotes || raw.promotion == legal.promotion;
}

}

auto polyglot_key(const position& pos) noexcept -> std::uint64_t
{
  auto key = std::uint64_t{0};
  for(auto c = 0; c < color_count; ++c) {
    for(auto pt = 0; pt < piece_type_count; ++pt) {
      auto bb = pos.pieces(static_cast<color>(c), static_cast<piece_type>(pt));
      while(bb != 0) {
        const auto sq = std::countr_zero(bb);
        bb &= bb - 1;
        const auto piece_index = 2 * pt + (c == color_index(color::white) ? 1 : 0);
        key ^= random_64[piece_index * square_count + sq];
      }
    }
  }

  const auto rights = pos.castling_rights();
  if((rights & white_king_side) != 0) {
    key ^= random_64[768];
  }
  if((rights & white_queen_side) != 0) {
    key ^= random_64[769];
  }
  if((rights & black_king_side) != 0) {
    key ^= random_64[770];
  }
  if((rights & black_queen_side) != 0) {
    key ^= random_64[771];
  }

  const auto ep = pos.en_passant_square();
  if(ep != no_square) {
    const auto f = file_of(ep);
    const auto us = pos.side_to_move();
    const auto capture_rank = rank_of(ep) + (us == color::black ? 1 : -1);
    for(const auto df : {-1, 1}) {
      const auto nf = f + df;
      if(nf >= 0 && nf < 8
         && (pos.pieces(us, piece_type::pawn) & bit(make_square(nf, capture_rank))) != 0) {
        key ^= random_64[772 + f];
        break;
      }
    }
  }

  if(pos.side_to_move() == color::white) {
    key ^= random_64[780];
  }
  return key;
}

auto decode_move(std::uint16_t raw) noexcept -> tuna::move
{
  const auto to = static_cast<std::uint8_t>(raw & 0x3f);
  const auto from = static_cast<std::uint8_t>((raw >> 6) & 0x3f);
  auto flag = move_flag::quiet;
  auto promotion = piece_type::queen;
  switch((raw >> 12) & 0x07) {
  case 1: flag = move_flag::promotion; promotion = piece_type::knight; break;
  case 2: flag = move_flag::promotion; promotion = piece_type::bishop; break;
  case 3: flag = move_flag::promotion; promotion = piece_type::rook; break;
  case 4: flag = move_flag::promotion; promotion = piece_type::queen; break;
  default: break;
  }
  return tuna::move{from, to, promotion, flag};
}

auto encode_move(tuna::move mv) noexcept -> std::uint16_t
{
  auto raw = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(mv.to) | (static_cast<std::uint16_t>(mv.from) << 6));
  if(mv.flag == move_flag::promotion || mv.flag == move_flag::promotion_capture) {
    auto code = std::uint16_t{4};
    switch(mv.promotion) {
    case piece_type::knight: code = 1; break;
    case piece_type::bishop: code = 2; break;
    case piece_type::rook: code = 3; break;
    default: break;
    }
    raw = static_cast<std::uint16_t>(raw | static_cast<std::uint16_t>(code << 12));
  }
  return raw;
}

auto polyglot_book::load(const std::string& path) -> bool
{
  auto file = std::ifstream{path, std::ios::binary};
  if(!file) {
    entries_.clear();
    loaded_ = false;
    return false;
  }
  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  if(size < 0 || size % 16 != 0) {
    entries_.clear();
    loaded_ = false;
    return false;
  }
  file.seekg(0, std::ios::beg);
  const auto count = static_cast<std::size_t>(size / 16);
  auto new_entries = std::vector<book_entry>{};
  new_entries.reserve(count);
  for(auto i = std::size_t{0}; i < count; ++i) {
    auto buf = std::array<std::uint8_t, 16>{};
    file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    if(!file) {
      entries_.clear();
      loaded_ = false;
      return false;
    }
    new_entries.push_back(book_entry{
        read_be64(buf, 0), read_be16(buf, 8), read_be16(buf, 10)});
  }
  std::sort(new_entries.begin(), new_entries.end(),
      [](const book_entry& lhs, const book_entry& rhs) { return lhs.key < rhs.key; });
  entries_ = std::move(new_entries);
  loaded_ = true;
  return true;
}

auto polyglot_book::load_builtin() -> void
{
  auto continuations = std::unordered_map<std::uint64_t, std::vector<tuna::move>>{};
  for(const auto& line : builtin_lines) {
    auto pos = position::start();
    auto valid = true;
    for(auto i = std::size_t{0}; line[i] != nullptr; ++i) {
      const auto key = polyglot_key(pos);
      const auto mv = parse_uci_move(pos, line[i]);
      if(!mv.has_value()) {
        valid = false;
        break;
      }
      continuations[key].push_back(*mv);
      static_cast<void>(pos.make_move(*mv));
    }
    static_cast<void>(valid);
  }

  auto new_entries = std::vector<book_entry>{};
  new_entries.reserve(64);
  for(const auto& [key, moves] : continuations) {
    for(const auto& mv : moves) {
      new_entries.push_back(book_entry{key, encode_move(mv), builtin_book_weight});
    }
  }
  std::sort(new_entries.begin(), new_entries.end(),
      [](const book_entry& lhs, const book_entry& rhs) {
        return lhs.key < rhs.key
            || (lhs.key == rhs.key && lhs.move < rhs.move);
      });
  new_entries.erase(
      std::unique(new_entries.begin(), new_entries.end(),
          [](const book_entry& lhs, const book_entry& rhs) {
            return lhs.key == rhs.key && lhs.move == rhs.move;
          }),
      new_entries.end());
  entries_ = std::move(new_entries);
  loaded_ = true;
}

auto polyglot_book::loaded() const noexcept -> bool
{
  return loaded_;
}

auto polyglot_book::moves_for(const position& pos) const -> std::vector<book_move>
{
  auto result = std::vector<book_move>{};
  if(!loaded_) {
    return result;
  }
  const auto key = polyglot_key(pos);
  auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
      [](const book_entry& entry, std::uint64_t k) { return entry.key < k; });
  if(it == entries_.end() || it->key != key) {
    return result;
  }
  auto pos_copy = pos;
  const auto legal = movegen::generate_legal(pos_copy);
  for(; it != entries_.end() && it->key == key; ++it) {
    const auto raw = decode_move(it->move);
    for(const auto& lm : legal) {
      if(matches(lm, raw)) {
        result.push_back(book_move{lm, it->weight});
        break;
      }
    }
  }
  return result;
}

auto polyglot_book::pick(const position& pos, std::uint64_t& rng_state) const -> std::optional<tuna::move>
{
  const auto moves = moves_for(pos);
  if(moves.empty()) {
    return std::nullopt;
  }
  auto total = std::uint64_t{0};
  for(const auto& bm : moves) {
    total += bm.weight;
  }
  if(total == 0) {
    return std::nullopt;
  }
  auto roll = next_random(rng_state) % total;
  for(const auto& bm : moves) {
    if(roll < bm.weight) {
      return bm.mv;
    }
    roll -= bm.weight;
  }
  return moves.back().mv;
}

}