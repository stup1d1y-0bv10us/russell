#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/ordering.hpp"
#include "search/search.hpp"

#include <cstdlib>
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

auto make_quiet(int from, int to) -> tuna::move
{
  return tuna::move{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to),
                    tuna::piece_type::queen, tuna::move_flag::quiet};
}

auto test_classification() -> void
{
  using tuna::move_flag;
  using tuna::move;
  using tuna::piece_type;
  using tuna::search::is_capture;
  using tuna::search::is_promotion;
  using tuna::search::is_quiet;

  const auto quiet = move{0, 8, piece_type::queen, move_flag::quiet};
  const auto capture = move{0, 8, piece_type::queen, move_flag::capture};
  const auto ep = move{0, 8, piece_type::queen, move_flag::en_passant};
  const auto promo = move{0, 8, piece_type::queen, move_flag::promotion};
  const auto promo_cap = move{0, 8, piece_type::queen, move_flag::promotion_capture};
  const auto castle = move{4, 6, piece_type::king, move_flag::castling};

  require(is_capture(capture) && is_capture(ep) && is_capture(promo_cap), "capture flags");
  require(!is_capture(quiet) && !is_capture(promo) && !is_capture(castle), "non-captures");
  require(is_promotion(promo) && is_promotion(promo_cap), "promotion flags");
  require(!is_promotion(quiet) && !is_promotion(capture), "non-promotions");
  require(is_quiet(quiet) && is_quiet(castle), "quiet flags");
  require(!is_quiet(capture) && !is_quiet(promo) && !is_quiet(ep), "non-quiets");
}

auto test_mvv_lva_ordering() -> void
{
  auto pos = tuna::position::from_fen("k7/p7/8/3q4/3Q4/8/8/K7 w - - 0 1");
  auto copy = pos;
  auto order = tuna::search::move_ordering{};
  const auto moves = tuna::movegen::generate_legal(copy);
  const auto ordered = tuna::search::order_moves(pos, moves, 0, order);

  auto queen_capture = tuna::move{};
  auto pawn_capture = tuna::move{};
  for(const auto mv : ordered) {
    if(mv.flag == tuna::move_flag::capture) {
      if(mv.to == 35) {
        queen_capture = mv;
      }
      if(mv.to == 48) {
        pawn_capture = mv;
      }
    }
  }
  require(queen_capture.from == 27, "queen capture from d4");
  require(pawn_capture.from == 27, "pawn capture from d4");
  require(queen_capture.to == 35, "queen capture to d5");
  require(pawn_capture.to == 48, "pawn capture to a7");

  const auto queen_score = order.score(pos, queen_capture, 0);
  const auto pawn_score = order.score(pos, pawn_capture, 0);
  require(queen_score > pawn_score, "queen victim outranks pawn victim (MVV-LVA)");
  require(ordered[0] == queen_capture, "best capture ordered first");
  require(ordered[1] == pawn_capture, "second capture ordered second");
}

auto test_captures_before_quiets() -> void
{
  auto pos = tuna::position::from_fen("k7/p7/8/3q4/3Q4/8/8/K7 w - - 0 1");
  auto copy = pos;
  auto order = tuna::search::move_ordering{};
  const auto moves = tuna::movegen::generate_legal(copy);
  const auto ordered = tuna::search::order_moves(pos, moves, 0, order);
  auto seen_quiet = false;
  auto capture_after_quiet = false;
  for(const auto mv : ordered) {
    if(tuna::search::is_quiet(mv)) {
      seen_quiet = true;
    } else if(seen_quiet) {
      capture_after_quiet = true;
    }
  }
  require(!capture_after_quiet, "captures never follow quiets");
}

auto test_killers() -> void
{
  auto pos = tuna::position::from_fen("8/8/8/8/8/8/8/K3k3 w - - 0 1");
  auto order = tuna::search::move_ordering{};
  const auto k1 = make_quiet(32, 40);
  const auto k2 = make_quiet(32, 24);

  require(order.score(pos, k1, 3) == 0, "fresh killer score is zero");
  order.update_killers(k1, 3);
  require(order.score(pos, k1, 3) > order.score(pos, k2, 3), "killer outranks history quiet");
  order.update_killers(k2, 3);
  require(order.score(pos, k2, 3) > order.score(pos, k1, 3), "recent killer outranks older killer");
  require(order.score(pos, k1, 3) > 0 && order.score(pos, k2, 3) > 0, "both killers boosted");

  require(order.score(pos, k1, 4) == 0, "killers are ply-local");
}

auto test_history() -> void
{
  auto pos = tuna::position::from_fen("8/8/8/8/8/8/8/K3k3 w - - 0 1");
  auto order = tuna::search::move_ordering{};
  const auto mv = make_quiet(8, 16);
  require(order.score(pos, mv, 2) == 0, "fresh history score is zero");
  order.update_history(tuna::color::white, mv, 3);
  require(order.score(pos, mv, 2) > 0, "history boosts quiet score");
  order.reset();
  require(order.score(pos, mv, 2) == 0, "reset clears history");
}

