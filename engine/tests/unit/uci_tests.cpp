#include "uci/uci.hpp"

#include "book/polyglot.hpp"
#include "core/position.hpp"
#include "movegen/movegen.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

auto require(bool value, const char* message) -> void
{
  if(!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

auto contains(const std::string& haystack, const std::string& needle) -> bool
{
  return haystack.find(needle) != std::string::npos;
}

auto extract_bestmove(const std::string& output) -> std::string
{
  const auto marker = std::string{"bestmove "};
  const auto pos = output.find(marker);
  if(pos == std::string::npos) {
    return {};
  }
  auto rest = output.substr(pos + marker.size());
  const auto end = rest.find('\n');
  return rest.substr(0, end);
}

auto legal_contains(const russell::position& pos, const std::string& mv) -> bool
{
  if(mv.size() < 4) {
    return false;
  }
  const auto from = russell::make_square(mv[0] - 'a', mv[1] - '1');
  const auto to = russell::make_square(mv[2] - 'a', mv[3] - '1');
  auto copy = pos;
  for(const auto candidate : russell::movegen::generate_legal(copy)) {
    if(candidate.from == static_cast<std::uint8_t>(from)
       && candidate.to == static_cast<std::uint8_t>(to)) {
      return true;
    }
  }
  return false;
}

auto test_handshake() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("uci");
  e.handle("isready");
  const auto text = out.str();

  require(contains(text, "id name russell v1.2"), "uci id name");
  require(contains(text, "uciok"), "uci uciok");
  require(contains(text, "readyok"), "isready readyok");
  require(contains(text, "option name Threads"), "uci threads option advertised");
  require(contains(text, "option name Hash"), "uci hash option advertised");
  require(contains(text, "option name MoveOverhead"), "uci move overhead option advertised");
  require(contains(text, "option name SyzygyPath"), "uci syzygy path option advertised");
}

auto test_position_startpos_moves() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("position startpos moves e2e4 e7e5");
  const auto expected = russell::position::from_fen(
      "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2");
  require(e.position() == expected, "startpos moves position");
}

auto test_position_fen_moves() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 moves g1f3");
  require(e.position().piece_on(russell::make_square(5, 2)) == russell::piece::wn, "fen moves knight on f3");
  require(e.position().piece_on(russell::make_square(6, 0)) == russell::piece::none, "fen moves g1 empty");
  require(e.position().side_to_move() == russell::color::black, "fen moves side to move");
}

auto test_position_promotion() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("position fen 8/1P6/8/8/8/8/8/k1K5 w - - 0 1 moves b7b8q");
  require(e.position().piece_on(russell::make_square(1, 7)) == russell::piece::wq, "promotion to queen");
}

auto test_go_depth() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("setoption name OwnBook value false");
  e.handle("position startpos");
  e.handle("go depth 3");
  e.wait();
  const auto text = out.str();
  require(contains(text, "info depth 3"), "go depth info line");
  const auto mv = extract_bestmove(text);
  require(mv.size() == 4, "go depth bestmove length");
  require(legal_contains(e.position(), mv), "go depth bestmove legal");
}

auto test_go_movetime() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("position startpos");
  e.handle("go movetime 100");
  e.wait();
  const auto text = out.str();
  require(contains(text, "bestmove "), "movetime bestmove");
}

auto test_go_wtime() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("position startpos");
  e.handle("go wtime 10000 btime 10000 winc 100 binc 100");
  e.wait();
  const auto text = out.str();
  require(contains(text, "bestmove "), "wtime bestmove");
}

auto test_go_movestogo() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("position startpos");
  e.handle("go wtime 1000 btime 1000 winc 0 binc 0 movestogo 10");
  e.wait();
  const auto text = out.str();
  require(contains(text, "bestmove "), "movestogo bestmove");
}

auto test_go_low_time_overhead() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("position startpos");
  e.handle("go wtime 10 btime 10 winc 0 binc 0 movestogo 1");
  e.wait();
  const auto text = out.str();
  require(contains(text, "bestmove "), "low time overhead bestmove");
}

auto test_go_default_movestogo_low_time() -> void
{

  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("setoption name OwnBook value false");
  e.handle("position startpos");
  e.handle("go wtime 1000 btime 1000 winc 0 binc 0");
  e.wait();
  const auto text = out.str();
  require(contains(text, "info depth"), "default movestogo low time runs a real search");
  const auto mv = extract_bestmove(text);
  require(mv.size() == 4, "default movestogo low time bestmove length");
  require(legal_contains(e.position(), mv), "default movestogo low time bestmove legal");
}

