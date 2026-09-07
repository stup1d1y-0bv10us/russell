#include "tb/tablebase.hpp"

#include "movegen/movegen.hpp"
#include "search/search.hpp"

#include "tbprobe.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <mutex>
#include <optional>

namespace tuna::tb {

namespace {

auto g_mutex = std::mutex{};
auto g_initialized = false;

struct fathom_position {
  std::uint64_t white = 0;
  std::uint64_t black = 0;
  std::uint64_t kings = 0;
  std::uint64_t queens = 0;
  std::uint64_t rooks = 0;
  std::uint64_t bishops = 0;
  std::uint64_t knights = 0;
  std::uint64_t pawns = 0;
  unsigned rule50 = 0;
  unsigned castling = 0;
  unsigned ep = 0;
  bool turn = true;
};

auto to_fathom(const position& pos) noexcept -> fathom_position
{
  auto combined = [&](piece_type pt) {
    return pos.pieces(color::white, pt) | pos.pieces(color::black, pt);
  };
  return {
    pos.occupancy(color::white),
    pos.occupancy(color::black),
    combined(piece_type::king),
    combined(piece_type::queen),
    combined(piece_type::rook),
    combined(piece_type::bishop),
    combined(piece_type::knight),
    combined(piece_type::pawn),
    static_cast<unsigned>(pos.halfmove_clock()),
    pos.castling_rights(),
    pos.en_passant_square() == no_square ? 0
                                         : static_cast<unsigned>(pos.en_passant_square()),
    pos.side_to_move() == color::white,
  };
}

auto piece_from_promotes(int promotes) noexcept -> piece_type
{
  switch(promotes) {
  case TB_PROMOTES_QUEEN: return piece_type::queen;
  case TB_PROMOTES_ROOK: return piece_type::rook;
  case TB_PROMOTES_BISHOP: return piece_type::bishop;
  case TB_PROMOTES_KNIGHT: return piece_type::knight;
  default: return piece_type::queen;
  }
}

auto find_move(const position& pos, int from, int to, int promotes)
    -> std::optional<move>
{
  if(from < 0 || from >= square_count || to < 0 || to >= square_count) {
    return std::nullopt;
  }
  const auto promo = piece_from_promotes(promotes);
  auto copy = pos;
  for(const auto mv : movegen::generate_legal(copy)) {
    if(static_cast<int>(mv.from) == from && static_cast<int>(mv.to) == to
       && mv.promotion == promo) {
      return mv;
    }
  }
  return std::nullopt;
}

auto wdl_to_score(int wdl, int dtz) noexcept -> int
{
  switch(wdl) {
  case win: {
    const auto d = std::max(1, std::min(dtz, 99));
    return search::mate_value - 2 * d + 1;
  }
  case loss: {
    const auto d = std::max(1, std::min(dtz, 99));
    return -(search::mate_value - 2 * d + 1);
  }
  case cursed_win:
    return std::max(1, 50 - std::min(dtz, 99));
  case blessed_loss:
    return -std::max(1, 50 - std::min(dtz, 99));
  default:
    return 0;
  }
}

auto fathom_score_to_engine(int tb_score, int rank, unsigned rule50) noexcept -> int
{
  if(tb_score >= 31743) {
    const auto d = std::max(1, std::min(1000 - rank - static_cast<int>(rule50), 99));
    return search::mate_value - 2 * d + 1;
  }
  if(tb_score <= -31743) {
    const auto d = std::max(1, std::min(1000 + rank - static_cast<int>(rule50), 99));
    return -(search::mate_value - 2 * d + 1);
  }
  return tb_score;
}

}

auto init(const std::string& path) -> bool
{
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto ok = tb_init(path.c_str()) != 0;
  g_initialized = true;
  return ok;
}

auto unload() -> void
{
  std::lock_guard<std::mutex> lock(g_mutex);
  tb_free();
  g_initialized = false;
}

auto is_loaded() -> bool
{
  return g_initialized;
}

auto largest() -> int
{
  return static_cast<int>(TB_LARGEST);
}

auto piece_count(const position& pos) -> int
{
  return static_cast<int>(std::popcount(pos.occupancy()));
}

auto probe_root(const position& pos, probe_result& out) -> bool
{
  std::lock_guard<std::mutex> lock(g_mutex);
  out = probe_result{};
  if(TB_LARGEST == 0 || piece_count(pos) > static_cast<int>(TB_LARGEST)) {
    return false;
  }
  const auto fp = to_fathom(pos);

  auto results = std::array<unsigned, TB_MAX_MOVES>{};
  const auto res = tb_probe_root(fp.white, fp.black, fp.kings, fp.queens, fp.rooks,
                                 fp.bishops, fp.knights, fp.pawns, fp.rule50,
                                 fp.castling, fp.ep, fp.turn, results.data());
  if(res != TB_RESULT_FAILED) {
    if(res == TB_RESULT_CHECKMATE || res == TB_RESULT_STALEMATE) {
      out.wdl = res == TB_RESULT_CHECKMATE ? win : draw;
      out.score = res == TB_RESULT_CHECKMATE ? -search::mate_value : 0;
      out.has_move = false;
      return true;
    }
    const auto wdl = static_cast<int>(TB_GET_WDL(res));
    const auto dtz = static_cast<int>(TB_GET_DTZ(res));
    const auto mv = find_move(pos, static_cast<int>(TB_GET_FROM(res)),
                              static_cast<int>(TB_GET_TO(res)),
                              static_cast<int>(TB_GET_PROMOTES(res)));
    if(!mv.has_value()) {
      return false;
    }
    out.best_move = *mv;
    out.has_move = true;
    out.wdl = wdl;
    out.dtz = dtz;
    out.score = wdl_to_score(wdl, dtz);
    return true;
  }

  auto rm = TbRootMoves{};
  if(tb_probe_root_wdl(fp.white, fp.black, fp.kings, fp.queens, fp.rooks, fp.bishops,
                       fp.knights, fp.pawns, fp.rule50, fp.castling, fp.ep, fp.turn,
                       true, &rm) != 0
     && rm.size > 0) {
    auto best_index = std::size_t{0};
    for(auto i = std::size_t{1}; i < rm.size; ++i) {
      const auto& best = rm.moves[best_index];
      const auto& cur = rm.moves[i];
      if(cur.tbRank > best.tbRank
         || (cur.tbRank == best.tbRank && cur.tbScore > best.tbScore)) {
        best_index = i;
      }
    }
    const auto& chosen = rm.moves[best_index];
    const auto mv = find_move(pos, static_cast<int>(TB_MOVE_FROM(chosen.move)),
                              static_cast<int>(TB_MOVE_TO(chosen.move)),
                              static_cast<int>(TB_MOVE_PROMOTES(chosen.move)));
    if(!mv.has_value()) {
      return false;
    }
    out.best_move = *mv;
    out.has_move = true;
    out.wdl = chosen.tbScore > 31743 ? win
             : chosen.tbScore < -31743 ? loss
             : chosen.tbScore > 0 ? cursed_win
             : chosen.tbScore < 0 ? blessed_loss
             : draw;
    out.score = fathom_score_to_engine(static_cast<int>(chosen.tbScore),
                                       static_cast<int>(chosen.tbRank), fp.rule50);
    return true;
  }
  return false;
}

auto probe_wdl(const position& pos, int* success) -> int
{
  std::lock_guard<std::mutex> lock(g_mutex);
  if(success != nullptr) {
    *success = 0;
  }
  if(TB_LARGEST == 0) {
    return -1;
  }
  const auto fp = to_fathom(pos);
  const auto res = tb_probe_wdl(fp.white, fp.black, fp.kings, fp.queens, fp.rooks,
                                fp.bishops, fp.knights, fp.pawns, fp.rule50,
                                fp.castling, fp.ep, fp.turn);
  if(res == TB_RESULT_FAILED) {
    return -1;
  }
  if(success != nullptr) {
    *success = 1;
  }
  return static_cast<int>(res);
}

}