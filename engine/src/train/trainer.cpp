#include "train/trainer.hpp"

#include "eval/evaluate.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace tuna::train {

namespace {

using namespace tuna::eval::nnue;

constexpr auto king_stride = 640;
constexpr auto square_stride = 64;
constexpr auto mirror = 56;
constexpr auto hidden_shift = 64.0f;
constexpr auto hidden_clamp = 127 * 64;
constexpr auto output_divisor = 16.0f;

auto king_square(const position& pos, color c) noexcept -> int
{
  const auto bb = pos.pieces(c, piece_type::king);
  return bb == 0 ? no_square : static_cast<int>(std::countr_zero(bb));
}

auto mirrored(const position& pos) -> position
{
  auto out = position::empty();
  for(auto c = 0; c < color_count; ++c) {
    for(auto pt = 0; pt < piece_type_count; ++pt) {
      auto bb = pos.pieces(static_cast<color>(c), static_cast<piece_type>(pt));
      while(bb != 0) {
        const auto sq = static_cast<int>(std::countr_zero(bb));
        bb &= bb - 1;
        out.set_piece(opposite(static_cast<color>(c)), static_cast<piece_type>(pt), sq ^ mirror);
      }
    }
  }
  out.set_side_to_move(opposite(pos.side_to_move()));
  return out;
}

auto score_target(const datagen::dataset_record& record, float score_cap, float loss_scale)
    -> float
{
  const auto clamped = std::clamp(static_cast<float>(record.score), -score_cap, score_cap);
  return clamped / loss_scale;
}

auto to_i8(float value) noexcept -> std::int8_t
{
  return static_cast<std::int8_t>(
      std::lround(std::clamp(value, -127.0f, 127.0f)));
}

auto to_i16(float value) noexcept -> std::int16_t
{
  return static_cast<std::int16_t>(
      std::lround(std::clamp(value, -32768.0f, 32767.0f)));
}

struct rng {
  std::uint64_t state = 0;
  explicit rng(std::uint64_t s) noexcept : state(s | std::uint64_t{1}) {}
  auto next() noexcept -> std::uint64_t
  {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }

