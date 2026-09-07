#pragma once

#include "book/polyglot.hpp"
#include "core/position.hpp"
#include "search/search.hpp"
#include "search/transposition_table.hpp"

#include <cstdint>
#include <iosfwd>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tuna::uci {

constexpr auto default_moves_to_go = 40;
constexpr auto default_move_overhead_ms = 50;

struct time_budget {
  int soft_ms = 0;
  int hard_ms = 0;
};

[[nodiscard]] auto time_budget_ms(int remaining_time, int increment, int moves_to_go,
                                  int move_overhead) noexcept -> time_budget;

class engine {
public:
  engine();
  explicit engine(std::ostream& out);
  ~engine();

  engine(const engine&) = delete;
  auto operator=(const engine&) -> engine& = delete;

  auto handle(const std::string& line) -> void;
  auto run() -> void;
  auto wait() -> void;

  [[nodiscard]] auto position() const noexcept -> const tuna::position&;

private:
  std::ostream& out_;
  std::mutex out_mutex_;
  tuna::position position_ = tuna::position::start();
  tuna::search::search_stopper stopper_;
  std::thread worker_;

  tuna::search::transposition_table tt_;
  int threads_ = 1;
  int move_overhead_ms_ = 50;
  std::string syzygy_path_{};
  std::string nnue_path_{};
  tuna::book::polyglot_book book_;
  std::string book_path_{};
  bool own_book_ = true;
  std::uint64_t rng_ = 0x9E3779B97F4A7C15ULL;
  bool quit_ = false;

  auto send(const std::string& line) -> void;
  auto stop_search() -> void;
  auto handle_go(const std::vector<std::string>& tokens) -> void;
  auto handle_position(const std::vector<std::string>& tokens) -> void;
  auto handle_setoption(const std::vector<std::string>& tokens) -> void;
  auto start_search(const tuna::search::search_limits& limits) -> void;
  auto send_info(const tuna::search::search_result& result) -> void;
  auto send_bestmove(const tuna::search::search_result& result) -> void;
};

auto run() -> void;

}