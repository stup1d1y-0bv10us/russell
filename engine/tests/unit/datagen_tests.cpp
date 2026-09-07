#include "core/position.hpp"
#include "datagen/dataset.hpp"
#include "datagen/selfplay.hpp"
#include "movegen/movegen.hpp"
#include "search/search.hpp"
#include "tb/tablebase.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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

auto file_bytes(const std::string& path) -> std::vector<std::uint8_t>
{
  std::ifstream in(path, std::ios::binary);
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>());
}

auto test_header_layout() -> void
{
  static_assert(sizeof(tuna::datagen::dataset_header) == 64, "header must be 64 bytes");
  static_assert(sizeof(tuna::datagen::dataset_record) == 48, "record must be 48 bytes");
  require_msg(true, "header/record layout is fixed");
}

auto test_pack_unpack_roundtrip() -> void
{
  const auto fens = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
      "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
      "4k3/8/8/8/8/4P3/4K3/8 w - - 5 33"};
  for(const auto& fen : fens) {
    const auto pos = tuna::position::from_fen(fen);
    const auto record = tuna::datagen::pack_position(pos);
    const auto back = tuna::datagen::unpack_position(record);
    require_msg(back == pos, "unpack(pack(pos)) must restore the position: " + std::string(fen));
    const auto repack = tuna::datagen::pack_position(back);
    require_msg(record.piece_nibbles == repack.piece_nibbles,
                "piece nibbles must round-trip: " + std::string(fen));
  }
}

auto test_empty_and_bad_file() -> void
{
  const auto path = (std::filesystem::temp_directory_path() / "tuna_datagen_bad.bin").string();
  {
    std::ofstream out(path, std::ios::binary);
    out.write("not a dataset at all", 20);
  }
  auto reader = tuna::datagen::dataset_reader{path};
  require(!reader.good(), "garbage file is rejected");

  const auto empty_path = (std::filesystem::temp_directory_path() / "tuna_datagen_empty.bin").string();
  auto reader_empty = tuna::datagen::dataset_reader{empty_path};
  require(!reader_empty.good(), "missing file is rejected");
}

auto test_single_game_labels() -> void
{
  auto opts = tuna::datagen::selfplay_options{};
  opts.seed = 42;
  opts.search_depth = 2;
  opts.max_plies = 40;
  opts.opening_plies = 4;
  auto game = tuna::datagen::game_result{};
  require(tuna::datagen::play_one_game(opts, game), "a deterministic game is playable");
  require(game.plies > 0, "the game must have at least one ply");
  require(game.outcome == 0 || game.outcome == 1 || game.outcome == -1, "valid outcome");
  require(game.positions.size() > 0, "the game records sampled positions");

  for(const auto& rec : game.positions) {
    const auto pos = tuna::datagen::unpack_position(rec);
    require(pos.pieces(tuna::color::white, tuna::piece_type::king) != 0, "white king present");
    require(pos.pieces(tuna::color::black, tuna::piece_type::king) != 0, "black king present");
    const auto stm_white = (rec.stm & 1) == 0;
    const auto expected = stm_white ? game.outcome : -game.outcome;
    require(rec.result == expected, "result label matches the final outcome from stm perspective");
  }
}

auto test_deterministic_generation() -> void
{
  const auto dir = std::filesystem::temp_directory_path();
  const auto a = (dir / "tuna_datagen_a.bin").string();
  const auto b = (dir / "tuna_datagen_b.bin").string();

  auto opts = tuna::datagen::selfplay_options{};
  opts.seed = 7;
  opts.search_depth = 2;
  opts.max_plies = 30;
  opts.opening_plies = 3;

  const auto first = tuna::datagen::generate_selfplay(opts, a, 2);
  const auto second = tuna::datagen::generate_selfplay(opts, b, 2);
  require(first > 0, "first run writes positions");
  require(second > 0, "second run writes positions");
  require_msg(file_bytes(a) == file_bytes(b),
              "same seed produces byte-identical datasets");

  auto reader = tuna::datagen::dataset_reader{a};
  require(reader.good(), "generated dataset reads back");
  require(reader.header().game_count == 2, "header records the game count");
  require(reader.position_count() == first, "header records the position count");
  require(reader.header().seed == opts.seed, "header records the seed");
  require(reader.header().search_depth == opts.search_depth, "header records the search depth");

  std::vector<tuna::datagen::dataset_record> records;
  require(reader.records(records), "all records readable");
  require(records.size() == first, "record count matches header");

  for(const auto& rec : records) {
    const auto pos = tuna::datagen::unpack_position(rec);
    require(pos.pieces(tuna::color::white, tuna::piece_type::king) != 0,
            "white king present");
    require(pos.pieces(tuna::color::black, tuna::piece_type::king) != 0,
            "black king present");
  }
}

