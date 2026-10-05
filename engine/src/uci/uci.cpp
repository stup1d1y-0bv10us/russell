#include "uci/uci.hpp"

#include "core/position.hpp"
#include "eval/evaluate.hpp"
#include "eval/nnue.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
#include "tb/tablebase.hpp"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace russell::uci {

namespace {

auto move_string(move mv) -> std::string
{
  auto out = std::string{};
  out.reserve(5);
  out.push_back(static_cast<char>('a' + file_of(mv.from)));
  out.push_back(static_cast<char>('1' + rank_of(mv.from)));
  out.push_back(static_cast<char>('a' + file_of(mv.to)));
  out.push_back(static_cast<char>('1' + rank_of(mv.to)));
  if(mv.flag == move_flag::promotion || mv.flag == move_flag::promotion_capture) {
    switch(mv.promotion) {
    case piece_type::queen: out.push_back('q'); break;
    case piece_type::rook: out.push_back('r'); break;
    case piece_type::bishop: out.push_back('b'); break;
    case piece_type::knight: out.push_back('n'); break;
    default: break;
    }
  }
  return out;
}

auto parse_move(const position& pos, const std::string& token) -> std::optional<move>
{
  if(token.size() < 4 || token.size() > 5) {
    return std::nullopt;
  }
  const auto from_file = token[0] - 'a';
  const auto from_rank = token[1] - '1';
  const auto to_file = token[2] - 'a';
  const auto to_rank = token[3] - '1';
  if(from_file < 0 || from_file > 7 || from_rank < 0 || from_rank > 7
     || to_file < 0 || to_file > 7 || to_rank < 0 || to_rank > 7) {
    return std::nullopt;
  }
  auto promotion = piece_type::queen;
  if(token.size() == 5) {
    switch(static_cast<char>(std::tolower(static_cast<unsigned char>(token[4])))) {
    case 'q': promotion = piece_type::queen; break;
    case 'r': promotion = piece_type::rook; break;
    case 'b': promotion = piece_type::bishop; break;
    case 'n': promotion = piece_type::knight; break;
    default: return std::nullopt;
    }
  }
  const auto from = make_square(from_file, from_rank);
  const auto to = make_square(to_file, to_rank);
  auto copy = pos;
  for(const auto mv : movegen::generate_legal(copy)) {
    if(mv.from == static_cast<std::uint8_t>(from)
       && mv.to == static_cast<std::uint8_t>(to)
       && mv.promotion == promotion) {
      return mv;
    }
  }
  return std::nullopt;
}

auto mate_in_moves(int score) -> int
{
  return (search::mate_value - score + 1) / 2;
}

auto hash_mb_to_entries(int mb) noexcept -> std::size_t
{
  if(mb < 1) mb = 1;
  return (static_cast<std::size_t>(mb) * 1024 * 1024) / sizeof(russell::search::tt_entry);
}

constexpr auto default_hash_mb = 16;

}

auto time_budget_ms(int remaining_time, int increment, int moves_to_go, int move_overhead) noexcept
    -> time_budget
{
  constexpr auto minimum_ms = 10;
  if(remaining_time <= 0) {
    return {minimum_ms, minimum_ms};
  }
  if(moves_to_go <= 0) {
    moves_to_go = default_moves_to_go;
  }
  const auto available_ms = remaining_time - move_overhead;

  int base = remaining_time / moves_to_go;
  int inc_part = static_cast<int>(increment * search::tm_increment_factor);
  int soft_ms = base + inc_part - move_overhead;

  if(soft_ms < minimum_ms) soft_ms = minimum_ms;
  int hard_ms = std::max(available_ms, minimum_ms);

  int hard_from_soft = static_cast<int>(soft_ms * search::tm_hard_factor);
  if(hard_from_soft < minimum_ms) hard_from_soft = minimum_ms;
  hard_ms = std::min(hard_ms, hard_from_soft);
  if(soft_ms > hard_ms) soft_ms = hard_ms;

  if(soft_ms > available_ms) soft_ms = std::max(minimum_ms, available_ms);
  if(hard_ms > available_ms) hard_ms = std::max(minimum_ms, available_ms);
  return {soft_ms, hard_ms};
}

engine::engine() : out_(std::cout)
{
  tt_.resize(hash_mb_to_entries(default_hash_mb));
  book_.load_builtin();
}

engine::engine(std::ostream& out) : out_(out)
{
  tt_.resize(hash_mb_to_entries(default_hash_mb));
  book_.load_builtin();
}

