#include "core/position.hpp"
#include "eval/evaluate.hpp"
#include "eval/nnue.hpp"
#include "movegen/movegen.hpp"

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

auto type_index(tuna::piece_type pt) -> int
{
  switch(pt) {
  case tuna::piece_type::pawn: return 0;
  case tuna::piece_type::knight: return 1;
  case tuna::piece_type::bishop: return 2;
  case tuna::piece_type::rook: return 3;
  case tuna::piece_type::queen: return 4;
  default: return -1;
  }
}

auto king_square(const tuna::position& pos, tuna::color c) -> int
{
  const auto bb = pos.pieces(c, tuna::piece_type::king);
  if(bb == 0) {
    return -1;
  }
  return static_cast<int>(std::countr_zero(bb));
}

auto flip(const tuna::position& pos) -> tuna::position
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

auto fp_reference(const tuna::eval::nnue::network& net, const tuna::position& pos) -> int
{
  using namespace tuna;
  using namespace tuna::eval;
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
        const auto own_index = own_king * 640 + enc * 64 + sq;
        const auto enemy_index = nnue::half_feature_space + enemy_king * 640 + enc * 64 + sq;
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
  const auto path = (std::filesystem::temp_directory_path() / "tuna_nnue_roundtrip.bin").string();
  auto net = tuna::eval::nnue::network::make_random(0x12345678);
  require(net.save(path), "placeholder net saves");
  auto loaded = tuna::eval::nnue::network{};
  require(loaded.load(path), "placeholder net loads");
  require(net.w1() == loaded.w1(), "w1 roundtrip");
  require(net.bias1() == loaded.bias1(), "bias1 roundtrip");
  require(net.w2() == loaded.w2(), "w2 roundtrip");
  require(net.bias2() == loaded.bias2(), "bias2 roundtrip");
  require(net.w3() == loaded.w3(), "w3 roundtrip");
  require(net.bias3() == loaded.bias3(), "bias3 roundtrip");
  require(net.w4() == loaded.w4(), "w4 roundtrip");
  require(net.bias4() == loaded.bias4(), "bias4 roundtrip");
  const auto pos = tuna::position::start();
  require(tuna::eval::nnue::evaluate(net, pos) == tuna::eval::nnue::evaluate(loaded, pos),
          "roundtrip eval matches");
  std::filesystem::remove(path);
}

auto test_load_rejects_garbage() -> void
{
  const auto path = (std::filesystem::temp_directory_path() / "tuna_nnue_garbage.bin").string();
  auto net = tuna::eval::nnue::network{};
  {
    auto out = std::ofstream{path, std::ios::binary};
    out << "not a network";
  }
  require(!net.load(path), "garbage file rejected");
  require(!net.save(path), "empty network cannot save");
  std::filesystem::remove(path);
}

auto verify_state(tuna::eval::nnue::evaluator& eval, const tuna::eval::nnue::network& net,
                  const tuna::position& pos) -> void
{
  auto own = std::array<std::int16_t, tuna::eval::nnue::ft_size>{};
  auto enemy = std::array<std::int16_t, tuna::eval::nnue::ft_size>{};
  require(tuna::eval::nnue::reference_accumulate(net, pos, own, enemy), "reference accumulates");
  const auto& acc = eval.acc();
  for(auto i = 0; i < tuna::eval::nnue::ft_size; ++i) {
    require(acc[0][i] == own[i], "white-frame own half matches reference");
    require(acc[1][i] == enemy[i], "white-frame enemy half matches reference");
  }
  const auto mirrored = flip(pos);
  require(tuna::eval::nnue::reference_accumulate(net, mirrored, own, enemy), "mirrored reference accumulates");
  for(auto i = 0; i < tuna::eval::nnue::ft_size; ++i) {
    require(acc[2][i] == own[i], "black-frame own half matches mirrored reference");
    require(acc[3][i] == enemy[i], "black-frame enemy half matches mirrored reference");
  }
  require(eval.evaluate(pos) == tuna::eval::nnue::evaluate(net, pos), "incremental eval matches stateless");
  require(tuna::eval::nnue::evaluate(net, pos) == fp_reference(net, pos), "stateless eval matches reference");
}

auto test_incremental_sequence(const tuna::position& start_pos) -> void
{
  auto r = rng{0xC0FFEE};
  const auto net = std::make_shared<const tuna::eval::nnue::network>(
      tuna::eval::nnue::network::make_random(0xDEADBEEF));
  auto pos = start_pos;
  auto eval = tuna::eval::nnue::evaluator{*net};
  eval.refresh(pos);
  verify_state(eval, *net, pos);

  auto history = std::vector<std::pair<tuna::move, tuna::move_state>>{};
  history.reserve(256);
  for(auto i = 0; i < 150; ++i) {
    auto legal = tuna::movegen::generate_legal(pos);
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
    tuna::position::start().fen(),
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
  };
  for(const auto& fen : fens) {
    test_incremental_sequence(tuna::position::from_fen(fen));
  }
}

auto side_in_check(const tuna::position& pos) -> bool
{
  const auto bb = pos.pieces(pos.side_to_move(), tuna::piece_type::king);
  if(bb == 0) {
    return false;
  }
  return tuna::movegen::is_square_attacked(pos, static_cast<int>(std::countr_zero(bb)),
                                           tuna::opposite(pos.side_to_move()));
}