auto is_legal_pos(const tuna::position& pos, tuna::move mv) -> bool
{
  auto copy = pos;
  for(const auto c : tuna::movegen::generate_legal(copy)) if(c == mv) return true;
  return false;
}
auto test_order_preserves_scores() -> void
{
  const auto positions = std::vector<std::pair<std::string, int>>{
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 3},
  };
  for(const auto& [fen, depth] : positions) {
    auto a = tuna::position::from_fen(fen);
    const auto plain = tuna::search::minimax(a, depth);
    auto pos_exact = tuna::position::from_fen(fen);
    const auto exact = tuna::search::alpha_beta(pos_exact, depth, false, false, false, false, false);
    require(exact.has_move == plain.has_move, ("ordered exact has_move matches " + fen).c_str());
    require(is_legal_pos(pos_exact, exact.best_move) || !exact.has_move, ("ordered exact best move legal " + fen).c_str());
    if(fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {
      require(exact.score == plain.score, ("ordered exact score matches minimax " + fen).c_str());
      if(exact.has_move && !(exact.best_move == plain.best_move)) require(exact.score == plain.score, ("ordered exact alternative same score " + fen).c_str());
    } else {
      require(exact.score > -29000 && exact.score < 29000, ("ordered exact not false mate " + fen).c_str());
      if(exact.score == plain.score && exact.has_move && !(exact.best_move == plain.best_move)) require(exact.score == plain.score, ("ordered exact alternative same score " + fen).c_str());
    }
    auto b = tuna::position::from_fen(fen);
    const auto ordered = tuna::search::alpha_beta(b, depth, true, true, false, false, false);
    require(ordered.has_move == plain.has_move, ("ordered has_move matches " + fen).c_str());
    if(fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {
      require(ordered.score == plain.score, ("ordered score matches minimax " + fen).c_str());
      require(is_legal_pos(b, ordered.best_move) || !ordered.has_move, ("ordered best move legal " + fen).c_str());
      if(!(ordered.best_move == plain.best_move)) require(ordered.score == plain.score, ("ordered alternative same score " + fen).c_str());
    } else {
      require(ordered.score > -29000 && ordered.score < 29000, ("ordered score not false mate " + fen).c_str());
      require(is_legal_pos(b, ordered.best_move) || !ordered.has_move, ("ordered best move legal " + fen).c_str());
    }
    require(ordered.nodes < plain.nodes, ("ordered search prunes " + fen).c_str());
  }
}

auto test_iterative_deepening_ordering() -> void
{
  auto pos = tuna::position::start();
  const auto id = tuna::search::iterative_deepening(pos, 4);
  auto pos2 = tuna::position::start();
  const auto ab = tuna::search::alpha_beta(pos2, 4);
  require(id.score == ab.score, "id score matches depth search");
  if(!(id.best_move == ab.best_move)) {
    require(is_legal_pos(pos, id.best_move), "id alternative legal");
    require(is_legal_pos(pos2, ab.best_move), "ab alternative legal");
    require(id.score == ab.score, "id alternative same score");
  }
  require(id.nodes >= ab.nodes, "id accumulates nodes");
}

auto test_see_refines_close_captures() -> void
{
  auto pos = tuna::position::from_fen("k7/1p6/2p1p3/3B4/8/8/8/K7 w - - 0 1");
  auto copy = pos;
  auto order = tuna::search::move_ordering{};
  const auto moves = tuna::movegen::generate_legal(copy);
  const auto ordered = tuna::search::order_moves(pos, moves, 0, order);

  auto bx_c6 = tuna::move{};
  auto bx_e6 = tuna::move{};
  for(const auto mv : ordered) {
    if(mv.flag == tuna::move_flag::capture) {
      if(mv.to == 42) {
        bx_c6 = mv;
      } else if(mv.to == 44) {
        bx_e6 = mv;
      }
    }
  }
  require(bx_c6.from == 35 && bx_c6.to == 42, "bishop captures c6 pawn");
  require(bx_e6.from == 35 && bx_e6.to == 44, "bishop captures e6 pawn");

  require(order.score(pos, bx_c6, 0) == order.score(pos, bx_e6, 0),
         "equal MVV-LVA before SEE refinement");

  auto pos_c6 = std::size_t{0};
  auto pos_e6 = std::size_t{0};
  for(auto i = std::size_t{0}; i < ordered.size(); ++i) {
    if(ordered[i] == bx_c6) {
      pos_c6 = i;
    }
    if(ordered[i] == bx_e6) {
      pos_e6 = i;
    }
  }
  require(pos_e6 < pos_c6, "SEE ranks winning capture before losing capture");
}

auto test_single_capture_no_refinement() -> void
{
  auto pos = tuna::position::from_fen("7k/8/4p3/3B4/8/8/8/K7 w - - 0 1");
  auto copy = pos;
  auto order = tuna::search::move_ordering{};
  const auto moves = tuna::movegen::generate_legal(copy);
  const auto ordered = tuna::search::order_moves(pos, moves, 0, order);
  require(ordered[0].flag == tuna::move_flag::capture, "sole capture ordered first");
  require(ordered[0].from == 35 && ordered[0].to == 44, "sole capture is bishop takes e6");
}

auto test_tt_move_first_with_refinement() -> void
{
  auto pos = tuna::position::from_fen("k7/1p6/2p1p3/3B4/8/8/8/K7 w - - 0 1");
  auto copy = pos;
  auto order = tuna::search::move_ordering{};
  const auto moves = tuna::movegen::generate_legal(copy);
  const auto tt = tuna::move{35, 42, tuna::piece_type::queen, tuna::move_flag::capture};
  const auto ordered = tuna::search::order_moves(pos, moves, 0, order, tt);
  require(ordered.size() > 0, "moves present");
  require(ordered[0] == tt, "tt move first despite SEE refinement");
}

}

auto main() -> int
{
  test_classification();
  test_mvv_lva_ordering();
  test_captures_before_quiets();
  test_killers();
  test_history();
  test_order_preserves_scores();
  test_iterative_deepening_ordering();
  test_see_refines_close_captures();
  test_single_capture_no_refinement();
  test_tt_move_first_with_refinement();
  return 0;
}