engine::~engine()
{
  stopper_.stop.store(true, std::memory_order_relaxed);
  if(worker_.joinable()) {
    worker_.join();
  }
}

auto engine::position() const noexcept -> const russell::position&
{
  return position_;
}

auto engine::send(const std::string& line) -> void
{
  std::lock_guard<std::mutex> lock(out_mutex_);
  out_ << line << '\n';
  out_.flush();
}

auto engine::run() -> void
{
  for(std::string line; std::getline(std::cin, line);) {
    handle(line);
    if(quit_) {
      break;
    }
  }
  stopper_.stop.store(true, std::memory_order_relaxed);
  if(worker_.joinable()) {
    worker_.join();
  }
}

auto engine::wait() -> void
{
  if(worker_.joinable()) {
    worker_.join();
  }
}

auto engine::handle(const std::string& line) -> void
{
  auto tokens = std::vector<std::string>{};
  auto ss = std::istringstream{line};
  for(std::string token; ss >> token;) {
    tokens.push_back(token);
  }
  if(tokens.empty()) {
    return;
  }
  const auto& command = tokens[0];
  if(command == "uci") {
    send("id name russell v1.3");
    send("id author Dominic Fuentes");
    send("option name Threads type spin default 1 min 1 max 64");
    send("option name Hash type spin default 16 min 1 max 4096");
    send("option name MoveOverhead type spin default 50 min 0 max 5000");
    send("option name SyzygyPath type string default <empty>");
    send("option name EvalFile type string default <empty>");
    send("option name OwnBook type check default true");
    send("option name BookPath type string default <empty>");
    send("uciok");
  } else if(command == "isready") {
    send("readyok");
  } else if(command == "position") {
    handle_position(tokens);
  } else if(command == "go") {
    handle_go(tokens);
  } else if(command == "setoption") {
    handle_setoption(tokens);
  } else if(command == "ucinewgame") {
    stop_search();
    tt_.clear();
  } else if(command == "stop") {
    stopper_.stop.store(true, std::memory_order_relaxed);
  } else if(command == "quit") {
    stop_search();
    quit_ = true;
  }
}

auto engine::handle_setoption(const std::vector<std::string>& tokens) -> void
{
  auto name = std::string{};
  auto value = std::string{};
  for(auto i = std::size_t{1}; i < tokens.size(); ++i) {
    if(tokens[i] == "name" && i + 1 < tokens.size()) {
      name = tokens[++i];
    } else if(tokens[i] == "value" && i + 1 < tokens.size()) {

      for(++i; i < tokens.size(); ++i) {
        if(!value.empty()) {
          value += ' ';
        }
        value += tokens[i];
      }
    }
  }
  for(auto& ch : name) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if(name == "threads") {
    const auto n = std::atoi(value.c_str());
    if(n >= 1) {
      threads_ = n;
    }
  } else if(name == "hash") {
    const auto mb = std::atoi(value.c_str());
    if(mb >= 1) {

      stop_search();
      tt_.resize(hash_mb_to_entries(mb));
      tt_.clear();
    }
  } else if(name == "moveoverhead") {
    const auto ms = std::atoi(value.c_str());
    if(ms >= 0) {
      move_overhead_ms_ = ms;
    }
  } else if(name == "syzygypath") {
    syzygy_path_ = value;
    static_cast<void>(russell::tb::init(syzygy_path_));
  } else if(name == "evalfile") {
    nnue_path_ = value;
    if(value.empty() || value == "<empty>") {

      eval::set_nnue(nullptr);
    } else {
      auto net = std::make_shared<eval::nnue::network>();
      if(net->load(value)) {
        eval::set_nnue(std::move(net));
      } else {
        eval::set_nnue(nullptr);
      }
    }
  } else if(name == "ownbook") {
    own_book_ = value == "true" || value == "on" || value == "1";
  } else if(name == "bookpath") {
    book_path_ = value;
    if(value.empty() || value == "<empty>") {
      book_.load_builtin();
    } else {
      static_cast<void>(book_.load(value));
    }
  }
}

auto engine::handle_position(const std::vector<std::string>& tokens) -> void
{
  if(tokens.size() < 2) {
    return;
  }
  auto i = std::size_t{1};
  if(tokens[i] == "startpos") {
    position_ = russell::position::start();
    ++i;
  } else if(tokens[i] == "fen") {
    ++i;
    auto fen = std::string{};
    auto saw_moves = false;
    for(; i < tokens.size(); ++i) {
      if(tokens[i] == "moves") {
        saw_moves = true;
        break;
      }
      if(!fen.empty()) {
        fen += ' ';
      }
      fen += tokens[i];
    }
    if(fen.empty()) {
      return;
    }
    position_ = russell::position::from_fen(fen);
    if(!saw_moves) {
      return;
    }
  } else {
    return;
  }
  if(i < tokens.size() && tokens[i] == "moves") {
    ++i;
    for(; i < tokens.size(); ++i) {
      const auto mv = parse_move(position_, tokens[i]);
      if(!mv.has_value()) {
        break;
      }
      static_cast<void>(position_.make_move(*mv));
    }
  }
}