  auto uniform(float spread) -> float
  {
    const auto unit = static_cast<float>((next() >> 40) & 0x7FFFFF) / 8388607.0f;
    return (unit * 2.0f - 1.0f) * spread;
  }
};

struct activations {
  std::array<float, input_dims> x0{};
  std::array<float, l1_size> z1{};
  std::array<float, l1_size> h1{};
  std::array<float, l2_size> z2{};
  std::array<float, l2_size> h2{};
  std::array<bool, input_dims> g0{};
  std::array<bool, l1_size> g1{};
  std::array<bool, l2_size> g2{};
  float y = 0.0f;
};

struct sample {
  features feat;
  float target = 0.0f;
};

struct gradients {
  std::vector<float> w1;
  std::vector<float> bias1;
  std::vector<float> w2;
  std::vector<float> bias2;
  std::vector<float> w3;
  std::vector<float> bias3;
  std::vector<float> w4;
  std::vector<float> bias4;
};

auto make_gradients(const float_net& net) -> gradients
{
  return gradients{
      std::vector<float>(net.w1.size(), 0.0f),
      std::vector<float>(net.bias1.size(), 0.0f),
      std::vector<float>(net.w2.size(), 0.0f),
      std::vector<float>(net.bias2.size(), 0.0f),
      std::vector<float>(net.w3.size(), 0.0f),
      std::vector<float>(net.bias3.size(), 0.0f),
      std::vector<float>(net.w4.size(), 0.0f),
      std::vector<float>(net.bias4.size(), 0.0f),
  };
}

auto zero_gradients(gradients& g) noexcept -> void
{
  std::fill(g.w1.begin(), g.w1.end(), 0.0f);
  std::fill(g.bias1.begin(), g.bias1.end(), 0.0f);
  std::fill(g.w2.begin(), g.w2.end(), 0.0f);
  std::fill(g.bias2.begin(), g.bias2.end(), 0.0f);
  std::fill(g.w3.begin(), g.w3.end(), 0.0f);
  std::fill(g.bias3.begin(), g.bias3.end(), 0.0f);
  std::fill(g.w4.begin(), g.w4.end(), 0.0f);
  std::fill(g.bias4.begin(), g.bias4.end(), 0.0f);
}

auto fake_quantize(float_net& net) noexcept -> void
{
  for(auto& x : net.w1) x = static_cast<float>(std::lround(std::clamp(x, -127.0f, 127.0f)));
  for(auto& x : net.bias1) x = static_cast<float>(std::lround(std::clamp(x, -32768.0f, 32767.0f)));
  for(auto& x : net.w2) x = static_cast<float>(std::lround(std::clamp(x, -127.0f, 127.0f)));
  for(auto& x : net.bias2) x = static_cast<float>(std::lround(std::clamp(x, -32768.0f, 32767.0f)));
  for(auto& x : net.w3) x = static_cast<float>(std::lround(std::clamp(x, -127.0f, 127.0f)));
  for(auto& x : net.bias3) x = static_cast<float>(std::lround(std::clamp(x, -32768.0f, 32767.0f)));
  for(auto& x : net.w4) x = static_cast<float>(std::lround(std::clamp(x, -127.0f, 127.0f)));
  for(auto& x : net.bias4) x = static_cast<float>(std::lround(std::clamp(x, -32768.0f, 32767.0f)));
}

struct velocities {
  std::vector<float> w1;
  std::vector<float> bias1;
  std::vector<float> w2;
  std::vector<float> bias2;
  std::vector<float> w3;
  std::vector<float> bias3;
  std::vector<float> w4;
  std::vector<float> bias4;
};

auto make_velocities(const float_net& net) -> velocities
{
  return velocities{
      std::vector<float>(net.w1.size(), 0.0f),
      std::vector<float>(net.bias1.size(), 0.0f),
      std::vector<float>(net.w2.size(), 0.0f),
      std::vector<float>(net.bias2.size(), 0.0f),
      std::vector<float>(net.w3.size(), 0.0f),
      std::vector<float>(net.bias3.size(), 0.0f),
      std::vector<float>(net.w4.size(), 0.0f),
      std::vector<float>(net.bias4.size(), 0.0f),
  };
}

struct adam_state {
  std::vector<float> m_w1, v_w1;
  std::vector<float> m_bias1, v_bias1;
  std::vector<float> m_w2, v_w2;
  std::vector<float> m_bias2, v_bias2;
  std::vector<float> m_w3, v_w3;
  std::vector<float> m_bias3, v_bias3;
  std::vector<float> m_w4, v_w4;
  std::vector<float> m_bias4, v_bias4;
  int step = 0;
};

auto make_adam_state(const float_net& net) -> adam_state
{
  return adam_state{
      std::vector<float>(net.w1.size(), 0.0f), std::vector<float>(net.w1.size(), 0.0f),
      std::vector<float>(net.bias1.size(), 0.0f), std::vector<float>(net.bias1.size(), 0.0f),
      std::vector<float>(net.w2.size(), 0.0f), std::vector<float>(net.w2.size(), 0.0f),
      std::vector<float>(net.bias2.size(), 0.0f), std::vector<float>(net.bias2.size(), 0.0f),
      std::vector<float>(net.w3.size(), 0.0f), std::vector<float>(net.w3.size(), 0.0f),
      std::vector<float>(net.bias3.size(), 0.0f), std::vector<float>(net.bias3.size(), 0.0f),
      std::vector<float>(net.w4.size(), 0.0f), std::vector<float>(net.w4.size(), 0.0f),
      std::vector<float>(net.bias4.size(), 0.0f), std::vector<float>(net.bias4.size(), 0.0f),
      0
  };
}

auto forward(const float_net& net, const sample& s, activations& a) noexcept -> void
{
  const auto& w1 = net.w1;
  for(auto i = 0; i < ft_size; ++i) {
    int acc_own = static_cast<int>(std::lround(net.bias1[static_cast<std::size_t>(i)]));
    int acc_enemy = acc_own;
    for(const auto f : s.feat.own) {
      acc_own += static_cast<int>(std::lround(w1[static_cast<std::size_t>(f) * ft_size + static_cast<std::size_t>(i)]));
    }
    for(const auto f : s.feat.enemy) {
      acc_enemy += static_cast<int>(std::lround(w1[static_cast<std::size_t>(f) * ft_size + static_cast<std::size_t>(i)]));
    }
    a.g0[static_cast<std::size_t>(i)] = acc_own > 0 && acc_own < 127;
    a.g0[static_cast<std::size_t>(ft_size + i)] = acc_enemy > 0 && acc_enemy < 127;
    a.x0[static_cast<std::size_t>(i)] = static_cast<float>(std::clamp(acc_own, 0, 127));
    a.x0[static_cast<std::size_t>(ft_size + i)] = static_cast<float>(std::clamp(acc_enemy, 0, 127));
  }

  for(auto j = 0; j < l1_size; ++j) {
    int sum = static_cast<int>(std::lround(net.bias2[static_cast<std::size_t>(j)]));
    for(auto k = 0; k < input_dims; ++k) {
      sum += static_cast<int>(std::lround(net.w2[static_cast<std::size_t>(j) * input_dims + static_cast<std::size_t>(k)]))
             * static_cast<int>(a.x0[static_cast<std::size_t>(k)]);
    }
    a.z1[static_cast<std::size_t>(j)] = static_cast<float>(sum);
    a.g1[static_cast<std::size_t>(j)] = sum > 0 && sum < hidden_clamp;
    a.h1[static_cast<std::size_t>(j)] = static_cast<float>(std::clamp(sum >> weight_scale_bits, 0, 127));
  }

  for(auto j = 0; j < l2_size; ++j) {
    int sum = static_cast<int>(std::lround(net.bias3[static_cast<std::size_t>(j)]));
    for(auto k = 0; k < l1_size; ++k) {
      sum += static_cast<int>(std::lround(net.w3[static_cast<std::size_t>(j) * l1_size + static_cast<std::size_t>(k)]))
             * static_cast<int>(a.h1[static_cast<std::size_t>(k)]);
    }
    a.z2[static_cast<std::size_t>(j)] = static_cast<float>(sum);
    a.g2[static_cast<std::size_t>(j)] = sum > 0 && sum < hidden_clamp;
    a.h2[static_cast<std::size_t>(j)] = static_cast<float>(std::clamp(sum >> weight_scale_bits, 0, 127));
  }

  int out = static_cast<int>(std::lround(net.bias4[0]));
  for(auto k = 0; k < l2_size; ++k) {
    out += static_cast<int>(std::lround(net.w4[static_cast<std::size_t>(k)])) * static_cast<int>(a.h2[static_cast<std::size_t>(k)]);
  }
  a.y = static_cast<float>(out / output_scale);
}

auto backward(const float_net& net, const sample& s, float dldy, const activations& a,
              gradients& g) noexcept -> void
{
  auto gh2 = std::array<float, l2_size>{};
  auto gh1 = std::array<float, l1_size>{};
  auto dx0 = std::array<float, input_dims>{};

  g.bias4[0] += dldy;
  for(auto j = 0; j < l2_size; ++j) {
    gh2[static_cast<std::size_t>(j)] = dldy * net.w4[static_cast<std::size_t>(j)];
    g.w4[static_cast<std::size_t>(j)] += dldy * a.h2[static_cast<std::size_t>(j)];
  }

  for(auto j = 0; j < l2_size; ++j) {
    const auto dz2 = a.g2[static_cast<std::size_t>(j)] ? gh2[static_cast<std::size_t>(j)] : 0.0f;
    g.bias3[static_cast<std::size_t>(j)] += dz2;
    for(auto m = 0; m < l1_size; ++m) {
      const auto idx = static_cast<std::size_t>(j) * l1_size + static_cast<std::size_t>(m);
      g.w3[idx] += dz2 * a.h1[static_cast<std::size_t>(m)];
      gh1[static_cast<std::size_t>(m)] += dz2 * net.w3[idx];
    }
  }

  for(auto j = 0; j < l1_size; ++j) {
    const auto dz1 = a.g1[static_cast<std::size_t>(j)] ? gh1[static_cast<std::size_t>(j)] : 0.0f;
    g.bias2[static_cast<std::size_t>(j)] += dz1;
    for(auto k = 0; k < input_dims; ++k) {
      const auto idx = static_cast<std::size_t>(j) * input_dims + static_cast<std::size_t>(k);
      g.w2[idx] += dz1 * a.x0[static_cast<std::size_t>(k)];
      dx0[static_cast<std::size_t>(k)] += dz1 * net.w2[idx];
    }
  }

  for(auto i = 0; i < ft_size; ++i) {
    const auto d_own = a.g0[static_cast<std::size_t>(i)] ? dx0[static_cast<std::size_t>(i)] : 0.0f;
    const auto d_enemy =
        a.g0[static_cast<std::size_t>(ft_size + i)] ? dx0[static_cast<std::size_t>(ft_size + i)] : 0.0f;
    g.bias1[static_cast<std::size_t>(i)] += d_own + d_enemy;
    for(const auto f : s.feat.own) {
      auto& w = g.w1[static_cast<std::size_t>(f) * ft_size + static_cast<std::size_t>(i)];
      w += d_own;
    }
    for(const auto f : s.feat.enemy) {
      auto& w = g.w1[static_cast<std::size_t>(f) * ft_size + static_cast<std::size_t>(i)];
      w += d_enemy;
    }
  }
}

auto apply_sgd(const train_options& options, float_net& net, gradients& g, velocities& v,
             std::size_t batch_count, float cur_lr) -> void
{
  const auto inv_count = 1.0f / static_cast<float>(batch_count);
  const auto mu = options.momentum;

  for(auto i = std::size_t{0}; i < net.w1.size(); ++i) {
    const auto grad = g.w1[i] * inv_count + options.weight_decay * net.w1[i];
    v.w1[i] = mu * v.w1[i] + grad;
    net.w1[i] -= cur_lr * v.w1[i];
  }
  for(auto i = std::size_t{0}; i < net.bias1.size(); ++i) {
    const auto grad = g.bias1[i] * inv_count;
    v.bias1[i] = mu * v.bias1[i] + grad;
    net.bias1[i] -= cur_lr * v.bias1[i];
  }
  for(auto i = std::size_t{0}; i < net.w2.size(); ++i) {
    const auto grad = g.w2[i] * inv_count;
    v.w2[i] = mu * v.w2[i] + grad;
    net.w2[i] -= cur_lr * v.w2[i];
  }
  for(auto i = std::size_t{0}; i < net.bias2.size(); ++i) {
    const auto grad = g.bias2[i] * inv_count;
    v.bias2[i] = mu * v.bias2[i] + grad;
    net.bias2[i] -= cur_lr * v.bias2[i];
  }
  for(auto i = std::size_t{0}; i < net.w3.size(); ++i) {
    const auto grad = g.w3[i] * inv_count;
    v.w3[i] = mu * v.w3[i] + grad;
    net.w3[i] -= cur_lr * v.w3[i];
  }
  for(auto i = std::size_t{0}; i < net.bias3.size(); ++i) {
    const auto grad = g.bias3[i] * inv_count;
    v.bias3[i] = mu * v.bias3[i] + grad;
    net.bias3[i] -= cur_lr * v.bias3[i];
  }
  for(auto i = std::size_t{0}; i < net.w4.size(); ++i) {
    const auto grad = g.w4[i] * inv_count;
    v.w4[i] = mu * v.w4[i] + grad;
    net.w4[i] -= cur_lr * v.w4[i];
  }
  for(auto i = std::size_t{0}; i < net.bias4.size(); ++i) {
    const auto grad = g.bias4[i] * inv_count;
    v.bias4[i] = mu * v.bias4[i] + grad;
    net.bias4[i] -= cur_lr * v.bias4[i];
  }

  const auto project = [](auto& vec, float lo, float hi) {
    for(auto& x : vec) x = std::clamp(x, lo, hi);
  };
  project(net.w1, -127.0f, 127.0f);
  project(net.bias1, -32768.0f, 32767.0f);
  project(net.w2, -127.0f, 127.0f);
  project(net.bias2, -32768.0f, 32767.0f);
  project(net.w3, -127.0f, 127.0f);
  project(net.bias3, -32768.0f, 32767.0f);
  project(net.w4, -127.0f, 127.0f);
  project(net.bias4, -32768.0f, 32767.0f);
}

auto apply_adamw(const train_options& options, float_net& net, gradients& g, adam_state& s,
                 std::size_t batch_count, float cur_lr) -> void
{
  const auto inv_count = 1.0f / static_cast<float>(batch_count);
  ++s.step;
  const float b1 = options.adam_beta1;
  const float b2 = options.adam_beta2;
  const float eps = options.adam_eps;
  const float wd = options.weight_decay;
  const float b1_pow = std::pow(b1, static_cast<float>(s.step));
  const float b2_pow = std::pow(b2, static_cast<float>(s.step));
  const float bc1 = 1.0f - b1_pow;
  const float bc2 = 1.0f - b2_pow;
  auto adam_update = [&](std::vector<float>& param, std::vector<float>& m, std::vector<float>& v, std::vector<float>& grad, bool apply_wd) {
    for(std::size_t i = 0; i < param.size(); ++i) {
      float g = grad[i] * inv_count;
      m[i] = b1 * m[i] + (1.0f - b1) * g;
      v[i] = b2 * v[i] + (1.0f - b2) * g * g;
      float m_hat = m[i] / bc1;
      float v_hat = v[i] / bc2;
      float step = m_hat / (std::sqrt(v_hat) + eps);
      if(apply_wd) step += wd * param[i];
      param[i] -= cur_lr * step;
    }
  };
  adam_update(net.w1, s.m_w1, s.v_w1, g.w1, true);
  adam_update(net.bias1, s.m_bias1, s.v_bias1, g.bias1, false);
  adam_update(net.w2, s.m_w2, s.v_w2, g.w2, false);
  adam_update(net.bias2, s.m_bias2, s.v_bias2, g.bias2, false);
  adam_update(net.w3, s.m_w3, s.v_w3, g.w3, false);
  adam_update(net.bias3, s.m_bias3, s.v_bias3, g.bias3, false);
  adam_update(net.w4, s.m_w4, s.v_w4, g.w4, false);
  adam_update(net.bias4, s.m_bias4, s.v_bias4, g.bias4, false);
  const auto project = [](auto& vec, float lo, float hi) {
    for(auto& x : vec) x = std::clamp(x, lo, hi);
  };
  project(net.w1, -127.0f, 127.0f);
  project(net.bias1, -32768.0f, 32767.0f);
  project(net.w2, -127.0f, 127.0f);
  project(net.bias2, -32768.0f, 32767.0f);
  project(net.w3, -127.0f, 127.0f);
  project(net.bias3, -32768.0f, 32767.0f);
  project(net.w4, -127.0f, 127.0f);
  project(net.bias4, -32768.0f, 32767.0f);
}

auto apply(const train_options& options, float_net& net, gradients& g, velocities& v, adam_state& s,
           std::size_t batch_count, float cur_lr) -> void
{
  if(options.optimizer == optimizer_type::adamw) {
    apply_adamw(options, net, g, s, batch_count, cur_lr);
  } else {
    apply_sgd(options, net, g, v, batch_count, cur_lr);
  }
}

auto worst_accumulator(const float_net& net, const std::vector<datagen::dataset_record>& records)
    -> double
{
  auto worst = 0.0;
  for(const auto& record : records) {
    const auto feat = features_of(record);
    for(auto i = 0; i < ft_size; ++i) {
      int acc_own = static_cast<int>(std::lround(net.bias1[static_cast<std::size_t>(i)]));
      int acc_enemy = acc_own;
      for(const auto f : feat.own) {
        acc_own += static_cast<int>(std::lround(net.w1[static_cast<std::size_t>(f) * ft_size + static_cast<std::size_t>(i)]));
      }
      for(const auto f : feat.enemy) {
        acc_enemy += static_cast<int>(std::lround(net.w1[static_cast<std::size_t>(f) * ft_size + static_cast<std::size_t>(i)]));
      }
      worst = std::max(worst, static_cast<double>(std::max(std::abs(acc_own), std::abs(acc_enemy))));
    }
  }
  return worst;
}

}