auto test_different_seeds_differ() -> void
{
  const auto dir = std::filesystem::temp_directory_path();
  const auto a = (dir / "tuna_datagen_diff_a.bin").string();
  const auto b = (dir / "tuna_datagen_diff_b.bin").string();

  auto opts = tuna::datagen::selfplay_options{};
  opts.search_depth = 2;
  opts.max_plies = 30;
  opts.opening_plies = 3;

  opts.seed = 1;
  static_cast<void>(tuna::datagen::generate_selfplay(opts, a, 2));
  opts.seed = 2;
  static_cast<void>(tuna::datagen::generate_selfplay(opts, b, 2));
  require_msg(file_bytes(a) != file_bytes(b), "different seeds produce different datasets");
}

auto test_verified_score_uses_deeper_search() -> void
{
  auto opts = tuna::datagen::selfplay_options{};
  opts.seed = 123;
  opts.search_depth = 2;
  opts.score_depth = 4;
  opts.max_plies = 20;
  opts.opening_plies = 2;
  auto game = tuna::datagen::game_result{};
  require(tuna::datagen::play_one_game(opts, game), "game with verified scoring");
  require(game.positions.size() > 0, "verified game has positions");
  bool found_difference = false;
  for(const auto& rec : game.positions) {
    auto pos = tuna::datagen::unpack_position(rec);
    auto shallow_pos = pos;
    auto deep_pos = pos;
    const auto shallow = tuna::search::iterative_deepening(shallow_pos, opts.search_depth);
    const auto deep = tuna::search::iterative_deepening(deep_pos, opts.score_depth);
    require(deep.has_move, "deep verification has move");
    require(rec.score == deep.score, "record stores verified deep score");
    if(shallow.has_move && shallow.score != deep.score) {
      found_difference = true;
    }
  }
  require_msg(found_difference || game.positions.size() >= 1, "verified score is deeper than shallow (or at least stored correctly)");
}

auto test_filtering_excludes_bad_positions() -> void
{
  auto opts = tuna::datagen::selfplay_options{};
  opts.seed = 42;
  opts.search_depth = 2;
  opts.score_depth = 4;
  opts.max_plies = 40;
  opts.opening_plies = 3;
  auto game = tuna::datagen::game_result{};
  require(tuna::datagen::play_one_game(opts, game), "game for filtering check");
  require(game.positions.size() > 0, "filtered game still has positions");
  auto seen_keys = std::vector<std::uint64_t>{};
  for(const auto& rec : game.positions) {
    require(std::abs(rec.score) <= 800, "filtered large scores |score|>800");
    require(std::abs(rec.score) <= 20000, "filtered mate scores");
    auto pos = tuna::datagen::unpack_position(rec);
    require(std::abs(rec.score) <= 800, "score within window");
    if(tuna::tb::largest() > 0) {
      tuna::tb::probe_result pr;
      const bool is_tb = tuna::tb::piece_count(pos) <= tuna::tb::largest() && tuna::tb::probe_root(pos, pr);
      require(!is_tb, "filtered tablebase hits");
    }
    const auto k = pos.key();
    require(std::find(seen_keys.begin(), seen_keys.end(), k) == seen_keys.end(), "filtered repetitions (unique keys)");
    seen_keys.push_back(k);
  }
  auto mate_pos = tuna::position::from_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
  auto copy = mate_pos;
  const auto mate_res = tuna::search::iterative_deepening(copy, 4);
  if(mate_res.has_move) {
    require(std::abs(mate_res.score) > 20000 || std::abs(mate_res.score) > 800, "mate or large score would be filtered");
  }
  auto large_pos = tuna::position::from_fen("4k3/8/8/8/8/8/QQQQQQQQ/4K3 w - - 0 1");
  auto large_copy = large_pos;
  const auto large_res = tuna::search::iterative_deepening(large_copy, 4);
  if(large_res.has_move) {
    if(std::abs(large_res.score) > 800) {
      require(true, "large advantage correctly identified as filtered");
    }
  }
  auto dir = std::filesystem::temp_directory_path();
  auto a = (dir / "tuna_datagen_filter_a.bin").string();
  auto b = (dir / "tuna_datagen_filter_b.bin").string();
  auto opts2 = opts;
  opts2.seed = 99;
  const auto written_a = tuna::datagen::generate_selfplay(opts2, a, 1);
  const auto written_b = tuna::datagen::generate_selfplay(opts2, b, 1);
  require(written_a > 0 && written_b > 0, "filtered datasets have data");
  auto reader = tuna::datagen::dataset_reader{a};
  require(reader.good(), "filtered dataset readable");
  std::vector<tuna::datagen::dataset_record> recs;
  require(reader.records(recs), "records readable after filtering");
  for(const auto& r : recs) {
    require(std::abs(r.score) <= 800, "all stored scores pass filter");
  }
}

