#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace russell::search {

enum class diagnostic_phase : std::uint8_t {
  main,
  qsearch,
  probcut,
  threat,
  see,
  count
};

inline constexpr auto diagnostic_phase_count = static_cast<std::size_t>(diagnostic_phase::count);
inline constexpr auto diagnostic_depth_count = std::size_t{17};

struct search_diagnostics {
  std::uint64_t search_calls = 0;
  std::uint64_t qsearch_calls = 0;
  std::uint64_t search_nodes = 0;
  std::uint64_t qsearch_nodes = 0;
  std::uint64_t leaf_nodes = 0;
  std::uint64_t terminal_nodes = 0;
  std::uint64_t root_nodes = 0;

  std::uint64_t tt_probes = 0;
  std::uint64_t tt_hits = 0;
  std::uint64_t tt_misses = 0;
  std::uint64_t tt_cutoffs = 0;
  std::uint64_t tt_exact_cutoffs = 0;
  std::uint64_t tt_lower_cutoffs = 0;
  std::uint64_t tt_upper_cutoffs = 0;
  std::uint64_t tt_stores = 0;
  std::uint64_t tt_replacements = 0;
  std::uint64_t tt_move_hits = 0;
  std::uint64_t tt_depth_rejected = 0;
  std::uint64_t tt_bound_rejected = 0;

  std::uint64_t pseudo_generations = 0;
  std::uint64_t legal_generations = 0;
  std::uint64_t gen_full_calls = 0;
  std::uint64_t gen_caps_calls = 0;
  std::uint64_t gen_quiet_calls = 0;
  std::uint64_t gen_evasion_calls = 0;
  std::uint64_t gen_caps_pseudo_moves = 0;
  std::uint64_t validation_makes = 0;
  std::uint64_t validation_unmakes = 0;
  std::uint64_t validation_accepted = 0;
  std::uint64_t validation_rejected = 0;
  std::uint64_t validation_attacks = 0;
  std::uint64_t fast_accepted = 0;
  std::uint64_t fast_rejected = 0;
  std::uint64_t fallback_validations = 0;
  std::uint64_t caps_gen_main_staged = 0;
  std::uint64_t caps_gen_qsearch = 0;
  std::uint64_t caps_gen_probcut = 0;
  std::uint64_t caps_gen_threat = 0;
  std::uint64_t caps_gen_picker_lazy = 0;
  std::uint64_t quiet_gen_main_staged = 0;
  std::uint64_t quiet_gen_picker_lazy = 0;
  std::uint64_t gen_full_cycles = 0;
  std::uint64_t gen_caps_cycles = 0;
  std::uint64_t gen_quiet_cycles = 0;
  std::uint64_t gen_evasion_cycles = 0;
  std::uint64_t is_legal_cycles = 0;
  std::uint64_t is_legal_calls = 0;
  std::uint64_t is_legal_success = 0;
  std::uint64_t is_legal_failure = 0;
  std::uint64_t generated_moves = 0;
  std::uint64_t legal_moves = 0;
  std::array<std::uint64_t, diagnostic_phase_count> phase_generated_moves{};
  std::array<std::uint64_t, diagnostic_phase_count> phase_legal_moves{};

  std::uint64_t make_calls = 0;
  std::uint64_t unmake_calls = 0;
  std::uint64_t temporary_make_unmake = 0;
  std::array<std::uint64_t, diagnostic_phase_count> phase_make_calls{};
  std::array<std::uint64_t, diagnostic_phase_count> phase_unmake_calls{};

  std::uint64_t see_calls = 0;
  std::uint64_t see_ordering = 0;
  std::uint64_t see_qsearch = 0;
  std::uint64_t see_probcut = 0;
  std::uint64_t see_threat = 0;
  std::uint64_t see_lmr_pruning = 0;
  std::uint64_t see_recursive_calls = 0;
  std::uint64_t see_max_recursion = 0;
  std::uint64_t see_position_copies = 0;
  std::uint64_t see_move_generations = 0;
  std::uint64_t see_make_calls = 0;
  std::uint64_t see_unmake_calls = 0;
  std::uint64_t see_attack_calculations = 0;
  std::uint64_t see_capture_moves = 0;
  std::uint64_t see_promotion_moves = 0;
  std::uint64_t see_quiet_moves = 0;
  std::uint64_t see_site_order_moves = 0;
  std::uint64_t see_site_picker = 0;
  std::uint64_t see_site_qsearch_refine = 0;
  std::uint64_t see_site_qsearch_delta = 0;
  std::uint64_t see_site_probcut = 0;
  std::uint64_t see_site_see_prune = 0;
  std::uint64_t see_site_recap = 0;
  std::uint64_t see_site_lmr = 0;
  std::uint64_t see_site_threat = 0;

