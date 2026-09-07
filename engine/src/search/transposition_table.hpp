#pragma once

#include "core/move.hpp"
#include "core/types.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace tuna::search {

constexpr auto mate_score_threshold = 29000;

enum class tt_bound : std::uint8_t {
  none,
  exact,
  lower,
  upper
};

struct tt_entry {
  std::atomic<std::uint64_t> key{0};
  std::atomic<std::uint32_t> move_packed{0};
  std::atomic<std::int32_t> score{0};
  std::atomic<std::int32_t> depth{0};
  std::atomic<std::uint8_t> bound{0};
};

class transposition_table {
public:
  transposition_table() : transposition_table(default_entries) {}
  explicit transposition_table(std::size_t entries);

  auto resize(std::size_t entries) noexcept -> void;
  auto clear() noexcept -> void;

  [[nodiscard]] auto probe(std::uint64_t key, move& best_move, int& score, int& depth,
                           tt_bound& bound) const noexcept -> bool;
  auto store(std::uint64_t key, move best_move, int score, int depth, tt_bound bound) noexcept -> void;

  [[nodiscard]] auto size() const noexcept -> std::size_t;

private:
  static constexpr auto default_entries = std::size_t{1} << 20;

  std::vector<tt_entry> table_{};
  std::size_t mask_ = 0;
};

[[nodiscard]] auto store_value(int score, int ply) noexcept -> int;
[[nodiscard]] auto read_value(int score, int ply) noexcept -> int;

}