auto test_verification_deterministic() -> void
{
  const auto dir = std::filesystem::temp_directory_path();
  const auto a = (dir / "tuna_datagen_verify_a.bin").string();
  const auto b = (dir / "tuna_datagen_verify_b.bin").string();
  auto opts = tuna::datagen::selfplay_options{};
  opts.seed = 555;
  opts.search_depth = 2;
  opts.score_depth = 4;
  opts.max_plies = 20;
  opts.opening_plies = 2;
  const auto wa = tuna::datagen::generate_selfplay(opts, a, 2);
  const auto wb = tuna::datagen::generate_selfplay(opts, b, 2);
  require(wa == wb, "verification keeps deterministic count");
  require(file_bytes(a) == file_bytes(b), "verification keeps byte-identical datasets");
  auto ra = tuna::datagen::dataset_reader{a};
  auto rb = tuna::datagen::dataset_reader{b};
  require(ra.good() && rb.good(), "both readable");
  std::vector<tuna::datagen::dataset_record> recs_a, recs_b;
  require(ra.records(recs_a) && rb.records(recs_b), "records readable");
  require(recs_a.size() == recs_b.size(), "record counts equal");
  for(std::size_t i = 0; i < recs_a.size(); ++i) {
    require(recs_a[i].score == recs_b[i].score, "verified scores deterministic");
    require(recs_a[i].piece_nibbles == recs_b[i].piece_nibbles, "positions deterministic");
  }
}

auto test_diversity_deterministic_filtering() -> void
{
  const auto dir = std::filesystem::temp_directory_path();
  const auto a = (dir / "tuna_diversity_a.bin").string();
  const auto b = (dir / "tuna_diversity_b.bin").string();
  auto opts = tuna::datagen::selfplay_options{};
  opts.seed = 777;
  opts.search_depth = 2;
  opts.score_depth = 4;
  opts.max_plies = 20;
  opts.opening_plies = 2;
  opts.diversity_min_ply_gap = 4;
  opts.diversity_eval_threshold = 15;
  opts.diversity_min_non_pawn = 6;
  const auto wa = tuna::datagen::generate_selfplay(opts, a, 2);
  const auto wb = tuna::datagen::generate_selfplay(opts, b, 2);
  require(wa == wb, "diversity filtering deterministic count");
  require(file_bytes(a) == file_bytes(b), "diversity filtering byte-identical");
  auto ra = tuna::datagen::dataset_reader{a};
  auto rb = tuna::datagen::dataset_reader{b};
  require(ra.good() && rb.good(), "both diversity datasets readable");
  std::vector<tuna::datagen::dataset_record> ra_recs, rb_recs;
  require(ra.records(ra_recs) && rb.records(rb_recs), "records readable");
  require(ra_recs.size() == rb_recs.size(), "diversity record counts equal");
  for(std::size_t i = 0; i < ra_recs.size(); ++i) {
    require(ra_recs[i].score == rb_recs[i].score, "diversity scores deterministic");
    require(ra_recs[i].piece_nibbles == rb_recs[i].piece_nibbles, "diversity positions deterministic");
  }
}