namespace {

auto frame_features(const position& frame_pos) -> features
{
  auto out = features{};
  out.own.reserve(18);
  out.enemy.reserve(18);
  const auto wk = king_square(frame_pos, color::white);
  const auto bk = king_square(frame_pos, color::black);
  if(wk == no_square || bk == no_square) {
    return out;
  }
  for(auto c = 0; c < color_count; ++c) {
    for(auto pt = 0; pt < piece_type_count; ++pt) {
      const auto ptype = static_cast<piece_type>(pt);
      if(ptype == piece_type::king) {
        continue;
      }
      auto bb = frame_pos.pieces(static_cast<color>(c), ptype);
      while(bb != 0) {
        const auto sq = static_cast<std::uint32_t>(std::countr_zero(bb));
        bb &= bb - 1;
        const auto enc =
            static_cast<std::uint32_t>(piece_encoding(static_cast<color>(c), ptype));
        const auto base = static_cast<std::uint32_t>(wk) * king_stride
                          + enc * square_stride + sq;
        out.own.push_back(base);
        out.enemy.push_back(half_feature_space + static_cast<std::uint32_t>(bk) * king_stride
                            + enc * square_stride + sq);
      }
    }
  }
  return out;
}

}

auto features_of(const datagen::dataset_record& record) -> features
{
  auto pos = datagen::unpack_position(record);
  if(pos.side_to_move() == color::black) {
    pos = mirrored(pos);
  }
  return frame_features(pos);
}

