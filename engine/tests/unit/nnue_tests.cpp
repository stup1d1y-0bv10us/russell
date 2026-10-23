#include "core/position.hpp"
#include "datagen/dataset.hpp"
#include "eval/evaluate.hpp"
#include "eval/nnue.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
#include "train/trainer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

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

struct rng {
  std::uint64_t state;
  explicit rng(std::uint64_t s) : state(s | std::uint64_t{1}) {}
  auto next() -> std::uint64_t
  {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }
};

auto type_index(russell::piece_type pt) -> int
{
  switch(pt) {
  case russell::piece_type::pawn: return 0;
  case russell::piece_type::knight: return 1;
  case russell::piece_type::bishop: return 2;
  case russell::piece_type::rook: return 3;
  case russell::piece_type::queen: return 4;
  default: return -1;
  }
}

auto king_square(const russell::position& pos, russell::color c) -> int
{
  const auto bb = pos.pieces(c, russell::piece_type::king);
  if(bb == 0) {
    return -1;
  }
  return static_cast<int>(std::countr_zero(bb));
}

auto flip(const russell::position& pos) -> russell::position
{
  auto out = russell::position::empty();
  for(auto c = 0; c < russell::color_count; ++c) {
    for(auto pt = 0; pt < russell::piece_type_count; ++pt) {
      auto bb = pos.pieces(static_cast<russell::color>(c), static_cast<russell::piece_type>(pt));
      while(bb != 0) {
        const auto sq = static_cast<int>(std::countr_zero(bb));
        bb &= bb - 1;
        out.set_piece(russell::opposite(static_cast<russell::color>(c)),
                      static_cast<russell::piece_type>(pt), sq ^ 56);
      }
    }
  }
  out.set_side_to_move(russell::opposite(pos.side_to_move()));
  return out;
}

auto fp_reference(const russell::eval::nnue::network& net, const russell::position& pos) -> int
{
  using namespace russell;
  using namespace russell::eval;
  auto wk = king_square(pos, color::white);
  auto bk = king_square(pos, color::black);
  if(wk < 0 || bk < 0) {
    return 0;
  }
  const auto stm_white = pos.side_to_move() == color::white;
  const auto square_mirror = stm_white ? 0 : 56;
  const auto color_swap = stm_white ? 0 : 1;
  const auto own_king = (stm_white ? wk : bk) ^ square_mirror;
  const auto enemy_king = (stm_white ? bk : wk) ^ square_mirror;
  const auto own_bucket = nnue::king_bucket(own_king);
  const auto enemy_bucket = nnue::king_bucket(enemy_king);

  auto acc_own = std::array<double, nnue::ft_size>{};
  auto acc_enemy = std::array<double, nnue::ft_size>{};
  for(auto i = 0; i < nnue::ft_size; ++i) {
    acc_own[i] = net.bias1()[static_cast<std::size_t>(i)];
    acc_enemy[i] = net.bias1()[static_cast<std::size_t>(i)];
  }
  for(auto c = 0; c < color_count; ++c) {
    for(auto pt = 0; pt < piece_type_count; ++pt) {
      const auto ptype = static_cast<piece_type>(pt);
      if(ptype == piece_type::king) {
        continue;
      }
      auto bb = pos.pieces(static_cast<color>(c), ptype);
      while(bb != 0) {
        const auto sq = static_cast<int>(std::countr_zero(bb)) ^ square_mirror;
        bb &= bb - 1;
        const auto enc = (c ^ color_swap) * 5 + type_index(ptype);
        const auto own_index = nnue::feature_index(
            own_bucket, enc, nnue::orient_square(own_king, sq));
        const auto enemy_index = nnue::half_feature_space + nnue::feature_index(
            enemy_bucket, enc, nnue::orient_square(enemy_king, sq));
        for(auto i = 0; i < nnue::ft_size; ++i) {
          acc_own[static_cast<std::size_t>(i)] +=
              net.w1()[static_cast<std::size_t>(own_index) * nnue::ft_size + i];
          acc_enemy[static_cast<std::size_t>(i)] +=
              net.w1()[static_cast<std::size_t>(enemy_index) * nnue::ft_size + i];
        }
      }
    }
  }

  auto hidden0 = std::array<double, nnue::input_dims>{};
  for(auto i = 0; i < nnue::ft_size; ++i) {

    hidden0[static_cast<std::size_t>(i)] =
        std::clamp(static_cast<double>(static_cast<std::int16_t>(acc_own[static_cast<std::size_t>(i)])), 0.0, 127.0);
    hidden0[static_cast<std::size_t>(nnue::ft_size + i)] =
        std::clamp(static_cast<double>(static_cast<std::int16_t>(acc_enemy[static_cast<std::size_t>(i)])), 0.0, 127.0);
  }

  const auto quant = [](double value) -> double {
    const auto shifted = std::floor(value / static_cast<double>(std::uint64_t{1} << nnue::weight_scale_bits));
    return std::clamp(shifted, 0.0, 127.0);
  };

  auto hidden1 = std::array<double, nnue::l1_size>{};
  for(auto j = 0; j < nnue::l1_size; ++j) {
    auto sum = 0.0;
    for(auto k = 0; k < nnue::input_dims; ++k) {
      sum += net.w2()[static_cast<std::size_t>(j) * nnue::input_dims + k] * hidden0[static_cast<std::size_t>(k)];
    }
    hidden1[static_cast<std::size_t>(j)] = quant(sum + net.bias2()[static_cast<std::size_t>(j)]);
  }
  auto hidden2 = std::array<double, nnue::l2_size>{};
  for(auto j = 0; j < nnue::l2_size; ++j) {
    auto sum = 0.0;
    for(auto k = 0; k < nnue::l1_size; ++k) {
      sum += net.w3()[static_cast<std::size_t>(j) * nnue::l1_size + k] * hidden1[static_cast<std::size_t>(k)];
    }
    hidden2[static_cast<std::size_t>(j)] = quant(sum + net.bias3()[static_cast<std::size_t>(j)]);
  }

  auto out = static_cast<double>(net.bias4()[0]);
  for(auto k = 0; k < nnue::l2_size; ++k) {
    out += net.w4()[static_cast<std::size_t>(k)] * hidden2[static_cast<std::size_t>(k)];
  }
  return static_cast<int>(std::trunc(out / static_cast<double>(nnue::output_scale)));
}

