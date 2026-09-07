#include "datagen/dataset.hpp"
#include "eval/nnue.hpp"
#include "train/trainer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct args {
  std::string dataset_path;
  std::string out_path;
  tuna::train::train_options options;
  bool augment = true;
};

auto usage(FILE* out) -> void
{
  std::fprintf(out,
               "usage: tuna_nnue_train <dataset.bin> <outfile.nnue> "
               "[epochs=20] [batch_size=256] [learning_rate=0.05] "
               "[validate_fraction=0.1] [seed=1] [momentum=0.9] [--no-augment] [--momentum=<val>] "
               "[--optimizer=sgd|adamw] [--adam-beta1=<val>] [--adam-beta2=<val>] [--adam-eps=<val>] [--weight-decay=<val>]\n");
}

auto parse_args(int argc, char** argv) -> args
{
  auto a = args{};
  auto positional = std::vector<const char*>{};
  bool momentum_overridden = false;
  bool optimizer_overridden = false;
  for(auto i = 1; i < argc; ++i) {
    if(std::strcmp(argv[i], "--no-augment") == 0) {
      a.augment = false;
    } else if(std::strncmp(argv[i], "--momentum=", 11) == 0) {
      a.options.momentum = std::strtof(argv[i] + 11, nullptr);
      momentum_overridden = true;
    } else if(std::strcmp(argv[i], "--momentum") == 0 && i + 1 < argc) {
      a.options.momentum = std::strtof(argv[++i], nullptr);
      momentum_overridden = true;
    } else if(std::strncmp(argv[i], "--optimizer=", 12) == 0) {
      const char* v = argv[i] + 12;
      if(std::strcmp(v, "adamw") == 0 || std::strcmp(v, "adam") == 0) a.options.optimizer = tuna::train::optimizer_type::adamw;
      else a.options.optimizer = tuna::train::optimizer_type::sgd;
      optimizer_overridden = true;
    } else if(std::strcmp(argv[i], "--optimizer") == 0 && i + 1 < argc) {
      const char* v = argv[++i];
      if(std::strcmp(v, "adamw") == 0 || std::strcmp(v, "adam") == 0) a.options.optimizer = tuna::train::optimizer_type::adamw;
      else a.options.optimizer = tuna::train::optimizer_type::sgd;
      optimizer_overridden = true;
    } else if(std::strncmp(argv[i], "--adam-beta1=", 13) == 0) {
      a.options.adam_beta1 = std::strtof(argv[i] + 13, nullptr);
    } else if(std::strcmp(argv[i], "--adam-beta1") == 0 && i + 1 < argc) {
      a.options.adam_beta1 = std::strtof(argv[++i], nullptr);
    } else if(std::strncmp(argv[i], "--adam-beta2=", 13) == 0) {
      a.options.adam_beta2 = std::strtof(argv[i] + 13, nullptr);
    } else if(std::strcmp(argv[i], "--adam-beta2") == 0 && i + 1 < argc) {
      a.options.adam_beta2 = std::strtof(argv[++i], nullptr);
    } else if(std::strncmp(argv[i], "--adam-eps=", 11) == 0) {
      a.options.adam_eps = std::strtof(argv[i] + 11, nullptr);
    } else if(std::strcmp(argv[i], "--adam-eps") == 0 && i + 1 < argc) {
      a.options.adam_eps = std::strtof(argv[++i], nullptr);
    } else if(std::strncmp(argv[i], "--weight-decay=", 15) == 0) {
      a.options.weight_decay = std::strtof(argv[i] + 15, nullptr);
    } else if(std::strcmp(argv[i], "--weight-decay") == 0 && i + 1 < argc) {
      a.options.weight_decay = std::strtof(argv[++i], nullptr);
    } else if(std::strncmp(argv[i], "--beta1=", 8) == 0) {
      a.options.adam_beta1 = std::strtof(argv[i] + 8, nullptr);
    } else if(std::strncmp(argv[i], "--beta2=", 8) == 0) {
      a.options.adam_beta2 = std::strtof(argv[i] + 8, nullptr);
    } else if(std::strncmp(argv[i], "--epsilon=", 10) == 0) {
      a.options.adam_eps = std::strtof(argv[i] + 10, nullptr);
    } else {
      positional.push_back(argv[i]);
    }
  }
  if(positional.size() < 2) {
    usage(stderr);
    std::exit(2);
  }
  a.dataset_path = positional[0];
  a.out_path = positional[1];
  auto& o = a.options;
  o.augment = a.augment;
  if(positional.size() >= 3) {
    o.epochs = std::atoi(positional[2]);
  }
  if(positional.size() >= 4) {
    o.batch_size = std::atoi(positional[3]);
  }
  if(positional.size() >= 5) {
    o.learning_rate = std::strtof(positional[4], nullptr);
  }
  if(positional.size() >= 6) {
    o.validate_fraction = std::strtod(positional[5], nullptr);
  }
  if(positional.size() >= 7) {
    o.seed = std::strtoull(positional[6], nullptr, 0);
  }
  if(positional.size() >= 8 && !momentum_overridden) {
    o.momentum = std::strtof(positional[7], nullptr);
  }
  if(positional.size() >= 9 && !optimizer_overridden) {
    const char* v = positional[8];
    if(std::strcmp(v, "adamw") == 0 || std::strcmp(v, "adam") == 0) o.optimizer = tuna::train::optimizer_type::adamw;
    else if(std::strcmp(v, "sgd") == 0) o.optimizer = tuna::train::optimizer_type::sgd;
  }
  o.augment = a.augment;
  return a;
}