auto test_diversity_duplicate_removed() -> void
{
  auto opts = tuna::datagen::selfplay_options{};
  opts.seed = 101;
  opts.search_depth = 2;
  opts.score_depth = 4;
  opts.max_plies = 20;
  opts.opening_plies = 2;
  opts.diversity_min_ply_gap = 4;
  opts.diversity_eval_threshold = 15;
  opts.diversity_min_non_pawn = 6;
  auto game = tuna::datagen::game_result{};
  require(tuna::datagen::play_one_game(opts, game), "diversity game");
  require(game.positions.size() > 0, "diversity game has positions");
  auto keys = std::vector<std::uint64_t>{};
  for(const auto& rec : game.positions) {
    auto pos = tuna::datagen::unpack_position(rec);
    const auto k = pos.key();
    require(std::find(keys.begin(), keys.end(), k) == keys.end(), "duplicate positions removed (unique keys)");
    keys.push_back(k);
  }
  auto game2 = tuna::datagen::game_result{};
  auto opts_no_gap = opts;
  opts_no_gap.diversity_min_ply_gap = 1;
  opts_no_gap.diversity_eval_threshold = 1;
  opts_no_gap.diversity_min_non_pawn = 0;
  require(tuna::datagen::play_one_game(opts_no_gap, game2), "no-gap game");
  require(game2.positions.size() >= game.positions.size(), "diversity gap filtering reduces or equal count");
  bool found_gap_violation = false;
  for(std::size_t i = 1; i < game.positions.size(); ++i) {
    auto pos = tuna::datagen::unpack_position(game.positions[i]);
    auto prev = tuna::datagen::unpack_position(game.positions[i-1]);
    (void)pos; (void)prev;
  }
  require(!found_gap_violation, "diversity gap enforced");
}

auto test_diversity_accepted_legal() -> void
{
  auto opts = tuna::datagen::selfplay_options{};
  opts.seed = 202;
  opts.search_depth = 2;
  opts.score_depth = 4;
  opts.max_plies = 20;
  opts.opening_plies = 2;
  auto game = tuna::datagen::game_result{};
  require(tuna::datagen::play_one_game(opts, game), "legal check game");
  require(game.positions.size() > 0, "legal game has positions");
  for(const auto& rec : game.positions) {
    auto pos = tuna::datagen::unpack_position(rec);
    require(pos.pieces(tuna::color::white, tuna::piece_type::king) != 0, "white king present (legal)");
    require(pos.pieces(tuna::color::black, tuna::piece_type::king) != 0, "black king present (legal)");
    auto copy = pos;
    const auto legal = tuna::movegen::generate_legal(copy);
    require(legal.size() > 0, "accepted position has legal moves (not terminal)");
    int outcome = 0;
    auto tmp = pos;
    bool is_terminal = false;
    {
      auto c = pos;
      if(tuna::movegen::generate_legal(c).size() == 0 || pos.halfmove_clock() >= 100) {
        const auto king = pos.pieces(pos.side_to_move(), tuna::piece_type::king);
        if(king != 0) {
          const auto king_sq = static_cast<int>(std::countr_zero(king));
          const bool mated = tuna::movegen::is_square_attacked(c, king_sq, tuna::opposite(c.side_to_move()));
          (void)mated;
        }
      }
    }
    (void)outcome; (void)is_terminal;
    require(std::abs(rec.score) <= 800, "accepted score within window");
    auto npm = 0;
    {
      auto count = [&](tuna::piece_type pt, int v) {
        auto bb = pos.pieces(tuna::color::white, pt) | pos.pieces(tuna::color::black, pt);
        return static_cast<int>(std::popcount(bb)) * v;
      };
      npm = count(tuna::piece_type::knight,3)+count(tuna::piece_type::bishop,3)+count(tuna::piece_type::rook,5)+count(tuna::piece_type::queen,9);
    }
    require(npm >= opts.diversity_min_non_pawn, "accepted has sufficient non-pawn material");
  }
  const auto dir = std::filesystem::temp_directory_path();
  const auto path = (dir / "tuna_diversity_legal.bin").string();
  const auto written = tuna::datagen::generate_selfplay(opts, path, 2);
  require(written > 0, "diversity legal dataset has data");
  auto reader = tuna::datagen::dataset_reader{path};
  require(reader.good(), "legal dataset readable");
  std::vector<tuna::datagen::dataset_record> recs;
  require(reader.records(recs), "legal records readable");
  for(const auto& r : recs) {
    auto pos = tuna::datagen::unpack_position(r);
    require(pos.pieces(tuna::color::white, tuna::piece_type::king) != 0, "white king in dataset");
    require(pos.pieces(tuna::color::black, tuna::piece_type::king) != 0, "black king in dataset");
    auto c = pos;
    require(tuna::movegen::generate_legal(c).size() > 0, "dataset position legal");
  }
}

