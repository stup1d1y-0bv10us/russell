#include "core/position.hpp"
#include "movegen/attacks.hpp"
#include "movegen/movegen.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using russell::bit;
using russell::bitboard;
using russell::color;
using russell::move;
using russell::move_flag;
using russell::piece_type;

auto splitmix64(std::uint64_t& state) noexcept -> std::uint64_t
{
  state += 0x9E3779B97F4A7C15ULL;
  auto z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

[[nodiscard]] auto pack_move(move mv) noexcept -> std::uint32_t
{
  return static_cast<std::uint32_t>(mv.from)
         | (static_cast<std::uint32_t>(mv.to) << 6)
         | (static_cast<std::uint32_t>(mv.promotion) << 12)
         | (static_cast<std::uint32_t>(mv.flag) << 15);
}

[[nodiscard]] auto is_capture_ref(move mv) noexcept -> bool
{
  return mv.flag == move_flag::capture || mv.flag == move_flag::en_passant
         || mv.flag == move_flag::promotion_capture;
}

[[nodiscard]] auto castling_ok(const russell::position& pos, move mv, color us) noexcept -> bool
{
  const auto rights = pos.castling_rights();
  const auto occ = pos.occupancy();
  const auto king_rank = us == color::white ? 0 : 7;
  const auto king_sq = russell::make_square(4, king_rank);
  if((pos.pieces(us, piece_type::king) & bit(king_sq)) == 0) return false;
  const auto by = russell::opposite(us);
  const auto from = static_cast<int>(mv.from);
  const auto to = static_cast<int>(mv.to);
  if(to > from) {
    const auto want = us == color::white ? russell::white_king_side : russell::black_king_side;
    if((rights & want) == 0) return false;
    if((pos.pieces(us, piece_type::rook) & bit(russell::make_square(7, king_rank))) == 0) return false;
    if((occ & (bit(russell::make_square(5, king_rank)) | bit(russell::make_square(6, king_rank)))) != 0)
      return false;
  } else {
    const auto want = us == color::white ? russell::white_queen_side : russell::black_queen_side;
    if((rights & want) == 0) return false;
    if((pos.pieces(us, piece_type::rook) & bit(russell::make_square(0, king_rank))) == 0) return false;
    if((occ
        & (bit(russell::make_square(1, king_rank)) | bit(russell::make_square(2, king_rank))
            | bit(russell::make_square(3, king_rank))))
       != 0)
      return false;
  }
  if(russell::movegen::is_square_attacked(pos, from, by)) return false;
  const auto between = to > from ? from + 1 : from - 1;
  if(russell::movegen::is_square_attacked(pos, between, by)) return false;
  if(russell::movegen::is_square_attacked(pos, to, by)) return false;
  return true;
}

[[nodiscard]] auto brute_legal(russell::position& pos) -> std::vector<std::uint32_t>
{
  const auto us = pos.side_to_move();
  const auto them = russell::opposite(us);
  auto out = std::vector<std::uint32_t>{};
  for(const auto mv : russell::movegen::generate_pseudo_legal(pos)) {
    auto ok = false;
    if(mv.flag == move_flag::castling) {
      ok = castling_ok(pos, mv, us);
    } else {
      const auto st = pos.make_move(mv);
      const auto kings = pos.pieces(us, piece_type::king);
      ok = true;
      if(kings != 0) {
        ok = !russell::movegen::is_square_attacked(
            pos, static_cast<int>(std::countr_zero(kings)), them);
      }
      pos.unmake_move(mv, st);
    }
    if(ok) out.push_back(pack_move(mv));
  }
  std::sort(out.begin(), out.end());
  return out;
}

[[nodiscard]] auto engine_set(russell::move_list list) -> std::vector<std::uint32_t>
{
  auto out = std::vector<std::uint32_t>{};
  for(const auto mv : list) out.push_back(pack_move(mv));
  std::sort(out.begin(), out.end());
  return out;
}

[[nodiscard]] auto filter_caps(const std::vector<std::uint32_t>& all, russell::position& pos)
    -> std::vector<std::uint32_t>
{

  (void)pos;
  auto out = std::vector<std::uint32_t>{};
  for(const auto p : all) {
    const auto flag = static_cast<move_flag>((p >> 15) & 0x7);
    if(flag == move_flag::capture || flag == move_flag::en_passant
       || flag == move_flag::promotion_capture || flag == move_flag::promotion) {
      out.push_back(p);
    }
  }
  return out;
}

[[nodiscard]] auto filter_quiets(const std::vector<std::uint32_t>& all) -> std::vector<std::uint32_t>
{
  auto out = std::vector<std::uint32_t>{};
  for(const auto p : all) {
    const auto flag = static_cast<move_flag>((p >> 15) & 0x7);
    if(flag == move_flag::capture || flag == move_flag::en_passant
       || flag == move_flag::promotion_capture) {
      continue;
    }
    out.push_back(p);
  }
  return out;
}

auto failures = 0;
auto checked = 0;
auto diffs_printed = 0;

auto print_diff(const std::vector<std::uint32_t>& eng, const std::vector<std::uint32_t>& ref) -> void
{
  if(diffs_printed >= 3) return;
  ++diffs_printed;
  const auto show = [](std::uint32_t p) {
    const auto from = static_cast<int>(p & 0x3f);
    const auto to = static_cast<int>((p >> 6) & 0x3f);
    const auto promo = static_cast<int>((p >> 12) & 0x7);
    const auto flag = static_cast<int>((p >> 15) & 0x7);
    std::printf("    %c%c-%c%c pr=%d fl=%d\n", 'a' + (from & 7), '1' + (from >> 3),
                'a' + (to & 7), '1' + (to >> 3), promo, flag);
  };
  std::printf("  engine-only:\n");
  for(const auto p : eng) {
    if(!std::binary_search(ref.begin(), ref.end(), p)) show(p);
  }
  std::printf("  ref-only:\n");
  for(const auto p : ref) {
    if(!std::binary_search(eng.begin(), eng.end(), p)) show(p);
  }
}

auto check_position(russell::position& pos, const std::string& tag) -> void
{
  const auto ref = brute_legal(pos);
  {
    auto copy = pos;
    const auto eng = engine_set(russell::movegen::generate_legal(copy));
    ++checked;
    if(eng != ref) {
      ++failures;
      std::printf("MISMATCH legal %s fen=%s\n", tag.c_str(), copy.fen().c_str());
      print_diff(eng, ref);
    }
  }
  {
    auto copy = pos;
    const auto eng = engine_set(russell::movegen::generate_captures_promotions(copy));
    ++checked;
    if(const auto exp = filter_caps(ref, copy); eng != exp) {
      ++failures;
      std::printf("MISMATCH caps %s fen=%s\n", tag.c_str(), copy.fen().c_str());
      print_diff(eng, exp);
    }
  }
  {
    auto copy = pos;
    const auto eng = engine_set(russell::movegen::generate_quiet_moves(copy));
    ++checked;
    if(const auto exp = filter_quiets(ref); eng != exp) {
      ++failures;
      std::printf("MISMATCH quiets %s fen=%s\n", tag.c_str(), copy.fen().c_str());
      print_diff(eng, exp);
    }
  }
  {

    const auto us = pos.side_to_move();
    auto copy = pos;
    const auto kings = copy.pieces(us, piece_type::king);
    if(kings != 0) {
      const auto ksq = static_cast<int>(std::countr_zero(kings));
      if(russell::movegen::is_square_attacked(copy, ksq, russell::opposite(us))) {
        const auto eng = engine_set(russell::movegen::generate_evasions(copy));
        ++checked;

        const auto occ = pos.occupancy();
        auto checkers = bitboard{0};
        {
          const auto by = russell::opposite(us);
          checkers =
              (russell::movegen::pawn_attacks(us, ksq) & pos.pieces(by, piece_type::pawn))
              | (russell::movegen::knight_attacks(ksq) & pos.pieces(by, piece_type::knight))
              | (russell::movegen::king_attacks(ksq) & pos.pieces(by, piece_type::king));
          checkers |= russell::movegen::bishop_attacks(ksq, occ)
                      & (pos.pieces(by, piece_type::bishop) | pos.pieces(by, piece_type::queen));
          checkers |= russell::movegen::rook_attacks(ksq, occ)
                      & (pos.pieces(by, piece_type::rook) | pos.pieces(by, piece_type::queen));
        }
        auto allow = bitboard{0};
        if(std::popcount(checkers) == 1) {
          const auto csq = static_cast<int>(std::countr_zero(checkers));
          allow = bit(csq);
          const auto cpiece = pos.piece_on(csq);
          if(cpiece != russell::piece::none) {
            const auto cpt = russell::piece_type_of(cpiece);
            if(cpt == piece_type::bishop || cpt == piece_type::rook || cpt == piece_type::queen) {
              allow |= (russell::movegen::rook_attacks(ksq, bit(csq))
                        & russell::movegen::rook_attacks(csq, bit(ksq)))
                       | (russell::movegen::bishop_attacks(ksq, bit(csq))
                          & russell::movegen::bishop_attacks(csq, bit(ksq)));
            }
          }
        }
        auto exp = std::vector<std::uint32_t>{};
        for(const auto p : ref) {
          const auto from = static_cast<int>(p & 0x3f);
          const auto to = static_cast<int>((p >> 6) & 0x3f);
          const auto flag = static_cast<move_flag>((p >> 15) & 0x7);
          if(flag == move_flag::castling) continue;
          if(from != ksq && (allow & bit(to)) == 0 && flag != move_flag::en_passant) continue;
          exp.push_back(p);
        }
        std::sort(exp.begin(), exp.end());
        if(eng != exp) {
          ++failures;
          std::printf("MISMATCH evasions %s fen=%s\n", tag.c_str(), copy.fen().c_str());
        }
      }
    }
  }
}

}

