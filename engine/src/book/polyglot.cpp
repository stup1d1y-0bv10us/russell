
#include "book/polyglot.hpp"

#include "book/polyglot_random.hpp"
#include "movegen/movegen.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace russell::book {

namespace {

struct builtin_line {
  std::uint16_t weight;
  const char* moves[20];
};

constexpr builtin_line builtin_lines[] = {
    {20, {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "g8f6", "e1g1", "f6e4", "d2d4", "e4d6", "b5c6", "d7c6", "d4e5", "d6f5", "d1d8", "e8d8", nullptr}},
    {20, {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5a4", "g8f6", "e1g1", "f8e7", "f1e1", "b7b5", "a4b3", "d7d6", "c2c3", "e8g8", nullptr}},
    {18, {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "c2c3", "g8f6", "d2d3", "d7d6", "e1g1", "e8g8", "h2h3", "a7a6", nullptr}},
    {15, {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", "f3g5", "d7d5", "e4d5", "c6a5", "c4b5", "c7c6", "d5c6", "b7c6", nullptr}},
    {17, {"e2e4", "e7e5", "g1f3", "b8c6", "d2d4", "e5d4", "f3d4", "g8f6", "b1c3", "f8b4", "d4c6", "b7c6", "f1d3", "d7d5", nullptr}},
    {14, {"e2e4", "e7e5", "b1c3", "g8f6", "g2g3", "d7d5", "e4d5", "f6d5", "f1g2", "c8e6", "g1e2", nullptr}},
    {19, {"d2d4", "d7d5", "c2c4", "e7e6", "b1c3", "g8f6", "c1g5", "f8e7", "e2e3", "e8g8", "g1f3", "h7h6", nullptr}},
    {15, {"d2d4", "d7d5", "c2c4", "d5c4", "e2e3", "g8f6", "f1c4", "e7e6", "g1f3", "c7c5", "e1g1", "a7a6", nullptr}},
    {18, {"d2d4", "g8f6", "c2c4", "e7e6", "g2g3", "d7d5", "f1g2", "f8e7", "g1f3", "e8g8", "e1g1", nullptr}},
    {16, {"c2c4", "e7e5", "b1c3", "g8f6", "g2g3", "d7d5", "c4d5", "f6d5", "f1g2", "d5b6", "g1f3", "b8c6", "e1g1", "f8e7", nullptr}},
    {14, {"c2c4", "c7c5", "b1c3", "b8c6", "g2g3", "g7g6", "f1g2", "f8g7", "g1f3", "g8f6", "e1g1", "e8g8", nullptr}},
    {13, {"g1f3", "d7d5", "g2g3", "g8f6", "f1g2", "e7e6", "e1g1", "f8e7", "d2d3", "e8g8", "c2c4", "c7c5", nullptr}},

    {18, {"e2e4", "c7c5", "g1f3", "d7d6", "d2d4", "c5d4", "f3d4", "g8f6", "b1c3", "a7a6", "c1e3", "e7e5", "d4b3", "c8e6", "f2f3", "f8e7", nullptr}},
    {16, {"e2e4", "c7c5", "g1f3", "b8c6", "d2d4", "c5d4", "f3d4", "g8f6", "b1c3", "e7e5", "d4b5", "d7d6", "c1g5", "a7a6", "b5a3", "b7b5", nullptr}},
    {14, {"e2e4", "c7c5", "b1c3", "b8c6", "g2g3", "g7g6", "f1g2", "f8g7", "d2d3", "d7d6", "c1e3", "e7e5", nullptr}},
    {16, {"e2e4", "e7e6", "d2d4", "d7d5", "b1c3", "g8f6", "e4e5", "f6d7", "f2f4", "c7c5", "g1f3", "b8c6", "c1e3", "c5d4", "f3d4", nullptr}},
    {17, {"e2e4", "c7c6", "d2d4", "d7d5", "b1c3", "d5e4", "c3e4", "c8f5", "e4g3", "f5g6", "g1f3", "b8d7", "h2h4", "h7h6", nullptr}},
    {10, {"e2e4", "d7d5", "e4d5", "d8d5", "b1c3", "d5a5", "d2d4", "g8f6", "g1f3", "c7c6", nullptr}},
    {17, {"e2e4", "e7e5", "g1f3", "g8f6", "f3e5", "d7d6", "e5f3", "f6e4", "d2d4", "d6d5", "f1d3", "f8d6", "e1g1", "e8g8", nullptr}},

    {18, {"d2d4", "d7d5", "c2c4", "c7c6", "g1f3", "g8f6", "b1c3", "d5c4", "a2a4", "c8f5", "e2e3", "e7e6", "f1c4", "f8b4", "e1g1", "e8g8", nullptr}},
    {17, {"d2d4", "d7d5", "c2c4", "e7e6", "b1c3", "c7c6", "g1f3", "g8f6", "e2e3", "b8d7", "f1d3", "d5c4", "d3c4", "b7b5", "c4e2", "c8b7", nullptr}},
    {18, {"d2d4", "g8f6", "c2c4", "e7e6", "b1c3", "f8b4", "e2e3", "e8g8", "f1d3", "d7d5", "g1f3", "c7c5", "e1g1", nullptr}},
    {16, {"d2d4", "g8f6", "c2c4", "g7g6", "b1c3", "f8g7", "e2e4", "d7d6", "g1f3", "e8g8", "f1e2", "e7e5", "e1g1", "b8c6", "d4d5", "c6e7", nullptr}},
    {16, {"d2d4", "g8f6", "c2c4", "g7g6", "b1c3", "d7d5", "c4d5", "f6d5", "e2e4", "d5c3", "b2c3", "f8g7", "g1f3", "c7c5", nullptr}},
    {14, {"d2d4", "d7d5", "c1f4", "g8f6", "e2e3", "c7c5", "c2c3", "b8c6", "b1d2", "c8f5", "g1f3", "e7e6", "f1e2", "f8d6", nullptr}},

    {14, {"c2c4", "g8f6", "b1c3", "e7e6", "g1f3", "d7d5", "d2d4", "f8b4", nullptr}}
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
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(buf[offset]) << 8) | buf[offset + 1]);
}

auto parse_uci_move(position& pos, const std::string& s) -> std::optional<russell::move>
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

auto matches(const russell::move& legal, const russell::move& raw) noexcept -> bool
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

  const auto raw_promotes =
      raw.flag == move_flag::promotion || raw.flag == move_flag::promotion_capture;

  const auto legal_promotes =
      legal.flag == move_flag::promotion || legal.flag == move_flag::promotion_capture;

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

        const auto piece_index =
            2 * pt + (c == color_index(color::white) ? 1 : 0);

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
         && (pos.pieces(us, piece_type::pawn)
             & bit(make_square(nf, capture_rank))) != 0) {
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

auto decode_move(std::uint16_t raw) noexcept -> russell::move
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

  return russell::move{from, to, promotion, flag};
}

auto encode_move(russell::move mv) noexcept -> std::uint16_t
{
  auto to = mv.to;

  if(mv.flag == move_flag::castling) {
    to = static_cast<std::uint8_t>(
        mv.to > mv.from ? mv.to + 1 : mv.to - 2);
  }

  auto raw = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(to)
      | (static_cast<std::uint16_t>(mv.from) << 6));

  if(mv.flag == move_flag::promotion
     || mv.flag == move_flag::promotion_capture) {
    auto code = std::uint16_t{4};

    switch(mv.promotion) {
    case piece_type::knight: code = 1; break;
    case piece_type::bishop: code = 2; break;
    case piece_type::rook: code = 3; break;
    default: break;
    }

    raw = static_cast<std::uint16_t>(
        raw | static_cast<std::uint16_t>(code << 12));
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

    file.read(
        reinterpret_cast<char*>(buf.data()),
        static_cast<std::streamsize>(buf.size()));

    if(!file) {
      entries_.clear();
      loaded_ = false;
      return false;
    }

    new_entries.push_back(book_entry{
        read_be64(buf, 0),
        read_be16(buf, 8),
        read_be16(buf, 10)
    });
  }

  std::sort(
      new_entries.begin(),
      new_entries.end(),
      [](const book_entry& lhs, const book_entry& rhs) {
        return lhs.key < rhs.key;
      });

  entries_ = std::move(new_entries);
  loaded_ = true;

  return true;
}

auto polyglot_book::load_builtin() -> void
{
  auto new_entries = std::vector<book_entry>{};
  new_entries.reserve(512);

  for(const auto& line : builtin_lines) {
    auto pos = position::start();
    auto line_entries = std::vector<book_entry>{};
    line_entries.reserve(20);

    auto valid = true;

    for(auto i = std::size_t{0}; line.moves[i] != nullptr; ++i) {
      const auto key = polyglot_key(pos);
      const auto mv = parse_uci_move(pos, line.moves[i]);

      if(!mv.has_value()) {
        valid = false;
        break;
      }

      line_entries.push_back(book_entry{
          key,
          encode_move(*mv),
          line.weight
      });

      static_cast<void>(pos.make_move(*mv));
    }

    if(valid) {
      new_entries.insert(
          new_entries.end(),
          line_entries.begin(),
          line_entries.end());
    }
  }

  std::sort(
      new_entries.begin(),
      new_entries.end(),
      [](const book_entry& lhs, const book_entry& rhs) {
        return lhs.key < rhs.key
            || (lhs.key == rhs.key && lhs.move < rhs.move);
      });

  auto merged = std::vector<book_entry>{};
  merged.reserve(new_entries.size());

  for(const auto& entry : new_entries) {
    if(!merged.empty()
       && merged.back().key == entry.key
       && merged.back().move == entry.move) {
      merged.back().weight =
          std::max(merged.back().weight, entry.weight);
    } else {
      merged.push_back(entry);
    }
  }

  entries_ = std::move(merged);
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

  auto it = std::lower_bound(
      entries_.begin(),
      entries_.end(),
      key,
      [](const book_entry& entry, std::uint64_t k) {
        return entry.key < k;
      });

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

auto polyglot_book::pick(
    const position& pos,
    std::uint64_t& rng_state) const -> std::optional<russell::move>
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
