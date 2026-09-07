#include "book/polyglot.hpp"

#include "core/position.hpp"
#include "movegen/movegen.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

auto require(bool value, const char* message) -> void
{
  if(!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

auto uci_string(tuna::move mv) -> std::string
{
  auto out = std::string{};
  out.reserve(5);
  out.push_back(static_cast<char>('a' + tuna::file_of(mv.from)));
  out.push_back(static_cast<char>('1' + tuna::rank_of(mv.from)));
  out.push_back(static_cast<char>('a' + tuna::file_of(mv.to)));
  out.push_back(static_cast<char>('1' + tuna::rank_of(mv.to)));
  if(mv.flag == tuna::move_flag::promotion || mv.flag == tuna::move_flag::promotion_capture) {
    switch(mv.promotion) {
    case tuna::piece_type::knight: out.push_back('n'); break;
    case tuna::piece_type::bishop: out.push_back('b'); break;
    case tuna::piece_type::rook: out.push_back('r'); break;
    default: out.push_back('q'); break;
    }
  }
  return out;
}

auto play(tuna::position& pos, const std::string& uci) -> void
{
  const auto from = tuna::make_square(uci[0] - 'a', uci[1] - '1');
  const auto to = tuna::make_square(uci[2] - 'a', uci[3] - '1');
  auto copy = pos;
  for(const auto mv : tuna::movegen::generate_legal(copy)) {
    if(mv.from == static_cast<std::uint8_t>(from)
       && mv.to == static_cast<std::uint8_t>(to)
       && uci_string(mv) == uci) {
      static_cast<void>(pos.make_move(mv));
      return;
    }
  }
  require(false, "test move is not legal");
}

auto is_legal(const tuna::position& pos, tuna::move mv) -> bool
{
  auto copy = pos;
  for(const auto& candidate : tuna::movegen::generate_legal(copy)) {
    if(candidate == mv) {
      return true;
    }
  }
  return false;
}

void write_entry(std::ofstream& file, std::uint64_t key, std::uint16_t raw_move, std::uint16_t weight)
{
  auto buf = std::array<unsigned char, 16>{};
  for(auto i = std::size_t{0}; i < 8; ++i) {
    buf[7 - i] = static_cast<unsigned char>(key >> (8 * i));
  }
  buf[8] = static_cast<unsigned char>(raw_move >> 8);
  buf[9] = static_cast<unsigned char>(raw_move & 0xFF);
  buf[10] = static_cast<unsigned char>(weight >> 8);
  buf[11] = static_cast<unsigned char>(weight & 0xFF);
  file.write(reinterpret_cast<const char*>(buf.data()), 16);
}

auto raw_move(int from, int to, int promotion = 0) -> std::uint16_t
{
  auto raw = static_cast<std::uint16_t>(static_cast<std::uint16_t>(to)
                                        | static_cast<std::uint16_t>(from << 6));
  if(promotion != 0) {
    raw = static_cast<std::uint16_t>(raw | static_cast<std::uint16_t>(promotion << 12));
  }
  return raw;
}

auto test_canonical_keys() -> void
{
  struct key_vector {
    const char* fen;
    std::uint64_t key;
  };
  const key_vector vectors[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 0x463B96181691FC9CULL},
    {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", 0x823C9B50FD114196ULL},
    {"rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2", 0x0756B94461C50FB0ULL},
    {"rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2", 0x662FAFB965DB29D4ULL},
    {"rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", 0x22A48B5A8E47FF78ULL},
    {"rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR b kq - 0 3", 0x652A607CA3F242C1ULL},
    {"rnbq1bnr/ppp1pkpp/8/3pPp2/8/8/PPPPKPPP/RNBQ1BNR w - - 0 4", 0x00FDD303C946BDD9ULL},
    {"rnbqkbnr/p1pppppp/8/8/PpP4P/8/1P1PPPP1/RNBQKBNR b KQkq c3 0 3", 0x3C8123EA7B067637ULL},
    {"rnbqkbnr/p1pppppp/8/8/P6P/R1p5/1P1PPPP1/1NBQKBNR b Kkq - 0 4", 0x5C3F9B829B279560ULL},
  };
  for(const auto& v : vectors) {
    const auto pos = tuna::position::from_fen(v.fen);
    require(tuna::book::polyglot_key(pos) == v.key, "polyglot key vector mismatch");
  }
}

auto test_move_encoding() -> void
{
  auto move = tuna::move{};
  auto raw = std::uint16_t{0};

  move = tuna::move{12, 28, tuna::piece_type::queen, tuna::move_flag::double_push};
  raw = tuna::book::encode_move(move);
  require(raw == static_cast<std::uint16_t>(28 | (12 << 6)), "e2e4 raw encoding");
  const auto decoded = tuna::book::decode_move(raw);
  require(decoded.from == 12 && decoded.to == 28, "e2e4 raw decoding");

  move = tuna::move{48, 56, tuna::piece_type::knight, tuna::move_flag::promotion};
  raw = tuna::book::encode_move(move);
  require(raw == static_cast<std::uint16_t>(56 | (48 << 6) | (1 << 12)), "knight promotion encoding");
  const auto knight = tuna::book::decode_move(raw);
  require(knight.from == 48 && knight.to == 56 && knight.promotion == tuna::piece_type::knight,
          "knight promotion decoding");

  move = tuna::move{48, 57, tuna::piece_type::queen, tuna::move_flag::promotion_capture};
  raw = tuna::book::encode_move(move);
  require(raw == static_cast<std::uint16_t>(57 | (48 << 6) | (4 << 12)), "queen promotion encoding");
  const auto queen = tuna::book::decode_move(raw);
  require(queen.from == 48 && queen.to == 57 && queen.promotion == tuna::piece_type::queen,
          "queen promotion decoding");

  const auto castle_g = tuna::book::decode_move(raw_move(4, 6));
  require(castle_g.from == 4 && castle_g.to == 6, "castling e1g1 decode");
  const auto castle_h = tuna::book::decode_move(raw_move(4, 7));
  require(castle_h.from == 4 && castle_h.to == 7, "castling e1h1 decode");
}

auto test_builtin_book() -> void
{

  const std::vector<std::vector<std::string>> lines = {
    {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6"},
    {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "c2c3"},
    {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", "d2d3"},
    {"e2e4", "e7e5", "g1f3", "b8c6", "d2d4", "e5d4"},
    {"e2e4", "c7c5", "g1f3", "d7d6"},
    {"e2e4", "c7c5", "b1c3", "b8c6"},
    {"e2e4", "e7e6", "d2d4", "d7d5"},
    {"e2e4", "c7c6", "d2d4", "d7d5"},
    {"d2d4", "d7d5", "c2c4", "e7e6", "b1c3"},
    {"d2d4", "d7d5", "c2c4", "c7c6", "b1c3"},
    {"d2d4", "g8f6", "c2c4", "g7g6", "b1c3"},
    {"d2d4", "g8f6", "c2c4", "e7e6", "b1c3"},
    {"g1f3", "d7d5", "d2d4"},
    {"g1f3", "g8f6", "c2c4"},
    {"c2c4", "e7e5", "b1c3"},
    {"d2d4", "f7f5", "g1f3"},
    {"e2e4", "e7e5", "b1c3", "b8c6"},
    {"e2e4", "e7e5", "g1f3", "g8f6", "f3e5"},
    {"d2d4", "d7d5", "g1f3", "g8f6"},
    {"c2c4", "g8f6", "b1c3"},
  };

  auto book = tuna::book::polyglot_book{};
  book.load_builtin();
  require(book.loaded(), "builtin book loaded");

  auto seen = std::set<std::uint64_t>{};
  auto position_count = std::size_t{0};
  for(const auto& line : lines) {
    auto pos = tuna::position::start();
    for(auto i = std::size_t{0}; i < line.size(); ++i) {
      auto moves = book.moves_for(pos);
      require(!moves.empty(), "builtin position has book moves");
      for(const auto& bm : moves) {
        require(is_legal(pos, bm.mv), "builtin book move is legal");
      }
      seen.insert(tuna::book::polyglot_key(pos));
      ++position_count;
      play(pos, line[i]);
    }
  }
  require(seen.size() >= 20, "at least 20 distinct book positions");
  require(position_count >= 20, "at least 20 tested positions");
}

auto test_book_variety() -> void
{
  auto book = tuna::book::polyglot_book{};
  book.load_builtin();

  auto pos = tuna::position::start();
  auto picked = std::set<std::string>{};
  auto rng = std::uint64_t{0x123456789};
  for(auto i = 0; i < 40; ++i) {
    const auto mv = book.pick(pos, rng);
    require(mv.has_value(), "startpos pick available");
    require(is_legal(pos, *mv), "startpos pick legal");
    picked.insert(uci_string(*mv));
  }
  require(picked.size() >= 2, "startpos book move variety");

  play(pos, "e2e4");
  picked.clear();
  for(auto i = 0; i < 40; ++i) {
    const auto mv = book.pick(pos, rng);
    require(mv.has_value(), "after-e4 pick available");
    require(is_legal(pos, *mv), "after-e4 pick legal");
    picked.insert(uci_string(*mv));
  }
  require(picked.size() >= 2, "after-e4 book move variety");
}

auto test_file_book() -> void
{
  const auto path = "TUN_book_file_test.bin";

  const auto pos = tuna::position::from_fen("r3k2r/8/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
  const auto key = tuna::book::polyglot_key(pos);
  {
    auto file = std::ofstream{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(file), "file book test open");

    write_entry(file, key, raw_move(12, 28), 10);
    write_entry(file, key, raw_move(11, 27), 0);
    write_entry(file, key, raw_move(4, 6), 10);
    write_entry(file, key, raw_move(4, 7), 10);
    require(static_cast<bool>(file), "file book test write");
  }

  auto book = tuna::book::polyglot_book{};
  require(book.load(path), "file book loads");

  const auto moves = book.moves_for(pos);
  require(!moves.empty(), "file book startpos moves");
  auto saw_castling = false;
  auto saw_e2e4 = false;
  auto saw_d2d4 = false;
  for(const auto& bm : moves) {
    require(is_legal(pos, bm.mv), "file book move legal");
    if(uci_string(bm.mv) == "e2e4") {
      saw_e2e4 = true;
    }
    if(uci_string(bm.mv) == "d2d4") {
      saw_d2d4 = true;
    }
    if(bm.mv.flag == tuna::move_flag::castling) {
      saw_castling = true;
    }
  }
  require(saw_e2e4, "file book reads e2e4 entry");
  require(saw_d2d4, "file book reads d2d4 entry");
  require(saw_castling, "file book maps both castling encodings to a legal move");

  auto rng = std::uint64_t{0xDEADBEEF};
  for(auto i = 0; i < 30; ++i) {
    const auto mv = book.pick(pos, rng);
    require(mv.has_value(), "file book pick available");
    require(uci_string(*mv) != "d2d4", "weight-zero entry never picked");
    require(is_legal(pos, *mv), "file book pick legal");
  }
  std::remove(path);

  require(!book.load("C:/definitely/not/a/real/book.bin"), "missing book file fails");
  {
    auto file = std::ofstream{path, std::ios::binary | std::ios::trunc};
    file.write("short", 5);
    require(static_cast<bool>(file), "malformed file write");
  }
  require(!book.load(path), "non-16-byte-multiple file fails");
  std::remove(path);
}

}

auto main() -> int
{
  test_canonical_keys();
  test_move_encoding();
  test_builtin_book();
  test_book_variety();
  test_file_book();
  return 0;
}