auto augmented_features(const datagen::dataset_record& record) -> features
{
  return frame_features(mirrored(datagen::unpack_position(record)));
}

auto make_initial_float_net(std::uint64_t seed) -> float_net
{
  auto net = float_net{};
  net.w1.resize(static_cast<std::size_t>(ft_size) * feature_space);
  net.bias1.resize(static_cast<std::size_t>(ft_size));
  net.w2.resize(static_cast<std::size_t>(l1_size) * input_dims);
  net.bias2.resize(static_cast<std::size_t>(l1_size));
  net.w3.resize(static_cast<std::size_t>(l2_size) * l1_size);
  net.bias3.resize(static_cast<std::size_t>(l2_size));
  net.w4.resize(static_cast<std::size_t>(l2_size));
  net.bias4.resize(1);

  auto rng_r = rng{seed};

  for(auto& v : net.w1) {
    v = rng_r.uniform(0.5f);
  }
  for(auto& v : net.bias1) {
    v = rng_r.uniform(24.0f);
  }
  for(auto& v : net.w2) {
    v = rng_r.uniform(2.0f);
  }
  for(auto& v : net.bias2) {
    v = 4064.0f * (rng_r.uniform(1.0f) * 0.5f + 1.0f);
  }
  for(auto& v : net.w3) {
    v = rng_r.uniform(2.0f);
  }
  for(auto& v : net.bias3) {
    v = 4064.0f * (rng_r.uniform(1.0f) * 0.5f + 1.0f);
  }
  for(auto& v : net.w4) {
    v = rng_r.uniform(2.0f);
  }
  for(auto& v : net.bias4) {
    v = rng_r.uniform(1000.0f);
  }
  return net;
}

