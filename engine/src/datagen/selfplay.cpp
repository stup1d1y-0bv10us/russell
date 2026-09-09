#include "datagen/selfplay.hpp"

#include "core/position.hpp"
#include "datagen/dataset.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
#include "tb/tablebase.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <string>
#include <vector>

namespace russell::datagen {

namespace {

struct rng {
  std::uint64_t state = 0;
  explicit rng(std::uint64_t s) noexcept : state(s == 0 ? 0x9e3779b97f4a7c15ULL : s) {}
  auto next() noexcept -> std::uint64_t
  {
    state += 0x9E3779B97F4A7C15ULL;
    auto z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  auto below(int n) noexcept -> int { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
};

auto probe_termination(const position& pos, int& outcome) noexcept -> bool
{
  auto copy = pos;
  if(movegen::generate_legal(copy).size() != 0 && pos.halfmove_clock() < 100) {
    return false;
  }
  const auto king = pos.pieces(pos.side_to_move(), piece_type::king);
  if(king == 0) {
    outcome = 0;
    return true;
  }
  const auto king_sq = static_cast<int>(std::countr_zero(king));
  const auto mated = movegen::is_square_attacked(copy, king_sq, opposite(copy.side_to_move()));
  if(mated) {
    outcome = pos.side_to_move() == color::white ? -1 : +1;
  } else {
    outcome = 0;
  }
  return true;
}

auto verification_depth_for(const selfplay_options& opts) noexcept -> int
{
  if(opts.score_depth > 0) {
    return opts.score_depth;
  }
  auto d = opts.search_depth + 2;
  if(d < 4) d = 4;
  if(d > 8) d = 8;
  return d;
}

auto is_mate_score(int score) noexcept -> bool
{
  return std::abs(score) > 20000;
}

auto is_large_score(int score) noexcept -> bool
{
  return std::abs(score) > 800;
}

auto is_tablebase_hit(const position& pos) -> bool
{
  if(tb::largest() == 0) return false;
  if(tb::piece_count(pos) > tb::largest()) return false;
  tb::probe_result pr;
  return tb::probe_root(pos, pr);
}

auto non_pawn_material_pos(const position& pos) noexcept -> int
{
  auto count = [&](piece_type pt, int value) {
    auto bb = pos.pieces(color::white, pt) | pos.pieces(color::black, pt);
    return static_cast<int>(std::popcount(bb)) * value;
  };
  return count(piece_type::knight, 3) + count(piece_type::bishop, 3) + count(piece_type::rook, 5) + count(piece_type::queen, 9);
}

auto material_signature(const position& pos) noexcept -> std::uint64_t
{
  std::uint64_t sig = 0;
  for(auto c = 0; c < color_count; ++c) {
    for(auto pt = 0; pt < piece_type_count; ++pt) {
      const auto ptype = static_cast<piece_type>(pt);
      if(ptype == piece_type::king) continue;
      auto bb = pos.pieces(static_cast<color>(c), ptype);
      const auto cnt = static_cast<int>(std::popcount(bb));
      sig = sig * 31 + static_cast<std::uint64_t>(cnt + 1);
      sig = sig * 31 + static_cast<std::uint64_t>(c);
      sig = sig * 31 + static_cast<std::uint64_t>(pt);
    }
  }
  sig ^= static_cast<std::uint64_t>(pos.side_to_move() == color::white ? 0x9e3779b97f4a7c15ULL : 0xbf58476d1ce4e5b9ULL);
  return sig;
}

}

auto play_one_game(const selfplay_options& options, game_result& out) -> bool
{
  out = game_result{};
  auto rng_r = rng{options.seed};
  auto pos = position::start();
  auto plies = std::uint64_t{0};
  auto outcome = 0;
  auto history_keys = std::vector<std::uint64_t>{};
  history_keys.reserve(static_cast<std::size_t>(options.max_plies > 0 ? options.max_plies + 16 : 256));
  const auto vdepth = verification_depth_for(options);
  std::int64_t last_saved_ply = -1000;
  std::uint64_t last_material_sig = 0;
  int last_saved_eval = 0;
  bool has_last_saved = false;

  for(;;) {
    if(probe_termination(pos, outcome)) {
      break;
    }
    if(options.max_plies > 0 && plies >= static_cast<std::uint64_t>(options.max_plies)) {
      break;
    }

    const auto legal = movegen::generate_legal(pos);
    const bool is_repetition = std::find(history_keys.begin(), history_keys.end(), pos.key()) != history_keys.end();
    const bool is_tb = is_tablebase_hit(pos);

    auto record = pack_position(pos);
    auto search_pos = pos;
    const auto result = search::iterative_deepening(search_pos, options.search_depth);
    int verified_score = result.has_move ? result.score : 0;
    bool has_verified = false;
    if(vdepth != options.search_depth) {
      auto vpos = pos;
      const auto vresult = search::iterative_deepening(vpos, vdepth);
      if(vresult.has_move) {
        verified_score = vresult.score;
        has_verified = true;
      }
    } else if(result.has_move) {
      has_verified = true;
    }
    const bool is_mate = has_verified && is_mate_score(verified_score);
    const bool is_large = has_verified && is_large_score(verified_score);
    bool should_filter = is_repetition || is_tb || is_mate || is_large || !has_verified;
    if(!should_filter && has_verified) {
      const int npm = non_pawn_material_pos(pos);
      if(npm < options.diversity_min_non_pawn) {
        should_filter = true;
      } else if(has_last_saved && static_cast<std::int64_t>(plies) - last_saved_ply < options.diversity_min_ply_gap) {
        should_filter = true;
      } else if(has_last_saved) {
        const auto cur_sig = material_signature(pos);
        if(cur_sig == last_material_sig && std::abs(verified_score - last_saved_eval) < options.diversity_eval_threshold) {
          should_filter = true;
        }
      }
    }
    if(options.recording && (plies % options.sample_interval) == 0 && !should_filter) {
      record.score = verified_score;
      out.positions.push_back(record);
      last_saved_ply = static_cast<std::int64_t>(plies);
      last_material_sig = material_signature(pos);
      last_saved_eval = verified_score;
      has_last_saved = true;
    }

    auto mv = result.has_move ? result.best_move : legal[0];
    if(plies < static_cast<std::uint64_t>(options.opening_plies)
       || rng_r.below(1000) < options.random_temperature) {
      mv = legal[rng_r.below(static_cast<int>(legal.size()))];
    }

    history_keys.push_back(pos.key());
    static_cast<void>(pos.make_move(mv));
    ++plies;
  }

  if(!probe_termination(pos, outcome) && options.max_plies > 0
     && plies >= static_cast<std::uint64_t>(options.max_plies)) {
    outcome = 0;
  }
  out.plies = plies;
  out.outcome = outcome;

  for(auto& record : out.positions) {
    const auto stm = static_cast<color>(record.stm & 1);
    const auto white_centric = outcome;
    record.result = static_cast<std::int8_t>(stm == color::white ? white_centric : -white_centric);
  }
  return true;
}

auto generate_selfplay(const selfplay_options& options, const std::string& path,
                       std::uint64_t game_count) -> std::uint64_t
{
  auto header = dataset_header{};
  header.seed = options.seed;
  header.search_depth = options.search_depth;
  header.score_depth = verification_depth_for(options);
  header.sample_interval = options.sample_interval;
  header.max_plies = options.max_plies;
  header.game_count = game_count;

  auto writer = dataset_writer{path, header};
  auto written = std::uint64_t{0};
  for(auto g = std::uint64_t{0}; g < game_count; ++g) {
    auto opts = options;
    opts.seed = options.seed + g * 0x9E3779B97F4A7C15ULL;
    auto game = game_result{};
    static_cast<void>(play_one_game(opts, game));
    for(const auto& record : game.positions) {
      if(writer.write(record)) {
        ++written;
      }
    }
    if(!writer.good()) {
      break;
    }
  }
  if(!writer.finish()) {
    return 0;
  }
  return written;
}

}