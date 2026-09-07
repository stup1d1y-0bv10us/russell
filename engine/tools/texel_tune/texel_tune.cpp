#include "core/position.hpp"
#include "eval/evaluate.hpp"
#include "movegen/movegen.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct sample {
  tuna::position pos;
  double result = 0.5;
};

auto square_name(int sq) -> std::string
{
  auto out = std::string{};
  out.push_back(static_cast<char>('a' + tuna::file_of(sq)));
  out.push_back(static_cast<char>('1' + tuna::rank_of(sq)));
  return out;
}

auto piece_letter(tuna::piece_type pt) noexcept -> char
{
  switch(pt) {
  case tuna::piece_type::knight: return 'N';
  case tuna::piece_type::bishop: return 'B';
  case tuna::piece_type::rook: return 'R';
  case tuna::piece_type::queen: return 'Q';
  case tuna::piece_type::king: return 'K';
  case tuna::piece_type::pawn: return 'P';
  }
  return 'P';
}

auto strip_san_suffix(std::string san) -> std::string
{
  while(!san.empty()) {
    const auto ch = san.back();
    if(ch == '+' || ch == '#' || ch == '!' || ch == '?') {
      san.pop_back();
    } else {
      break;
    }
  }
  return san;
}

auto normalize_token(std::string token) -> std::string
{
  token.erase(std::remove(token.begin(), token.end(), '\r'), token.end());
  token.erase(std::remove(token.begin(), token.end(), '\n'), token.end());
  const auto dot = token.find_last_of('.');
  if(dot != std::string::npos) {
    token.erase(0, dot + 1);
  }
  if(token == "0-0") {
    token = "O-O";
  } else if(token == "0-0-0") {
    token = "O-O-O";
  }
  return strip_san_suffix(token);
}

auto is_capture(const tuna::position& pos, tuna::move mv) noexcept -> bool
{
  return mv.flag == tuna::move_flag::capture
      || mv.flag == tuna::move_flag::en_passant
      || mv.flag == tuna::move_flag::promotion_capture
      || pos.piece_on(static_cast<int>(mv.to)) != tuna::piece::none;
}

auto move_san(tuna::position& pos, tuna::move mv) -> std::string
{
  const auto from = static_cast<int>(mv.from);
  const auto to = static_cast<int>(mv.to);
  const auto moving = pos.piece_on(from);
  const auto pt = tuna::piece_type_of(moving);
  if(mv.flag == tuna::move_flag::castling) {
    return to > from ? "O-O" : "O-O-O";
  }

  auto san = std::string{};
  const auto capture = is_capture(pos, mv);
  if(pt == tuna::piece_type::pawn) {
    if(capture) {
      san.push_back(static_cast<char>('a' + tuna::file_of(from)));
    }
  } else {
    san.push_back(piece_letter(pt));
    auto need_file = false;
    auto need_rank = false;
    const auto legal = tuna::movegen::generate_legal(pos);
    for(const auto other : legal) {
      if(other == mv || static_cast<int>(other.to) != to || static_cast<int>(other.from) == from) {
        continue;
      }
      if(pos.piece_on(static_cast<int>(other.from)) == moving) {
        if(tuna::file_of(static_cast<int>(other.from)) != tuna::file_of(from)) {
          need_file = true;
        } else {
          need_rank = true;
        }
      }
    }
    if(need_file) {
      san.push_back(static_cast<char>('a' + tuna::file_of(from)));
    }
    if(need_rank) {
      san.push_back(static_cast<char>('1' + tuna::rank_of(from)));
    }
  }
  if(capture) {
    san.push_back('x');
  }
  san += square_name(to);
  if(mv.flag == tuna::move_flag::promotion || mv.flag == tuna::move_flag::promotion_capture) {
    san.push_back('=');
    san.push_back(piece_letter(mv.promotion));
  }
  return san;
}

auto parse_san(tuna::position& pos, const std::string& token) -> std::optional<tuna::move>
{
  const auto wanted = normalize_token(token);
  const auto legal = tuna::movegen::generate_legal(pos);
  for(const auto mv : legal) {
    if(strip_san_suffix(move_san(pos, mv)) == wanted) {
      return mv;
    }
  }
  return std::nullopt;
}

auto result_value(std::string_view token) -> std::optional<double>
{
  if(token == "1-0") {
    return 1.0;
  }
  if(token == "0-1") {
    return 0.0;
  }
  if(token == "1/2-1/2") {
    return 0.5;
  }
  return std::nullopt;
}

auto pgn_tokens(const std::string& text) -> std::vector<std::string>
{
  auto cleaned = std::string{};
  cleaned.reserve(text.size());
  auto in_header = false;
  auto in_comment = false;
  auto variation_depth = 0;
  for(auto i = std::size_t{0}; i < text.size(); ++i) {
    const auto ch = text[i];
    if(in_comment) {
      if(ch == '}') {
        in_comment = false;
      }
      cleaned.push_back(' ');
      continue;
    }
    if(ch == '{') {
      in_comment = true;
      cleaned.push_back(' ');
      continue;
    }
    if(ch == ';') {
      while(i < text.size() && text[i] != '\n') {
        cleaned.push_back(' ');
        ++i;
      }
      continue;
    }
    if(ch == '[' && (i == 0 || text[i - 1] == '\n' || text[i - 1] == '\r')) {
      in_header = true;
      cleaned.push_back(' ');
      continue;
    }
    if(in_header) {
      if(ch == ']') {
        in_header = false;
      }
      cleaned.push_back(' ');
      continue;
    }
    if(ch == '(') {
      ++variation_depth;
      cleaned.push_back(' ');
      continue;
    }
    if(ch == ')') {
      if(variation_depth > 0) {
        --variation_depth;
      }
      cleaned.push_back(' ');
      continue;
    }
    cleaned.push_back(variation_depth == 0 ? ch : ' ');
  }

  auto tokens = std::vector<std::string>{};
  auto ss = std::istringstream{cleaned};
  for(auto token = std::string{}; ss >> token;) {
    tokens.push_back(token);
  }
  return tokens;
}