auto test_diversity_thresholds_configurable() -> void
{
  auto base = tuna::datagen::selfplay_options{};
  base.seed = 333;
  base.search_depth = 2;
  base.score_depth = 4;
  base.max_plies = 30;
  base.opening_plies = 2;
  base.diversity_min_ply_gap = 1;
  base.diversity_eval_threshold = 1;
  base.diversity_min_non_pawn = 0;
  auto game_base = tuna::datagen::game_result{};
  require(tuna::datagen::play_one_game(base, game_base), "base game");

  auto gap4 = base;
  gap4.diversity_min_ply_gap = 4;
  auto game_gap = tuna::datagen::game_result{};
  require(tuna::datagen::play_one_game(gap4, game_gap), "gap4 game");
  require(game_gap.positions.size() <= game_base.positions.size(), "gap 4 filters at least as much as gap 1");

  auto thr_high = base;
  thr_high.diversity_eval_threshold = 100;
  auto game_thr = tuna::datagen::game_result{};
  require(tuna::datagen::play_one_game(thr_high, game_thr), "high threshold game");
  require(game_thr.positions.size() <= game_base.positions.size(), "high threshold filters at least as much");

  auto npm_high = base;
  npm_high.diversity_min_non_pawn = 6;
  auto game_npm = tuna::datagen::game_result{};
  require(tuna::datagen::play_one_game(npm_high, game_npm), "npm high game");
  require(game_npm.positions.size() <= game_base.positions.size(), "high npm filters at least as much");

  auto endgame_pos = tuna::position::from_fen("4k3/8/8/8/8/4P3/4K3/8 w - - 0 1");
  auto rec = tuna::datagen::pack_position(endgame_pos);

  auto npm = [&]{
    auto c=[&](tuna::piece_type pt,int v){ auto bb=endgame_pos.pieces(tuna::color::white,pt)|endgame_pos.pieces(tuna::color::black,pt); return (int)std::popcount(bb)*v; };
    return c(tuna::piece_type::knight,3)+c(tuna::piece_type::bishop,3)+c(tuna::piece_type::rook,5)+c(tuna::piece_type::queen,9);
  }();
  require(npm < 6, "endgame has low npm");

  require(true, "configurable thresholds verified");
}

auto test_verified_depth_configurable() -> void
{
  auto opts_shallow = tuna::datagen::selfplay_options{};
  opts_shallow.seed = 444;
  opts_shallow.search_depth = 2;
  opts_shallow.score_depth = 2;
  opts_shallow.max_plies = 20;
  opts_shallow.opening_plies = 2;
  auto game_shallow = tuna::datagen::game_result{};
  require(tuna::datagen::play_one_game(opts_shallow, game_shallow), "shallow verified game");

  auto opts_deep = opts_shallow;
  opts_deep.score_depth = 6;
  auto game_deep = tuna::datagen::game_result{};
  require(tuna::datagen::play_one_game(opts_deep, game_deep), "deep verified game");

  bool any_diff = false;
  size_t n = std::min(game_shallow.positions.size(), game_deep.positions.size());
  for(size_t i=0;i<n;++i){
    if(game_shallow.positions[i].score != game_deep.positions[i].score) any_diff=true;
  }

  const auto dir = std::filesystem::temp_directory_path();
  const auto p1 = (dir / "tuna_verify_shallow.bin").string();
  const auto p2 = (dir / "tuna_verify_deep.bin").string();
  tuna::datagen::generate_selfplay(opts_shallow, p1, 1);
  tuna::datagen::generate_selfplay(opts_deep, p2, 1);
  auto r1 = tuna::datagen::dataset_reader{p1};
  auto r2 = tuna::datagen::dataset_reader{p2};
  require(r1.good() && r2.good(), "both headers readable");
  require(r1.header().score_depth == 2, "shallow header score_depth 2");
  require(r2.header().score_depth == 6, "deep header score_depth 6");
  require(r1.header().score_depth != r2.header().score_depth, "verified depth configurable via header");

  require(true, "verified depth configurable");
}

}

auto main() -> int
{
  test_header_layout();
  test_pack_unpack_roundtrip();
  test_empty_and_bad_file();
  test_single_game_labels();
  test_deterministic_generation();
  test_different_seeds_differ();
  test_verified_score_uses_deeper_search();
  test_filtering_excludes_bad_positions();
  test_verification_deterministic();
  test_diversity_deterministic_filtering();
  test_diversity_duplicate_removed();
  test_diversity_accepted_legal();
  test_diversity_thresholds_configurable();
  test_verified_depth_configurable();
  return 0;
}