auto test_setoption_move_overhead() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("setoption name OwnBook value false");
  e.handle("setoption name MoveOverhead value 1000");
  e.handle("position startpos");
  e.handle("go wtime 100 btime 100 winc 0 binc 0");
  e.wait();
  const auto text = out.str();
  require(contains(text, "bestmove "), "move overhead option bestmove");
  const auto mv = extract_bestmove(text);
  require(legal_contains(e.position(), mv), "move overhead option bestmove legal");
}

auto test_setoption_threads() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("setoption name Threads value 2");
  e.handle("position startpos");
  e.handle("go depth 3");
  e.wait();
  const auto text = out.str();
  require(contains(text, "bestmove "), "threads bestmove");
  const auto mv = extract_bestmove(text);
  require(legal_contains(e.position(), mv), "threads bestmove legal");
}

auto test_setoption_hash() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("setoption name Hash value 32");
  e.handle("position startpos");
  e.handle("go depth 3");
  e.wait();
  const auto text = out.str();
  require(contains(text, "bestmove "), "hash resized bestmove");
  const auto mv = extract_bestmove(text);
  require(legal_contains(e.position(), mv), "hash resized bestmove legal");
}

auto test_ucinewgame_stops_search() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("position startpos");
  e.handle("go infinite");

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  e.handle("ucinewgame");
  e.handle("setoption name OwnBook value false");
  e.handle("position startpos");
  e.handle("go depth 3");
  e.wait();
  const auto text = out.str();
  require(contains(text, "info depth 3"), "ucinewgame then search reports depth");
  const auto mv = extract_bestmove(text);
  require(legal_contains(e.position(), mv), "ucinewgame then search bestmove legal");
}

auto test_go_stop() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("position startpos");
  e.handle("go infinite");
  e.handle("stop");
  e.wait();
  const auto text = out.str();
  require(contains(text, "bestmove "), "stop bestmove");
  const auto mv = extract_bestmove(text);
  require(legal_contains(e.position(), mv), "stop bestmove legal");
}

auto test_quit_joins() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("go depth 3");
  e.handle("quit");
  const auto text = out.str();
  require(contains(text, "bestmove "), "quit bestmove");
}

auto test_setoption_syzygy_path() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("setoption name SyzygyPath value C:/nonexistent/path");
  e.handle("position startpos");
  e.handle("go depth 3");
  e.wait();
  const auto text = out.str();
  require(contains(text, "bestmove "), "syzygy path setoption bestmove");
  const auto mv = extract_bestmove(text);
  require(legal_contains(e.position(), mv), "syzygy path setoption bestmove legal");
}

auto test_setoption_syzygy_path_empty() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("setoption name SyzygyPath value <empty>");
  e.handle("position startpos");
  e.handle("go depth 3");
  e.wait();
  const auto text = out.str();
  require(contains(text, "bestmove "), "empty syzygy path bestmove");
  const auto mv = extract_bestmove(text);
  require(legal_contains(e.position(), mv), "empty syzygy path bestmove legal");
}

auto test_go_tablebase_info_score() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("setoption name SyzygyPath value " TUN_TB_TEST_DIR);
  e.handle("position fen k7/2Q5/1K6/8/8/8/8/8 w - - 0 1");
  e.handle("go depth 3");
  e.wait();
  const auto text = out.str();
  require(contains(text, "info depth 1 score mate 1"), "tablebase info reports mate in one");
  const auto mv = extract_bestmove(text);
  require(mv.size() == 4, "tablebase bestmove length");
  require(legal_contains(e.position(), mv), "tablebase bestmove legal");
}

auto write_polyglot_entry(std::ofstream& file, std::uint64_t key, std::uint16_t raw_move, std::uint16_t weight) -> void
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

auto test_book_options_advertised() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("uci");
  const auto text = out.str();
  require(contains(text, "option name OwnBook type check default true"), "own book option advertised");
  require(contains(text, "option name BookPath type string"), "book path option advertised");
}

