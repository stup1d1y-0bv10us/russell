#pragma once

#include "core/move.hpp"
#include "core/position.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tuna::book {

struct book_entry {
  std::uint64_t key = 0;
  std::uint16_t move = 0;
  std::uint16_t weight = 0;
};

struct book_move {
  tuna::move mv{};
  std::uint16_t weight = 0;
};

[[nodiscard]] auto polyglot_key(const position& pos) noexcept -> std::uint64_t;

[[nodiscard]] auto decode_move(std::uint16_t raw) noexcept -> tuna::move;

[[nodiscard]] auto encode_move(tuna::move mv) noexcept -> std::uint16_t;

class polyglot_book {
public:

  auto load(const std::string& path) -> bool;

  auto load_builtin() -> void;

  [[nodiscard]] auto loaded() const noexcept -> bool;

  [[nodiscard]] auto moves_for(const position& pos) const -> std::vector<book_move>;

  [[nodiscard]] auto pick(const position& pos, std::uint64_t& rng_state) const -> std::optional<tuna::move>;

private:
  std::vector<book_entry> entries_{};
  bool loaded_ = false;
};

}