auto valid(const args& a) -> bool
{
  const auto& o = a.options;
  bool adam_ok = o.adam_beta1 > 0.0f && o.adam_beta1 < 1.0f && o.adam_beta2 > 0.0f && o.adam_beta2 < 1.0f && o.adam_eps > 0.0f && o.weight_decay >= 0.0f;
  return o.epochs >= 1 && o.batch_size >= 1 && o.learning_rate > 0.0f
         && o.validate_fraction > 0.0 && o.validate_fraction < 1.0
         && o.momentum >= 0.0f && o.momentum < 1.0f && adam_ok;
}

}

auto main(int argc, char** argv) -> int
{
  const auto a = parse_args(argc, argv);
  if(!valid(a)) {
    usage(stderr);
    return 2;
  }

  auto reader = tuna::datagen::dataset_reader{a.dataset_path};
  if(!reader.good()) {
    std::fprintf(stderr, "failed to read dataset %s\n", a.dataset_path.c_str());
    return 1;
  }
  auto records = std::vector<tuna::datagen::dataset_record>{};
  if(!reader.records(records) || records.empty()) {
    std::fprintf(stderr, "dataset %s contains no positions\n", a.dataset_path.c_str());
    return 1;
  }

  auto report = tuna::train::train_report{};

  const auto net = tuna::train::train(records, a.options, report);
  if(report.positions == 0) {
    std::fprintf(stderr,
                 "no trainable positions (all records without both kings) in %s\n",
                 a.dataset_path.c_str());
    return 1;
  }
  if(net.empty()) {
    std::fprintf(stderr, "training produced an empty network\n");
    return 1;
  }

  std::printf("dataset: %s (%llu records)\n", a.dataset_path.c_str(),
               static_cast<unsigned long long>(records.size()));
  const char* opt_name = a.options.optimizer == tuna::train::optimizer_type::adamw ? "adamw" : "sgd";
  std::printf("training: %llu positions (mirror augmentation: %s), "
              "validating on %llu, %d epochs, batch %d, lr %.6f, decay %.4f, optimizer %s, momentum %.4f, seed 0x%llx\n",
              static_cast<unsigned long long>(report.positions), a.augment ? "on" : "off",
              static_cast<unsigned long long>(report.validation_positions), report.epochs,
              a.options.batch_size, a.options.learning_rate, a.options.learning_rate_decay,
              opt_name, a.options.momentum, static_cast<unsigned long long>(a.options.seed));
  if(a.options.optimizer == tuna::train::optimizer_type::adamw) {
    std::printf("adam: beta1 %.4f beta2 %.4f eps %.2e wd %.6f\n",
                a.options.adam_beta1, a.options.adam_beta2, a.options.adam_eps, a.options.weight_decay);
  }

  const auto has_validation = report.validation_positions > 0;
  for(auto i = 0; i < report.epochs; ++i) {
    const auto train_mse =
        i < static_cast<int>(report.epoch_loss.size()) ? report.epoch_loss[static_cast<std::size_t>(i)] : -1.0;
    if(has_validation && i < static_cast<int>(report.validation_loss.size())) {
      std::printf("epoch %3d: train_mse %.9f  val_mse %.9f\n", i + 1, train_mse,
                  report.validation_loss[static_cast<std::size_t>(i)]);
    } else {
      std::printf("epoch %3d: train_mse %.9f\n", i + 1, train_mse);
    }
  }

  if(has_validation) {
    std::printf("validation_mse: initial %.9f final %.9f classical %.9f\n",
                report.initial_mse, report.final_mse, report.classical_mse);
    std::printf("score_correlation: %.6f\n", report.score_correlation);
    std::printf("max_accumulator: %.0f (int16 limit 32768)\n", report.max_accumulator);
    std::printf("improved_over_initial: %s\n",
                report.final_mse < report.initial_mse ? "yes" : "no");
    std::printf("improved_over_classical: %s\n",
                report.final_mse < report.classical_mse ? "yes" : "no");
  }

  if(!net.save(a.out_path)) {
    std::fprintf(stderr, "failed to write %s\n", a.out_path.c_str());
    return 1;
  }
  std::printf("wrote quantized halfKP network (%d -> %d -> %d -> 1) to %s\n",
              tuna::eval::nnue::ft_size, tuna::eval::nnue::l1_size, tuna::eval::nnue::l2_size,
              a.out_path.c_str());
  return 0;
}