auto quantize(const float_net& f) -> eval::nnue::network
{
  auto out = eval::nnue::network{};
  out.w1_.resize(f.w1.size());
  out.bias1_.resize(f.bias1.size());
  out.w2_.resize(f.w2.size());
  out.bias2_.resize(f.bias2.size());
  out.w3_.resize(f.w3.size());
  out.bias3_.resize(f.bias3.size());
  out.w4_.resize(f.w4.size());
  out.bias4_.resize(f.bias4.size());

  for(auto i = std::size_t{0}; i < out.w1_.size(); ++i) {
    out.w1_[i] = to_i8(f.w1[i]);
  }
  for(auto i = std::size_t{0}; i < out.bias1_.size(); ++i) {
    out.bias1_[i] = to_i16(f.bias1[i]);
  }
  for(auto i = std::size_t{0}; i < out.w2_.size(); ++i) {
    out.w2_[i] = to_i8(f.w2[i]);
  }
  for(auto i = std::size_t{0}; i < out.bias2_.size(); ++i) {
    out.bias2_[i] = to_i16(f.bias2[i]);
  }
  for(auto i = std::size_t{0}; i < out.w3_.size(); ++i) {
    out.w3_[i] = to_i8(f.w3[i]);
  }
  for(auto i = std::size_t{0}; i < out.bias3_.size(); ++i) {
    out.bias3_[i] = to_i16(f.bias3[i]);
  }
  for(auto i = std::size_t{0}; i < out.w4_.size(); ++i) {
    out.w4_[i] = to_i8(f.w4[i]);
  }
  for(auto i = std::size_t{0}; i < out.bias4_.size(); ++i) {
    out.bias4_[i] = to_i16(f.bias4[i]);
  }
  return out;
}

