#include "search/transposition_table.hpp"

#include <algorithm>

namespace tuna::search {

namespace {

auto pack_move(move mv) noexcept -> std::uint32_t
{
  return static_cast<std::uint32_t>(mv.from)
         | (static_cast<std::uint32_t>(mv.to) << 6)
         | (static_cast<std::uint32_t>(mv.promotion) << 12)
         | (static_cast<std::uint32_t>(mv.flag) << 15);
}

auto unpack_move(std::uint32_t packed) noexcept -> move
{
  return move{static_cast<std::uint8_t>(packed & 0x3f),
              static_cast<std::uint8_t>((packed >> 6) & 0x3f),
              static_cast<piece_type>((packed >> 12) & 0x7),
              static_cast<move_flag>((packed >> 15) & 0x7)};
}

}

transposition_table::transposition_table(std::size_t entries)
{
  resize(entries);
}

auto transposition_table::resize(std::size_t entries) noexcept -> void
{
  auto size = std::size_t{1};
  while(size < entries) {
    size <<= 1;
  }
  table_ = std::vector<tt_entry>(size);
  mask_ = size - 1;
}

auto transposition_table::clear() noexcept -> void
{
  for(auto& entry : table_) {
    entry.key.store(0, std::memory_order_relaxed);
    entry.move_packed.store(0, std::memory_order_relaxed);
    entry.score.store(0, std::memory_order_relaxed);
    entry.depth.store(0, std::memory_order_relaxed);
    entry.bound.store(0, std::memory_order_relaxed);
  }
}

auto transposition_table::probe(std::uint64_t key, move& best_move, int& score, int& depth,
                                tt_bound& bound) const noexcept -> bool
{
  const auto& entry = table_[key & mask_];
  if(entry.key.load(std::memory_order_acquire) != key) {
    return false;
  }
  best_move = unpack_move(entry.move_packed.load(std::memory_order_relaxed));
  score = entry.score.load(std::memory_order_relaxed);
  depth = entry.depth.load(std::memory_order_relaxed);
  bound = static_cast<tt_bound>(entry.bound.load(std::memory_order_relaxed));
  return true;
}

auto transposition_table::store(std::uint64_t key, move best_move, int score, int depth,
                                tt_bound bound) noexcept -> void
{
  auto& entry = table_[key & mask_];
  entry.move_packed.store(pack_move(best_move), std::memory_order_relaxed);
  entry.score.store(score, std::memory_order_relaxed);
  entry.depth.store(depth, std::memory_order_relaxed);
  entry.bound.store(static_cast<std::uint8_t>(bound), std::memory_order_relaxed);
  entry.key.store(key, std::memory_order_release);
}

auto transposition_table::size() const noexcept -> std::size_t
{
  return table_.size();
}

auto store_value(int score, int ply) noexcept -> int
{
  if(score > mate_score_threshold) {
    return score + ply;
  }
  if(score < -mate_score_threshold) {
    return score - ply;
  }
  return score;
}

auto read_value(int score, int ply) noexcept -> int
{
  if(score > mate_score_threshold) {
    return score - ply;
  }
  if(score < -mate_score_threshold) {
    return score + ply;
  }
  return score;
}

}