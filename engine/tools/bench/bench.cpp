#include "core/position.hpp"
#include "search/search.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct bench_case {
  const char* name;
  const char* fen;
};

constexpr auto suite = std::to_array<bench_case>({
  {"startpos",
   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
  {"kiwipete",
   "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
  {"position 3",
   "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"},
  {"position 4",
   "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"},
  {"position 5",
   "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"},
  {"position 6",
   "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"},
});

}

auto main(int argc, char** argv) -> int
{
  const auto depth = argc >= 2 ? std::atoi(argv[1]) : 8;
  if(depth < 1 || depth > 16) {
    std::fprintf(stderr, "usage: russell_bench [depth 1..16]\n");
    return 2;
  }

  auto total_nodes = std::uint64_t{0};
  auto total_diagnostics = russell::search::search_diagnostics{};
  const auto start = std::chrono::steady_clock::now();
  for(const auto& test : suite) {
    auto pos = russell::position::from_fen(test.fen);
    const auto result = russell::search::iterative_deepening(pos, depth);
    total_nodes += result.nodes;
    total_diagnostics.merge(result.diagnostics);
    std::printf("%-11s depth %d %8llu nodes\n", test.name, depth,
                static_cast<unsigned long long>(result.nodes));
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start)
                      .count();
  const auto nps = ms == 0 ? 0 : static_cast<std::uint64_t>(total_nodes) * 1000 / ms;
  std::printf("total %llu nodes, %llu ms, %llu nps\n",
              static_cast<unsigned long long>(total_nodes),
              static_cast<unsigned long long>(ms),
              static_cast<unsigned long long>(nps));
  std::printf("diag search %llu qsearch %llu tt_probe %llu tt_hit %llu tt_cutoff %llu eval %llu see %llu see_rec %llu see_max %llu see_copy %llu see_gen %llu see_make %llu see_unmake %llu legal_gen %llu is_legal %llu make %llu unmake %llu\n",
              static_cast<unsigned long long>(total_diagnostics.search_nodes),
              static_cast<unsigned long long>(total_diagnostics.qsearch_nodes),
              static_cast<unsigned long long>(total_diagnostics.tt_probes),
              static_cast<unsigned long long>(total_diagnostics.tt_hits),
              static_cast<unsigned long long>(total_diagnostics.tt_cutoffs),
              static_cast<unsigned long long>(total_diagnostics.static_eval_calls),
              static_cast<unsigned long long>(total_diagnostics.see_calls),
              static_cast<unsigned long long>(total_diagnostics.see_recursive_calls),
              static_cast<unsigned long long>(total_diagnostics.see_max_recursion),
              static_cast<unsigned long long>(total_diagnostics.see_position_copies),
              static_cast<unsigned long long>(total_diagnostics.see_move_generations),
              static_cast<unsigned long long>(total_diagnostics.see_make_calls),
              static_cast<unsigned long long>(total_diagnostics.see_unmake_calls),
              static_cast<unsigned long long>(total_diagnostics.legal_generations),
              static_cast<unsigned long long>(total_diagnostics.is_legal_calls),
              static_cast<unsigned long long>(total_diagnostics.make_calls),
              static_cast<unsigned long long>(total_diagnostics.unmake_calls));
  std::printf("diag rates tt_hit_per_probe %.6f tt_cutoff_per_probe %.6f see_per_node %.6f eval_per_node %.6f legal_gen_per_node %.6f make_per_node %.6f\n",
              total_diagnostics.tt_probes == 0 ? 0.0 : static_cast<double>(total_diagnostics.tt_hits) / total_diagnostics.tt_probes,
              total_diagnostics.tt_probes == 0 ? 0.0 : static_cast<double>(total_diagnostics.tt_cutoffs) / total_diagnostics.tt_probes,
              total_diagnostics.search_nodes == 0 ? 0.0 : static_cast<double>(total_diagnostics.see_calls) / total_diagnostics.search_nodes,
              total_diagnostics.search_nodes == 0 ? 0.0 : static_cast<double>(total_diagnostics.static_eval_calls) / total_diagnostics.search_nodes,
              total_diagnostics.search_nodes == 0 ? 0.0 : static_cast<double>(total_diagnostics.legal_generations) / total_diagnostics.search_nodes,
              total_diagnostics.search_nodes == 0 ? 0.0 : static_cast<double>(total_diagnostics.make_calls) / total_diagnostics.search_nodes);
  std::printf("see_sites order_moves %llu picker %llu qsearch_refine %llu qsearch_delta %llu probcut %llu see_prune %llu recap %llu lmr %llu threat %llu\n",
              static_cast<unsigned long long>(total_diagnostics.see_site_order_moves),
              static_cast<unsigned long long>(total_diagnostics.see_site_picker),
              static_cast<unsigned long long>(total_diagnostics.see_site_qsearch_refine),
              static_cast<unsigned long long>(total_diagnostics.see_site_qsearch_delta),
              static_cast<unsigned long long>(total_diagnostics.see_site_probcut),
              static_cast<unsigned long long>(total_diagnostics.see_site_see_prune),
              static_cast<unsigned long long>(total_diagnostics.see_site_recap),
              static_cast<unsigned long long>(total_diagnostics.see_site_lmr),
              static_cast<unsigned long long>(total_diagnostics.see_site_threat));
  std::printf("gen full %llu caps %llu caps_pseudo %llu quiet %llu evasion %llu valid_make %llu valid_unmake %llu valid_ok %llu valid_reject %llu valid_attacks %llu\n",
              static_cast<unsigned long long>(total_diagnostics.gen_full_calls),
              static_cast<unsigned long long>(total_diagnostics.gen_caps_calls),
              static_cast<unsigned long long>(total_diagnostics.gen_caps_pseudo_moves),
              static_cast<unsigned long long>(total_diagnostics.gen_quiet_calls),
              static_cast<unsigned long long>(total_diagnostics.gen_evasion_calls),
              static_cast<unsigned long long>(total_diagnostics.validation_makes),
              static_cast<unsigned long long>(total_diagnostics.validation_unmakes),
              static_cast<unsigned long long>(total_diagnostics.validation_accepted),
              static_cast<unsigned long long>(total_diagnostics.validation_rejected),
              static_cast<unsigned long long>(total_diagnostics.validation_attacks));
  std::printf("gen_cycles full %llu caps %llu quiet %llu evasion %llu is_legal %llu\n",
              static_cast<unsigned long long>(total_diagnostics.gen_full_cycles),
              static_cast<unsigned long long>(total_diagnostics.gen_caps_cycles),
              static_cast<unsigned long long>(total_diagnostics.gen_quiet_cycles),
              static_cast<unsigned long long>(total_diagnostics.gen_evasion_cycles),
              static_cast<unsigned long long>(total_diagnostics.is_legal_cycles));
  std::printf("fastpath accepted %llu rejected %llu fallback %llu\n",
              static_cast<unsigned long long>(total_diagnostics.fast_accepted),
              static_cast<unsigned long long>(total_diagnostics.fast_rejected),
              static_cast<unsigned long long>(total_diagnostics.fallback_validations));
  std::printf("check_calls in_check %llu attacks %llu valid_attacks %llu\n",
              static_cast<unsigned long long>(total_diagnostics.in_check_calls),
              static_cast<unsigned long long>(total_diagnostics.attack_detection_calls),
              static_cast<unsigned long long>(total_diagnostics.validation_attacks));
  std::printf("gen_sites caps_main %llu caps_qsearch %llu caps_probcut %llu caps_threat %llu caps_picker %llu quiet_main %llu quiet_picker %llu\n",
              static_cast<unsigned long long>(total_diagnostics.caps_gen_main_staged),
              static_cast<unsigned long long>(total_diagnostics.caps_gen_qsearch),
              static_cast<unsigned long long>(total_diagnostics.caps_gen_probcut),
              static_cast<unsigned long long>(total_diagnostics.caps_gen_threat),
              static_cast<unsigned long long>(total_diagnostics.caps_gen_picker_lazy),
              static_cast<unsigned long long>(total_diagnostics.quiet_gen_main_staged),
              static_cast<unsigned long long>(total_diagnostics.quiet_gen_picker_lazy));
  for(std::size_t phase = 0; phase < russell::search::diagnostic_phase_count; ++phase) {
    std::printf("phase %llu generated %llu legal %llu make %llu unmake %llu\n",
                static_cast<unsigned long long>(phase),
                static_cast<unsigned long long>(total_diagnostics.phase_generated_moves[phase]),
                static_cast<unsigned long long>(total_diagnostics.phase_legal_moves[phase]),
                static_cast<unsigned long long>(total_diagnostics.phase_make_calls[phase]),
                static_cast<unsigned long long>(total_diagnostics.phase_unmake_calls[phase]));
  }
  for(std::size_t d = 1; d < russell::search::diagnostic_depth_count; ++d) {
    if(total_diagnostics.depth_nodes[d] == 0 && total_diagnostics.depth_qsearch_nodes[d] == 0) continue;
    std::printf("depth %llu nodes %llu qsearch %llu tt_probe %llu tt_hit %llu eval %llu see %llu make %llu lmr %llu lmr_research %llu null %llu probcut %llu\n",
                static_cast<unsigned long long>(d),
                static_cast<unsigned long long>(total_diagnostics.depth_nodes[d]),
                static_cast<unsigned long long>(total_diagnostics.depth_qsearch_nodes[d]),
                static_cast<unsigned long long>(total_diagnostics.depth_tt_probes[d]),
                static_cast<unsigned long long>(total_diagnostics.depth_tt_hits[d]),
                static_cast<unsigned long long>(total_diagnostics.depth_evals[d]),
                static_cast<unsigned long long>(total_diagnostics.depth_see[d]),
                static_cast<unsigned long long>(total_diagnostics.depth_make[d]),
                static_cast<unsigned long long>(total_diagnostics.depth_lmr[d]),
                static_cast<unsigned long long>(total_diagnostics.depth_lmr_research[d]),
                static_cast<unsigned long long>(total_diagnostics.depth_null[d]),
                static_cast<unsigned long long>(total_diagnostics.depth_probcut[d]));
  }
  return 0;
}