auto trainer_predict(const float_net& net, const datagen::dataset_record& record) -> int
{
  using namespace tuna::eval::nnue;
  const auto feat = features_of(record);
  if(feat.own.empty() && feat.enemy.empty()) return 0;
  int hidden0[input_dims]{};
  for(auto i = 0; i < ft_size; ++i) {
    int acc_own = static_cast<int>(std::lround(net.bias1[static_cast<std::size_t>(i)]));
    int acc_enemy = acc_own;
    for(const auto f : feat.own) acc_own += static_cast<int>(std::lround(net.w1[static_cast<std::size_t>(f) * ft_size + static_cast<std::size_t>(i)]));
    for(const auto f : feat.enemy) acc_enemy += static_cast<int>(std::lround(net.w1[static_cast<std::size_t>(f) * ft_size + static_cast<std::size_t>(i)]));
    hidden0[i] = std::clamp(acc_own, 0, 127);
    hidden0[ft_size + i] = std::clamp(acc_enemy, 0, 127);
  }
  int hidden1[l1_size]{};
  for(auto j = 0; j < l1_size; ++j) {
    int sum = static_cast<int>(std::lround(net.bias2[static_cast<std::size_t>(j)]));
    for(auto k = 0; k < input_dims; ++k) sum += static_cast<int>(std::lround(net.w2[static_cast<std::size_t>(j) * input_dims + static_cast<std::size_t>(k)])) * hidden0[k];
    hidden1[j] = std::clamp(sum >> weight_scale_bits, 0, 127);
  }
  int hidden2[l2_size]{};
  for(auto j = 0; j < l2_size; ++j) {
    int sum = static_cast<int>(std::lround(net.bias3[static_cast<std::size_t>(j)]));
    for(auto k = 0; k < l1_size; ++k) sum += static_cast<int>(std::lround(net.w3[static_cast<std::size_t>(j) * l1_size + static_cast<std::size_t>(k)])) * hidden1[k];
    hidden2[j] = std::clamp(sum >> weight_scale_bits, 0, 127);
  }
  int out = static_cast<int>(std::lround(net.bias4[0]));
  for(auto k = 0; k < l2_size; ++k) out += static_cast<int>(std::lround(net.w4[static_cast<std::size_t>(k)])) * hidden2[k];
  return out / output_scale;
}