auto engine::handle_go(const std::vector<std::string>& tokens) -> void
{
  auto limits = search::search_limits{};
  auto wtime = 0;
  auto btime = 0;
  auto winc = 0;
  auto binc = 0;
  auto has_time = false;
  auto movetime = 0;
  auto moves_to_go = default_moves_to_go;
  for(auto i = std::size_t{1}; i < tokens.size(); ++i) {
    const auto& token = tokens[i];
    const auto value = [&]() -> int {
      if(i + 1 < tokens.size()) {
        return std::atoi(tokens[i + 1].c_str());
      }
      return 0;
    };
    if(token == "wtime") {
      wtime = value();
      has_time = true;
    } else if(token == "btime") {
      btime = value();
      has_time = true;
    } else if(token == "winc") {
      winc = value();
    } else if(token == "binc") {
      binc = value();
    } else if(token == "movetime") {
      movetime = value();
    } else if(token == "movestogo") {
      moves_to_go = value();
    } else if(token == "depth") {
      limits.depth = value();
    } else if(token == "nodes") {
      if(i + 1 < tokens.size()) {
        const char* s = tokens[i + 1].c_str();
        char* end = nullptr;
        unsigned long long v = std::strtoull(s, &end, 10);
        if(end != s && v > 0) {
          limits.nodes = static_cast<std::uint64_t>(v);
        }
      }
    } else if(token == "infinite") {
      limits.infinite = true;
    }
  }

  if(movetime > 0) {
    limits.soft_time_ms = movetime;
    limits.hard_time_ms = movetime;
  } else if(has_time && !limits.infinite && limits.depth == 0 && limits.nodes == 0) {
    const auto us = position_.side_to_move();
    const auto time_left = us == color::white ? wtime : btime;
    const auto inc = us == color::white ? winc : binc;
    const auto budget = time_budget_ms(time_left, inc, moves_to_go, move_overhead_ms_);
    limits.soft_time_ms = budget.soft_ms;
    limits.hard_time_ms = budget.hard_ms;
  }
  start_search(limits);
}

auto engine::stop_search() -> void
{
  stopper_.stop.store(true, std::memory_order_relaxed);
  if(worker_.joinable()) {
    worker_.join();
  }
}

auto engine::start_search(const search::search_limits& limits) -> void
{
  if(worker_.joinable()) {
    stopper_.stop.store(true, std::memory_order_relaxed);
    worker_.join();
  }
  stopper_.stop.store(false, std::memory_order_relaxed);

  if(own_book_ && !limits.infinite
     && (limits.depth > 0 || limits.nodes > 0 || limits.soft_time_ms > 0 || limits.hard_time_ms > 0)) {
    const auto book_move = book_.pick(position_, rng_);
    if(book_move.has_value()) {
      auto result = search::search_result{};
      result.best_move = *book_move;
      result.has_move = true;
      send_bestmove(result);
      return;
    }
  }

  auto pos = position_;
  worker_ = std::thread([this, limits, pos]() mutable {
    const auto result = search::parallel_search(
        pos, limits, stopper_, threads_, tt_,
        [this](const search::search_result& r) { send_info(r); });
    send_bestmove(result);
  });
}

auto engine::send_info(const search::search_result& result) -> void
{
  if(!result.has_move) {
    return;
  }
  auto ss = std::ostringstream{};
  ss << "info depth " << result.depth << " score ";
  if(result.score > 20000) {
    ss << "mate " << mate_in_moves(result.score);
  } else if(result.score < -20000) {
    ss << "mate " << -mate_in_moves(-result.score);
  } else {
    ss << "cp " << result.score;
  }
  ss << " nodes " << result.nodes << " pv " << move_string(result.best_move);
  send(ss.str());
}

auto engine::send_bestmove(const search::search_result& result) -> void
{
  if(result.has_move) {
    send("bestmove " + move_string(result.best_move));
  } else {
    send("bestmove 0000");
  }
}

auto run() -> void
{
  auto engine = russell::uci::engine{};
  engine.run();
}

}
