#include "core/position.hpp"
#include "search/search.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct audit_case {
  const char* name;
  const char* fen;
};

constexpr audit_case cases[] = {
    {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R2Q1RK1 w KQkq - 0 1"},
    {"endgame", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"},
    {"tactical", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"},
    {"promotion", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"},
    {"middlegame", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"},
};

auto run_case(const audit_case& c, int max_depth) -> void
{
  for(auto depth = 1; depth <= max_depth; ++depth) {
    auto pos = russell::position::from_fen(c.fen);
    const auto start = std::chrono::steady_clock::now();
    const auto r = russell::search::iterative_deepening(pos, depth);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    const auto& d = r.diagnostics;
    std::printf(
        "AUDIT pos=%s depth=%d nodes=%llu ms=%llu score=%d "
        "search=%llu qsearch=%llu root=%llu "
        "tt_probe=%llu tt_hit=%llu tt_miss=%llu tt_cut=%llu tt_exact=%llu tt_lower=%llu tt_upper=%llu "
        "tt_move=%llu tt_depth_rej=%llu tt_bound_rej=%llu tt_store=%llu tt_repl=%llu "
        "null_att=%llu null_cut=%llu "
        "rfp=%llu razor=%llu lmp=%llu fut=%llu histpr=%llu "
        "prob_att=%llu prob_cut=%llu probcut_ctx=%llu/%llu "
        "sing_att=%llu sing_cut=%llu sing_ext=%llu "
        "lmr=%llu lmr_re=%llu diag_lmr=%llu diag_lmr_re=%llu iir=%llu ext=%llu checkext=%llu "
        "asp_att=%llu asp_lo=%llu asp_hi=%llu asp_re=%llu "
        "threat=%llu/%llu tteval=%llu improving=%llu "
        "fail_hi=%llu fail_lo=%llu moves_searched=%llu eval=%llu see=%llu\n",
        c.name, depth, static_cast<unsigned long long>(r.nodes),
        static_cast<unsigned long long>(ms), r.score,
        static_cast<unsigned long long>(d.search_nodes),
        static_cast<unsigned long long>(d.qsearch_nodes),
        static_cast<unsigned long long>(d.root_nodes),
        static_cast<unsigned long long>(d.tt_probes), static_cast<unsigned long long>(d.tt_hits),
        static_cast<unsigned long long>(d.tt_misses), static_cast<unsigned long long>(d.tt_cutoffs),
        static_cast<unsigned long long>(d.tt_exact_cutoffs),
        static_cast<unsigned long long>(d.tt_lower_cutoffs),
        static_cast<unsigned long long>(d.tt_upper_cutoffs),
        static_cast<unsigned long long>(d.tt_move_hits),
        static_cast<unsigned long long>(d.tt_depth_rejected),
        static_cast<unsigned long long>(d.tt_bound_rejected),
        static_cast<unsigned long long>(d.tt_stores),
        static_cast<unsigned long long>(d.tt_replacements),
        static_cast<unsigned long long>(d.null_attempts),
        static_cast<unsigned long long>(d.null_cutoffs), static_cast<unsigned long long>(r.rfp_pruned),
        static_cast<unsigned long long>(r.razor_pruned),
        static_cast<unsigned long long>(r.lmp_pruned),
        static_cast<unsigned long long>(d.futility_decisions),
        static_cast<unsigned long long>(d.history_pruning_decisions),
        static_cast<unsigned long long>(d.probcut_attempts),
        static_cast<unsigned long long>(d.probcut_cutoffs),
        static_cast<unsigned long long>(r.probcut_considered),
        static_cast<unsigned long long>(r.probcut_cutoff),
        static_cast<unsigned long long>(d.singular_attempts),
        static_cast<unsigned long long>(d.singular_cutoffs),
        static_cast<unsigned long long>(r.singular_extensions),
        static_cast<unsigned long long>(r.lmr_applied),
        static_cast<unsigned long long>(r.lmr_research),
        static_cast<unsigned long long>(d.lmr_applications),
        static_cast<unsigned long long>(d.lmr_researches),
        static_cast<unsigned long long>(r.iir_applied),
        static_cast<unsigned long long>(r.extensions_applied),
        static_cast<unsigned long long>(r.check_extensions),
        static_cast<unsigned long long>(r.aspiration_attempts),
        static_cast<unsigned long long>(r.aspiration_fail_low),
        static_cast<unsigned long long>(r.aspiration_fail_high),
        static_cast<unsigned long long>(r.aspiration_researches),
        static_cast<unsigned long long>(r.threat_considered),
        static_cast<unsigned long long>(r.threat_detected),
        static_cast<unsigned long long>(r.tt_eval_reused),
        static_cast<unsigned long long>(r.improving_count),
        static_cast<unsigned long long>(d.fail_high), static_cast<unsigned long long>(d.fail_low),
        static_cast<unsigned long long>(d.moves_searched),
        static_cast<unsigned long long>(d.static_eval_calls),
        static_cast<unsigned long long>(d.see_calls));
  }
}

}

auto main(int argc, char** argv) -> int
{
  const auto max_depth = argc >= 2 ? std::atoi(argv[1]) : 10;
  const std::string only = argc >= 3 ? argv[2] : "";
  for(const auto& c : cases) {
    if(!only.empty() && only != c.name) continue;
    run_case(c, max_depth);
  }
  return 0;
}
