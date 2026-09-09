cmake_minimum_required(VERSION 3.24)
project(russell LANGUAGES CXX)

option(russell_sanitize "enable address and undefined behavior sanitizers" OFF)

find_package(Threads REQUIRED)

if(MSVC)
  add_compile_options(/utf-8)
  add_link_options(/Brepro)
else()
  add_compile_options(-frandom-seed=russell)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)
  endif()
endif()

add_library(russell_fathom STATIC
  third_party/fathom/tbprobe.c
)
set_source_files_properties(third_party/fathom/tbprobe.c PROPERTIES LANGUAGE CXX)
target_include_directories(russell_fathom PUBLIC third_party/fathom)
target_compile_features(russell_fathom PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(russell_fathom PRIVATE /W3)
else()
  target_compile_options(russell_fathom PRIVATE -w)
endif()

add_library(russell_core STATIC
  src/book/polyglot.cpp
  src/core/position.cpp
  src/core/zobrist.cpp
  src/datagen/dataset.cpp
  src/datagen/selfplay.cpp
  src/eval/evaluate.cpp
  src/eval/nnue.cpp
  src/movegen/attacks.cpp
  src/movegen/movegen.cpp
  src/search/ordering.cpp
  src/search/transposition_table.cpp
  src/search/search.cpp
  src/tb/tablebase.cpp
  src/train/trainer.cpp
  src/uci/uci.cpp
)
target_include_directories(russell_core PUBLIC src)
target_compile_features(russell_core PUBLIC cxx_std_20)
target_link_libraries(russell_core PUBLIC Threads::Threads russell_fathom)

add_executable(russell src/main.cpp)
target_link_libraries(russell PRIVATE russell_core)
target_compile_features(russell PRIVATE cxx_std_20)

add_executable(russell_perft tools/perft/perft.cpp)
target_link_libraries(russell_perft PRIVATE russell_core)
target_compile_features(russell_perft PRIVATE cxx_std_20)

add_executable(russell_lmr_exp tools/lmr_exp/lmr_exp.cpp)
target_link_libraries(russell_lmr_exp PRIVATE russell_core)
target_compile_features(russell_lmr_exp PRIVATE cxx_std_20)

add_executable(russell_texel_tune tools/texel_tune/texel_tune.cpp)
target_link_libraries(russell_texel_tune PRIVATE russell_core)
target_compile_features(russell_texel_tune PRIVATE cxx_std_20)

add_executable(russell_bench tools/bench/bench.cpp)
target_link_libraries(russell_bench PRIVATE russell_core)
target_compile_features(russell_bench PRIVATE cxx_std_20)

add_executable(russell_time_manager_test tools/time_manager_test/time_manager_test.cpp)
target_link_libraries(russell_time_manager_test PRIVATE russell_core)
target_compile_features(russell_time_manager_test PRIVATE cxx_std_20)

add_executable(russell_nnue_gen tools/nnue_gen/nnue_gen.cpp)
target_link_libraries(russell_nnue_gen PRIVATE russell_core)
target_compile_features(russell_nnue_gen PRIVATE cxx_std_20)

add_executable(russell_nnue_train tools/nnue_train/nnue_train.cpp)
target_link_libraries(russell_nnue_train PRIVATE russell_core)
target_compile_features(russell_nnue_train PRIVATE cxx_std_20)

add_executable(russell_datagen tools/datagen/datagen.cpp)
target_link_libraries(russell_datagen PRIVATE russell_core)
target_compile_features(russell_datagen PRIVATE cxx_std_20)

add_executable(russell_dataset_report tools/dataset_report/dataset_report.cpp)
target_link_libraries(russell_dataset_report PRIVATE russell_core)
target_compile_features(russell_dataset_report PRIVATE cxx_std_20)

add_executable(russell_datagen_tests tests/unit/datagen_tests.cpp)
target_link_libraries(russell_datagen_tests PRIVATE russell_core)
target_compile_features(russell_datagen_tests PRIVATE cxx_std_20)

add_executable(russell_nnue_train_tests tests/unit/nnue_train_tests.cpp)
target_link_libraries(russell_nnue_train_tests PRIVATE russell_core)
target_compile_features(russell_nnue_train_tests PRIVATE cxx_std_20)

add_executable(russell_nnue_tests tests/unit/nnue_tests.cpp)
target_link_libraries(russell_nnue_tests PRIVATE russell_core)
target_compile_features(russell_nnue_tests PRIVATE cxx_std_20)

add_executable(russell_core_tests tests/unit/core_tests.cpp)
target_link_libraries(russell_core_tests PRIVATE russell_core)
target_compile_features(russell_core_tests PRIVATE cxx_std_20)

add_executable(russell_movegen_tests tests/unit/movegen_tests.cpp)
target_link_libraries(russell_movegen_tests PRIVATE russell_core)
target_compile_features(russell_movegen_tests PRIVATE cxx_std_20)

add_executable(russell_slider_tests tests/unit/slider_tests.cpp)
target_link_libraries(russell_slider_tests PRIVATE russell_core)
target_compile_features(russell_slider_tests PRIVATE cxx_std_20)

add_executable(russell_makeunmake_tests tests/unit/makeunmake_tests.cpp)
target_link_libraries(russell_makeunmake_tests PRIVATE russell_core)
target_include_directories(russell_makeunmake_tests PRIVATE tests)
target_compile_features(russell_makeunmake_tests PRIVATE cxx_std_20)

add_executable(russell_legal_tests tests/unit/legal_tests.cpp)
target_link_libraries(russell_legal_tests PRIVATE russell_core)
target_include_directories(russell_legal_tests PRIVATE tests)
target_compile_features(russell_legal_tests PRIVATE cxx_std_20)

add_executable(russell_fen_tests tests/unit/fen_tests.cpp)
target_link_libraries(russell_fen_tests PRIVATE russell_core)
target_compile_features(russell_fen_tests PRIVATE cxx_std_20)

add_executable(russell_search_tests tests/unit/search_tests.cpp)
target_link_libraries(russell_search_tests PRIVATE russell_core)
target_compile_features(russell_search_tests PRIVATE cxx_std_20)

add_executable(russell_ordering_tests tests/unit/ordering_tests.cpp)
target_link_libraries(russell_ordering_tests PRIVATE russell_core)
target_compile_features(russell_ordering_tests PRIVATE cxx_std_20)

add_executable(russell_transposition_tests tests/unit/transposition_tests.cpp)
target_link_libraries(russell_transposition_tests PRIVATE russell_core)
target_compile_features(russell_transposition_tests PRIVATE cxx_std_20)

add_executable(russell_nullmove_tests tests/unit/nullmove_tests.cpp)
target_link_libraries(russell_nullmove_tests PRIVATE russell_core)
target_compile_features(russell_nullmove_tests PRIVATE cxx_std_20)

add_executable(russell_uci_tests tests/unit/uci_tests.cpp)
target_link_libraries(russell_uci_tests PRIVATE russell_core)
target_compile_features(russell_uci_tests PRIVATE cxx_std_20)
target_compile_definitions(russell_uci_tests PRIVATE
  TUN_TB_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/tablebases")

add_executable(russell_parallel_tests tests/unit/parallel_tests.cpp)
target_link_libraries(russell_parallel_tests PRIVATE russell_core)
target_compile_features(russell_parallel_tests PRIVATE cxx_std_20)

add_executable(russell_tablebase_tests tests/unit/tablebase_tests.cpp)
target_link_libraries(russell_tablebase_tests PRIVATE russell_core)
target_compile_definitions(russell_tablebase_tests PRIVATE
  TUN_TB_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/tablebases")
target_compile_features(russell_tablebase_tests PRIVATE cxx_std_20)

add_executable(russell_book_tests tests/unit/book_tests.cpp)
target_link_libraries(russell_book_tests PRIVATE russell_core)
target_compile_features(russell_book_tests PRIVATE cxx_std_20)

add_executable(russell_lmr_table_tests tests/unit/lmr_table_tests.cpp)
target_link_libraries(russell_lmr_table_tests PRIVATE russell_core)
target_compile_features(russell_lmr_table_tests PRIVATE cxx_std_20)

add_executable(russell_move_ordering_opt_tests tests/unit/move_ordering_opt_tests.cpp)
target_link_libraries(russell_move_ordering_opt_tests PRIVATE russell_core)
target_compile_features(russell_move_ordering_opt_tests PRIVATE cxx_std_20)

add_executable(russell_lmr_eligibility_tests tests/unit/lmr_eligibility_tests.cpp)
target_link_libraries(russell_lmr_eligibility_tests PRIVATE russell_core)
target_compile_features(russell_lmr_eligibility_tests PRIVATE cxx_std_20)

add_executable(russell_lmr_depth_tests tests/unit/lmr_depth_tests.cpp)
target_link_libraries(russell_lmr_depth_tests PRIVATE russell_core)
target_compile_features(russell_lmr_depth_tests PRIVATE cxx_std_20)

add_executable(russell_hard_timeout_tests tests/unit/hard_timeout_tests.cpp)
target_link_libraries(russell_hard_timeout_tests PRIVATE russell_core)
target_compile_features(russell_hard_timeout_tests PRIVATE cxx_std_20)

add_executable(russell_aspiration_nodes_tests tests/unit/aspiration_nodes_tests.cpp)
target_link_libraries(russell_aspiration_nodes_tests PRIVATE russell_core)
target_compile_features(russell_aspiration_nodes_tests PRIVATE cxx_std_20)

add_executable(russell_parallel_search_opt_tests tests/unit/parallel_search_opt_tests.cpp)
target_link_libraries(russell_parallel_search_opt_tests PRIVATE russell_core)
target_compile_features(russell_parallel_search_opt_tests PRIVATE cxx_std_20)

add_executable(russell_search_cleanup_tests tests/unit/search_cleanup_tests.cpp)
target_link_libraries(russell_search_cleanup_tests PRIVATE russell_core)
target_compile_features(russell_search_cleanup_tests PRIVATE cxx_std_20)

add_executable(russell_lmr_tuning_tests tests/unit/lmr_tuning_tests.cpp)
target_link_libraries(russell_lmr_tuning_tests PRIVATE russell_core)
target_compile_features(russell_lmr_tuning_tests PRIVATE cxx_std_20)

add_executable(russell_see_pruning_tests tests/unit/see_pruning_tests.cpp)
target_link_libraries(russell_see_pruning_tests PRIVATE russell_core)
target_compile_features(russell_see_pruning_tests PRIVATE cxx_std_20)

add_executable(russell_rfp_tests tests/unit/rfp_tests.cpp)
target_link_libraries(russell_rfp_tests PRIVATE russell_core)
target_compile_features(russell_rfp_tests PRIVATE cxx_std_20)

add_executable(russell_iir_tests tests/unit/iir_tests.cpp)
target_link_libraries(russell_iir_tests PRIVATE russell_core)
target_compile_features(russell_iir_tests PRIVATE cxx_std_20)

add_executable(russell_history_pruning_tests tests/unit/history_pruning_tests.cpp)
target_link_libraries(russell_history_pruning_tests PRIVATE russell_core)
target_compile_features(russell_history_pruning_tests PRIVATE cxx_std_20)

add_executable(russell_countermove_tests tests/unit/countermove_tests.cpp)
target_link_libraries(russell_countermove_tests PRIVATE russell_core)
target_compile_features(russell_countermove_tests PRIVATE cxx_std_20)

add_executable(russell_tuning_tests tests/unit/tuning_tests.cpp)
target_link_libraries(russell_tuning_tests PRIVATE russell_core)
target_compile_features(russell_tuning_tests PRIVATE cxx_std_20)

add_executable(russell_continuation_tests tests/unit/continuation_tests.cpp)
target_link_libraries(russell_continuation_tests PRIVATE russell_core)
target_compile_features(russell_continuation_tests PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(russell_core PRIVATE /W4 /permissive-)
  target_compile_options(russell PRIVATE /W4 /permissive-)
  target_compile_options(russell_perft PRIVATE /W4 /permissive-)
  target_compile_options(russell_texel_tune PRIVATE /W4 /permissive-)
  target_compile_options(russell_bench PRIVATE /W4 /permissive-)
  target_compile_options(russell_time_manager_test PRIVATE /W4 /permissive-)
  target_compile_options(russell_nnue_gen PRIVATE /W4 /permissive-)
  target_compile_options(russell_nnue_train PRIVATE /W4 /permissive-)
  target_compile_options(russell_datagen PRIVATE /W4 /permissive-)
  target_compile_options(russell_dataset_report PRIVATE /W4 /permissive-)
  target_compile_options(russell_nnue_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_datagen_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_nnue_train_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_core_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_movegen_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_slider_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_makeunmake_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_legal_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_fen_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_search_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_ordering_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_transposition_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_nullmove_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_uci_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_parallel_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_tablebase_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_book_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_lmr_table_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_move_ordering_opt_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_lmr_eligibility_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_lmr_depth_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_hard_timeout_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_aspiration_nodes_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_parallel_search_opt_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_search_cleanup_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_lmr_tuning_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_see_pruning_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_rfp_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_iir_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_history_pruning_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_countermove_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_continuation_tests PRIVATE /W4 /permissive-)
  target_compile_options(russell_tuning_tests PRIVATE /W4 /permissive-)
else()
  target_compile_options(russell_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_perft PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_texel_tune PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_bench PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_nnue_gen PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_nnue_train PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_datagen PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_dataset_report PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_nnue_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_datagen_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_nnue_train_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_core_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_movegen_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_slider_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_makeunmake_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_legal_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_fen_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_search_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_ordering_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_transposition_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_nullmove_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_uci_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_parallel_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_tablebase_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_book_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_lmr_table_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_move_ordering_opt_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_lmr_eligibility_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_lmr_depth_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_hard_timeout_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_aspiration_nodes_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_parallel_search_opt_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_search_cleanup_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_lmr_tuning_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_see_pruning_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_rfp_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_iir_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_history_pruning_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_countermove_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_continuation_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(russell_tuning_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(russell_sanitize)
  if(MSVC)
    set(russell_sanitize_flags /fsanitize=address)
    set(russell_sanitize_link_flags /fsanitize=address)
  else()
    set(russell_sanitize_flags -fsanitize=address,undefined -fno-omit-frame-pointer)
    set(russell_sanitize_link_flags -fsanitize=address,undefined)
  endif()
  set(russell_all_targets
    russell_core russell russell_perft russell_lmr_exp russell_texel_tune russell_bench russell_nnue_gen russell_datagen russell_dataset_report
    russell_nnue_train
    russell_core_tests russell_movegen_tests russell_slider_tests russell_makeunmake_tests
    russell_legal_tests russell_fen_tests russell_search_tests russell_ordering_tests
    russell_transposition_tests russell_nullmove_tests russell_uci_tests
    russell_parallel_tests russell_tablebase_tests russell_book_tests russell_nnue_tests russell_datagen_tests
    russell_nnue_train_tests russell_lmr_table_tests russell_move_ordering_opt_tests russell_lmr_eligibility_tests russell_lmr_depth_tests russell_hard_timeout_tests russell_aspiration_nodes_tests russell_parallel_search_opt_tests russell_search_cleanup_tests russell_lmr_tuning_tests russell_see_pruning_tests russell_rfp_tests russell_iir_tests russell_history_pruning_tests russell_countermove_tests russell_continuation_tests russell_tuning_tests)
  foreach(russell_target IN LISTS russell_all_targets)
    target_compile_options(${russell_target} PRIVATE ${russell_sanitize_flags})
    target_link_options(${russell_target} PRIVATE ${russell_sanitize_link_flags})
  endforeach()
  if(MSVC)

    get_filename_component(russell_msvc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(russell_asan_runtime "${russell_msvc_bin}/clang_rt.asan_dynamic-x86_64.dll")
    if(EXISTS "${russell_asan_runtime}")
      foreach(russell_target IN LISTS russell_all_targets)
        add_custom_command(TARGET ${russell_target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${russell_asan_runtime}" "$<TARGET_FILE_DIR:${russell_target}>"
          COMMENT "Copying ASan runtime next to ${russell_target}")
      endforeach()
    endif()
  endif()
endif()

enable_testing()
add_test(NAME russell_runs COMMAND russell)
add_test(NAME russell_core_tests COMMAND russell_core_tests)
add_test(NAME russell_movegen_tests COMMAND russell_movegen_tests)
add_test(NAME russell_slider_tests COMMAND russell_slider_tests)
add_test(NAME russell_makeunmake_tests COMMAND russell_makeunmake_tests)
add_test(NAME russell_legal_tests COMMAND russell_legal_tests)
add_test(NAME russell_fen_tests COMMAND russell_fen_tests)
add_test(NAME russell_search_tests COMMAND russell_search_tests)
add_test(NAME russell_ordering_tests COMMAND russell_ordering_tests)
add_test(NAME russell_transposition_tests COMMAND russell_transposition_tests)
add_test(NAME russell_nullmove_tests COMMAND russell_nullmove_tests)
add_test(NAME russell_uci_tests COMMAND russell_uci_tests)
add_test(NAME russell_parallel_tests COMMAND russell_parallel_tests)
add_test(NAME russell_tablebase_tests COMMAND russell_tablebase_tests)
add_test(NAME russell_book_tests COMMAND russell_book_tests)
add_test(NAME russell_nnue_tests COMMAND russell_nnue_tests)
add_test(NAME russell_datagen_tests COMMAND russell_datagen_tests)
add_test(NAME russell_nnue_train_tests COMMAND russell_nnue_train_tests)
add_test(NAME russell_lmr_table_tests COMMAND russell_lmr_table_tests)
add_test(NAME russell_move_ordering_opt_tests COMMAND russell_move_ordering_opt_tests)
add_test(NAME russell_lmr_eligibility_tests COMMAND russell_lmr_eligibility_tests)
add_test(NAME russell_lmr_depth_tests COMMAND russell_lmr_depth_tests)
add_test(NAME russell_hard_timeout_tests COMMAND russell_hard_timeout_tests)
add_test(NAME russell_aspiration_nodes_tests COMMAND russell_aspiration_nodes_tests)
add_test(NAME russell_parallel_search_opt_tests COMMAND russell_parallel_search_opt_tests)
add_test(NAME russell_search_cleanup_tests COMMAND russell_search_cleanup_tests)
add_test(NAME russell_lmr_tuning_tests COMMAND russell_lmr_tuning_tests)
add_test(NAME russell_see_pruning_tests COMMAND russell_see_pruning_tests)
add_test(NAME russell_rfp_tests COMMAND russell_rfp_tests)
add_test(NAME russell_iir_tests COMMAND russell_iir_tests)
add_test(NAME russell_history_pruning_tests COMMAND russell_history_pruning_tests)
add_test(NAME russell_countermove_tests COMMAND russell_countermove_tests)
add_test(NAME russell_continuation_tests COMMAND russell_continuation_tests)
add_test(NAME russell_tuning_tests COMMAND russell_tuning_tests)
add_test(NAME russell_perft_smoke COMMAND russell_perft --suite 4)
add_test(NAME russell_bench_smoke COMMAND russell_bench 3)

include(GNUInstallDirs)
install(TARGETS russell russell_perft russell_bench russell_texel_tune russell_nnue_gen russell_datagen russell_dataset_report russell_nnue_train
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ../README.md DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME README.txt)