auto test_go_book_from_startpos() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("position startpos");
  e.handle("go depth 3");
  e.wait();
  const auto text = out.str();
  require(!contains(text, "info depth"), "book move short-circuits search");
  const auto mv = extract_bestmove(text);
  require(mv.size() == 4, "book bestmove length");
  require(legal_contains(e.position(), mv), "book bestmove legal");
}

auto test_go_book_after_e4() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("position startpos moves e2e4");
  e.handle("go depth 3");
  e.wait();
  const auto text = out.str();
  const auto mv = extract_bestmove(text);
  require(mv.size() == 4, "book position bestmove length");
  require(legal_contains(e.position(), mv), "book position bestmove legal");
  require(mv == "e7e5" || mv == "c7c5" || mv == "e7e6" || mv == "c7c6", "book position bestmove from book");
}

auto test_bookpath_external_book() -> void
{
  const auto path = "TUN_bookpath_test.bin";
  const auto key = russell::book::polyglot_key(russell::position::start());
  const auto odd_move = russell::move{static_cast<std::uint8_t>(russell::make_square(1, 0)),
                                   static_cast<std::uint8_t>(russell::make_square(0, 2)),
                                   russell::piece_type::queen, russell::move_flag::quiet};
  {
    auto file = std::ofstream{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(file), "bookpath test file open");
    write_polyglot_entry(file, key, russell::book::encode_move(odd_move), 10);
    require(static_cast<bool>(file), "bookpath test file write");
  }
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("setoption name BookPath value " "TUN_bookpath_test.bin");
  e.handle("position startpos");
  e.handle("go depth 3");
  e.wait();
  const auto mv = extract_bestmove(out.str());
  require(mv == "b1a3", "external book move played");
  std::remove(path);
}

auto test_bookpath_bad_file_falls_back_to_search() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("setoption name OwnBook value true");
  e.handle("setoption name BookPath value C:/definitely/not/a/real/book.bin");
  e.handle("position startpos");
  e.handle("go depth 3");
  e.wait();
  const auto text = out.str();
  require(contains(text, "info depth"), "bad book path falls back to search");
  const auto mv = extract_bestmove(text);
  require(mv.size() == 4, "bad book path bestmove length");
  require(legal_contains(e.position(), mv), "bad book path bestmove legal");
}

auto test_bookpath_empty_uses_builtin() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("setoption name BookPath value <empty>");
  e.handle("position startpos");
  e.handle("go depth 3");
  e.wait();
  const auto mv = extract_bestmove(out.str());
  require(mv.size() == 4, "builtin bestmove length");
  require(legal_contains(e.position(), mv), "builtin bestmove legal");
}

auto test_book_disabled_searches() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("setoption name OwnBook value false");
  e.handle("position startpos");
  e.handle("go depth 3");
  e.wait();
  const auto text = out.str();
  require(contains(text, "info depth"), "own book off runs search");
  const auto mv = extract_bestmove(text);
  require(mv.size() == 4, "own book off bestmove length");
}

auto test_book_analysis_mode_searches() -> void
{
  auto out = std::ostringstream{};
  auto e = russell::uci::engine{out};
  e.handle("position startpos");
  e.handle("go infinite");

  for(auto i = 0; i < 200 && !contains(out.str(), "info "); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  e.handle("stop");
  e.wait();
  const auto text = out.str();
  require(contains(text, "info "), "analysis mode does not short-circuit to book");
  const auto mv = extract_bestmove(text);
  require(mv.size() == 4, "analysis mode bestmove length");
  require(legal_contains(e.position(), mv), "analysis mode bestmove legal");
}

}

auto main() -> int
{
  test_handshake();
  test_position_startpos_moves();
  test_position_fen_moves();
  test_position_promotion();
  test_go_depth();
  test_go_movetime();
  test_go_wtime();
  test_go_movestogo();
  test_go_low_time_overhead();
  test_go_default_movestogo_low_time();
  test_setoption_move_overhead();
  test_setoption_threads();
  test_setoption_hash();
  test_ucinewgame_stops_search();
  test_go_stop();
  test_quit_joins();
  test_setoption_syzygy_path();
  test_setoption_syzygy_path_empty();
  test_go_tablebase_info_score();
  test_book_options_advertised();
  test_go_book_from_startpos();
  test_go_book_after_e4();
  test_bookpath_external_book();
  test_bookpath_bad_file_falls_back_to_search();
  test_bookpath_empty_uses_builtin();
  test_book_disabled_searches();
  test_book_analysis_mode_searches();
  return 0;
}