auto main() -> int
{
  const std::vector<std::string> fens = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "rnbqkbnr/ppp1pppp/8/8/4P3/8/PPPP1PPP/RNBQK2R b KQkq e3 0 2",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "r3k2r/8/8/8/3Q4/8/8/R3K2R b KQkq - 0 1",
    "4rk1/8/8/8/8/8/5PPP/4RK1 w - - 0 1",
    "4r1k1/8/8/8/8/8/5PPP/4R1K1 w - - 0 1",
    "2r3k1/8/8/8/8/8/5PPP/2R3K1 w - - 0 1",
    "k7/8/8/8/8/8/8/R6K w - - 0 1",
    "k7/8/8/8/8/8/8/R5K1 w - - 0 1",
    "k7/8/8/8/8/8/1r6/R5K1 w - - 0 1",
    "k7/8/8/8/8/8/3q4/R5K1 w - - 0 1",
    "k7/8/8/8/8/8/8/R5K1 b - - 0 1",
    "8/8/8/3k4/8/8/3N4/3K4 b - - 0 1",
    "8/8/8/3k4/8/8/3B4/3K4 b - - 0 1",
    "8/8/8/3k4/8/8/3R4/3K4 b - - 0 1",
    "8/8/8/3k4/8/8/3Q4/3K4 b - - 0 1",
    "8/8/8/3k4/2p5/8/3P4/3K4 b - - 0 1",
    "8/8/8/8/3k4/8/2K5/8 w - - 0 1",
    "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1",
    "r3k2r/pppppppp/8/8/8/3q4/PPPPPPPP/R3K2R w KQkq - 0 1",
    "r3k2r/pppppppp/8/8/8/3q4/PPPPPPPP/R3K1R w KQkq - 0 1",
    "r3k2r/pppppppp/8/8/8/8/PPPPnPPP/R3K2R w KQkq - 0 1",
    "r3k2r/pppppppp/8/8/8/8/PPPPbPPP/R3K2R w KQkq - 0 1",
    "8/1P6/8/8/8/1K6/8/7k w - - 0 1",
    "8/1P6/1K6/8/8/8/8/7k w - - 0 1",
    "8/8/8/2K5/3Pp3/8/8/7k w - e3 0 1",
    "8/8/8/2K5/3Pp3/8/8/7k b - - 0 1",
    "8/8/1k6/8/3Pp3/8/8/6K1 w - e3 0 1",
    "8/8/8/8/1K2Pp1R/8/8/7k w - f3 0 1",
    "6k1/5ppp/8/8/3Q4/8/5PPP/6K1 b - - 0 1",
    "rnbqkb1r/pppppppp/5n2/8/8/5N2/PPPPPPPP/RNBQKB1R w KQkq - 2 2",
    "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3",
    "k7/3p4/8/3P4/8/8/8/K7 b - - 0 1",
    "k7/8/8/3pP3/8/8/8/K7 w - d6 0 1",
    "r2q1rk1/pp1bppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 0 10",
    "2r3k1/p5pp/8/8/8/8/P4PPP/2R3K1 w - - 0 1",
    "3qk3/8/8/8/8/8/8/3QK3 w - - 0 1",
    "3qk3/8/8/8/8/8/5PPP/3QK3 b - - 0 1",
    "8/8/8/8/8/1k6/3r4/R5K1 w - - 0 1",
    "8/8/8/8/8/1k6/3b4/R5K1 w - - 0 1",
    "8/8/8/8/8/1k6/3n4/R5K1 w - - 0 1",

    "4r1k1/8/8/8/1b6/8/5PPP/4K2R w K - 0 1",
    "4r1k1/8/8/8/8/5n2/5PPP/4K2R w K - 0 1",

    "4r1k1/8/8/3pP3/8/8/5PPP/4K2R w - d6 0 1",
    "6k1/8/8/3pP3/8/8/5PPP/4K2R w - d6 0 1",

    "r3k2r/pppppppp/8/8/2b5/8/PPPP1PPP/R3K2R w KQkq - 0 1",

    "r3k2r/ppp2ppp/8/8/8/8/PPPP1PPP/R3K2R w KQkq - 0 1",

    "8/8/8/3k4/3K4/8/8/8 w - - 0 1",

    "4rk1/8/8/8/1b6/2N5/5PPP/4K2R w K - 0 1",
    "3qk3/8/8/8/8/8/3B1PPP/4K2R w K - 0 1",

    "4r1k1/8/8/8/8/8/4RPPP/4K2R w K - 0 1",

    "4rk1/8/8/8/1b6/8/3Q1PPP/4K2R w K - 0 1",

    "4r1k1/8/8/8/8/3p1p2/4P1PP/4K2R w K - 0 1",

    "8/8/8/8/8/5K2/6p1/7k w - - 0 1",

    "R5k1/1P6/8/8/8/8/8/1K6 w - - 0 1",

    "6k1/8/8/8/8/8/5rPP/6K1 w - - 0 1",
    "6k1/8/8/8/8/3n4/5PPP/6K1 w - - 0 1",
    "6k1/8/8/8/8/8/4qPPP/6K1 w - - 0 1",
    "6k1/8/8/8/1b6/8/5P1P/6K1 w - - 0 1",
    "8/8/8/8/8/5Kb1/6p1/7k w - - 0 1",
    "6k1/8/8/8/8/8/4qrPP/6K1 w - - 0 1",
  };
  auto total = 0;
  for(auto fen : fens) {
    auto pos = russell::position::from_fen(fen);
    check_position(pos, "fixed");
    ++total;
  }

  auto rng = std::uint64_t{0x123456789ABCDEFULL};
  constexpr int kGames = 200;
  constexpr int kPlies = 150;
  for(auto g = 0; g < kGames; ++g) {
    auto pos = russell::position::start();
    for(auto p = 0; p < kPlies; ++p) {
      auto copy = pos;
      const auto legal = russell::movegen::generate_legal(copy);
      if(legal.size() == 0) break;
      auto tag = "rand" + std::to_string(g) + ":" + std::to_string(p);
      check_position(pos, tag);
      ++total;
      const auto pick = static_cast<std::size_t>(splitmix64(rng) % legal.size());
      static_cast<void>(pos.make_move(legal[pick]));
      if(pos.halfmove_clock() >= 100) break;
    }
  }
  std::printf("legal_oracle: %d positions, %d set comparisons, %d failures\n", total, checked,
              failures);
  return failures == 0 ? 0 : 1;
}