auto test_roundtrip() -> void
{
  const auto path = (std::filesystem::temp_directory_path() / "russell_nnue_roundtrip.bin").string();
  auto net = russell::eval::nnue::network::make_random(0x12345678);
  require(net.save(path), "placeholder net saves");
  auto loaded = russell::eval::nnue::network{};
  require(loaded.load(path), "placeholder net loads");
  require(net.w1() == loaded.w1(), "w1 roundtrip");
  require(net.bias1() == loaded.bias1(), "bias1 roundtrip");
  require(net.w2() == loaded.w2(), "w2 roundtrip");
  require(net.bias2() == loaded.bias2(), "bias2 roundtrip");
  require(net.w3() == loaded.w3(), "w3 roundtrip");
  require(net.bias3() == loaded.bias3(), "bias3 roundtrip");
  require(net.w4() == loaded.w4(), "w4 roundtrip");
  require(net.bias4() == loaded.bias4(), "bias4 roundtrip");
  const auto pos = russell::position::start();
  require(russell::eval::nnue::evaluate(net, pos) == russell::eval::nnue::evaluate(loaded, pos),
          "roundtrip eval matches");
  std::filesystem::remove(path);
}

auto test_load_rejects_garbage() -> void
{
  const auto path = (std::filesystem::temp_directory_path() / "russell_nnue_garbage.bin").string();
  auto net = russell::eval::nnue::network{};
  {
    auto out = std::ofstream{path, std::ios::binary};
    out << "not a network";
  }
  require(!net.load(path), "garbage file rejected");
  require(!net.save(path), "empty network cannot save");
  std::filesystem::remove(path);
}

auto verify_state(russell::eval::nnue::evaluator& eval, const russell::eval::nnue::network& net,
                  const russell::position& pos) -> void
{
  auto own = std::array<std::int16_t, russell::eval::nnue::ft_size>{};
  auto enemy = std::array<std::int16_t, russell::eval::nnue::ft_size>{};
  require(russell::eval::nnue::reference_accumulate(net, pos, own, enemy), "reference accumulates");
  const auto& acc = eval.acc();
  for(auto i = 0; i < russell::eval::nnue::ft_size; ++i) {
    require(acc[0][i] == own[i], "white-frame own half matches reference");
    require(acc[1][i] == enemy[i], "white-frame enemy half matches reference");
  }
  const auto mirrored = flip(pos);
  require(russell::eval::nnue::reference_accumulate(net, mirrored, own, enemy), "mirrored reference accumulates");
  for(auto i = 0; i < russell::eval::nnue::ft_size; ++i) {
    require(acc[2][i] == own[i], "black-frame own half matches mirrored reference");
    require(acc[3][i] == enemy[i], "black-frame enemy half matches mirrored reference");
  }
  require(eval.evaluate(pos) == russell::eval::nnue::evaluate(net, pos), "incremental eval matches stateless");
  require(russell::eval::nnue::evaluate(net, pos) == fp_reference(net, pos), "stateless eval matches reference");
}

auto test_incremental_sequence(const russell::position& start_pos) -> void
{
  auto r = rng{0xC0FFEE};
  const auto net = std::make_shared<const russell::eval::nnue::network>(
      russell::eval::nnue::network::make_random(0xDEADBEEF));
  auto pos = start_pos;
  auto eval = russell::eval::nnue::evaluator{*net};
  eval.refresh(pos);
  verify_state(eval, *net, pos);

  auto history = std::vector<std::pair<russell::move, russell::move_state>>{};
  history.reserve(256);
  for(auto i = 0; i < 150; ++i) {
    auto legal = russell::movegen::generate_legal(pos);
    if(legal.size() == 0) {
      break;
    }
    const auto mv = legal[static_cast<std::size_t>(r.next() % legal.size())];

    const auto snapshot_acc = eval.acc();
    const auto snapshot_ready = eval.ready();
    auto st = pos.make_move(mv);
    eval.make_move(pos, mv, st);
    verify_state(eval, *net, pos);

    eval.unmake_move();
    pos.unmake_move(mv, st);
    require(eval.ready() == snapshot_ready, "undo restores ready flag");
    for(auto k = 0; k < 4; ++k) {
      require(eval.acc()[k] == snapshot_acc[k], "undo restores accumulator");
    }

    st = pos.make_move(mv);
    eval.make_move(pos, mv, st);
    history.push_back({mv, st});
  }

  while(!history.empty()) {
    const auto [mv, st] = history.back();
    history.pop_back();
    eval.unmake_move();
    pos.unmake_move(mv, st);
  }
  verify_state(eval, *net, pos);
}

