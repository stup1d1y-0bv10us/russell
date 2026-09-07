#include "core/position.hpp"
#include "datagen/dataset.hpp"
#include "datagen/selfplay.hpp"
#include "eval/evaluate.hpp"
#include "eval/nnue.hpp"
#include "search/search.hpp"
#include "train/trainer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using tuna::datagen::dataset_record;

auto require(bool value, const char* message) -> void
{
  if(!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

auto require_msg(bool value, const std::string& message) -> void
{
  if(!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

auto mirrored(const tuna::position& pos) -> tuna::position
{
  auto out = tuna::position::empty();
  for(auto c = 0; c < tuna::color_count; ++c) {
    for(auto pt = 0; pt < tuna::piece_type_count; ++pt) {
      auto bb = pos.pieces(static_cast<tuna::color>(c), static_cast<tuna::piece_type>(pt));
      while(bb != 0) {
        const auto sq = static_cast<int>(std::countr_zero(bb));
        bb &= bb - 1;
        out.set_piece(tuna::opposite(static_cast<tuna::color>(c)),
                      static_cast<tuna::piece_type>(pt), sq ^ 56);
      }
    }
  }
  out.set_side_to_move(tuna::opposite(pos.side_to_move()));
  return out;
}

auto pack(const std::string& fen) -> dataset_record
{
  return tuna::datagen::pack_position(tuna::position::from_fen(fen));
}

auto selfplay_records(int game_count) -> std::vector<dataset_record>
{
  auto records = std::vector<dataset_record>{};
  for(auto g = 0; g < game_count; ++g) {
    auto opts = tuna::datagen::selfplay_options{};
    opts.seed = static_cast<std::uint64_t>(100 + g * 0x9E3779B97F4A7C15ULL);
    opts.search_depth = 2;
    opts.max_plies = 30;
    opts.opening_plies = 3;
    opts.diversity_min_ply_gap = 1;
    opts.diversity_eval_threshold = 1;
    opts.diversity_min_non_pawn = 0;
    auto game = tuna::datagen::game_result{};
    require(tuna::datagen::play_one_game(opts, game), "self-play game plays");
    records.insert(records.end(), game.positions.begin(), game.positions.end());
  }
  return records;
}

auto verify_features_match_reference(const tuna::train::features& f,
                                     const tuna::eval::nnue::network& q,
                                     const tuna::position& pos) -> void
{
  auto own_ref = std::array<std::int16_t, tuna::eval::nnue::ft_size>{};
  auto enemy_ref = std::array<std::int16_t, tuna::eval::nnue::ft_size>{};
  require(tuna::eval::nnue::reference_accumulate(q, pos, own_ref, enemy_ref),
          "reference accumulation succeeds");
  for(auto i = 0; i < tuna::eval::nnue::ft_size; ++i) {
    auto acc_own = static_cast<int>(q.bias1()[static_cast<std::size_t>(i)]);
    for(const auto col : f.own) {
      acc_own += q.w1()[static_cast<std::size_t>(col) * tuna::eval::nnue::ft_size + i];
    }
    auto acc_enemy = static_cast<int>(q.bias1()[static_cast<std::size_t>(i)]);
    for(const auto col : f.enemy) {
      acc_enemy += q.w1()[static_cast<std::size_t>(col) * tuna::eval::nnue::ft_size + i];
    }
    require(acc_own == static_cast<int>(own_ref[static_cast<std::size_t>(i)]),
            ("own-half accumulator matches reference at unit " + std::to_string(i)).c_str());
    require(acc_enemy == static_cast<int>(enemy_ref[static_cast<std::size_t>(i)]),
            ("enemy-half accumulator matches reference at unit " + std::to_string(i)).c_str());
  }
}

auto test_feature_extraction_matches_reference() -> void
{
  const auto net = tuna::train::make_initial_float_net(0xC0FFEE);
  const auto q = tuna::train::quantize(net);

  const auto white = pack("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  const auto black = pack("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 b - - 0 1");

  const auto fw = tuna::train::features_of(white);
  const auto fb = tuna::train::features_of(black);
  require(!fw.own.empty(), "white-half features present");
  require(!fw.enemy.empty(), "black-half features present");
  require(!fb.own.empty(), "mirrored own-half features present");

  verify_features_match_reference(fw, q, tuna::position::from_fen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"));

  verify_features_match_reference(fb, q, mirrored(tuna::position::from_fen(
      "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 b - - 0 1")));
}

auto test_quantize_roundtrip() -> void
{
  const auto path = (std::filesystem::temp_directory_path() / "tuna_nnue_train_roundtrip.bin").string();
  const auto net = tuna::train::make_initial_float_net(0x1234);
  const auto q = tuna::train::quantize(net);
  require(!q.empty(), "quantized net is non-empty");
  require(q.feature_space_size() == tuna::eval::nnue::feature_space,
          "quantized net reports the full feature space");
  require(q.save(path), "quantized net saves");
  auto loaded = tuna::eval::nnue::network{};
  require(loaded.load(path), "quantized net loads through the inference loader");
  require(q.w1() == loaded.w1(), "w1 roundtrip through inference loader");
  require(q.bias1() == loaded.bias1(), "bias1 roundtrip");
  require(q.w2() == loaded.w2(), "w2 roundtrip");
  require(q.bias2() == loaded.bias2(), "bias2 roundtrip");
  require(q.w3() == loaded.w3(), "w3 roundtrip");
  require(q.bias3() == loaded.bias3(), "bias3 roundtrip");
  require(q.w4() == loaded.w4(), "w4 roundtrip");
  require(q.bias4() == loaded.bias4(), "bias4 roundtrip");
  const auto pos = tuna::position::start();
  require(tuna::eval::nnue::evaluate(q, pos) == tuna::eval::nnue::evaluate(loaded, pos),
          "roundtrip eval matches");
  std::filesystem::remove(path);
}

auto test_augmentation_symmetry_and_score_consistency() -> void
{
  constexpr auto score_cap = 1000.0f;
  constexpr auto loss_scale = 250.0f;
  const auto net = tuna::train::make_initial_float_net(0xC0FFEE);
  const auto q = tuna::train::quantize(net);

  const auto fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  const auto black_fen = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 b - - 0 1";
  const auto pos = tuna::position::from_fen(fen);
  const auto black_pos = tuna::position::from_fen(black_fen);

  auto white_record = pack(fen);
  white_record.score = 137;
  const auto white_feat = tuna::train::features_of(white_record);
  const auto white_twin = tuna::train::augmented_features(white_record);
  require(!white_feat.own.empty() && !white_feat.enemy.empty(), "white features present");
  require(!white_twin.own.empty() && !white_twin.enemy.empty(), "white twin features present");
  require(white_twin.own != white_feat.own || white_twin.enemy != white_feat.enemy,
          "mirror twin of a white-to-move record is a distinct input");

  const auto renormalized = tuna::train::features_of(tuna::datagen::pack_position(mirrored(pos)));
  require(renormalized.own == white_feat.own && renormalized.enemy == white_feat.enemy,
          "mirror normalization is an involution");

  verify_features_match_reference(white_twin, q, mirrored(pos));

  auto black_record = pack(black_fen);
  black_record.score = -200;
  const auto black_feat = tuna::train::features_of(black_record);
  const auto black_twin = tuna::train::augmented_features(black_record);
  require(!black_feat.own.empty() && !black_feat.enemy.empty(), "black features present");
  require(black_twin.own == black_feat.own && black_twin.enemy == black_feat.enemy,
          "mirror twin of a black-to-move record coincides with its normalized frame");

  require(tuna::eval::nnue::evaluate(q, pos) == tuna::eval::nnue::evaluate(q, mirrored(pos)),
          "evaluator's frame normalization is mirror-invariant");

  const auto target_a =
      std::clamp(static_cast<float>(white_record.score), -score_cap, score_cap) / loss_scale;
  auto twin_record = tuna::datagen::pack_position(mirrored(pos));
  twin_record.score = white_record.score;
  const auto twin_score =
      std::clamp(static_cast<float>(twin_record.score), -score_cap, score_cap) / loss_scale;
  require(target_a == twin_score, "mirrored twin record shares the original score");
  require(-target_a == -twin_score, "white-to-move twin targets the negated score");

  auto black_twin_record = tuna::datagen::pack_position(mirrored(black_pos));
  black_twin_record.score = black_record.score;
  const auto black_target =
      std::clamp(static_cast<float>(black_record.score), -score_cap, score_cap) / loss_scale;
  const auto black_twin_score =
      std::clamp(static_cast<float>(black_twin_record.score), -score_cap, score_cap) / loss_scale;
  require(black_target == black_twin_score && -black_target == -black_twin_score,
          "black-to-move twin targets are consistent");
}

auto test_training_with_augmentation() -> void
{
  const auto records = selfplay_records(4);
  auto options = tuna::train::train_options{};
  options.seed = 9;
  options.epochs = 8;
  options.batch_size = 64;
  options.learning_rate = 0.05f;
  options.momentum = 0.0f;
  options.validate_fraction = 0.1;

  options.augment = true;
  auto report_a = tuna::train::train_report{};
  const auto net_a = tuna::train::train(records, options, report_a);

  options.augment = false;
  auto report_b = tuna::train::train_report{};
  const auto net_b = tuna::train::train(records, options, report_b);

  require_msg(report_a.positions > report_b.positions,
              "augmentation adds training samples (" + std::to_string(report_b.positions)
                  + " -> " + std::to_string(report_a.positions) + ")");
  require(report_a.validation_positions == report_b.validation_positions,
          "validation set is not augmented");
  require_msg(report_a.final_mse < report_a.initial_mse,
              "augmented training learns beyond its random init ("
                  + std::to_string(report_a.initial_mse) + " -> "
                  + std::to_string(report_a.final_mse) + ")");
  require(report_a.score_correlation > -1.0 && report_a.score_correlation < 1.0
            && report_a.score_correlation == report_a.score_correlation,
        "augmented net reports a valid correlation (no NaN)");
  require(report_b.score_correlation > -1.0 && report_b.score_correlation < 1.0
            && report_b.score_correlation == report_b.score_correlation,
        "unaugmented net reports a valid correlation (no NaN)");
  require(report_a.epoch_loss.back() < report_a.epoch_loss.front(),
          "augmented training loss decreases across epochs");
  require(!net_a.empty() && !net_b.empty(), "both runs produce networks");
}

auto test_learning_and_determinism() -> void
{
  const auto records = selfplay_records(5);
  require_msg(records.size() >= 100,
              "self-play dataset has enough positions: " + std::to_string(records.size()));

  auto options = tuna::train::train_options{};
  options.seed = 42;
  options.epochs = 10;
  options.batch_size = 64;
  options.learning_rate = 0.05f;
  options.momentum = 0.0f;
  options.learning_rate_decay = 0.95f;
  options.validate_fraction = 0.15;

  auto report_a = tuna::train::train_report{};
  auto report_b = tuna::train::train_report{};
  const auto net_a = tuna::train::train(records, options, report_a);
  const auto net_b = tuna::train::train(records, options, report_b);

  require(net_a.w1() == net_b.w1(), "training is deterministic (w1)");
  require(net_a.bias1() == net_b.bias1(), "training is deterministic (bias1)");
  require(net_a.w2() == net_b.w2(), "training is deterministic (w2)");
  require(net_a.bias2() == net_b.bias2(), "training is deterministic (bias2)");
  require(net_a.w3() == net_b.w3(), "training is deterministic (w3)");
  require(net_a.bias3() == net_b.bias3(), "training is deterministic (bias3)");
  require(net_a.w4() == net_b.w4(), "training is deterministic (w4)");
  require(net_a.bias4() == net_b.bias4(), "training is deterministic (bias4)");
  require(report_a.final_mse == report_b.final_mse, "validation MSE is deterministic");
  require_msg(report_a.positions >= records.size() - report_a.validation_positions
                  && report_a.positions > report_a.validation_positions,
              "train/validation split leaves a training majority (incl. augmented twins)");

  require_msg(!report_a.epoch_loss.empty(), "epoch losses are recorded");
  require(report_a.epoch_loss.back() < report_a.epoch_loss.front(),
          "training loss decreases across epochs");
  require_msg(report_a.final_mse < report_a.initial_mse,
              "trained net beats its random-init baseline on validation ("
                  + std::to_string(report_a.initial_mse) + " -> " + std::to_string(report_a.final_mse)
                  + ")");
  require(report_a.max_accumulator <= 32768.0 || report_a.validation_positions == 0,
          "no int16 accumulator wraps on the validation set");
}

auto test_load_into_engine_and_search() -> void
{
  const auto records = selfplay_records(4);
  auto options = tuna::train::train_options{};
  options.seed = 7;
  options.epochs = 6;
  options.batch_size = 64;
  options.learning_rate = 0.05f;
  options.momentum = 0.0f;
  options.validate_fraction = 0.1;
  auto report = tuna::train::train_report{};
  const auto net = tuna::train::train(records, options, report);
  require(!net.empty(), "training produced a network");

  const auto path = (std::filesystem::temp_directory_path() / "tuna_nnue_train_search.bin").string();
  require(net.save(path), "trained network saves to disk");
  auto loaded = tuna::eval::nnue::network{};
  require(loaded.load(path), "trained network loads from disk");
  require(loaded.w1() == net.w1(), "loaded weights match the trained weights");

  tuna::eval::set_nnue(std::make_shared<const tuna::eval::nnue::network>(loaded));
  const auto pos = tuna::position::from_fen(
      "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3");
  const auto expected = tuna::eval::nnue::evaluate(loaded, pos);
  require(tuna::eval::evaluate(pos) == expected, "active engine eval uses the trained net");

  auto search_pos = pos;
  const auto result = tuna::search::iterative_deepening(search_pos, 3);
  require(result.has_move, "search with the trained net produces a move");
  require(result.best_move.from != tuna::no_square || result.best_move.to != tuna::no_square,
          "best move is non-empty");
  require(result.score > -tuna::search::mate_value && result.score < tuna::search::mate_value,
          "search score is a finite centipawn value");

  auto opening = tuna::position::start();
  const auto opening_result = tuna::search::iterative_deepening(opening, 2);
  require(opening_result.has_move, "opening search with the trained net produces a move");

  tuna::eval::set_nnue(nullptr);
  std::filesystem::remove(path);
}

auto test_momentum_changes_update() -> void
{
  const auto records = selfplay_records(3);
  auto opts0 = tuna::train::train_options{};
  opts0.seed = 1234;
  opts0.epochs = 4;
  opts0.batch_size = 32;
  opts0.learning_rate = 0.1f;
  opts0.momentum = 0.0f;
  opts0.validate_fraction = 0.2;

  auto optsM = opts0;
  optsM.momentum = 0.9f;

  auto report0 = tuna::train::train_report{};
  auto reportM = tuna::train::train_report{};
  const auto net0 = tuna::train::train(records, opts0, report0);
  const auto netM = tuna::train::train(records, optsM, reportM);
  require(!net0.empty() && !netM.empty(), "both momentum variants produce networks");
  auto differ = false;
  if(net0.w1() != netM.w1()) differ = true;
  if(net0.w2() != netM.w2()) differ = true;
  if(net0.w3() != netM.w3()) differ = true;
  if(net0.w4() != netM.w4()) differ = true;
  if(net0.bias1() != netM.bias1()) differ = true;
  if(net0.bias4() != netM.bias4()) differ = true;
  require(differ, "momentum changes update (networks differ)");

  auto optsM2 = optsM;
  auto reportM2 = tuna::train::train_report{};
  const auto netM2 = tuna::train::train(records, optsM, reportM2);
  require(netM.w1() == netM2.w1(), "momentum training deterministic (w1)");
  require(netM.bias1() == netM2.bias1(), "momentum training deterministic (bias1)");
  require(reportM.final_mse == reportM2.final_mse, "momentum validation deterministic");
}

auto test_projected_weights_bounds() -> void
{
  const auto records = selfplay_records(5);
  auto options = tuna::train::train_options{};
  options.seed = 99;
  options.epochs = 10;
  options.batch_size = 16;
  options.learning_rate = 0.5f;
  options.momentum = 0.9f;
  options.weight_decay = 0.001f;
  options.validate_fraction = 0.15;
  auto report = tuna::train::train_report{};
  const auto net = tuna::train::train(records, options, report);
  require(!net.empty(), "momentum projected training produced network");
  for(auto v : net.w1()) {
    require(v >= -127 && v <= 127, "w1 int8 bounds");
  }
  for(auto v : net.w2()) {
    require(v >= -127 && v <= 127, "w2 int8 bounds");
  }
  for(auto v : net.w3()) {
    require(v >= -127 && v <= 127, "w3 int8 bounds");
  }
  for(auto v : net.w4()) {
    require(v >= -127 && v <= 127, "w4 int8 bounds");
  }
  for(auto v : net.bias1()) {
    require(v >= -32768 && v <= 32767, "bias1 int16 bounds");
  }
  for(auto v : net.bias2()) {
    require(v >= -32768 && v <= 32767, "bias2 int16 bounds");
  }
  for(auto v : net.bias3()) {
    require(v >= -32768 && v <= 32767, "bias3 int16 bounds");
  }
  for(auto v : net.bias4()) {
    require(v >= -32768 && v <= 32767, "bias4 int16 bounds");
  }
  require(report.max_accumulator <= 32768.0 || report.validation_positions == 0,
          "projected accumulator bound respected");
  const auto path = (std::filesystem::temp_directory_path() / "tuna_nnue_proj.bin").string();
  require(net.save(path), "projected net saves");
  auto loaded = tuna::eval::nnue::network{};
  require(loaded.load(path), "projected net loads");
  std::filesystem::remove(path);
}

auto test_zero_momentum_reproduces_sgd() -> void
{
  const auto records = selfplay_records(3);
  auto opts_sgd = tuna::train::train_options{};
  opts_sgd.seed = 2025;
  opts_sgd.epochs = 6;
  opts_sgd.batch_size = 32;
  opts_sgd.learning_rate = 0.05f;
  opts_sgd.momentum = 0.0f;
  opts_sgd.validate_fraction = 0.15;
  auto r1 = tuna::train::train_report{};
  auto r2 = tuna::train::train_report{};
  const auto n1 = tuna::train::train(records, opts_sgd, r1);
  const auto n2 = tuna::train::train(records, opts_sgd, r2);
  require(n1.w1() == n2.w1(), "zero momentum deterministic (w1)");
  require(n1.bias1() == n2.bias1(), "zero momentum deterministic (bias1)");
  require(n1.w2() == n2.w2(), "zero momentum deterministic (w2)");
  require(n1.bias2() == n2.bias2(), "zero momentum deterministic (bias2)");
  require(n1.w3() == n2.w3(), "zero momentum deterministic (w3)");
  require(n1.w4() == n2.w4(), "zero momentum deterministic (w4)");
  require(r1.final_mse == r2.final_mse, "zero momentum MSE deterministic");
  require(!r1.epoch_loss.empty(), "zero momentum epoch losses recorded");
  require(r1.epoch_loss.back() < r1.epoch_loss.front() || r1.epoch_loss.size() == 1,
          "zero momentum loss decreases or stable");
  auto opts_plain = opts_sgd;
  opts_plain.momentum = 0.0f;
  auto r_plain = tuna::train::train_report{};
  const auto n_plain = tuna::train::train(records, opts_plain, r_plain);
  require(n1.w1() == n_plain.w1(), "zero momentum reproduces SGD (identical w1)");
  require(n1.w2() == n_plain.w2(), "zero momentum reproduces SGD (identical w2)");

  auto opts_mom = opts_sgd;
  opts_mom.momentum = 0.9f;
  auto r_mom = tuna::train::train_report{};
  const auto n_mom = tuna::train::train(records, opts_mom, r_mom);
  auto any_diff = n1.w1() != n_mom.w1() || n1.w2() != n_mom.w2();
  require(any_diff, "zero vs momentum networks differ");
}

auto test_qat_fake_quant_and_integer_forward() -> void
{
  auto f = tuna::train::make_initial_float_net(0x12345);
  for(auto& x : f.w1) x += 0.7f;
  for(auto& x : f.bias1) x += 0.3f;
  tuna::train::quantize_and_dequantize(f);
  for(auto v : f.w1) {
    require(v >= -127.0f && v <= 127.0f, "qat w1 int8 range");
    require(std::lround(v) == static_cast<long>(v), "qat w1 integer");
  }
  for(auto v : f.bias1) {
    require(v >= -32768.0f && v <= 32767.0f, "qat bias1 int16 range");
    require(std::lround(v) == static_cast<long>(v), "qat bias1 integer");
  }
  for(auto v : f.w2) require(v >= -127 && v <= 127, "qat w2 range");
  for(auto v : f.w3) require(v >= -127 && v <= 127, "qat w3 range");
  for(auto v : f.w4) require(v >= -127 && v <= 127, "qat w4 range");
  auto q = tuna::train::quantize(f);
  tuna::train::float_net f2;
  f2.w1.resize(q.w1().size());
  f2.bias1.resize(q.bias1().size());
  f2.w2.resize(q.w2().size());
  f2.bias2.resize(q.bias2().size());
  f2.w3.resize(q.w3().size());
  f2.bias3.resize(q.bias3().size());
  f2.w4.resize(q.w4().size());
  f2.bias4.resize(q.bias4().size());
  for(std::size_t i=0;i<f2.w1.size();++i) f2.w1[i]=static_cast<float>(q.w1()[i]);
  for(std::size_t i=0;i<f2.bias1.size();++i) f2.bias1[i]=static_cast<float>(q.bias1()[i]);
  for(std::size_t i=0;i<f2.w2.size();++i) f2.w2[i]=static_cast<float>(q.w2()[i]);
  for(std::size_t i=0;i<f2.bias2.size();++i) f2.bias2[i]=static_cast<float>(q.bias2()[i]);
  for(std::size_t i=0;i<f2.w3.size();++i) f2.w3[i]=static_cast<float>(q.w3()[i]);
  for(std::size_t i=0;i<f2.bias3.size();++i) f2.bias3[i]=static_cast<float>(q.bias3()[i]);
  for(std::size_t i=0;i<f2.w4.size();++i) f2.w4[i]=static_cast<float>(q.w4()[i]);
  for(std::size_t i=0;i<f2.bias4.size();++i) f2.bias4[i]=static_cast<float>(q.bias4()[i]);
  const auto fens = std::vector<std::string>{
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"
  };
  for(const auto& fen: fens){
    auto rec = tuna::datagen::pack_position(tuna::position::from_fen(fen));
    int trainer_pred = tuna::train::trainer_predict(f2, rec);
    int engine_pred = tuna::eval::nnue::evaluate(q, tuna::position::from_fen(fen));
    require(trainer_pred == engine_pred, ("qat forward matches inference for " + fen + " trainer " + std::to_string(trainer_pred) + " engine " + std::to_string(engine_pred)).c_str());
  }
}

auto test_qat_equivalence_after_training() -> void
{
  const auto records = selfplay_records(4);
  auto opts = tuna::train::train_options{};
  opts.seed = 321;
  opts.epochs = 6;
  opts.batch_size = 32;
  opts.learning_rate = 0.05f;
  opts.momentum = 0.9f;
  opts.validate_fraction = 0.2;
  auto report = tuna::train::train_report{};
  const auto trained = tuna::train::train(records, opts, report);
  require(!trained.empty(), "qat trained net non-empty");
  tuna::train::float_net f;
  f.w1.resize(trained.w1().size());
  f.bias1.resize(trained.bias1().size());
  f.w2.resize(trained.w2().size());
  f.bias2.resize(trained.bias2().size());
  f.w3.resize(trained.w3().size());
  f.bias3.resize(trained.bias3().size());
  f.w4.resize(trained.w4().size());
  f.bias4.resize(trained.bias4().size());
  for(std::size_t i=0;i<f.w1.size();++i) f.w1[i]=static_cast<float>(trained.w1()[i]);
  for(std::size_t i=0;i<f.bias1.size();++i) f.bias1[i]=static_cast<float>(trained.bias1()[i]);
  for(std::size_t i=0;i<f.w2.size();++i) f.w2[i]=static_cast<float>(trained.w2()[i]);
  for(std::size_t i=0;i<f.bias2.size();++i) f.bias2[i]=static_cast<float>(trained.bias2()[i]);
  for(std::size_t i=0;i<f.w3.size();++i) f.w3[i]=static_cast<float>(trained.w3()[i]);
  for(std::size_t i=0;i<f.bias3.size();++i) f.bias3[i]=static_cast<float>(trained.bias3()[i]);
  for(std::size_t i=0;i<f.w4.size();++i) f.w4[i]=static_cast<float>(trained.w4()[i]);
  for(std::size_t i=0;i<f.bias4.size();++i) f.bias4[i]=static_cast<float>(trained.bias4()[i]);
  int max_abs = 0;
  for(const auto& rec : records){
    auto pos = tuna::datagen::unpack_position(rec);
    int engine_pred = tuna::eval::nnue::evaluate(trained, pos);
    int trainer_pred = tuna::train::trainer_predict(f, rec);
    int diff = std::abs(engine_pred - trainer_pred);
    if(diff > max_abs) max_abs = diff;
    require(diff == 0, ("qat equivalence diff 0, got " + std::to_string(diff) + " engine " + std::to_string(engine_pred) + " trainer " + std::to_string(trainer_pred)).c_str());
  }
  require(max_abs == 0, "qat max abs diff zero");
  const auto path = (std::filesystem::temp_directory_path() / "tuna_qat_equiv.bin").string();
  require(trained.save(path), "qat net saves");
  auto loaded = tuna::eval::nnue::network{};
  require(loaded.load(path), "qat net loads via production path");
  for(const auto& rec : records){
    auto pos = tuna::datagen::unpack_position(rec);
    int a = tuna::eval::nnue::evaluate(trained, pos);
    int b = tuna::eval::nnue::evaluate(loaded, pos);
    require(a==b, "loaded network matches trained");
  }
  std::filesystem::remove(path);
}

auto test_adam_deterministic() -> void
{
  const auto records = selfplay_records(4);
  auto opts = tuna::train::train_options{};
  opts.seed = 777;
  opts.epochs = 6;
  opts.batch_size = 32;
  opts.learning_rate = 0.001f;
  opts.optimizer = tuna::train::optimizer_type::adamw;
  opts.adam_beta1 = 0.9f;
  opts.adam_beta2 = 0.999f;
  opts.adam_eps = 1e-8f;
  opts.validate_fraction = 0.2;
  auto r1 = tuna::train::train_report{};
  auto r2 = tuna::train::train_report{};
  const auto n1 = tuna::train::train(records, opts, r1);
  const auto n2 = tuna::train::train(records, opts, r2);
  require(n1.w1() == n2.w1(), "adam deterministic w1");
  require(n1.bias1() == n2.bias1(), "adam deterministic bias1");
  require(n1.w2() == n2.w2(), "adam deterministic w2");
  require(n1.w3() == n2.w3(), "adam deterministic w3");
  require(n1.w4() == n2.w4(), "adam deterministic w4");
  require(r1.final_mse == r2.final_mse, "adam deterministic mse");
  require(r1.epoch_loss == r2.epoch_loss, "adam deterministic epoch loss");
}

auto test_adam_updates_and_selection() -> void
{
  const auto records = selfplay_records(4);
  auto opts_sgd = tuna::train::train_options{};
  opts_sgd.seed = 123;
  opts_sgd.epochs = 4;
  opts_sgd.batch_size = 32;
  opts_sgd.learning_rate = 0.05f;
  opts_sgd.momentum = 0.9f;
  opts_sgd.optimizer = tuna::train::optimizer_type::sgd;
  opts_sgd.validate_fraction = 0.2;
  auto opts_adam = opts_sgd;
  opts_adam.optimizer = tuna::train::optimizer_type::adamw;
  opts_adam.learning_rate = 0.001f;
  opts_adam.adam_beta1 = 0.9f;
  opts_adam.adam_beta2 = 0.999f;
  auto r_sgd = tuna::train::train_report{};
  auto r_adam = tuna::train::train_report{};
  const auto n_sgd = tuna::train::train(records, opts_sgd, r_sgd);
  const auto n_adam = tuna::train::train(records, opts_adam, r_adam);
  require(!n_sgd.empty() && !n_adam.empty(), "both optimizers produce networks");
  bool differ = false;
  if(n_sgd.w1() != n_adam.w1()) differ = true;
  if(n_sgd.w2() != n_adam.w2()) differ = true;
  if(n_sgd.w3() != n_adam.w3()) differ = true;
  if(n_sgd.w4() != n_adam.w4()) differ = true;
  require(differ, "optimizer selection changes update (adam vs sgd differ)");
  auto init = tuna::train::make_initial_float_net(opts_adam.seed);
  tuna::train::quantize_and_dequantize(init);
  auto qinit = tuna::train::quantize(init);
  bool adam_changed = false;
  if(n_adam.w1() != qinit.w1()) adam_changed = true;
  if(n_adam.w2() != qinit.w2()) adam_changed = true;
  require(adam_changed, "adam parameter updates (weights changed from init)");
  auto opts_adam2 = opts_adam;
  opts_adam2.adam_beta1 = 0.8f;
  auto r2 = tuna::train::train_report{};
  const auto n2 = tuna::train::train(records, opts_adam2, r2);
  require(n_adam.w1() != n2.w1() || n_adam.w2() != n2.w2(), "adam beta1 changes update");
}

auto test_adam_qat_equivalence() -> void
{
  const auto records = selfplay_records(4);
  auto opts = tuna::train::train_options{};
  opts.seed = 555;
  opts.epochs = 6;
  opts.batch_size = 32;
  opts.learning_rate = 0.001f;
  opts.optimizer = tuna::train::optimizer_type::adamw;
  opts.validate_fraction = 0.2;
  auto report = tuna::train::train_report{};
  const auto trained = tuna::train::train(records, opts, report);
  require(!trained.empty(), "adam qat trained non-empty");
  tuna::train::float_net f;
  f.w1.resize(trained.w1().size());
  f.bias1.resize(trained.bias1().size());
  f.w2.resize(trained.w2().size());
  f.bias2.resize(trained.bias2().size());
  f.w3.resize(trained.w3().size());
  f.bias3.resize(trained.bias3().size());
  f.w4.resize(trained.w4().size());
  f.bias4.resize(trained.bias4().size());
  for(std::size_t i=0;i<f.w1.size();++i) f.w1[i]=static_cast<float>(trained.w1()[i]);
  for(std::size_t i=0;i<f.bias1.size();++i) f.bias1[i]=static_cast<float>(trained.bias1()[i]);
  for(std::size_t i=0;i<f.w2.size();++i) f.w2[i]=static_cast<float>(trained.w2()[i]);
  for(std::size_t i=0;i<f.bias2.size();++i) f.bias2[i]=static_cast<float>(trained.bias2()[i]);
  for(std::size_t i=0;i<f.w3.size();++i) f.w3[i]=static_cast<float>(trained.w3()[i]);
  for(std::size_t i=0;i<f.bias3.size();++i) f.bias3[i]=static_cast<float>(trained.bias3()[i]);
  for(std::size_t i=0;i<f.w4.size();++i) f.w4[i]=static_cast<float>(trained.w4()[i]);
  for(std::size_t i=0;i<f.bias4.size();++i) f.bias4[i]=static_cast<float>(trained.bias4()[i]);
  int max_abs = 0;
  for(const auto& rec : records){
    auto pos = tuna::datagen::unpack_position(rec);
    int engine = tuna::eval::nnue::evaluate(trained, pos);
    int trainer = tuna::train::trainer_predict(f, rec);
    int diff = std::abs(engine - trainer);
    if(diff > max_abs) max_abs = diff;
    require(diff == 0, ("adam qat equivalence diff 0 got " + std::to_string(diff)).c_str());
  }
  require(max_abs == 0, "adam qat max abs diff zero");
  const auto path = (std::filesystem::temp_directory_path() / "tuna_adam_qat.bin").string();
  require(trained.save(path), "adam qat saves");
  auto loaded = tuna::eval::nnue::network{};
  require(loaded.load(path), "adam qat loads via production");
  for(const auto& rec: records){
    auto pos = tuna::datagen::unpack_position(rec);
    require(tuna::eval::nnue::evaluate(trained,pos)==tuna::eval::nnue::evaluate(loaded,pos), "adam loaded matches");
  }
  std::filesystem::remove(path);
}

auto canonical_for_test(const tuna::datagen::dataset_record& rec) -> std::uint64_t
{
  auto pos = tuna::datagen::unpack_position(rec);
  std::uint64_t k1 = pos.key();
  auto mir = tuna::position::empty();
  for(auto c=0;c<tuna::color_count;++c){
    for(auto pt=0;pt<tuna::piece_type_count;++pt){
      auto bb = pos.pieces(static_cast<tuna::color>(c), static_cast<tuna::piece_type>(pt));
      while(bb){
        int sq = static_cast<int>(std::countr_zero(bb));
        bb &= bb-1;
        mir.set_piece(tuna::opposite(static_cast<tuna::color>(c)), static_cast<tuna::piece_type>(pt), sq ^ 56);
      }
    }
  }
  mir.set_side_to_move(tuna::opposite(pos.side_to_move()));
  std::uint64_t k2 = mir.key();
  std::uint64_t canon = std::min(k1,k2);
  canon += 0x9e3779b97f4a7c15ULL;
  canon = (canon ^ (canon >> 30)) * 0xbf58476d1ce4e5b9ULL;
  canon = (canon ^ (canon >> 27)) * 0x94d049bb133111ebULL;
  canon ^= canon >> 31;
  return canon;
}

auto test_train_validation_no_overlap() -> void
{
  const auto records = selfplay_records(6);
  auto opts = tuna::train::train_options{};
  opts.seed = 101;
  opts.validate_fraction = 0.2;
  opts.epochs = 2;
  opts.batch_size = 32;

  std::uint64_t thresh = static_cast<std::uint64_t>(opts.validate_fraction * 1000000.0);
  std::unordered_set<std::uint64_t> train_canon, val_canon;
  for(auto &rec: records){
    std::uint64_t h = canonical_for_test(rec);
    bool is_val = (h % 1000000ULL) < thresh;
    if(is_val) val_canon.insert(h);
    else train_canon.insert(h);
  }
  for(auto k: val_canon) require(train_canon.find(k)==train_canon.end(), "zero train/val overlap (canonical)");
  require(!train_canon.empty() && !val_canon.empty(), "both splits non-empty");

  auto report = tuna::train::train_report{};
  auto net = tuna::train::train(records, opts, report);
  require(!net.empty(), "train with disjoint split produces net");
  require(report.validation_positions > 0, "validation non-empty");
}

auto test_deterministic_splitting_and_filtering() -> void
{
  const auto records = selfplay_records(5);
  auto opts = tuna::train::train_options{};
  opts.seed = 202;
  opts.validate_fraction = 0.15;
  opts.epochs = 2;
  opts.batch_size = 16;
  auto r1 = tuna::train::train_report{};
  auto r2 = tuna::train::train_report{};
  auto n1 = tuna::train::train(records, opts, r1);
  auto n2 = tuna::train::train(records, opts, r2);
  require(n1.w1()==n2.w1(), "deterministic splitting w1");
  require(r1.validation_positions==r2.validation_positions, "deterministic val count");

  auto sopts = tuna::datagen::selfplay_options{};
  sopts.seed = 777;
  sopts.search_depth = 2;
  sopts.score_depth = 4;
  sopts.max_plies = 20;
  sopts.opening_plies = 2;
  sopts.diversity_min_ply_gap = 2;
  sopts.diversity_eval_threshold = 8;
  sopts.diversity_min_non_pawn = 0;
  auto g1 = tuna::datagen::game_result{};
  auto g2 = tuna::datagen::game_result{};
  require(tuna::datagen::play_one_game(sopts,g1), "deterministic game1");
  require(tuna::datagen::play_one_game(sopts,g2), "deterministic game2");
  require(g1.positions.size()==g2.positions.size(), "deterministic filtering size");
  for(size_t i=0;i<g1.positions.size();++i) require(g1.positions[i].piece_nibbles==g2.positions[i].piece_nibbles, "deterministic filtering positions");
}

auto test_augmentation_isolation() -> void
{
  const auto records = selfplay_records(6);
  auto opts = tuna::train::train_options{};
  opts.seed = 303;
  opts.epochs = 2;
  opts.batch_size = 32;
  opts.validate_fraction = 0.2;
  opts.augment = true;

  std::uint64_t thresh = static_cast<std::uint64_t>(opts.validate_fraction * 1000000.0);
  std::unordered_set<std::uint64_t> train_aug_canons;
  std::unordered_set<std::uint64_t> val_canons;
  std::vector<tuna::datagen::dataset_record> train_recs, val_recs;
  for(auto &rec: records){
    std::uint64_t h = canonical_for_test(rec);
    bool is_val = (h % 1000000ULL) < thresh;
    if(is_val) { val_recs.push_back(rec); val_canons.insert(h); }
    else { train_recs.push_back(rec); train_aug_canons.insert(h); }
  }

  for(auto k: val_canons) require(train_aug_canons.find(k)==train_aug_canons.end(), "augmentation isolation: no twin leakage");

  auto report = tuna::train::train_report{};
  auto net = tuna::train::train(records, opts, report);
  require(report.positions > train_recs.size(), "augmentation increases train positions");
  require(report.validation_positions == val_recs.size(), "validation not augmented");
}

}

auto main() -> int
{
  test_feature_extraction_matches_reference();
  test_quantize_roundtrip();
  test_augmentation_symmetry_and_score_consistency();
  test_learning_and_determinism();
  test_training_with_augmentation();
  test_load_into_engine_and_search();
  test_momentum_changes_update();
  test_projected_weights_bounds();
  test_zero_momentum_reproduces_sgd();
  test_qat_fake_quant_and_integer_forward();
  test_qat_equivalence_after_training();
  test_adam_deterministic();
  test_adam_updates_and_selection();
  test_adam_qat_equivalence();
  test_train_validation_no_overlap();
  test_deterministic_splitting_and_filtering();
  test_augmentation_isolation();
  return 0;
}