  std::uint64_t static_eval_calls = 0;
  std::uint64_t classical_eval_calls = 0;
  std::uint64_t eval_qsearch = 0;
  std::uint64_t eval_main = 0;
  std::uint64_t eval_pruning = 0;
  std::uint64_t eval_tt_logic = 0;
  std::uint64_t tt_eval_reused = 0;

  std::uint64_t in_check_calls = 0;
  std::uint64_t attack_detection_calls = 0;
  std::uint64_t check_node_entry = 0;
  std::uint64_t check_move_validation = 0;
  std::uint64_t check_pruning = 0;
  std::uint64_t check_temporary = 0;
  std::uint64_t non_pawn_material_calls = 0;
  std::uint64_t position_derivations = 0;

  std::uint64_t tt_move_processing = 0;
  std::uint64_t capture_stage_processing = 0;
  std::uint64_t quiet_stage_processing = 0;
  std::uint64_t losing_capture_processing = 0;
  std::uint64_t move_score_calls = 0;
  std::uint64_t moves_scored = 0;
  std::uint64_t moves_searched = 0;
  std::uint64_t moves_rejected = 0;
  std::uint64_t picker_initializations = 0;
  std::uint64_t picker_selections = 0;
  std::uint64_t picker_scans = 0;
  std::uint64_t picker_elements_scanned = 0;

  std::uint64_t null_attempts = 0;
  std::uint64_t null_cutoffs = 0;
  std::uint64_t probcut_attempts = 0;
  std::uint64_t probcut_cutoffs = 0;
  std::uint64_t threat_attempts = 0;
  std::uint64_t threat_cutoffs = 0;
  std::uint64_t threat_detected = 0;
  std::uint64_t futility_decisions = 0;
  std::uint64_t rfp_decisions = 0;
  std::uint64_t razor_decisions = 0;
  std::uint64_t lmp_decisions = 0;
  std::uint64_t history_pruning_decisions = 0;
  std::uint64_t lmr_applications = 0;
  std::uint64_t lmr_researches = 0;
  std::uint64_t aspiration_failures = 0;
  std::uint64_t singular_attempts = 0;
  std::uint64_t singular_cutoffs = 0;
  std::uint64_t verification_searches = 0;
  std::uint64_t fail_high = 0;
  std::uint64_t fail_low = 0;

  std::array<std::uint64_t, diagnostic_depth_count> depth_nodes{};
  std::array<std::uint64_t, diagnostic_depth_count> depth_qsearch_nodes{};
  std::array<std::uint64_t, diagnostic_depth_count> depth_tt_probes{};
  std::array<std::uint64_t, diagnostic_depth_count> depth_tt_hits{};
  std::array<std::uint64_t, diagnostic_depth_count> depth_evals{};
  std::array<std::uint64_t, diagnostic_depth_count> depth_see{};
  std::array<std::uint64_t, diagnostic_depth_count> depth_legality{};
  std::array<std::uint64_t, diagnostic_depth_count> depth_make{};
  std::array<std::uint64_t, diagnostic_depth_count> depth_lmr{};
  std::array<std::uint64_t, diagnostic_depth_count> depth_lmr_research{};
  std::array<std::uint64_t, diagnostic_depth_count> depth_null{};
  std::array<std::uint64_t, diagnostic_depth_count> depth_probcut{};