auto test_random_games() -> void
{
  const auto fens = std::vector<std::string>{
    russell::position::start().fen(),
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "4k3/8/8/8/3K4/8/8/8 w - - 0 1",
    "4k3/8/8/8/3K4/8/8/8 b - - 0 1",
    "6k1/8/8/8/8/8/8/2K5 w - - 0 1",
    "6k1/8/8/8/8/8/8/2K5 b - - 0 1",
    "8/8/8/3k4/8/2K5/8/8 w - - 0 1",
    "8/8/8/3k4/8/2K5/8/8 b - - 0 1",
  };
  for(const auto& fen : fens) {
    test_incremental_sequence(russell::position::from_fen(fen));
  }
}

auto side_in_check(const russell::position& pos) -> bool
{
  const auto bb = pos.pieces(pos.side_to_move(), russell::piece_type::king);
  if(bb == 0) {
    return false;
  }
  return russell::movegen::is_square_attacked(pos, static_cast<int>(std::countr_zero(bb)),
                                           russell::opposite(pos.side_to_move()));
}

auto verify_make_unmake(russell::eval::nnue::evaluator& eval,
                        const russell::eval::nnue::network& net, russell::position& pos,
                        russell::move mv, const char* what) -> void
{
  auto st = pos.make_move(mv);
  eval.make_move(pos, mv, st);
  verify_state(eval, net, pos);
  eval.unmake_move();
  pos.unmake_move(mv, st);
  verify_state(eval, net, pos);
  static_cast<void>(what);
}

auto test_move_type_sync() -> void
{
  struct type_case {
    const char* fen;
    russell::move_flag flag;
    const char* name;
  };
  const auto cases = std::vector<type_case>{
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
     russell::move_flag::quiet, "ordinary quiet move"},
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
     russell::move_flag::double_push, "double push"},
    {"3k4/8/3q4/8/3R4/8/8/4K3 w - - 0 1",
     russell::move_flag::capture, "capture"},
    {"8/2P5/8/8/8/8/8/k1K5 w - - 0 1",
     russell::move_flag::promotion, "promotion"},
    {"1r6/2P5/8/8/8/8/8/k1K5 w - - 0 1",
     russell::move_flag::promotion_capture, "promotion capture"},
    {"k7/8/8/8/3pP3/8/8/K7 b KQkq e3 0 1",
     russell::move_flag::en_passant, "en-passant"},
    {"4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1",
     russell::move_flag::castling, "castling"},
    {"k7/8/8/8/8/8/8/K7 w - - 0 1",
     russell::move_flag::quiet, "king move"},
  };

  auto seed = std::uint64_t{0xFEEDBEEF};
  for(const auto& c : cases) {
    const auto net = std::make_shared<const russell::eval::nnue::network>(
        russell::eval::nnue::network::make_random(seed));
    seed = seed * 0x9E3779B97F4A7C15ULL + 1;

    auto pos = russell::position::from_fen(c.fen);
    auto eval = russell::eval::nnue::evaluator{*net};
    eval.refresh(pos);
    verify_state(eval, *net, pos);

    auto legal = russell::movegen::generate_legal(pos);
    auto chosen = russell::move{};
    auto found = false;
    for(const auto mv : legal) {
      if(mv.flag == c.flag) {
        chosen = mv;
        found = true;
        break;
      }
    }
    require_msg(found, std::string("move type is legal: ") + c.name);
    if(std::string(c.name).find("king move") != std::string::npos) {
      require(russell::piece_type_of(pos.piece_on(static_cast<int>(chosen.from)))
                  == russell::piece_type::king,
              "king move case actually moves the king");
    }

    verify_make_unmake(eval, *net, pos, chosen, c.name);
  }
}

auto test_null_move_integrity() -> void
{
  const auto net = std::make_shared<const russell::eval::nnue::network>(
      russell::eval::nnue::network::make_random(0xB00B5));
  auto pos = russell::position::from_fen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto eval = russell::eval::nnue::evaluator{*net};
  eval.refresh(pos);
  verify_state(eval, *net, pos);

  const auto before = eval.acc();
  const auto before_ready = eval.ready();

  const auto null_st = pos.make_null_move();
  eval.make_null_move(pos);
  require(eval.ready() == before_ready, "null move keeps the ready flag");
  for(auto k = 0; k < 4; ++k) {
    require(eval.acc()[static_cast<std::size_t>(k)] == before[static_cast<std::size_t>(k)],
            "null move does not modify accumulator halves");
  }

  verify_state(eval, *net, pos);

  auto rng_state = std::uint64_t{0x1234ABCD};
  for(auto i = 0; i < 4; ++i) {
    auto legal = russell::movegen::generate_legal(pos);
    require(legal.size() > 0, "a move exists inside the null window");
    const auto mv = legal[static_cast<std::size_t>(rng_state % legal.size())];
    rng_state = rng_state * 0x9E3779B97F4A7C15ULL + 1;
    verify_make_unmake(eval, *net, pos, mv, "null-window move");
  }

  eval.unmake_move();
  pos.unmake_null_move(null_st);
  require(eval.ready() == before_ready, "null unmake restores the ready flag");
  for(auto k = 0; k < 4; ++k) {
    require(eval.acc()[static_cast<std::size_t>(k)] == before[static_cast<std::size_t>(k)],
            "null unmake restores accumulator halves");
  }
  verify_state(eval, *net, pos);
}

