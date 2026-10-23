#include "core/position.hpp"
#include "movegen/movegen.hpp"
#include "search/ordering.hpp"
#include "search/search.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
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

auto make_quiet(int from, int to) -> russell::move
{
  return russell::move{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to),
                    russell::piece_type::queen, russell::move_flag::quiet};
}

auto test_classification() -> void
{
  using russell::move_flag;
  using russell::move;
  using russell::piece_type;
  using russell::search::is_capture;
  using russell::search::is_promotion;
  using russell::search::is_quiet;

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
  auto pos = russell::position::from_fen("k7/p7/8/3q4/3Q4/8/8/K7 w - - 0 1");
  auto copy = pos;
  auto order = russell::search::move_ordering{};
  const auto moves = russell::movegen::generate_legal(copy);
  const auto ordered = russell::search::order_moves(pos, moves, 0, order);

  auto queen_capture = russell::move{};
  auto pawn_capture = russell::move{};
  for(const auto mv : ordered) {
    if(mv.flag == russell::move_flag::capture) {
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
  auto pos = russell::position::from_fen("k7/p7/8/3q4/3Q4/8/8/K7 w - - 0 1");
  auto copy = pos;
  auto order = russell::search::move_ordering{};
  const auto moves = russell::movegen::generate_legal(copy);
  const auto ordered = russell::search::order_moves(pos, moves, 0, order);
  auto seen_quiet = false;
  auto capture_after_quiet = false;
  for(const auto mv : ordered) {
    if(russell::search::is_quiet(mv)) {
      seen_quiet = true;
    } else if(seen_quiet) {
      capture_after_quiet = true;
    }
  }
  require(!capture_after_quiet, "captures never follow quiets");
}

auto test_killers() -> void
{
  auto pos = russell::position::from_fen("8/8/8/8/8/8/8/K3k3 w - - 0 1");
  auto order = russell::search::move_ordering{};
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

auto test_killer_refinement() -> void
{
  using russell::color;
  using russell::move;
  using russell::move_flag;
  using russell::piece_type;
  auto pos = russell::position::from_fen("8/8/8/8/8/8/8/K3k3 w - - 0 1");
  const auto q1 = move{8, 16, piece_type::queen, move_flag::quiet};
  const auto q2 = move{9, 17, piece_type::queen, move_flag::quiet};
  const auto q3 = move{10, 18, piece_type::queen, move_flag::quiet};

  {
    auto order = russell::search::move_ordering{};
    require(!order.is_killer(q1, 5), "fresh killer state empty");
    order.update_killers(q1, 5);
    require(order.is_killer(q1, 5), "first killer inserted");
    require(!order.is_killer(q2, 5), "second slot still empty");
    order.update_killers(q2, 5);
    require(order.is_killer(q1, 5) && order.is_killer(q2, 5), "both killers stored");
    require(order.score(pos, q2, 5) > order.score(pos, q1, 5), "primary is newest");
  }

  {
    auto order = russell::search::move_ordering{};
    order.update_killers(q1, 5);
    order.update_killers(q2, 5);
    const auto s1 = order.score(pos, q1, 5);
    const auto s2 = order.score(pos, q2, 5);
    order.update_killers(q2, 5);
    require(order.score(pos, q1, 5) == s1 && order.score(pos, q2, 5) == s2,
            "duplicate primary insertion changes nothing");
  }

  {
    auto order = russell::search::move_ordering{};
    order.update_killers(q1, 5);
    order.update_killers(q2, 5);
    order.update_killers(q1, 5);
    require(order.is_killer(q1, 5) && order.is_killer(q2, 5), "no duplication after promotion");
    require(order.score(pos, q1, 5) > order.score(pos, q2, 5), "secondary promoted to primary");
    order.update_killers(q3, 5);
    require(order.is_killer(q3, 5) && order.is_killer(q1, 5), "eviction drops oldest secondary");
    require(!order.is_killer(q2, 5), "evicted secondary fully gone");
  }

  {
    auto order = russell::search::move_ordering{};
    const auto cap = move{8, 16, piece_type::queen, move_flag::capture};
    const auto ep = move{8, 16, piece_type::queen, move_flag::en_passant};
    const auto promo_cap = move{8, 16, piece_type::queen, move_flag::promotion_capture};
    const auto promo = move{8, 16, piece_type::queen, move_flag::promotion};
    order.update_killers(cap, 5);
    order.update_killers(ep, 5);
    order.update_killers(promo_cap, 5);
    order.update_killers(promo, 5);
    require(!order.is_killer(cap, 5), "capture rejected");
    require(!order.is_killer(ep, 5), "en-passant rejected");
    require(!order.is_killer(promo_cap, 5), "capturing promotion rejected");
    require(!order.is_killer(promo, 5), "quiet promotion rejected");

    const auto castle = move{4, 6, piece_type::king, move_flag::castling};
    order.update_killers(q1, 5);
    order.update_killers(castle, 5);
    require(order.is_killer(q1, 5), "quiet accepted");
    require(order.is_killer(castle, 5), "castling accepted as quiet");

    order.update_killers(q2, 5);
    order.update_killers(russell::search::no_move, 5);
    require(!order.is_killer(russell::search::no_move, 5), "no_move rejected");
    require(order.is_killer(q2, 5) && order.is_killer(castle, 5), "no_move store changed nothing");
  }

  {
    auto order = russell::search::move_ordering{};
    order.update_killers(q1, 5);
    order.update_history(color::white, q2.from, q2.to, 100);
    require(order.score(pos, q2, 5) == 100, "killer does not leak onto other quiets");
    require(order.score(pos, q1, 5) > order.score(pos, q2, 5), "stored killer keeps band");
  }

  {
    auto order = russell::search::move_ordering{};
    order.update_killers(q1, 0);
    order.update_killers(q2, 64);
    order.update_killers(q3, 127);
    require(order.is_killer(q1, 0) && !order.is_killer(q1, 1), "ply 0 local");
    require(order.is_killer(q2, 64) && !order.is_killer(q2, 63), "interior ply local");
    require(order.is_killer(q3, 127), "maximum ply stored");
  }

  {
    auto order = russell::search::move_ordering{};
    order.update_killers(q1, 500);
    require(order.is_killer(q1, 500), "clamped ply readable at same depth");
    require(order.is_killer(q1, 127), "clamped ply aliases slot 127 deterministically");
    require(!order.is_killer(q1, 126), "aliasing does not leak downward");
  }

  {
    auto order = russell::search::move_ordering{};
    order.update_killers(q1, 5);
    order.reset();
    require(!order.is_killer(q1, 5), "reset clears killers");
    require(order.score(pos, q1, 5) == 0, "reset killer falls to zero butterfly");
  }

  {
    auto order = russell::search::move_ordering{};
    order.update_killers(q1, 5);
    order.update_killers(q2, 5);
    const auto s1 = order.score(pos, q1, 5);
    const auto s2 = order.score(pos, q2, 5);
    require(s2 == 70000 && s1 == 60000, "killer bands exact (primary 70000, secondary 60000)");
    require(s2 > s1, "primary above secondary");
  }

  {
    auto order = russell::search::move_ordering{};
    order.update_history(color::white, q1.from, q1.to, 16384);
    require(order.score(pos, q1, 5) == 16384, "butterfly max without killer");
    order.update_killers(q1, 5);
    require(order.score(pos, q1, 5) == 70000, "killer dominates butterfly");
  }

  {
    auto ppos = russell::position::from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
    auto probe = ppos;
    auto km = russell::move{};
    for(const auto mv : russell::movegen::generate_legal(probe)) {
      if(russell::search::is_quiet(mv)) {
        km = mv;
        break;
      }
    }
    require(!(km == russell::search::no_move), "quiet exists for killer picker test");
    auto order = russell::search::move_ordering{};
    order.update_killers(km, 1);
    auto p = ppos;
    auto picker = russell::search::move_picker(order, 1, russell::search::no_move, false,
                                               russell::move_list{}, false, russell::move_list{},
                                               false, russell::search::no_move, russell::piece::none,
                                               russell::search::no_move, russell::piece::none, km);
    auto mv = russell::search::no_move;
    auto see = std::numeric_limits<int>::max();
    auto idx = 0;
    auto count = 0;
    while(picker.next(p, mv, see, idx)) {
      require(!(mv == km), "excluded killer never emitted");
      ++count;
    }
    require(count > 0, "picker still emits non-excluded moves");
  }
}

auto test_history() -> void
{
  auto pos = russell::position::from_fen("8/8/8/8/8/8/8/K3k3 w - - 0 1");
  auto order = russell::search::move_ordering{};
  const auto mv = make_quiet(8, 16);
  require(order.score(pos, mv, 2) == 0, "fresh history score is zero");
  order.update_history(russell::color::white, mv, 3);
  require(order.score(pos, mv, 2) > 0, "history boosts quiet score");
  order.reset();
  require(order.score(pos, mv, 2) == 0, "reset clears history");
}

auto is_legal_pos(const russell::position& pos, russell::move mv) -> bool
{
  auto copy = pos;
  for(const auto c : russell::movegen::generate_legal(copy)) if(c == mv) return true;
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
    auto a = russell::position::from_fen(fen);
    const auto plain = russell::search::minimax(a, depth);
    auto pos_exact = russell::position::from_fen(fen);
    const auto exact = russell::search::alpha_beta(pos_exact, depth, false, false, false, false, false);
    require(exact.has_move == plain.has_move, ("ordered exact has_move matches " + fen).c_str());
    require(is_legal_pos(pos_exact, exact.best_move) || !exact.has_move, ("ordered exact best move legal " + fen).c_str());
    if(fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {
      require(exact.score == plain.score, ("ordered exact score matches minimax " + fen).c_str());
      if(exact.has_move && !(exact.best_move == plain.best_move)) require(exact.score == plain.score, ("ordered exact alternative same score " + fen).c_str());
    } else {
      require(exact.score > -29000 && exact.score < 29000, ("ordered exact not false mate " + fen).c_str());
      if(exact.score == plain.score && exact.has_move && !(exact.best_move == plain.best_move)) require(exact.score == plain.score, ("ordered exact alternative same score " + fen).c_str());
    }
    auto b = russell::position::from_fen(fen);
    const auto ordered = russell::search::alpha_beta(b, depth, true, true, false, false, false);
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
  auto pos = russell::position::start();
  const auto id = russell::search::iterative_deepening(pos, 4);
  auto pos2 = russell::position::start();
  const auto ab = russell::search::alpha_beta(pos2, 4);
  require(id.score == ab.score, "id score matches depth search");
  if(!(id.best_move == ab.best_move)) {
    require(is_legal_pos(pos, id.best_move), "id alternative legal");
    require(is_legal_pos(pos2, ab.best_move), "ab alternative legal");
    require(id.score == ab.score, "id alternative same score");
  }

  require(id.nodes > 0 && ab.nodes > 0, "both searches expend nodes");
}

auto test_see_refines_close_captures() -> void
{
  auto pos = russell::position::from_fen("k7/1p6/2p1p3/3B4/8/8/8/K7 w - - 0 1");
  auto copy = pos;
  auto order = russell::search::move_ordering{};
  const auto moves = russell::movegen::generate_legal(copy);
  const auto ordered = russell::search::order_moves(pos, moves, 0, order);

  auto bx_c6 = russell::move{};
  auto bx_e6 = russell::move{};
  for(const auto mv : ordered) {
    if(mv.flag == russell::move_flag::capture) {
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
  auto pos = russell::position::from_fen("7k/8/4p3/3B4/8/8/8/K7 w - - 0 1");
  auto copy = pos;
  auto order = russell::search::move_ordering{};
  const auto moves = russell::movegen::generate_legal(copy);
  const auto ordered = russell::search::order_moves(pos, moves, 0, order);
  require(ordered[0].flag == russell::move_flag::capture, "sole capture ordered first");
  require(ordered[0].from == 35 && ordered[0].to == 44, "sole capture is bishop takes e6");
}

auto test_tt_move_first_with_refinement() -> void
{
  auto pos = russell::position::from_fen("k7/1p6/2p1p3/3B4/8/8/8/K7 w - - 0 1");
  auto copy = pos;
  auto order = russell::search::move_ordering{};
  const auto moves = russell::movegen::generate_legal(copy);
  const auto tt = russell::move{35, 42, russell::piece_type::queen, russell::move_flag::capture};
  const auto ordered = russell::search::order_moves(pos, moves, 0, order, tt);
  require(ordered.size() > 0, "moves present");
  require(ordered[0] == tt, "tt move first despite SEE refinement");
}

namespace picker_detail {

struct picked {
  russell::move mv{};
  int see = std::numeric_limits<int>::max();
  int idx = -1;
};

auto legacy_stage_sequence(russell::position& pos, russell::search::move_ordering& order, int ply,
                           russell::move tt_move, bool tt_searchable, russell::move prev_move,
                           russell::piece prev_moved, russell::move prev2_move,
                           russell::piece prev2_moved, russell::move excluded_move)
    -> std::vector<picked>
{
  using namespace russell;
  using namespace russell::search;
  struct scored {
    move mv{};
    int score = 0;
    int see = std::numeric_limits<int>::max();
  };
  auto out = std::vector<picked>{};
  auto emitted = 0;
  if(tt_searchable) {
    out.push_back({tt_move, std::numeric_limits<int>::max(), emitted++});
  }

  auto cap_moves = movegen::generate_captures_promotions(pos);
  auto cap_list = std::vector<scored>{};
  for(const auto candidate : cap_moves) {
    if(tt_searchable && candidate == tt_move) {
      continue;
    }
    cap_list.push_back({candidate,
                        order.score(pos, candidate, ply, no_move, prev_move, prev_moved,
                                    prev2_move, prev2_moved),
                        std::numeric_limits<int>::max()});
  }
  auto best_cap = 0;
  auto have_cap = false;
  if(tt_searchable && is_capture(tt_move)) {
    best_cap = order.score(pos, tt_move, ply, tt_move, prev_move, prev_moved, prev2_move,
                           prev2_moved);
    have_cap = true;
  }
  for(const auto& e : cap_list) {
    if(is_capture(e.mv) && (!have_cap || e.score > best_cap)) {
      best_cap = e.score;
      have_cap = true;
    }
  }
  if(have_cap) {
    auto refined = std::size_t{0};
    for(const auto& e : cap_list) {
      if(is_capture(e.mv) && best_cap - e.score <= 32) {
        ++refined;
      }
    }
    if(refined >= 2) {
      for(auto& e : cap_list) {
        if(!is_capture(e.mv) || best_cap - e.score > 32) {
          continue;
        }
        const int see = static_exchange_eval(pos, e.mv);
        e.see = see;
        ++order.see_ordered_captures;
        if(see >= 0) {
          ++order.see_winning_captures;
        } else {
          ++order.see_losing_captures;
        }
        auto bonus = 0;
        if(see >= 300) {
          bonus = 2000;
        } else if(see >= 0) {
          bonus = 500;
        } else if(see >= -100) {
          bonus = -120000;
        } else {
          bonus = -121500;
        }
        e.score += bonus;
      }
    }
  }
  auto losing = std::vector<scored>{};
  auto cap_skip = std::vector<bool>(cap_list.size(), false);
  for(auto i = std::size_t{0}; i < cap_list.size(); ++i) {
    if(cap_list[i].see != std::numeric_limits<int>::max() && cap_list[i].see < 0) {
      losing.push_back(cap_list[i]);
      cap_skip[i] = true;
    }
  }
  auto cap_taken = std::vector<bool>(cap_list.size(), false);
  for(;;) {
    auto pick = cap_list.size();
    for(auto k = std::size_t{0}; k < cap_list.size(); ++k) {
      if(cap_skip[k] || cap_taken[k]) {
        continue;
      }
      if(pick == cap_list.size() || cap_list[k].score > cap_list[pick].score) {
        pick = k;
      }
    }
    if(pick == cap_list.size()) {
      break;
    }
    cap_taken[pick] = true;
    if(cap_list[pick].mv == excluded_move) {
      ++emitted;
      continue;
    }
    out.push_back({cap_list[pick].mv, cap_list[pick].see, emitted++});
  }

  auto quiet_moves = movegen::generate_quiet_moves(pos);
  auto quiet_list = std::vector<scored>{};
  for(const auto candidate : quiet_moves) {
    if(tt_searchable && candidate == tt_move) {
      continue;
    }
    if(is_promotion(candidate)) {
      continue;
    }
    quiet_list.push_back({candidate,
                          order.score(pos, candidate, ply, no_move, prev_move, prev_moved,
                                      prev2_move, prev2_moved),
                          std::numeric_limits<int>::max()});
  }
  auto quiet_taken = std::vector<bool>(quiet_list.size(), false);
  for(;;) {
    auto pick = quiet_list.size();
    for(auto k = std::size_t{0}; k < quiet_list.size(); ++k) {
      if(quiet_taken[k]) {
        continue;
      }
      if(pick == quiet_list.size() || quiet_list[k].score > quiet_list[pick].score) {
        pick = k;
      }
    }
    if(pick == quiet_list.size()) {
      break;
    }
    quiet_taken[pick] = true;
    if(quiet_list[pick].mv == excluded_move) {
      ++emitted;
      continue;
    }
    out.push_back({quiet_list[pick].mv, quiet_list[pick].see, emitted++});
  }

  auto losing_taken = std::vector<bool>(losing.size(), false);
  for(;;) {
    auto pick = losing.size();
    for(auto k = std::size_t{0}; k < losing.size(); ++k) {
      if(losing_taken[k]) {
        continue;
      }
      if(pick == losing.size() || losing[k].score > losing[pick].score) {
        pick = k;
      }
    }
    if(pick == losing.size()) {
      break;
    }
    losing_taken[pick] = true;
    if(losing[pick].mv == excluded_move) {
      ++emitted;
      continue;
    }
    out.push_back({losing[pick].mv, losing[pick].see, emitted++});
  }
  return out;
}

auto drain_picker(russell::position& pos, russell::search::move_ordering& order, int ply,
                  russell::move tt_move, bool tt_searchable, russell::move prev_move,
                  russell::piece prev_moved, russell::move prev2_move, russell::piece prev2_moved,
                  russell::move excluded_move)
    -> std::vector<picked>
{
  using namespace russell;
  using namespace russell::search;
  auto picker = move_picker(order, ply, tt_move, tt_searchable, move_list{}, false, move_list{},
                            false, prev_move, prev_moved, prev2_move, prev2_moved, excluded_move);
  auto out = std::vector<picked>{};
  auto mv = move{};
  auto see = std::numeric_limits<int>::max();
  auto idx = 0;
  while(picker.next(pos, mv, see, idx)) {
    out.push_back({mv, see, idx});
  }
  return out;
}

auto check_sequence(const std::vector<picked>& got, const std::vector<picked>& want,
                    const char* tag) -> void
{
  if(got.size() != want.size()) {
    std::cerr << tag << " size " << got.size() << " != " << want.size() << '\n';
    std::exit(1);
  }
  for(auto i = std::size_t{0}; i < want.size(); ++i) {
    if(!(got[i].mv == want[i].mv) || got[i].see != want[i].see || got[i].idx != want[i].idx) {
      std::cerr << tag << " mismatch at " << i << '\n';
      std::exit(1);
    }
  }
}

auto find_legal(russell::position& pos, russell::move_flag flag, int nth) -> russell::move
{
  auto n = 0;
  for(const auto mv : russell::movegen::generate_legal(pos)) {
    if(mv.flag == flag) {
      if(n++ == nth) {
        return mv;
      }
    }
  }
  return russell::search::no_move;
}

}

auto test_picker_matches_legacy_staging() -> void
{
  using namespace russell;
  using namespace russell::search;
  const auto fens = std::vector<std::string>{
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "k7/1p6/2p1p3/3B4/8/8/8/K7 w - - 0 1",
    "4k3/8/8/8/8/8/4R3/4K3 w - - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",

    "5rk1/4P3/8/8/8/8/8/4K3 w - - 0 1",

    "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
  };
  const auto ply = 3;
  for(const auto& fen : fens) {
    auto pos = position::from_fen(fen);
    auto probe = pos;

    auto prev_move = no_move;
    auto prev_moved = piece::none;
    auto prev2_move = no_move;
    auto prev2_moved = piece::none;
    {
      auto scan = pos;
      auto n = 0;
      for(const auto mv : movegen::generate_legal(scan)) {
        if(is_quiet(mv) && n++ == 0) {
          prev_move = mv;
          prev_moved = pos.piece_on(mv.from);
        }
        if(is_quiet(mv) && n == 3) {
          prev2_move = mv;
          prev2_moved = pos.piece_on(mv.from);
        }
      }
    }

    auto tt_quiet = picker_detail::find_legal(probe, move_flag::quiet, 0);
    auto tt_cap = picker_detail::find_legal(probe, move_flag::capture, 0);
    if(tt_cap == no_move) {
      tt_cap = picker_detail::find_legal(probe, move_flag::en_passant, 0);
    }

    auto excl = no_move;
    {
      auto scan = pos;
      auto n = 0;
      for(const auto mv : movegen::generate_legal(scan)) {
        if(!(mv == tt_quiet) && !(mv == tt_cap) && n++ == 2) {
          excl = mv;
        }
      }
    }
    auto tt_cases = std::vector<std::pair<move, bool>>{
      {no_move, false},
      {tt_quiet, tt_quiet != no_move},
      {tt_cap, tt_cap != no_move},
    };
    if(excl != no_move) {

      tt_cases.push_back({excl, false});
    }
    for(const auto& [tt, searchable] : tt_cases) {
      for(auto round = 0; round < 2; ++round) {
        const auto excluded = (round == 0) ? no_move : excl;

        for(auto hist_round = 0; hist_round < 2; ++hist_round) {
          auto order_a = move_ordering{};
          auto order_b = move_ordering{};
          if(hist_round == 1) {

            auto scan = pos;
            auto nq = 0;
            for(const auto mv : movegen::generate_legal(scan)) {
              if(is_quiet(mv) && nq++ < 3) {
                order_a.update_history(pos.side_to_move(), mv, 4);
                order_b.update_history(pos.side_to_move(), mv, 4);
              }
            }
            if(prev_move != no_move) {
              auto rep = pos;
              auto rn = 0;
              for(const auto mv : movegen::generate_legal(rep)) {
                if(is_quiet(mv) && !(mv == prev_move) && rn++ == 0) {
                  order_a.update_countermove_history(prev_move, mv, 4);
                  order_b.update_countermove_history(prev_move, mv, 4);
                  if(prev_moved != piece::none) {
                    const auto cur = pos.piece_on(mv.from);
                    if(cur != piece::none) {
                      order_a.update_continuation_1ply(prev_moved, prev_move.to, cur, mv.to, 64);
                      order_b.update_continuation_1ply(prev_moved, prev_move.to, cur, mv.to, 64);
                      if(prev2_move != no_move && prev2_moved != piece::none) {
                        order_a.update_continuation_2ply(prev2_moved, prev2_move.to, cur, mv.to,
                                                         64);
                        order_b.update_continuation_2ply(prev2_moved, prev2_move.to, cur, mv.to,
                                                         64);
                      }
                    }
                  }
                }
              }
            }
            auto capscan = pos;
            for(const auto mv : movegen::generate_legal(capscan)) {
              if(is_capture(mv)) {
                order_a.update_capture_history(pos.side_to_move(), mv, pos, 16384);
                order_b.update_capture_history(pos.side_to_move(), mv, pos, 16384);
                break;
              }
            }
            auto kscan = pos;
            for(const auto mv : movegen::generate_legal(kscan)) {
              if(is_quiet(mv)) {
                order_a.update_killers(mv, ply);
                order_b.update_killers(mv, ply);
                break;
              }
            }
          }
          auto p1 = pos;
          const auto want = picker_detail::legacy_stage_sequence(
              p1, order_a, ply, tt, searchable, prev_move, prev_moved, prev2_move, prev2_moved,
              excluded);
          auto p2 = pos;
          const auto got = picker_detail::drain_picker(p2, order_b, ply, tt, searchable, prev_move,
                                                       prev_moved, prev2_move, prev2_moved,
                                                       excluded);
          picker_detail::check_sequence(got, want, ("picker-conformance " + fen).c_str());

          auto seen_first = std::vector<move>{};
          auto last_idx = -1;
          auto gaps = 0;
          for(const auto& e : got) {
            require(!(e.mv == excluded), "picker never emits excluded");
            for(const auto s : seen_first) {
              require(!(s == e.mv), "picker never duplicates");
            }
            seen_first.push_back(e.mv);
            if(last_idx >= 0) {
              require(e.idx == last_idx + 1 || e.idx == last_idx + 2, "picker idx continuity");
              if(e.idx == last_idx + 2) {
                ++gaps;
              }
            } else {
              require(e.idx >= 0, "picker first idx non-negative");
            }
            last_idx = e.idx;
          }
          require(gaps <= (excluded == no_move ? 0 : 1), "picker idx gaps only from excluded");
        }
      }
    }
  }
}

auto test_picker_targeted_bands() -> void
{
  using namespace russell;
  using namespace russell::search;

  {
    auto pos = position::from_fen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
    auto order = move_ordering{};
    const auto run = [&](int& n, move& first) {
      auto picker = move_picker(order, 1, no_move, false, move_list{}, true, move_list{}, true,
                                no_move, piece::none, no_move, piece::none, no_move);
      n = 0;
      auto mv = no_move;
      auto see = std::numeric_limits<int>::max();
      auto idx = 0;
      while(picker.next(pos, mv, see, idx)) {
        if(n++ == 0) {
          first = mv;
        }
      }
    };
    auto n1 = 0;
    auto f1 = no_move;
    auto n2 = 0;
    auto f2 = no_move;
    run(n1, f1);
    run(n2, f2);
    require(n1 == 0 && n2 == 0, "picker ends on empty lists");
  }

  {
    auto pos = position::from_fen("k7/1p6/2p1p3/3B4/8/8/8/K7 w - - 0 1");
    auto probe = pos;
    auto tt_cap = no_move;
    auto tt_quiet = no_move;
    for(const auto mv : movegen::generate_legal(probe)) {
      if(mv.flag == move_flag::capture && mv.to == 44) {
        tt_cap = mv;
      }
      if(is_quiet(mv) && tt_quiet == no_move) {
        tt_quiet = mv;
      }
    }
    require(tt_cap != no_move && tt_quiet != no_move, "split FEN has both kinds");
    auto order = move_ordering{};
    const auto drain_first = [&](move tt, bool searchable) {
      auto p = pos;
      auto picker = move_picker(order, 1, tt, searchable, move_list{}, false, move_list{},
                                false, no_move, piece::none, no_move, piece::none, no_move);
      auto mv = no_move;
      auto see = std::numeric_limits<int>::max();
      auto idx = 0;
      auto first = no_move;
      auto seq = std::vector<move>{};
      while(picker.next(p, mv, see, idx)) {
        if(seq.empty()) {
          first = mv;
        }
        seq.push_back(mv);
      }
      return std::pair<move, std::vector<move>>(first, seq);
    };
    {
      const auto [first, seq] = drain_first(tt_cap, true);
      require(first == tt_cap, "TT capture first");
      auto count = 0;
      for(const auto m : seq) {
        if(m == tt_cap) {
          ++count;
        }
      }
      require(count == 1, "TT capture exactly once");
    }
    {
      const auto [first, seq] = drain_first(tt_quiet, true);
      require(first == tt_quiet, "TT quiet first");
    }
    {

      const auto [first, seq] = drain_first(no_move, false);
      auto bx_e6 = no_move;
      auto bx_c6 = no_move;
      for(const auto mv : seq) {
        if(mv.flag == move_flag::capture && mv.to == 44) {
          bx_e6 = mv;
        }
        if(mv.flag == move_flag::capture && mv.to == 42) {
          bx_c6 = mv;
        }
      }
      require(bx_e6 != no_move && bx_c6 != no_move, "both captures present");
      auto pos_e6 = -1;
      auto pos_c6 = -1;
      auto first_quiet = -1;
      for(auto i = 0; i < static_cast<int>(seq.size()); ++i) {
        if(seq[static_cast<std::size_t>(i)] == bx_e6) {
          pos_e6 = i;
        }
        if(seq[static_cast<std::size_t>(i)] == bx_c6) {
          pos_c6 = i;
        }
        if(first_quiet < 0 && is_quiet(seq[static_cast<std::size_t>(i)])) {
          first_quiet = i;
        }
      }
      require(pos_e6 >= 0 && first_quiet > pos_e6, "good capture before quiets");
      require(pos_c6 > first_quiet, "losing capture after quiets");
    }
  }

  {
    auto pos = position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    auto order = move_ordering{};
    auto probe = pos;
    auto killer = no_move;
    auto plain = no_move;
    for(const auto mv : movegen::generate_legal(probe)) {
      if(is_quiet(mv) && killer == no_move) {
        killer = mv;
      } else if(is_quiet(mv) && !(mv == killer) && plain == no_move) {
        plain = mv;
      }
    }
    require(killer != no_move && plain != no_move, "quiets exist");
    order.update_killers(killer, 3);
    order.update_history(pos.side_to_move(), plain, 4);
    auto p = pos;
    auto picker = move_picker(order, 3, no_move, false, move_list{}, false, move_list{}, false,
                              no_move, piece::none, no_move, piece::none, no_move);
    auto mv = no_move;
    auto see = std::numeric_limits<int>::max();
    auto idx = 0;
    auto first_quiet = no_move;
    auto saw_killer_first = false;
    while(picker.next(p, mv, see, idx)) {
      if(is_quiet(mv) && !is_promotion(mv)) {
        if(first_quiet == no_move) {
          first_quiet = mv;
          saw_killer_first = (mv == killer);
        }
      }
    }
    require(saw_killer_first, "killer ahead of butterfly quiet");
  }
}

auto test_root_picker_policy() -> void
{
  using namespace russell;
  using namespace russell::search;

  for(const auto& fen : {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                         "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                         "4k3/8/8/8/8/8/3q4/4K3 w - - 0 1"}) {
    auto pos = position::from_fen(fen);
    auto legal = pos;
    auto legal_moves = movegen::generate_legal(legal);
    auto order = move_ordering{};
    for(auto round = 0; round < 3; ++round) {
      auto tt = no_move;
      auto searchable = false;
      auto excluded = no_move;
      if(round == 1) {
        for(const auto mv : legal_moves) {
          tt = mv;
          searchable = true;
          break;
        }
      }
      if(round == 2) {
        auto n = 0;
        for(const auto mv : legal_moves) {
          if(n++ == 1) {
            excluded = mv;
          }
        }
      }
      auto p = pos;
      auto picker = move_picker(order, 0, tt, searchable, move_list{}, false, move_list{},
                                false, no_move, piece::none, no_move, piece::none, excluded);
      auto mv = no_move;
      auto see = std::numeric_limits<int>::max();
      auto idx = 0;
      auto emitted = std::vector<move>{};
      while(picker.next(p, mv, see, idx)) {
        emitted.push_back(mv);
      }
      auto expected = std::size_t{0};
      for(const auto mv2 : legal_moves) {
        if(!(mv2 == excluded)) {
          ++expected;
        }
      }
      require(emitted.size() == expected, "root picker emits exactly the legal set");
      for(const auto mv2 : legal_moves) {
        if(mv2 == excluded) {
          continue;
        }
        auto count = 0;
        for(const auto e : emitted) {
          if(e == mv2) {
            ++count;
          }
        }
        require(count == 1, "root picker emits each legal move once");
      }
    }
  }

  for(const auto& fen : {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                         "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"}) {
    auto pos = position::from_fen(fen);
    search_limits lim;
    lim.depth = 3;
    search_stopper s1;
    auto p1 = pos;
    const auto r1 = russell::search::search(p1, lim, s1);
    require(r1.has_move, "root search has move");
    {
      auto legal = pos;
      auto ok = false;
      for(const auto mv : movegen::generate_legal(legal)) {
        if(mv == r1.best_move) {
          ok = true;
        }
      }
      require(ok, "root best move legal");
    }
    search_stopper s2;
    auto p2 = pos;
    const auto r2 = russell::search::search(p2, lim, s2);
    require(r2.best_move == r1.best_move && r2.score == r1.score, "root search deterministic");
    auto p3 = position::from_fen(fen);
    const auto r3 = iterative_deepening(p3, 3);
    require(r3.has_move, "iterative deepening has move");
    {
      auto legal = pos;
      auto ok = false;
      for(const auto mv : movegen::generate_legal(legal)) {
        if(mv == r3.best_move) {
          ok = true;
        }
      }
      require(ok, "iterative best move legal");
    }
  }
}

auto test_capture_history() -> void
{
  using russell::color;
  using russell::move_flag;
  using russell::piece_type;

  {
    auto order = russell::search::move_ordering{};
    require(order.capture_history_score(color::white, piece_type::pawn, 35, piece_type::pawn) == 0,
            "fresh capture history zero");
  }

  {
    auto order = russell::search::move_ordering{};
    order.update_capture_history(color::white, piece_type::knight, 27, piece_type::pawn, 32);
    require(order.capture_history_score(color::white, piece_type::knight, 27, piece_type::pawn) == 32,
            "capture bonus applies on fresh entry");
    order.update_capture_history(color::white, piece_type::knight, 27, piece_type::pawn, -32);
    require(order.capture_history_score(color::white, piece_type::knight, 27, piece_type::pawn) == 0,
            "capture malus revokes bonus via gravity");
  }

  {
    auto order = russell::search::move_ordering{};
    order.update_capture_history(color::white, piece_type::rook, 10, piece_type::bishop, 1000);
    require(order.capture_history_score(color::white, piece_type::rook, 10, piece_type::bishop) == 1000,
            "capture history first bonus exact");
    order.update_capture_history(color::white, piece_type::rook, 10, piece_type::bishop, 1000);
    require(order.capture_history_score(color::white, piece_type::rook, 10, piece_type::bishop) == 1939,
            "capture history gravity sublinear");
  }

  {
    auto order = russell::search::move_ordering{};
    order.update_capture_history(color::black, piece_type::queen, 63, piece_type::rook, 16384);
    require(order.capture_history_score(color::black, piece_type::queen, 63, piece_type::rook) == 16384,
            "capture history clamps at max");
    for(int i = 0; i < 500; ++i) {
      order.update_capture_history(color::black, piece_type::queen, 63, piece_type::rook, 16384);
    }
    require(order.capture_history_score(color::black, piece_type::queen, 63, piece_type::rook) == 16384,
            "capture history stays clamped (no overflow)");
    for(int i = 0; i < 500; ++i) {
      order.update_capture_history(color::black, piece_type::queen, 63, piece_type::rook, -16384);
    }
    require(order.capture_history_score(color::black, piece_type::queen, 63, piece_type::rook) == -16384,
            "capture history clamps at min (no overflow)");
  }

  {
    auto order = russell::search::move_ordering{};
    order.update_capture_history(color::white, piece_type::knight, 27, piece_type::pawn, 32);
    require(order.capture_history_score(color::white, piece_type::knight, 27, piece_type::pawn) == 32,
            "capture entry set");
    require(order.capture_history_score(color::white, piece_type::bishop, 27, piece_type::pawn) == 0,
            "attacker piece indexes independently");
    require(order.capture_history_score(color::white, piece_type::knight, 28, piece_type::pawn) == 0,
            "destination indexes independently");
    require(order.capture_history_score(color::white, piece_type::knight, 27, piece_type::rook) == 0,
            "victim piece indexes independently");
    require(order.capture_history_score(color::black, piece_type::knight, 27, piece_type::pawn) == 0,
            "side indexes independently");
  }

  {
    auto pos = russell::position::from_fen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1");
    auto copy = pos;
    auto cap = russell::move{};
    auto found = false;
    for(const auto mv : russell::movegen::generate_legal(copy)) {
      if(mv.flag == move_flag::capture) {
        cap = mv;
        found = true;
      }
    }
    require(found, "normal capture exists");
    auto order = russell::search::move_ordering{};
    order.update_capture_history(color::white, cap, pos, 32);
    require(order.capture_history_score(color::white, piece_type::pawn, 35, piece_type::pawn) == 32,
            "normal capture indexed as (pawn, d5, pawn)");
  }

  {
    auto pos = russell::position::from_fen("5rk1/4P3/8/8/8/8/8/4K3 w - - 0 1");
    auto copy = pos;
    auto promo_cap = russell::move{};
    auto found = false;
    for(const auto mv : russell::movegen::generate_legal(copy)) {
      if(mv.flag == move_flag::promotion_capture && mv.to == 61) {
        promo_cap = mv;
        found = true;
      }
    }
    require(found, "capturing promotion exists");
    auto order = russell::search::move_ordering{};
    order.update_capture_history(color::white, promo_cap, pos, 32);
    require(order.capture_history_score(color::white, piece_type::pawn, 61, piece_type::rook) == 32,
            "capturing promotion indexed as (pawn, f8, rook)");
  }

  {
    auto pos = russell::position::from_fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    auto copy = pos;
    auto ep = russell::move{};
    auto found = false;
    for(const auto mv : russell::movegen::generate_legal(copy)) {
      if(mv.flag == move_flag::en_passant) {
        ep = mv;
        found = true;
      }
    }
    require(found, "en-passant capture exists");
    auto order = russell::search::move_ordering{};
    order.update_capture_history(color::white, ep, pos, 32);
    require(order.capture_history_score(color::white, piece_type::pawn, 43, piece_type::pawn) == 32,
            "en-passant indexed as (pawn, d6, pawn)");
  }

  {
    auto pos = russell::position::from_fen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1");
    auto copy = pos;
    auto quiet = russell::move{};
    auto found = false;
    for(const auto mv : russell::movegen::generate_legal(copy)) {
      if(russell::search::is_quiet(mv)) {
        quiet = mv;
        found = true;
      }
    }
    require(found, "quiet move exists");
    auto order = russell::search::move_ordering{};
    order.update_capture_history(color::white, quiet, pos, 32);
    require(order.capture_history_score(color::white, piece_type::pawn, 35, piece_type::pawn) == 0,
            "quiet move does not touch capture history");
  }

  {
    auto pos = russell::position::from_fen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1");
    auto copy = pos;
    auto cap = russell::move{};
    for(const auto mv : russell::movegen::generate_legal(copy)) {
      if(mv.flag == move_flag::capture) {
        cap = mv;
      }
    }
    auto order = russell::search::move_ordering{};
    const auto before = order.score(pos, cap, 0);
    order.update_capture_history(color::white, cap, pos, 16384);
    const auto after = order.score(pos, cap, 0);
    require(after == before + 16, "capture history contributes ch/1024 to ordering");
  }

  {
    auto pos = russell::position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    auto p = pos;
    const auto r = russell::search::alpha_beta(p, 3);
    require(r.has_move, "capture-history search has move");
    auto legal = pos;
    auto ok = false;
    for(const auto mv : russell::movegen::generate_legal(legal)) {
      if(mv == r.best_move) {
        ok = true;
      }
    }
    require(ok, "capture-history best move legal");
    require(r.capture_history_updates > 0, "capture history updated during search");
  }
}

}

auto main() -> int
{
  test_classification();
  test_mvv_lva_ordering();
  test_captures_before_quiets();
  test_killers();
  test_killer_refinement();
  test_history();
  test_order_preserves_scores();
  test_iterative_deepening_ordering();
  test_see_refines_close_captures();
  test_single_capture_no_refinement();
  test_tt_move_first_with_refinement();
  test_capture_history();
  test_picker_matches_legacy_staging();
  test_picker_targeted_bands();
  test_root_picker_policy();
  return 0;
}