auto skip_token(const std::string& token) -> bool
{
  if(token.empty() || token[0] == '$') {
    return true;
  }
  return std::all_of(token.begin(), token.end(), [](unsigned char ch) {
    return std::isdigit(ch) != 0 || ch == '.';
  });
}

auto load_samples(const char* path, std::size_t max_samples) -> std::vector<sample>
{
  auto input = std::ifstream{path};
  if(!input) {
    std::fprintf(stderr, "failed to open PGN: %s\n", path);
    return {};
  }
  auto text = std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  auto samples = std::vector<sample>{};
  auto game_positions = std::vector<tuna::position>{};
  auto pos = tuna::position::start();
  for(const auto& token : pgn_tokens(text)) {
    if(skip_token(token)) {
      continue;
    }
    if(const auto result = result_value(token)) {
      for(const auto& game_pos : game_positions) {
        samples.push_back(sample{game_pos, *result});
        if(max_samples != 0 && samples.size() >= max_samples) {
          return samples;
        }
      }
      game_positions.clear();
      pos = tuna::position::start();
      continue;
    }
    const auto mv = parse_san(pos, token);
    if(!mv) {
      pos = tuna::position::start();
      continue;
    }
    const auto quiet = mv->flag == tuna::move_flag::quiet || mv->flag == tuna::move_flag::double_push;
    const auto st = pos.make_move(*mv);
    (void)st;
    if(quiet) {
      game_positions.push_back(pos);
    }
  }
  return samples;
}

auto sigmoid(double score, double k) noexcept -> double
{
  const auto x = std::clamp(k * score, -60.0, 60.0);
  return 1.0 / (1.0 + std::exp(-x));
}

auto mse(const std::vector<sample>& samples, const tuna::eval::weights& weights, double k) -> double
{
  auto sum = 0.0;
  for(const auto& s : samples) {
    const auto score = static_cast<double>(tuna::eval::evaluate(s.pos, weights));
    const auto prediction = sigmoid(score, k);
    const auto error = prediction - s.result;
    sum += error * error;
  }
  return samples.empty() ? 0.0 : sum / static_cast<double>(samples.size());
}

auto component(tuna::eval::weights& weights, int index) -> double&
{
  switch(index) {
  case 0: return weights.material;
  case 1: return weights.pst;
  case 2: return weights.mobility;
  case 3: return weights.pawn_structure;
  case 4: return weights.king_safety;
  default: return weights.rook_bonuses;
  }
}

auto name(int index) -> const char*
{
  switch(index) {
  case 0: return "material";
  case 1: return "pst";
  case 2: return "mobility";
  case 3: return "pawn_structure";
  case 4: return "king_safety";
  default: return "rook_bonuses";
  }
}

auto tune(const std::vector<sample>& samples, int passes, double k) -> tuna::eval::weights
{
  auto weights = tuna::eval::default_weights();
  auto best = mse(samples, weights, k);
  auto step = 0.25;
  for(auto pass = 0; pass < passes; ++pass) {
    auto changed = false;
    for(auto i = 0; i < 6; ++i) {
      auto& value = component(weights, i);
      const auto original = value;
      auto local_best = best;
      auto local_value = original;
      for(const auto delta : {step, -step}) {
        value = std::clamp(original + delta, 0.0, 4.0);
        const auto candidate = mse(samples, weights, k);
        if(candidate < local_best) {
          local_best = candidate;
          local_value = value;
        }
      }
      value = local_value;
      if(local_best < best) {
        best = local_best;
        changed = true;
      }
    }
    if(!changed) {
      step *= 0.5;
    }
    if(step < 0.001) {
      break;
    }
  }
  return weights;
}

}

auto main(int argc, char** argv) -> int
{
  if(argc < 2) {
    std::fprintf(stderr, "usage: tuna_texel_tune <games.pgn> [max_samples=0] [passes=40] [k=0.004]\n");
    return 2;
  }
  const auto max_samples = argc >= 3 ? static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10)) : std::size_t{0};
  const auto passes = argc >= 4 ? std::atoi(argv[3]) : 40;
  const auto k = argc >= 5 ? std::atof(argv[4]) : 0.004;
  const auto samples = load_samples(argv[1], max_samples);
  if(samples.empty()) {
    std::fprintf(stderr, "no quiet positions loaded\n");
    return 1;
  }

  const auto base = tuna::eval::default_weights();
  const auto base_mse = mse(samples, base, k);
  const auto tuned = tune(samples, passes, k);
  const auto tuned_mse = mse(samples, tuned, k);
  std::printf("samples: %llu\n", static_cast<unsigned long long>(samples.size()));
  std::printf("baseline_mse: %.9f\n", base_mse);
  std::printf("tuned_mse: %.9f\n", tuned_mse);
  std::printf("improved: %s\n", tuned_mse < base_mse ? "yes" : "no");
  auto out = tuned;
  for(auto i = 0; i < 6; ++i) {
    std::printf("%s = %.6f\n", name(i), component(out, i));
  }
  return tuned_mse <= base_mse ? 0 : 1;
}