  auto merge(const search_diagnostics& other) noexcept -> void
  {
    const auto add = [](auto& a, const auto& b) { a += b; };
    add(search_calls, other.search_calls); add(qsearch_calls, other.qsearch_calls);
    add(search_nodes, other.search_nodes); add(qsearch_nodes, other.qsearch_nodes);
    add(leaf_nodes, other.leaf_nodes); add(terminal_nodes, other.terminal_nodes);
    add(root_nodes, other.root_nodes); add(tt_probes, other.tt_probes);
    add(tt_hits, other.tt_hits); add(tt_misses, other.tt_misses);
    add(tt_cutoffs, other.tt_cutoffs); add(tt_exact_cutoffs, other.tt_exact_cutoffs);
    add(tt_lower_cutoffs, other.tt_lower_cutoffs); add(tt_upper_cutoffs, other.tt_upper_cutoffs);
    add(tt_stores, other.tt_stores); add(tt_replacements, other.tt_replacements);
    add(tt_move_hits, other.tt_move_hits); add(tt_depth_rejected, other.tt_depth_rejected);
    add(tt_bound_rejected, other.tt_bound_rejected); add(pseudo_generations, other.pseudo_generations);
    add(legal_generations, other.legal_generations);
    add(gen_full_calls, other.gen_full_calls); add(gen_caps_calls, other.gen_caps_calls);
    add(gen_quiet_calls, other.gen_quiet_calls); add(gen_evasion_calls, other.gen_evasion_calls);
    add(gen_caps_pseudo_moves, other.gen_caps_pseudo_moves);
    add(validation_makes, other.validation_makes); add(validation_unmakes, other.validation_unmakes);
    add(validation_accepted, other.validation_accepted);
    add(validation_rejected, other.validation_rejected);
    add(validation_attacks, other.validation_attacks);
    add(fast_accepted, other.fast_accepted); add(fast_rejected, other.fast_rejected);
    add(fallback_validations, other.fallback_validations);
    add(caps_gen_main_staged, other.caps_gen_main_staged);
    add(caps_gen_qsearch, other.caps_gen_qsearch);
    add(caps_gen_probcut, other.caps_gen_probcut);
    add(caps_gen_threat, other.caps_gen_threat);
    add(caps_gen_picker_lazy, other.caps_gen_picker_lazy);
    add(quiet_gen_main_staged, other.quiet_gen_main_staged);
    add(quiet_gen_picker_lazy, other.quiet_gen_picker_lazy);
    add(gen_full_cycles, other.gen_full_cycles); add(gen_caps_cycles, other.gen_caps_cycles);
    add(gen_quiet_cycles, other.gen_quiet_cycles); add(gen_evasion_cycles, other.gen_evasion_cycles);
    add(is_legal_cycles, other.is_legal_cycles);
    add(is_legal_calls, other.is_legal_calls);
    add(is_legal_success, other.is_legal_success); add(is_legal_failure, other.is_legal_failure);
    add(generated_moves, other.generated_moves); add(legal_moves, other.legal_moves);
    add(make_calls, other.make_calls); add(unmake_calls, other.unmake_calls);
    add(temporary_make_unmake, other.temporary_make_unmake); add(see_calls, other.see_calls);
    add(see_ordering, other.see_ordering); add(see_qsearch, other.see_qsearch);
    add(see_probcut, other.see_probcut); add(see_threat, other.see_threat);
    add(see_lmr_pruning, other.see_lmr_pruning); add(see_recursive_calls, other.see_recursive_calls);
    if(see_max_recursion < other.see_max_recursion) see_max_recursion = other.see_max_recursion;
    add(see_position_copies, other.see_position_copies); add(see_move_generations, other.see_move_generations);
    add(see_make_calls, other.see_make_calls); add(see_unmake_calls, other.see_unmake_calls);
    add(see_attack_calculations, other.see_attack_calculations);
    add(see_capture_moves, other.see_capture_moves); add(see_promotion_moves, other.see_promotion_moves);
    add(see_quiet_moves, other.see_quiet_moves);
    add(see_site_order_moves, other.see_site_order_moves); add(see_site_picker, other.see_site_picker);
    add(see_site_qsearch_refine, other.see_site_qsearch_refine);
    add(see_site_qsearch_delta, other.see_site_qsearch_delta);
    add(see_site_probcut, other.see_site_probcut); add(see_site_see_prune, other.see_site_see_prune);
    add(see_site_recap, other.see_site_recap); add(see_site_lmr, other.see_site_lmr);
    add(see_site_threat, other.see_site_threat);
    add(static_eval_calls, other.static_eval_calls); add(classical_eval_calls, other.classical_eval_calls);
    add(eval_qsearch, other.eval_qsearch); add(eval_main, other.eval_main);
    add(eval_pruning, other.eval_pruning); add(eval_tt_logic, other.eval_tt_logic);
    add(tt_eval_reused, other.tt_eval_reused); add(in_check_calls, other.in_check_calls);
    add(attack_detection_calls, other.attack_detection_calls); add(check_node_entry, other.check_node_entry);
    add(check_move_validation, other.check_move_validation); add(check_pruning, other.check_pruning);
    add(check_temporary, other.check_temporary); add(non_pawn_material_calls, other.non_pawn_material_calls);
    add(position_derivations, other.position_derivations); add(tt_move_processing, other.tt_move_processing);
    add(capture_stage_processing, other.capture_stage_processing); add(quiet_stage_processing, other.quiet_stage_processing);
    add(losing_capture_processing, other.losing_capture_processing); add(move_score_calls, other.move_score_calls);
    add(moves_scored, other.moves_scored); add(moves_searched, other.moves_searched);
    add(moves_rejected, other.moves_rejected); add(picker_initializations, other.picker_initializations);
    add(picker_selections, other.picker_selections); add(picker_scans, other.picker_scans);
    add(picker_elements_scanned, other.picker_elements_scanned); add(null_attempts, other.null_attempts);
    add(null_cutoffs, other.null_cutoffs); add(probcut_attempts, other.probcut_attempts);
    add(probcut_cutoffs, other.probcut_cutoffs); add(threat_attempts, other.threat_attempts);
    add(threat_cutoffs, other.threat_cutoffs); add(threat_detected, other.threat_detected);
    add(futility_decisions, other.futility_decisions);
    add(rfp_decisions, other.rfp_decisions); add(razor_decisions, other.razor_decisions);
    add(lmp_decisions, other.lmp_decisions);
    add(history_pruning_decisions, other.history_pruning_decisions); add(lmr_applications, other.lmr_applications);
    add(lmr_researches, other.lmr_researches); add(aspiration_failures, other.aspiration_failures);
    add(singular_attempts, other.singular_attempts); add(singular_cutoffs, other.singular_cutoffs);
    add(verification_searches, other.verification_searches); add(fail_high, other.fail_high);
    add(fail_low, other.fail_low);
    for(std::size_t i = 0; i < diagnostic_phase_count; ++i) {
      add(phase_generated_moves[i], other.phase_generated_moves[i]);
      add(phase_legal_moves[i], other.phase_legal_moves[i]);
      add(phase_make_calls[i], other.phase_make_calls[i]);
      add(phase_unmake_calls[i], other.phase_unmake_calls[i]);
    }
    for(std::size_t i = 0; i < diagnostic_depth_count; ++i) {
      add(depth_nodes[i], other.depth_nodes[i]); add(depth_qsearch_nodes[i], other.depth_qsearch_nodes[i]);
      add(depth_tt_probes[i], other.depth_tt_probes[i]); add(depth_tt_hits[i], other.depth_tt_hits[i]);
      add(depth_evals[i], other.depth_evals[i]); add(depth_see[i], other.depth_see[i]);
      add(depth_legality[i], other.depth_legality[i]); add(depth_make[i], other.depth_make[i]);
      add(depth_lmr[i], other.depth_lmr[i]); add(depth_lmr_research[i], other.depth_lmr_research[i]);
      add(depth_null[i], other.depth_null[i]); add(depth_probcut[i], other.depth_probcut[i]);
    }
  }
};

