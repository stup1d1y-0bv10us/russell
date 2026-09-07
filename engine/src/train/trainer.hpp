#pragma once

#include "core/position.hpp"
#include "core/types.hpp"
#include "datagen/dataset.hpp"
#include "eval/nnue.hpp"

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace tuna::train {

struct float_net {
  std::vector<float> w1;
  std::vector<float> bias1;
  std::vector<float> w2;
  std::vector<float> bias2;
  std::vector<float> w3;
  std::vector<float> bias3;
  std::vector<float> w4;
  std::vector<float> bias4;
};

[[nodiscard]] auto make_initial_float_net(std::uint64_t seed) -> float_net;

[[nodiscard]] auto quantize(const float_net& f) -> eval::nnue::network;

struct features {
  std::vector<std::uint32_t> own;
  std::vector<std::uint32_t> enemy;
};

[[nodiscard]] auto features_of(const datagen::dataset_record& record) -> features;

[[nodiscard]] auto augmented_features(const datagen::dataset_record& record) -> features;

enum class optimizer_type { sgd, adamw };

struct train_options {
  std::uint64_t seed = 1;
  int epochs = 20;
  int batch_size = 256;
  float learning_rate = 0.05f;
  float learning_rate_decay = 0.95f;
  float weight_decay = 0.00001f;
  float momentum = 0.9f;
  optimizer_type optimizer = optimizer_type::sgd;
  float adam_beta1 = 0.9f;
  float adam_beta2 = 0.999f;
  float adam_eps = 1e-8f;
  float score_cap = 1000.0f;
  float loss_scale = 250.0f;
  double validate_fraction = 0.1;

  bool augment = true;
};

struct train_report {
  std::uint64_t positions = 0;
  std::uint64_t validation_positions = 0;
  int epochs = 0;
  std::vector<double> epoch_loss{};

  std::vector<double> validation_loss{};
  double initial_mse = 0.0;
  double final_mse = 0.0;
  double classical_mse = 0.0;
  double score_correlation = 0.0;
  double max_accumulator = 0.0;
};

[[nodiscard]] auto trainer_predict(const float_net& net, const datagen::dataset_record& record) -> int;

auto quantize_and_dequantize(float_net& net) -> void;

[[nodiscard]] auto regression_mse(const eval::nnue::network& net,
                                   const std::vector<datagen::dataset_record>& records,
                                   float score_cap, float loss_scale) -> double;
[[nodiscard]] auto classical_regression_mse(const std::vector<datagen::dataset_record>& records,
                                             float score_cap, float loss_scale) -> double;

[[nodiscard]] auto train(const std::vector<datagen::dataset_record>& records,
                         const train_options& options, train_report& report)
    -> eval::nnue::network;

[[nodiscard]] auto train_with_validation(const std::vector<datagen::dataset_record>& train_records,
                                         const std::vector<datagen::dataset_record>& val_records,
                                         const train_options& options, train_report& report)
    -> eval::nnue::network;

}