auto test_sequence_with_null_moves() -> void
{
  const auto net = std::make_shared<const russell::eval::nnue::network>(
      russell::eval::nnue::network::make_random(0xBEEFCAFE));
  auto pos = russell::position::start();
  auto eval = russell::eval::nnue::evaluator{*net};
  eval.refresh(pos);
  verify_state(eval, *net, pos);

  auto rng_state = std::uint64_t{0x0DDBA11};
  for(auto i = 0; i < 60; ++i) {
    auto legal = russell::movegen::generate_legal(pos);
    if(legal.size() == 0) {
      break;
    }
    const auto mv = legal[static_cast<std::size_t>(rng_state % legal.size())];
    rng_state = rng_state * 0x9E3779B97F4A7C15ULL + 1;

    auto st = pos.make_move(mv);
    eval.make_move(pos, mv, st);
    verify_state(eval, *net, pos);

    if((rng_state & 0xF) == 0 && !side_in_check(pos)) {
      const auto null_st = pos.make_null_move();
      eval.make_null_move(pos);
      verify_state(eval, *net, pos);
      eval.unmake_move();
      pos.unmake_null_move(null_st);
      verify_state(eval, *net, pos);
    }

    eval.unmake_move();
    pos.unmake_move(mv, st);
    verify_state(eval, *net, pos);
  }
}

auto test_symmetry() -> void
{
  const auto net = std::make_shared<const russell::eval::nnue::network>(
      russell::eval::nnue::network::make_random(0xBEEF));
  russell::eval::set_nnue(net);
  const auto fens = std::vector<std::string>{
    russell::position::start().fen(),
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1",
  };
  for(const auto& fen : fens) {
    const auto pos = russell::position::from_fen(fen);
    require(russell::eval::evaluate(flip(pos)) == -russell::eval::evaluate(pos),
            ("nnue eval symmetry " + fen).c_str());
  }
  russell::eval::set_nnue(nullptr);
}

auto test_eval_dispatch() -> void
{
  const auto net = std::make_shared<const russell::eval::nnue::network>(
      russell::eval::nnue::network::make_random(0x5EED));
  const auto pos = russell::position::from_fen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

  require(russell::eval::evaluate(pos) == russell::eval::evaluate(pos, russell::eval::default_weights()),
          "classical eval is the default");

  russell::eval::set_nnue(net);
  const auto stm_white = pos.side_to_move() == russell::color::white;
  const auto nnue_score = russell::eval::nnue::evaluate(*net, pos);
  require(russell::eval::evaluate(pos) == (stm_white ? nnue_score : -nnue_score),
          "nnue replaces classical when a network is loaded");
  require(russell::eval::evaluate(pos) != russell::eval::evaluate(pos, russell::eval::default_weights()),
          "nnue eval differs from classical on a busy position");

  russell::eval::set_nnue(nullptr);
  require(russell::eval::evaluate(pos) == russell::eval::evaluate(pos, russell::eval::default_weights()),
          "clearing the network restores classical eval");
}

