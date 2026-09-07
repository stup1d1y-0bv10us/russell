#pragma once

#include "datagen/dataset.hpp"

#include <cstdint>
#include <string>

namespace tuna::datagen {

struct selfplay_options {
  std::uint64_t seed = 0;
  int search_depth = 6;
  int score_depth = 0;

  int sample_interval = 1;
  int max_plies = 100;
  int opening_plies = 4;

  int random_temperature = 30;

  bool recording = true;

  int diversity_min_ply_gap = 2;
  int diversity_eval_threshold = 8;
  int diversity_min_non_pawn = 0;
};

struct game_result {
  std::uint64_t plies = 0;
  int outcome = 0;
  std::vector<dataset_record> positions{};
};

auto generate_selfplay(const selfplay_options& options, const std::string& path,
                       std::uint64_t game_count) -> std::uint64_t;

[[nodiscard]] auto play_one_game(const selfplay_options& options, game_result& out) -> bool;

}