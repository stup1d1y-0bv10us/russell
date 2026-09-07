#pragma once

#include "core/move.hpp"
#include "core/position.hpp"
#include "core/types.hpp"

#include <string>

namespace tuna::tb {

constexpr auto loss = 0;
constexpr auto blessed_loss = 1;
constexpr auto draw = 2;
constexpr auto cursed_win = 3;
constexpr auto win = 4;

struct probe_result {
  move best_move{};
  int score = 0;
  int wdl = 0;
  int dtz = 0;
  bool has_move = false;
};

auto init(const std::string& path) -> bool;

auto unload() -> void;

[[nodiscard]] auto is_loaded() -> bool;

[[nodiscard]] auto largest() -> int;

[[nodiscard]] auto piece_count(const position& pos) -> int;

[[nodiscard]] auto probe_root(const position& pos, probe_result& out) -> bool;

[[nodiscard]] auto probe_wdl(const position& pos, int* success) -> int;

}