inline thread_local search_diagnostics* active_diagnostics = nullptr;
inline thread_local diagnostic_phase active_diagnostic_phase = diagnostic_phase::main;
inline thread_local int active_diagnostic_depth = 0;
inline thread_local bool in_validation = false;

struct validation_scope {
  bool old;
  explicit validation_scope() noexcept : old(in_validation) { in_validation = true; }
  ~validation_scope() { in_validation = old; }
};

struct diagnostic_scope {
  diagnostic_phase old_phase;
  explicit diagnostic_scope(diagnostic_phase phase) noexcept : old_phase(active_diagnostic_phase)
  {
    active_diagnostic_phase = phase;
  }
  ~diagnostic_scope() { active_diagnostic_phase = old_phase; }
};

struct diagnostic_binding {
  search_diagnostics* old = nullptr;
  explicit diagnostic_binding(search_diagnostics& diagnostics) noexcept : old(active_diagnostics)
  {
    active_diagnostics = &diagnostics;
  }
  ~diagnostic_binding() { active_diagnostics = old; }
};

struct diagnostic_depth_scope {
  int old_depth;
  explicit diagnostic_depth_scope(int depth) noexcept : old_depth(active_diagnostic_depth)
  {
    active_diagnostic_depth = depth;
  }
  ~diagnostic_depth_scope() { active_diagnostic_depth = old_depth; }
};

[[nodiscard]] inline auto diagnostic_index(diagnostic_phase phase) noexcept -> std::size_t
{
  return static_cast<std::size_t>(phase);
}

[[nodiscard]] inline auto read_cycles() noexcept -> std::uint64_t
{
#if defined(_MSC_VER)
  return static_cast<std::uint64_t>(__rdtsc());
#elif defined(__i386__) || defined(__x86_64__)
  return static_cast<std::uint64_t>(__builtin_ia32_rdtsc());
#else
  return 0;
#endif
}

struct cycle_timer {
  std::uint64_t search_diagnostics::* field;
  std::uint64_t start;
  explicit cycle_timer(std::uint64_t search_diagnostics::* field) noexcept
      : field(field)
      , start(read_cycles())
  {
  }
  ~cycle_timer()
  {
    if(active_diagnostics != nullptr) {
      active_diagnostics->*field += read_cycles() - start;
    }
  }
};

}