auto test_trainer_feature_parity() -> void
{

  const auto fnet = russell::train::make_initial_float_net(0x9A217);
  const auto q = russell::train::quantize(fnet);
  const auto fens = std::vector<std::string>{
    russell::position::start().fen(),
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 b - - 0 1",
    "4k3/8/8/8/3K4/8/8/8 w - - 0 1",
    "6k1/8/8/8/8/8/8/2K5 w - - 0 1",
    "6k1/8/8/8/8/8/8/2K5 b - - 0 1",
    "8/8/8/3k4/8/2K4/8/8 w - - 0 1",
    "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 1",
    "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
    "4k3/2P5/8/8/8/8/8/4K3 w - - 0 1",
    "3qk3/8/8/8/8/8/8/3QK3 b - - 0 1",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
  };
  auto checked = 0;
  for(const auto& fen : fens) {
    const auto pos = russell::position::from_fen(fen);
    auto record = russell::datagen::pack_position(pos);
    record.score = 137;
    record.result = 1;
    require_msg(russell::datagen::unpack_position(record).fen() == pos.fen(),
                "dataset board roundtrip: " + fen);

    const auto feat = russell::train::features_of(record);
    for(const auto f : feat.own) {
      require(f < static_cast<std::uint32_t>(russell::eval::nnue::feature_space),
              "trainer own index in v1.4 range");
    }
    for(const auto f : feat.enemy) {
      require(f < static_cast<std::uint32_t>(russell::eval::nnue::feature_space),
              "trainer enemy index in v1.4 range");
    }

    const auto frame = pos.side_to_move() == russell::color::black ? flip(pos) : pos;
    auto own_ref = std::array<std::int16_t, russell::eval::nnue::ft_size>{};
    auto enemy_ref = std::array<std::int16_t, russell::eval::nnue::ft_size>{};
    require_msg(russell::eval::nnue::reference_accumulate(q, frame, own_ref, enemy_ref),
                "reference accumulates: " + fen);
    for(auto i = 0; i < russell::eval::nnue::ft_size; ++i) {
      auto acc_own = static_cast<int>(q.bias1()[static_cast<std::size_t>(i)]);
      for(const auto f : feat.own) {
        acc_own += q.w1()[static_cast<std::size_t>(f) * russell::eval::nnue::ft_size + i];
      }
      auto acc_enemy = static_cast<int>(q.bias1()[static_cast<std::size_t>(i)]);
      for(const auto f : feat.enemy) {
        acc_enemy += q.w1()[static_cast<std::size_t>(f) * russell::eval::nnue::ft_size + i];
      }
      require_msg(acc_own == static_cast<int>(own_ref[static_cast<std::size_t>(i)]),
                  "trainer own == production own: " + fen);
      require_msg(acc_enemy == static_cast<int>(enemy_ref[static_cast<std::size_t>(i)]),
                  "trainer enemy == production enemy: " + fen);
    }

    auto rec2 = record;
    russell::train::float_net f2;
    f2.w1.resize(q.w1().size());
    f2.bias1.resize(q.bias1().size());
    f2.w2.resize(q.w2().size());
    f2.bias2.resize(q.bias2().size());
    f2.w3.resize(q.w3().size());
    f2.bias3.resize(q.bias3().size());
    f2.w4.resize(q.w4().size());
    f2.bias4.resize(q.bias4().size());
    for(std::size_t k = 0; k < f2.w1.size(); ++k) f2.w1[k] = static_cast<float>(q.w1()[k]);
    for(std::size_t k = 0; k < f2.bias1.size(); ++k) f2.bias1[k] = static_cast<float>(q.bias1()[k]);
    for(std::size_t k = 0; k < f2.w2.size(); ++k) f2.w2[k] = static_cast<float>(q.w2()[k]);
    for(std::size_t k = 0; k < f2.bias2.size(); ++k) f2.bias2[k] = static_cast<float>(q.bias2()[k]);
    for(std::size_t k = 0; k < f2.w3.size(); ++k) f2.w3[k] = static_cast<float>(q.w3()[k]);
    for(std::size_t k = 0; k < f2.bias3.size(); ++k) f2.bias3[k] = static_cast<float>(q.bias3()[k]);
    for(std::size_t k = 0; k < f2.w4.size(); ++k) f2.w4[k] = static_cast<float>(q.w4()[k]);
    for(std::size_t k = 0; k < f2.bias4.size(); ++k) f2.bias4[k] = static_cast<float>(q.bias4()[k]);
    require_msg(russell::train::trainer_predict(f2, rec2) == russell::eval::nnue::evaluate(q, pos),
                "trainer forward == engine inference: " + fen);
    ++checked;
  }
  require(checked == static_cast<int>(fens.size()), "parity battery fully covered");
}

auto test_architecture_dimensions() -> void
{
  using namespace russell::eval::nnue;
  static_assert(ft_size == 512, "v1.4 FT width");
  static_assert(l1_size == 64, "v1.4 first hidden width");
  static_assert(l2_size == 32, "v1.4 second hidden width");
  static_assert(king_buckets == 8, "v1.4 king buckets");
  static_assert(features_per_bucket == 640, "v1.4 features per bucket");
  static_assert(half_feature_space == 5120, "v1.4 half feature space");
  static_assert(feature_space == 10240, "v1.4 full feature space");
  static_assert(input_dims == 1024, "v1.4 network input dims");
  const auto net = network::make_random(0xD1E5);
  require(!net.empty(), "v1.4 network is non-empty");
  require(net.feature_space_size() == 10240, "runtime reports v1.4 feature space");
  require(net.w1().size() == static_cast<std::size_t>(10240 * 512), "FT weight count");
  require(net.bias1().size() == static_cast<std::size_t>(512), "FT bias count");
  require(net.w2().size() == static_cast<std::size_t>(1024 * 64), "L1 weight count");
  require(net.w3().size() == static_cast<std::size_t>(64 * 32), "L2 weight count");
  require(net.w4().size() == static_cast<std::size_t>(32), "output weight count");
}

