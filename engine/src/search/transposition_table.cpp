#include "search/transposition_table.hpp"

#include <algorithm>
#include <limits>

namespace russell::search {

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
  if(entries == 0) entries = 1;
  std::size_t buckets_needed = (entries + cluster_size - 1) / cluster_size;
  std::size_t buckets = 1;
  while(buckets < buckets_needed) {
    buckets <<= 1;
  }
  auto new_table = std::make_unique<tt_bucket[]>(buckets);
  for(std::size_t i = 0; i < buckets; ++i) {
    for(auto& e : new_table[i].entries) {
      e.version.store(0, std::memory_order_relaxed);
      e.key = 0;
      e.move_packed = 0;
      e.score = 0;
      e.depth = 0;
      e.bound = 0;
      e.generation = 0;
    }
  }
  table_ = std::move(new_table);
  bucket_count_ = buckets;
  mask_ = buckets - 1;
  generation_.store(0, std::memory_order_relaxed);
}

auto transposition_table::clear() noexcept -> void
{
  for(std::size_t i = 0; i < bucket_count_; ++i) {
    auto& b = table_[i];
    for(auto& e : b.entries) {
      e.version.store(0, std::memory_order_relaxed);
      e.key = 0;
      e.move_packed = 0;
      e.score = 0;
      e.depth = 0;
      e.bound = 0;
      e.generation = 0;
    }
  }
}

auto transposition_table::probe(std::uint64_t key, move& best_move, int& score, int& depth,
                                tt_bound& bound) const noexcept -> bool
{
  if(bucket_count_ == 0) return false;
  const auto& bucket = table_[key & mask_];
  for(int i = 0; i < 4; ++i) {
    const auto& e = bucket.entries[i];
    for(int attempt = 0; attempt < 2; ++attempt) {
      std::uint32_t v1 = e.version.load(std::memory_order_acquire);
      if(v1 & 1u) {
        continue;
      }
      std::uint64_t k = e.key;
      std::uint32_t mp = e.move_packed;
      std::int32_t sc = e.score;
      std::int32_t d = e.depth;
      std::uint8_t b = e.bound;
      std::uint32_t v2 = e.version.load(std::memory_order_acquire);
      if(v1 != v2) {
        continue;
      }
      if(k != key) {
        break;
      }
      best_move = unpack_move(mp);
      score = sc;
      depth = d;
      bound = static_cast<tt_bound>(b);
      return true;
    }
  }
  return false;
}

auto transposition_table::store(std::uint64_t key, move best_move, int score, int depth,
                                tt_bound bound) noexcept -> void
{
  if(bucket_count_ == 0) return;
  auto& bucket = table_[key & mask_];
  std::uint8_t cur_gen = generation_.load(std::memory_order_relaxed);

  int victim = 0;
  int best_replace_score = std::numeric_limits<int>::min();
  bool found_empty = false;

  for(int i = 0; i < 4; ++i) {
    auto& e = bucket.entries[i];
    std::uint32_t v1 = e.version.load(std::memory_order_acquire);
    if(v1 & 1u) {
      continue;
    }
    std::uint64_t k = e.key;
    std::int32_t d = e.depth;
    std::uint8_t g = e.generation;
    std::uint8_t b = e.bound;
    std::uint32_t v2 = e.version.load(std::memory_order_acquire);
    if(v1 != v2) {
      continue;
    }
    if(k == 0) {
      victim = i;
      found_empty = true;
      break;
    }
    std::uint8_t age = static_cast<std::uint8_t>(cur_gen - g);
    int replace_score = static_cast<int>(age) * 32 - static_cast<int>(d);
    if(b == static_cast<std::uint8_t>(tt_bound::exact)) {
      replace_score -= 1;
    }
    if(replace_score > best_replace_score) {
      best_replace_score = replace_score;
      victim = i;
    }
  }

  if(!found_empty && best_replace_score == std::numeric_limits<int>::min()) {
    victim = 0;
  }

  auto& e = bucket.entries[victim];
  std::uint32_t v = e.version.fetch_add(1, std::memory_order_acquire);

  e.key = key;
  e.move_packed = pack_move(best_move);
  e.score = score;
  e.depth = depth;
  e.bound = static_cast<std::uint8_t>(bound);
  e.generation = cur_gen;
  e.version.store(v + 2, std::memory_order_release);
}

auto transposition_table::size() const noexcept -> std::size_t
{
  return bucket_count_ * cluster_size;
}

auto transposition_table::new_generation() noexcept -> void
{
  std::uint8_t g = generation_.load(std::memory_order_relaxed);
  std::uint8_t ng = static_cast<std::uint8_t>(g + 1);
  if(ng == 0) ng = 1;
  generation_.store(ng, std::memory_order_relaxed);
}

auto transposition_table::generation() const noexcept -> std::uint8_t
{
  return generation_.load(std::memory_order_relaxed);
}

auto transposition_table::hashfull() const noexcept -> int
{
  if(bucket_count_ == 0) return 0;
  std::uint8_t cur = generation_.load(std::memory_order_relaxed);
  std::size_t sample = std::min<std::size_t>(bucket_count_, 1000);
  std::size_t count = 0;
  for(std::size_t i = 0; i < sample; ++i) {
    const auto& b = table_[i];
    for(int j = 0; j < 4; ++j) {
      const auto& e = b.entries[j];
      std::uint32_t v1 = e.version.load(std::memory_order_acquire);
      if(v1 & 1u) continue;
      std::uint64_t k = e.key;
      std::uint8_t g = e.generation;
      std::uint32_t v2 = e.version.load(std::memory_order_acquire);
      if(v1 != v2) continue;
      if(k != 0 && g == cur) ++count;
    }
  }
  std::size_t total = sample * cluster_size;
  return static_cast<int>((count * 1000) / total);
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