auto quantize_and_dequantize(float_net& net) -> void
{
  for(auto& x : net.w1) x = static_cast<float>(std::lround(std::clamp(x, -127.0f, 127.0f)));
  for(auto& x : net.bias1) x = static_cast<float>(std::lround(std::clamp(x, -32768.0f, 32767.0f)));
  for(auto& x : net.w2) x = static_cast<float>(std::lround(std::clamp(x, -127.0f, 127.0f)));
  for(auto& x : net.bias2) x = static_cast<float>(std::lround(std::clamp(x, -32768.0f, 32767.0f)));
  for(auto& x : net.w3) x = static_cast<float>(std::lround(std::clamp(x, -127.0f, 127.0f)));
  for(auto& x : net.bias3) x = static_cast<float>(std::lround(std::clamp(x, -32768.0f, 32767.0f)));
  for(auto& x : net.w4) x = static_cast<float>(std::lround(std::clamp(x, -127.0f, 127.0f)));
  for(auto& x : net.bias4) x = static_cast<float>(std::lround(std::clamp(x, -32768.0f, 32767.0f)));
}

auto regression_mse(const eval::nnue::network& net,
                    const std::vector<datagen::dataset_record>& records, float score_cap,
                    float loss_scale) -> double
{
  auto sum = 0.0;
  for(const auto& record : records) {
    const auto pos = datagen::unpack_position(record);
    const auto prediction =
        static_cast<double>(eval::nnue::evaluate(net, pos)) / static_cast<double>(loss_scale);
    const auto target = static_cast<double>(score_target(record, score_cap, loss_scale));
    const auto error = prediction - target;
    sum += error * error;
  }
  return records.empty() ? 0.0 : sum / static_cast<double>(records.size());
}

auto classical_regression_mse(const std::vector<datagen::dataset_record>& records,
                              float score_cap, float loss_scale) -> double
{
  auto sum = 0.0;
  for(const auto& record : records) {
    const auto pos = datagen::unpack_position(record);
    auto score = eval::evaluate(pos, eval::default_weights());
    if(pos.side_to_move() == color::black) {
      score = -score;
    }
    const auto prediction = static_cast<double>(score) / static_cast<double>(loss_scale);
    const auto target = static_cast<double>(score_target(record, score_cap, loss_scale));
    const auto error = prediction - target;
    sum += error * error;
  }
  return records.empty() ? 0.0 : sum / static_cast<double>(records.size());
}

namespace {

auto score_correlation(const eval::nnue::network& net,
                       const std::vector<datagen::dataset_record>& records, float score_cap)
    -> double
{
  auto sp = 0.0, st = 0.0, spp = 0.0, stt = 0.0, spt = 0.0;
  auto count = 0.0;
  for(const auto& record : records) {
    const auto pos = datagen::unpack_position(record);
    const auto p = static_cast<double>(eval::nnue::evaluate(net, pos));
    const auto t = static_cast<double>(
        std::clamp(static_cast<float>(record.score), -score_cap, score_cap));
    sp += p;
    st += t;
    spp += p * p;
    stt += t * t;
    spt += p * t;
    count += 1.0;
  }
  if(count < 2.0) {
    return 0.0;
  }
  const auto cov = spt - sp * st / count;
  const auto var_p = spp - sp * sp / count;
  const auto var_t = stt - st * st / count;
  const auto denom = std::sqrt(var_p * var_t);
  return denom > 0.0 ? cov / denom : 0.0;
}

}