auto test_eval_path_integration() -> void
{

  const auto net = std::make_shared<const russell::eval::nnue::network>(
      russell::eval::nnue::network::make_random(0x1CA7E9));
  russell::eval::set_nnue(net);

  const auto fens = std::vector<std::string>{
    russell::position::start().fen(),
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 b - - 0 1",
    "4k3/8/8/8/3K4/8/8/8 w - - 0 1",
    "4k3/8/8/8/3K4/8/8/8 b - - 0 1",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 b - - 0 10",
  };
  auto r = rng{0xE7A1};
  auto paths_compared = 0;
  for(const auto& fen : fens) {
    auto pos = russell::position::from_fen(fen);
    auto incr = russell::eval::nnue::evaluator{*net};
    incr.refresh(pos);
    for(auto i = 0; i < 60; ++i) {

      const auto a_raw = incr.evaluate(pos);

      auto fresh = russell::eval::nnue::evaluator{*net};
      fresh.refresh(pos);
      const auto b_raw = fresh.evaluate(pos);

      const auto c_raw = russell::eval::nnue::evaluate(*net, pos);
      require_msg(a_raw == b_raw && b_raw == c_raw,
                  "A==B==C raw NNUE: " + fen + " ply " + std::to_string(i));

      const auto stm_white = pos.side_to_move() == russell::color::white;
      const auto expected_final = stm_white ? c_raw : -c_raw;
      require_msg(russell::eval::evaluate(pos) == expected_final,
                  "production dispatch scales raw output: " + fen);
      ++paths_compared;

      auto legal = russell::movegen::generate_legal(pos);
      if(legal.size() == 0) {
        break;
      }
      const auto mv = legal[static_cast<std::size_t>(r.next() % legal.size())];
      auto st = pos.make_move(mv);
      incr.make_move(pos, mv, st);
    }

    if(!side_in_check(pos)) {
      const auto null_st = pos.make_null_move();
      incr.make_null_move(pos);
      auto fresh = russell::eval::nnue::evaluator{*net};
      fresh.refresh(pos);
      require(incr.evaluate(pos) == fresh.evaluate(pos), "null A==B");
      require(incr.evaluate(pos) == russell::eval::nnue::evaluate(*net, pos), "null A==C");
      const auto stm_white = pos.side_to_move() == russell::color::white;
      const auto c_raw = russell::eval::nnue::evaluate(*net, pos);
      require(russell::eval::evaluate(pos) == (stm_white ? c_raw : -c_raw),
              "null production dispatch scales raw output");
      incr.unmake_move();
      pos.unmake_null_move(null_st);
      ++paths_compared;
    }
  }
  require_msg(paths_compared > 200, "integration sweep compared enough positions");

  const auto search_fens = std::vector<std::string>{
    russell::position::start().fen(),
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
  };
  for(const auto& fen : search_fens) {
    auto pos = russell::position::from_fen(fen);
    const auto first = russell::search::iterative_deepening(pos, 3);
    require(first.has_move, "search with v1.4 net produces a move");
    auto legal = russell::movegen::generate_legal(pos);
    auto found = false;
    for(const auto mv : legal) {
      if(mv == first.best_move) {
        found = true;
        break;
      }
    }
    require_msg(found, "search bestmove is legal: " + fen);
    require(first.score > -russell::search::mate_value && first.score < russell::search::mate_value,
            "search score is finite");
    auto pos2 = russell::position::from_fen(fen);
    const auto second = russell::search::iterative_deepening(pos2, 3);
    require(second.has_move && second.best_move == first.best_move
                && second.score == first.score,
            "search with v1.4 net is deterministic");
  }
  russell::eval::set_nnue(nullptr);
  const auto classical_pos = russell::position::start();
  require(russell::eval::evaluate(classical_pos)
              == russell::eval::evaluate(classical_pos, russell::eval::default_weights()),
          "teardown restores classical eval");
}

auto test_determinism() -> void
{
  const auto net = russell::eval::nnue::network::make_random(0xABCDEF);
  const auto pos = russell::position::from_fen(
      "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10");
  auto a = russell::eval::nnue::evaluator{net};
  auto b = russell::eval::nnue::evaluator{net};
  a.refresh(pos);
  b.refresh(pos);
  require(a.evaluate(pos) == b.evaluate(pos), "independent evaluators agree");
  require(a.evaluate(pos) == a.evaluate(pos), "evaluation is deterministic");
}

auto test_missing_king_falls_back() -> void
{
  const auto net = russell::eval::nnue::network::make_random(0x13579);
  const auto pos = russell::position::empty();
  require(russell::eval::nnue::evaluate(net, pos) == 0, "no kings evaluates to zero");
}

auto flip_horizontal(const russell::position& pos) -> russell::position
{
  auto out = russell::position::empty();
  for(auto c = 0; c < russell::color_count; ++c) {
    for(auto pt = 0; pt < russell::piece_type_count; ++pt) {
      auto bb = pos.pieces(static_cast<russell::color>(c), static_cast<russell::piece_type>(pt));
      while(bb != 0) {
        const auto sq = static_cast<int>(std::countr_zero(bb));
        bb &= bb - 1;
        out.set_piece(static_cast<russell::color>(c), static_cast<russell::piece_type>(pt), sq ^ 7);
      }
    }
  }
  out.set_side_to_move(pos.side_to_move());
  return out;
}

auto test_king_buckets() -> void
{
  using namespace russell::eval::nnue;
  require(king_buckets == 8, "eight mirrored king buckets");
  require(half_feature_space == king_buckets * 640, "half space matches buckets");
  require(feature_space == 2 * half_feature_space, "full space doubles halves");

  const auto b1 = 1;
  const auto g1 = 6;
  const auto a1 = 0;
  const auto h1 = 7;
  require(king_bucket(b1) == king_bucket(g1), "mirrored files share a bucket");
  require(king_bucket(a1) == king_bucket(h1), "edge files share a bucket");
  require(king_bucket(b1) != king_bucket(a1), "distinct files use distinct buckets");

  require(orient_square(g1, g1) == orient_square(b1, b1), "mirrored king maps to itself");
  const auto c3 = 18;
  require(orient_square(g1, c3) == orient_square(b1, c3 ^ 7),
          "mirrored pieces share oriented squares");

  const auto net = russell::eval::nnue::network::make_random(0xB0C4E7);
  const auto fens = std::vector<std::string>{
    russell::position::start().fen(),
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
  };
  for(const auto& fen : fens) {
    const auto pos = russell::position::from_fen(fen);
    const auto mirrored_pos = flip_horizontal(pos);
    require_msg(russell::eval::nnue::evaluate(net, pos)
                    == russell::eval::nnue::evaluate(net, mirrored_pos),
                "horizontal mirror is bucket-invariant: " + fen);

    auto fresh = russell::eval::nnue::evaluator{net};
    fresh.refresh(pos);
    require(fresh.evaluate(pos) == russell::eval::nnue::evaluate(net, pos),
            "fresh accumulator matches stateless eval");
    auto fresh_m = russell::eval::nnue::evaluator{net};
    fresh_m.refresh(mirrored_pos);
    require(fresh_m.evaluate(mirrored_pos) == russell::eval::nnue::evaluate(net, mirrored_pos),
            "fresh mirrored accumulator matches stateless eval");
  }
}