auto verify_make_unmake(tuna::eval::nnue::evaluator& eval,
                        const tuna::eval::nnue::network& net, tuna::position& pos,
                        tuna::move mv, const char* what) -> void
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
    tuna::move_flag flag;
    const char* name;
  };
  const auto cases = std::vector<type_case>{
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
     tuna::move_flag::quiet, "ordinary quiet move"},
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
     tuna::move_flag::double_push, "double push"},
    {"3k4/8/3q4/8/3R4/8/8/4K3 w - - 0 1",
     tuna::move_flag::capture, "capture"},
    {"8/2P5/8/8/8/8/8/k1K5 w - - 0 1",
     tuna::move_flag::promotion, "promotion"},
    {"1r6/2P5/8/8/8/8/8/k1K5 w - - 0 1",
     tuna::move_flag::promotion_capture, "promotion capture"},
    {"k7/8/8/8/3pP3/8/8/K7 b KQkq e3 0 1",
     tuna::move_flag::en_passant, "en-passant"},
    {"4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1",
     tuna::move_flag::castling, "castling"},
    {"k7/8/8/8/8/8/8/K7 w - - 0 1",
     tuna::move_flag::quiet, "king move"},
  };

  auto seed = std::uint64_t{0xFEEDBEEF};
  for(const auto& c : cases) {
    const auto net = std::make_shared<const tuna::eval::nnue::network>(
        tuna::eval::nnue::network::make_random(seed));
    seed = seed * 0x9E3779B97F4A7C15ULL + 1;

    auto pos = tuna::position::from_fen(c.fen);
    auto eval = tuna::eval::nnue::evaluator{*net};
    eval.refresh(pos);
    verify_state(eval, *net, pos);

    auto legal = tuna::movegen::generate_legal(pos);
    auto chosen = tuna::move{};
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
      require(tuna::piece_type_of(pos.piece_on(static_cast<int>(chosen.from)))
                  == tuna::piece_type::king,
              "king move case actually moves the king");
    }

    verify_make_unmake(eval, *net, pos, chosen, c.name);
  }
}

auto test_null_move_integrity() -> void
{
  const auto net = std::make_shared<const tuna::eval::nnue::network>(
      tuna::eval::nnue::network::make_random(0xB00B5));
  auto pos = tuna::position::from_fen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  auto eval = tuna::eval::nnue::evaluator{*net};
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
    auto legal = tuna::movegen::generate_legal(pos);
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
  const auto net = std::make_shared<const tuna::eval::nnue::network>(
      tuna::eval::nnue::network::make_random(0xBEEFCAFE));
  auto pos = tuna::position::start();
  auto eval = tuna::eval::nnue::evaluator{*net};
  eval.refresh(pos);
  verify_state(eval, *net, pos);

  auto rng_state = std::uint64_t{0x0DDBA11};
  for(auto i = 0; i < 60; ++i) {
    auto legal = tuna::movegen::generate_legal(pos);
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
  const auto net = std::make_shared<const tuna::eval::nnue::network>(
      tuna::eval::nnue::network::make_random(0xBEEF));
  tuna::eval::set_nnue(net);
  const auto fens = std::vector<std::string>{
    tuna::position::start().fen(),
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "6k1/5ppp/8/8/8/8/8/1K1Q4 w - - 0 1",
  };
  for(const auto& fen : fens) {
    const auto pos = tuna::position::from_fen(fen);
    require(tuna::eval::evaluate(flip(pos)) == -tuna::eval::evaluate(pos),
            ("nnue eval symmetry " + fen).c_str());
  }
  tuna::eval::set_nnue(nullptr);
}

auto test_eval_dispatch() -> void
{
  const auto net = std::make_shared<const tuna::eval::nnue::network>(
      tuna::eval::nnue::network::make_random(0x5EED));
  const auto pos = tuna::position::from_fen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

  require(tuna::eval::evaluate(pos) == tuna::eval::evaluate(pos, tuna::eval::default_weights()),
          "classical eval is the default");

  tuna::eval::set_nnue(net);
  const auto stm_white = pos.side_to_move() == tuna::color::white;
  const auto nnue_score = tuna::eval::nnue::evaluate(*net, pos);
  require(tuna::eval::evaluate(pos) == (stm_white ? nnue_score : -nnue_score),
          "nnue replaces classical when a network is loaded");
  require(tuna::eval::evaluate(pos) != tuna::eval::evaluate(pos, tuna::eval::default_weights()),
          "nnue eval differs from classical on a busy position");

  tuna::eval::set_nnue(nullptr);
  require(tuna::eval::evaluate(pos) == tuna::eval::evaluate(pos, tuna::eval::default_weights()),
          "clearing the network restores classical eval");
}

auto test_determinism() -> void
{
  const auto net = tuna::eval::nnue::network::make_random(0xABCDEF);
  const auto pos = tuna::position::from_fen(
      "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10");
  auto a = tuna::eval::nnue::evaluator{net};
  auto b = tuna::eval::nnue::evaluator{net};
  a.refresh(pos);
  b.refresh(pos);
  require(a.evaluate(pos) == b.evaluate(pos), "independent evaluators agree");
  require(a.evaluate(pos) == a.evaluate(pos), "evaluation is deterministic");
}

auto test_missing_king_falls_back() -> void
{
  const auto net = tuna::eval::nnue::network::make_random(0x13579);
  const auto pos = tuna::position::empty();
  require(tuna::eval::nnue::evaluate(net, pos) == 0, "no kings evaluates to zero");
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
  test_eval_dispatch();
  test_determinism();
  test_missing_king_falls_back();
  return 0;
}