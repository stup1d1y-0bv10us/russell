#pragma once

#include "core/move.hpp"
#include "core/types.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace russell::search {

constexpr auto mate_score_threshold = 29000;

enum class tt_bound : std::uint8_t {
  none,
  exact,
  lower,
  upper
};

struct tt_entry {
  std::atomic<std::uint32_t> version{0};
  std::uint64_t key{0};
  std::uint32_t move_packed{0};
  std::int32_t score{0};
  std::int32_t depth{0};
  std::uint8_t bound{0};
  std::uint8_t generation{0};
};

struct tt_bucket {
  tt_entry entries[4];
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

  auto new_generation() noexcept -> void;
  [[nodiscard]] auto generation() const noexcept -> std::uint8_t;

  [[nodiscard]] auto hashfull() const noexcept -> int;

private:
  static constexpr auto default_entries = std::size_t{1} << 20;
  static constexpr auto cluster_size = std::size_t{4};

  std::unique_ptr<tt_bucket[]> table_{};
  std::size_t bucket_count_ = 0;
  std::size_t mask_ = 0;
  std::atomic<std::uint8_t> generation_{0};
};

[[nodiscard]] auto store_value(int score, int ply) noexcept -> int;
[[nodiscard]] auto read_value(int score, int ply) noexcept -> int;

}