auto test_bucket_boundary_walk() -> void
{
  using namespace russell::eval::nnue;

  const auto probe_piece_sq = 27;
  const auto probe_enc = 0;
  for(auto ksq = 0; ksq < 64; ++ksq) {
    const auto mirror_ksq = ksq ^ 7;
    require(king_bucket(ksq) == king_bucket(mirror_ksq), "mirror pair shares bucket");
    require(orient_square(ksq, probe_piece_sq) == orient_square(mirror_ksq, probe_piece_sq ^ 7),
            "mirror pair shares oriented squares");
    require(feature_index(king_bucket(ksq), probe_enc, orient_square(ksq, probe_piece_sq))
                == feature_index(king_bucket(mirror_ksq), probe_enc,
                                 orient_square(mirror_ksq, probe_piece_sq ^ 7)),
            "mirror pair shares feature index");
  }
  require(king_bucket(0) != king_bucket(1), "a1/b1 buckets differ");
  require(king_bucket(3) == king_bucket(4), "d-file/e-file buckets mirror");
  require(king_bucket(24) != king_bucket(32), "rank-half boundary changes bucket");

  const auto net = russell::eval::nnue::network::make_random(0xB04D4);
  const auto transitions = std::vector<std::pair<int, int>>{
    {11, 12}, {12, 11}, {19, 20}, {27, 28}, {35, 36}, {43, 44}, {51, 52}, {59, 60},
    {8, 9}, {16, 17}, {9, 10}, {24, 25}, {24, 32}, {27, 35}, {30, 38}, {26, 34},
  };
  const auto sides = std::array<russell::color, 2>{russell::color::white, russell::color::black};
  for(std::size_t s = 0; s < sides.size(); ++s) {
    const auto stm = sides[s];
    for(std::size_t t = 0; t < transitions.size(); ++t) {
      const auto from_sq = transitions[t].first;
      const auto to_sq = transitions[t].second;
      const auto walk = stm;
      const auto sit = russell::opposite(stm);
      auto sit_sq = 60;
      if(from_sq == 60 || to_sq == 60) {
        sit_sq = 4;
      }
      auto pos = russell::position::empty();
      pos.set_piece(walk, russell::piece_type::king, from_sq);
      pos.set_piece(sit, russell::piece_type::king, sit_sq);
      pos.set_piece(russell::color::white, russell::piece_type::pawn, 48);
      pos.set_piece(russell::color::black, russell::piece_type::pawn, 53);
      pos.set_piece(russell::color::white, russell::piece_type::knight, 42);
      pos.set_side_to_move(stm);

      auto eval = russell::eval::nnue::evaluator{net};
      eval.refresh(pos);
      verify_state(eval, net, pos);

      auto mv = russell::move{};
      mv.from = static_cast<std::uint8_t>(from_sq);
      mv.to = static_cast<std::uint8_t>(to_sq);
      mv.flag = russell::move_flag::quiet;
      const auto before = eval.acc();
      auto st = pos.make_move(mv);
      eval.make_move(pos, mv, st);
      require_msg(king_square(pos, walk) == to_sq, "king crossed boundary");
      verify_state(eval, net, pos);

      eval.unmake_move();
      pos.unmake_move(mv, st);
      require(eval.ready(), "boundary unmake stays ready");
      for(auto k = 0; k < 4; ++k) {
        require(eval.acc()[k] == before[k], "boundary unmake restores accumulator");
      }
      verify_state(eval, net, pos);
    }
  }
}