auto train_with_validation(const std::vector<datagen::dataset_record>& train_records,
                           const std::vector<datagen::dataset_record>& val_records,
                           const train_options& options, train_report& report)
    -> eval::nnue::network
{
  report = train_report{};
  report.epochs = options.epochs;
  report.epoch_loss.clear();

  auto samples = std::vector<sample>{};
  samples.reserve(train_records.size() * 2);
  for(const auto& record : train_records) {
    const auto pos = datagen::unpack_position(record);
    auto feat = features_of(record);
    if(feat.own.empty() && feat.enemy.empty()) {
      continue;
    }
    const auto target = score_target(record, options.score_cap, options.loss_scale);
    samples.push_back(sample{std::move(feat), target});
    if(options.augment) {

      const auto twin = augmented_features(record);
      const auto twin_target = pos.side_to_move() == color::white ? -target : target;
      if(!twin.own.empty() || !twin.enemy.empty()) {
        if(twin.own != samples.back().feat.own || twin.enemy != samples.back().feat.enemy) {
          samples.push_back(sample{twin, twin_target});
        }
      }
    }
  }
  report.positions = samples.size();
  if(samples.empty()) {
    return eval::nnue::network{};
  }

  auto net = make_initial_float_net(options.seed);
  const auto initial = quantize(net);
  auto grads = make_gradients(net);
  auto vels = make_velocities(net);
  auto adam = make_adam_state(net);

  auto order = std::vector<std::size_t>(samples.size());
  for(auto i = std::size_t{0}; i < order.size(); ++i) {
    order[i] = i;
  }
  auto rng_r = rng{options.seed ^ 0x9E3779B97F4A7C15ULL};

  auto acts = activations{};
  auto cur_lr = options.learning_rate;
  report.epoch_loss.reserve(static_cast<std::size_t>(options.epochs));
  report.validation_loss.reserve(static_cast<std::size_t>(options.epochs));
  for(auto epoch = 0; epoch < options.epochs; ++epoch) {
    for(auto i = std::size_t{1}; i < order.size(); ++i) {
      const auto j = static_cast<std::size_t>(rng_r.next() % (i + 1));
      std::swap(order[i], order[j]);
    }
    auto epoch_loss = 0.0;
    for(auto start = std::size_t{0}; start < order.size(); start += static_cast<std::size_t>(options.batch_size)) {
      const auto end = std::min(order.size(), start + static_cast<std::size_t>(options.batch_size));
      zero_gradients(grads);
      auto batch_loss = 0.0f;
      for(auto idx = start; idx < end; ++idx) {
        const auto& s = samples[order[idx]];
        forward(net, s, acts);
        const auto prediction = acts.y / options.loss_scale;
        const auto error = prediction - s.target;
        batch_loss += error * error;
        backward(net, s, 2.0f * error / options.loss_scale, acts, grads);
      }
      apply(options, net, grads, vels, adam, end - start, cur_lr);
      epoch_loss += batch_loss;
    }
    report.epoch_loss.push_back(epoch_loss / static_cast<double>(samples.size()));
    cur_lr *= options.learning_rate_decay;

    if(!val_records.empty()) {
      report.validation_loss.push_back(
          regression_mse(quantize(net), val_records, options.score_cap, options.loss_scale));
    }
  }

  const auto trained = quantize(net);
  if(!val_records.empty()) {
    report.validation_positions = val_records.size();
    report.initial_mse =
        regression_mse(initial, val_records, options.score_cap, options.loss_scale);
    report.final_mse = regression_mse(trained, val_records, options.score_cap, options.loss_scale);
    report.classical_mse =
        classical_regression_mse(val_records, options.score_cap, options.loss_scale);
    report.score_correlation = score_correlation(trained, val_records, options.score_cap);
    report.max_accumulator = worst_accumulator(net, val_records);
  }
  return trained;
}

auto train(const std::vector<datagen::dataset_record>& records, const train_options& options,
           train_report& report) -> eval::nnue::network
{
  if(records.empty()) {
    train_report r{};
    return train_with_validation({}, {}, options, r);
  }
  std::vector<datagen::dataset_record> train_records;
  std::vector<datagen::dataset_record> val_records;
  train_records.reserve(records.size());
  val_records.reserve(records.size());
  const std::uint64_t thresh = static_cast<std::uint64_t>(options.validate_fraction * 1000000.0);
  for(const auto& rec : records) {
    auto pos = datagen::unpack_position(rec);
    uint64_t k1 = pos.key();
    auto mir = position::empty();
    for(auto c = 0; c < color_count; ++c) {
      for(auto pt = 0; pt < piece_type_count; ++pt) {
        auto bb = pos.pieces(static_cast<color>(c), static_cast<piece_type>(pt));
        while(bb != 0) {
          int sq = static_cast<int>(std::countr_zero(bb));
          bb &= bb - 1;
          mir.set_piece(opposite(static_cast<color>(c)), static_cast<piece_type>(pt), sq ^ 56);
        }
      }
    }
    mir.set_side_to_move(opposite(pos.side_to_move()));
    uint64_t k2 = mir.key();
    uint64_t canon = std::min(k1, k2);
    canon += 0x9e3779b97f4a7c15ULL;
    canon = (canon ^ (canon >> 30)) * 0xbf58476d1ce4e5b9ULL;
    canon = (canon ^ (canon >> 27)) * 0x94d049bb133111ebULL;
    canon ^= canon >> 31;
    bool is_val = (canon % 1000000ULL) < thresh;
    if(is_val) val_records.push_back(rec);
    else train_records.push_back(rec);
  }
  if(train_records.empty() || val_records.empty()) {
    const auto vcount = static_cast<std::size_t>(static_cast<double>(records.size()) * options.validate_fraction);
    const auto split = records.size() - vcount;
    train_records.assign(records.begin(), records.begin() + split);
    val_records.assign(records.begin() + split, records.end());
  }
  return train_with_validation(train_records, val_records, options, report);
}

}