#pragma once

#include "core/position.hpp"
#include "core/types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tuna::train {
struct float_net;
}

namespace tuna::eval::nnue {
class network;
}

namespace tuna::train {

[[nodiscard]] auto quantize(const float_net& f) -> tuna::eval::nnue::network;
}

namespace tuna::eval::nnue {

constexpr auto ft_size = 256;
constexpr auto l1_size = 32;
constexpr auto l2_size = 32;
constexpr auto piece_encodings = 10;
constexpr auto half_feature_space = 64 * piece_encodings * 64;
constexpr auto feature_space = 2 * half_feature_space;
constexpr auto input_dims = 2 * ft_size;

constexpr auto weight_scale_bits = 6;
constexpr auto output_scale = 16;

[[nodiscard]] constexpr auto piece_encoding(color c, piece_type pt) noexcept -> int
{
  const auto type = [](piece_type p) -> int {
    switch(p) {
    case piece_type::pawn: return 0;
    case piece_type::knight: return 1;
    case piece_type::bishop: return 2;
    case piece_type::rook: return 3;
    case piece_type::queen: return 4;
    default: return -1;
    }
  };
  return color_index(c) * 5 + type(pt);
}

class network {
public:
  network() = default;

  [[nodiscard]] auto load(const std::string& path) -> bool;

  [[nodiscard]] auto save(const std::string& path) const -> bool;

  static auto make_random(std::uint64_t seed) -> network;

  [[nodiscard]] auto empty() const noexcept -> bool { return w1_.empty(); }
  [[nodiscard]] auto feature_space_size() const noexcept -> int { return feature_space; }

  [[nodiscard]] auto w1() const noexcept -> const std::vector<std::int8_t>& { return w1_; }
  [[nodiscard]] auto bias1() const noexcept -> const std::vector<std::int16_t>& { return bias1_; }
  [[nodiscard]] auto w2() const noexcept -> const std::vector<std::int8_t>& { return w2_; }
  [[nodiscard]] auto bias2() const noexcept -> const std::vector<std::int16_t>& { return bias2_; }
  [[nodiscard]] auto w3() const noexcept -> const std::vector<std::int8_t>& { return w3_; }
  [[nodiscard]] auto bias3() const noexcept -> const std::vector<std::int16_t>& { return bias3_; }
  [[nodiscard]] auto w4() const noexcept -> const std::vector<std::int8_t>& { return w4_; }
  [[nodiscard]] auto bias4() const noexcept -> const std::vector<std::int16_t>& { return bias4_; }

  friend class evaluator;
  friend auto forward(const network& net, const std::array<std::int16_t, ft_size>& own,
                      const std::array<std::int16_t, ft_size>& enemy) -> int;
  friend auto reference_accumulate(const network& net, const position& pos,
                                   std::array<std::int16_t, ft_size>& own,
                                   std::array<std::int16_t, ft_size>& enemy) -> bool;

  friend auto tuna::train::quantize(const tuna::train::float_net&) -> network;

private:

  std::vector<std::int8_t> w1_;
  std::vector<std::int16_t> bias1_;
  std::vector<std::int8_t> w2_;
  std::vector<std::int16_t> bias2_;
  std::vector<std::int8_t> w3_;
  std::vector<std::int16_t> bias3_;
  std::vector<std::int8_t> w4_;
  std::vector<std::int16_t> bias4_;
};

[[nodiscard]] auto forward(const network& net, const std::array<std::int16_t, ft_size>& own,
                           const std::array<std::int16_t, ft_size>& enemy) -> int;

[[nodiscard]] auto reference_accumulate(const network& net, const position& pos,
                                        std::array<std::int16_t, ft_size>& own,
                                        std::array<std::int16_t, ft_size>& enemy) -> bool;

[[nodiscard]] auto evaluate(const network& net, const position& pos) -> int;

class evaluator {
public:
  explicit evaluator(const network& net) noexcept;

  auto refresh(const position& pos) -> void;

  auto make_move(const position& after, move mv, const move_state& st) -> void;

  auto unmake_move() -> void;

  auto make_null_move(const position& after) -> void;

  [[nodiscard]] auto evaluate(const position& pos) -> int;

  [[nodiscard]] auto acc() const noexcept
      -> const std::array<std::array<std::int16_t, ft_size>, 4>&
  {
    return acc_;
  }

  [[nodiscard]] auto ready() const noexcept -> bool { return ready_; }

private:
  const network& net_;
  std::array<std::array<std::int16_t, ft_size>, 4> acc_{};
  int wk_ = no_square;
  int bk_ = no_square;

  std::uint64_t sig_ = 0;
  bool ready_ = false;

  struct undo {
    std::array<std::array<std::int16_t, ft_size>, 4> acc;
    int wk = no_square;
    int bk = no_square;
    std::uint64_t sig = 0;
    bool ready = false;
  };
  std::vector<undo> undo_;

  auto apply_piece(piece p, int sq, int sign) -> void;
  auto fresh_accumulate(const position& pos) -> bool;
};

auto set_active(std::shared_ptr<const network> net) -> void;
[[nodiscard]] auto active() -> std::shared_ptr<const network>;

}