auto test_full_sequence_unwind() -> void
{
  const auto fens = std::vector<std::string>{
    russell::position::start().fen(),
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "4k3/8/8/8/3K4/8/8/8 w - - 0 1",
    "6k1/8/8/8/8/8/8/2K5 b - - 0 1",
    "8/8/8/3k4/8/2K5/8/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 b kq - 0 1",
  };
  auto seed_list = std::vector<std::uint64_t>{0xA11CEULL, 0xB2B00ULL, 0xC40FFULL};
  const auto net = std::make_shared<const russell::eval::nnue::network>(
      russell::eval::nnue::network::make_random(0xF007D));
  auto checked = 0;
  for(const auto& fen : fens) {
    for(const auto seed : seed_list) {
      auto r = rng{seed};
      auto pos = russell::position::from_fen(fen);
      auto eval = russell::eval::nnue::evaluator{*net};
      eval.refresh(pos);
      const auto initial_acc = eval.acc();
      const auto initial_ready = eval.ready();
      const auto initial_eval = eval.evaluate(pos);
      verify_state(eval, *net, pos);

      auto history = std::vector<std::pair<russell::move, russell::move_state>>{};
      auto null_history = std::vector<russell::move_state>{};
      history.reserve(256);
      for(auto i = 0; i < 200; ++i) {
        auto legal = russell::movegen::generate_legal(pos);
        if(legal.size() == 0) {
          break;
        }
        const auto mv = legal[static_cast<std::size_t>(r.next() % legal.size())];
        auto st = pos.make_move(mv);
        eval.make_move(pos, mv, st);
        history.push_back({mv, st});

        verify_state(eval, *net, pos);
        ++checked;

        if((r.next() & 0x7) == 0 && !side_in_check(pos)) {
          const auto null_st = pos.make_null_move();
          eval.make_null_move(pos);
          null_history.push_back(null_st);
          verify_state(eval, *net, pos);
          ++checked;
          eval.unmake_move();
          pos.unmake_null_move(null_history.back());
          null_history.pop_back();
          verify_state(eval, *net, pos);
        }
      }

      while(!history.empty()) {
        const auto [mv, st] = history.back();
        history.pop_back();
        eval.unmake_move();
        pos.unmake_move(mv, st);
      }
      require(eval.ready() == initial_ready, "full unwind restores ready flag");
      for(auto k = 0; k < 4; ++k) {
        require(eval.acc()[k] == initial_acc[k], "full unwind restores accumulator");
      }
      require(eval.evaluate(pos) == initial_eval, "full unwind restores evaluation");
      require(pos.fen() == fen, "full unwind restores position");
      verify_state(eval, *net, pos);
    }
  }
  require_msg(checked > 500, "differential sweep covered enough plies: " + std::to_string(checked));
}

auto write_nnue_header(const std::string& path, std::uint32_t version, std::uint32_t ft,
                       std::uint32_t l1, std::uint32_t l2, std::uint32_t fspace,
                       std::uint32_t idims, std::size_t body_bytes) -> void
{
  auto out = std::ofstream{path, std::ios::binary | std::ios::trunc};
  out.write("TUNNU1", 6);
  const auto put = [&out](std::uint32_t v) {
    out.put(static_cast<char>(v & 0xFF));
    out.put(static_cast<char>((v >> 8) & 0xFF));
    out.put(static_cast<char>((v >> 16) & 0xFF));
    out.put(static_cast<char>((v >> 24) & 0xFF));
  };
  put(version);
  put(ft);
  put(l1);
  put(l2);
  put(fspace);
  put(idims);
  for(std::size_t i = 0; i < body_bytes; ++i) {
    out.put(static_cast<char>(i & 0xFF));
  }
}

auto test_serialization_rejection() -> void
{
  using namespace russell::eval::nnue;
  const auto dir = std::filesystem::temp_directory_path().string();
  const auto valid = dir + "/russell_nnue_v2_valid.bin";
  const auto net = network::make_random(0x5E21A1);
  require(net.save(valid), "v2 network saves");
  auto loaded = network{};
  require(loaded.load(valid), "valid v2 file loads");
  require(loaded.w1() == net.w1(), "v2 roundtrip weights match");
  const auto pos = russell::position::start();
  require(evaluate(loaded, pos) == evaluate(net, pos), "v2 roundtrip eval matches");

  auto probe = network{};
  const auto v1 = dir + "/russell_nnue_v1_reject.bin";
  write_nnue_header(v1, 1, 256, 32, 32, 81920, 512, 1024);
  require(!probe.load(v1), "v1 file rejected");

  const auto dims = dir + "/russell_nnue_dims_reject.bin";
  write_nnue_header(dims, 2, 999, l1_size, l2_size, feature_space, input_dims, 1024);
  require(!probe.load(dims), "dimension mismatch rejected");

  const auto trunc = dir + "/russell_nnue_trunc_reject.bin";
  write_nnue_header(trunc, 2, ft_size, l1_size, l2_size, feature_space, input_dims, 16);
  require(!probe.load(trunc), "truncated file rejected");

  const auto empty_path = dir + "/russell_nnue_empty_reject.bin";
  {
    auto out = std::ofstream{empty_path, std::ios::binary | std::ios::trunc};
  }
  require(!probe.load(empty_path), "empty file rejected");

  const auto badver = dir + "/russell_nnue_badver_reject.bin";
  write_nnue_header(badver, 99, ft_size, l1_size, l2_size, feature_space, input_dims, 16);
  require(!probe.load(badver), "version mismatch rejected");

  std::filesystem::remove(valid);
  std::filesystem::remove(v1);
  std::filesystem::remove(dims);
  std::filesystem::remove(trunc);
  std::filesystem::remove(empty_path);
  std::filesystem::remove(badver);
}

}

auto main() -> int
{
  test_roundtrip();
  test_load_rejects_garbage();
  test_random_games();
  test_move_type_sync();
  test_null_move_integrity();
  test_sequence_with_null_moves();
  test_symmetry();
  test_king_buckets();
  test_bucket_boundary_walk();
  test_full_sequence_unwind();
  test_serialization_rejection();
  test_trainer_feature_parity();
  test_architecture_dimensions();
  test_eval_path_integration();
  test_eval_dispatch();
  test_determinism();
  test_missing_king_falls_back();
  return 0;
}
