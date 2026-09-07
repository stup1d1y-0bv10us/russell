cmake_minimum_required(VERSION 3.24)
project(tuna LANGUAGES CXX)

option(tuna_sanitize "enable address and undefined behavior sanitizers" OFF)

find_package(Threads REQUIRED)

if(MSVC)
  add_compile_options(/utf-8)
  add_link_options(/Brepro)
else()
  add_compile_options(-frandom-seed=tuna)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)
  endif()
endif()

add_library(tuna_fathom STATIC
  third_party/fathom/tbprobe.c
)
set_source_files_properties(third_party/fathom/tbprobe.c PROPERTIES LANGUAGE CXX)
target_include_directories(tuna_fathom PUBLIC third_party/fathom)
target_compile_features(tuna_fathom PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(tuna_fathom PRIVATE /W3)
else()
  target_compile_options(tuna_fathom PRIVATE -w)
endif()

add_library(tuna_core STATIC
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
target_include_directories(tuna_core PUBLIC src)
target_compile_features(tuna_core PUBLIC cxx_std_20)
target_link_libraries(tuna_core PUBLIC Threads::Threads tuna_fathom)

add_executable(tuna src/main.cpp)
target_link_libraries(tuna PRIVATE tuna_core)
target_compile_features(tuna PRIVATE cxx_std_20)

add_executable(tuna_perft tools/perft/perft.cpp)
target_link_libraries(tuna_perft PRIVATE tuna_core)
target_compile_features(tuna_perft PRIVATE cxx_std_20)

add_executable(tuna_lmr_exp tools/lmr_exp/lmr_exp.cpp)
target_link_libraries(tuna_lmr_exp PRIVATE tuna_core)
target_compile_features(tuna_lmr_exp PRIVATE cxx_std_20)

add_executable(tuna_texel_tune tools/texel_tune/texel_tune.cpp)
target_link_libraries(tuna_texel_tune PRIVATE tuna_core)
target_compile_features(tuna_texel_tune PRIVATE cxx_std_20)

add_executable(tuna_bench tools/bench/bench.cpp)
target_link_libraries(tuna_bench PRIVATE tuna_core)
target_compile_features(tuna_bench PRIVATE cxx_std_20)

add_executable(tuna_time_manager_test tools/time_manager_test/time_manager_test.cpp)
target_link_libraries(tuna_time_manager_test PRIVATE tuna_core)
target_compile_features(tuna_time_manager_test PRIVATE cxx_std_20)

add_executable(tuna_nnue_gen tools/nnue_gen/nnue_gen.cpp)
target_link_libraries(tuna_nnue_gen PRIVATE tuna_core)
target_compile_features(tuna_nnue_gen PRIVATE cxx_std_20)

add_executable(tuna_nnue_train tools/nnue_train/nnue_train.cpp)
target_link_libraries(tuna_nnue_train PRIVATE tuna_core)
target_compile_features(tuna_nnue_train PRIVATE cxx_std_20)

add_executable(tuna_datagen tools/datagen/datagen.cpp)
target_link_libraries(tuna_datagen PRIVATE tuna_core)
target_compile_features(tuna_datagen PRIVATE cxx_std_20)

add_executable(tuna_dataset_report tools/dataset_report/dataset_report.cpp)
target_link_libraries(tuna_dataset_report PRIVATE tuna_core)
target_compile_features(tuna_dataset_report PRIVATE cxx_std_20)

add_executable(tuna_datagen_tests tests/unit/datagen_tests.cpp)
target_link_libraries(tuna_datagen_tests PRIVATE tuna_core)
target_compile_features(tuna_datagen_tests PRIVATE cxx_std_20)

add_executable(tuna_nnue_train_tests tests/unit/nnue_train_tests.cpp)
target_link_libraries(tuna_nnue_train_tests PRIVATE tuna_core)
target_compile_features(tuna_nnue_train_tests PRIVATE cxx_std_20)

add_executable(tuna_nnue_tests tests/unit/nnue_tests.cpp)
target_link_libraries(tuna_nnue_tests PRIVATE tuna_core)
target_compile_features(tuna_nnue_tests PRIVATE cxx_std_20)

add_executable(tuna_core_tests tests/unit/core_tests.cpp)
target_link_libraries(tuna_core_tests PRIVATE tuna_core)
target_compile_features(tuna_core_tests PRIVATE cxx_std_20)

add_executable(tuna_movegen_tests tests/unit/movegen_tests.cpp)
target_link_libraries(tuna_movegen_tests PRIVATE tuna_core)
target_compile_features(tuna_movegen_tests PRIVATE cxx_std_20)

add_executable(tuna_slider_tests tests/unit/slider_tests.cpp)
target_link_libraries(tuna_slider_tests PRIVATE tuna_core)
target_compile_features(tuna_slider_tests PRIVATE cxx_std_20)

add_executable(tuna_makeunmake_tests tests/unit/makeunmake_tests.cpp)
target_link_libraries(tuna_makeunmake_tests PRIVATE tuna_core)
target_include_directories(tuna_makeunmake_tests PRIVATE tests)
target_compile_features(tuna_makeunmake_tests PRIVATE cxx_std_20)

add_executable(tuna_legal_tests tests/unit/legal_tests.cpp)
target_link_libraries(tuna_legal_tests PRIVATE tuna_core)
target_include_directories(tuna_legal_tests PRIVATE tests)
target_compile_features(tuna_legal_tests PRIVATE cxx_std_20)

add_executable(tuna_fen_tests tests/unit/fen_tests.cpp)
target_link_libraries(tuna_fen_tests PRIVATE tuna_core)
target_compile_features(tuna_fen_tests PRIVATE cxx_std_20)

add_executable(tuna_search_tests tests/unit/search_tests.cpp)
target_link_libraries(tuna_search_tests PRIVATE tuna_core)
target_compile_features(tuna_search_tests PRIVATE cxx_std_20)

add_executable(tuna_ordering_tests tests/unit/ordering_tests.cpp)
target_link_libraries(tuna_ordering_tests PRIVATE tuna_core)
target_compile_features(tuna_ordering_tests PRIVATE cxx_std_20)

add_executable(tuna_transposition_tests tests/unit/transposition_tests.cpp)
target_link_libraries(tuna_transposition_tests PRIVATE tuna_core)
target_compile_features(tuna_transposition_tests PRIVATE cxx_std_20)

add_executable(tuna_nullmove_tests tests/unit/nullmove_tests.cpp)
target_link_libraries(tuna_nullmove_tests PRIVATE tuna_core)
target_compile_features(tuna_nullmove_tests PRIVATE cxx_std_20)

add_executable(tuna_uci_tests tests/unit/uci_tests.cpp)
target_link_libraries(tuna_uci_tests PRIVATE tuna_core)
target_compile_features(tuna_uci_tests PRIVATE cxx_std_20)
target_compile_definitions(tuna_uci_tests PRIVATE
  TUN_TB_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/tablebases")

add_executable(tuna_parallel_tests tests/unit/parallel_tests.cpp)
target_link_libraries(tuna_parallel_tests PRIVATE tuna_core)
target_compile_features(tuna_parallel_tests PRIVATE cxx_std_20)

add_executable(tuna_tablebase_tests tests/unit/tablebase_tests.cpp)
target_link_libraries(tuna_tablebase_tests PRIVATE tuna_core)
target_compile_definitions(tuna_tablebase_tests PRIVATE
  TUN_TB_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/tablebases")
target_compile_features(tuna_tablebase_tests PRIVATE cxx_std_20)

add_executable(tuna_book_tests tests/unit/book_tests.cpp)
target_link_libraries(tuna_book_tests PRIVATE tuna_core)
target_compile_features(tuna_book_tests PRIVATE cxx_std_20)

add_executable(tuna_lmr_table_tests tests/unit/lmr_table_tests.cpp)
target_link_libraries(tuna_lmr_table_tests PRIVATE tuna_core)
target_compile_features(tuna_lmr_table_tests PRIVATE cxx_std_20)

add_executable(tuna_move_ordering_opt_tests tests/unit/move_ordering_opt_tests.cpp)
target_link_libraries(tuna_move_ordering_opt_tests PRIVATE tuna_core)
target_compile_features(tuna_move_ordering_opt_tests PRIVATE cxx_std_20)

add_executable(tuna_lmr_eligibility_tests tests/unit/lmr_eligibility_tests.cpp)
target_link_libraries(tuna_lmr_eligibility_tests PRIVATE tuna_core)
target_compile_features(tuna_lmr_eligibility_tests PRIVATE cxx_std_20)

add_executable(tuna_lmr_depth_tests tests/unit/lmr_depth_tests.cpp)
target_link_libraries(tuna_lmr_depth_tests PRIVATE tuna_core)
target_compile_features(tuna_lmr_depth_tests PRIVATE cxx_std_20)

add_executable(tuna_hard_timeout_tests tests/unit/hard_timeout_tests.cpp)
target_link_libraries(tuna_hard_timeout_tests PRIVATE tuna_core)
target_compile_features(tuna_hard_timeout_tests PRIVATE cxx_std_20)

add_executable(tuna_aspiration_nodes_tests tests/unit/aspiration_nodes_tests.cpp)
target_link_libraries(tuna_aspiration_nodes_tests PRIVATE tuna_core)
target_compile_features(tuna_aspiration_nodes_tests PRIVATE cxx_std_20)

add_executable(tuna_parallel_search_opt_tests tests/unit/parallel_search_opt_tests.cpp)
target_link_libraries(tuna_parallel_search_opt_tests PRIVATE tuna_core)
target_compile_features(tuna_parallel_search_opt_tests PRIVATE cxx_std_20)

add_executable(tuna_search_cleanup_tests tests/unit/search_cleanup_tests.cpp)
target_link_libraries(tuna_search_cleanup_tests PRIVATE tuna_core)
target_compile_features(tuna_search_cleanup_tests PRIVATE cxx_std_20)

add_executable(tuna_lmr_tuning_tests tests/unit/lmr_tuning_tests.cpp)
target_link_libraries(tuna_lmr_tuning_tests PRIVATE tuna_core)
target_compile_features(tuna_lmr_tuning_tests PRIVATE cxx_std_20)

add_executable(tuna_see_pruning_tests tests/unit/see_pruning_tests.cpp)
target_link_libraries(tuna_see_pruning_tests PRIVATE tuna_core)
target_compile_features(tuna_see_pruning_tests PRIVATE cxx_std_20)

add_executable(tuna_rfp_tests tests/unit/rfp_tests.cpp)
target_link_libraries(tuna_rfp_tests PRIVATE tuna_core)
target_compile_features(tuna_rfp_tests PRIVATE cxx_std_20)

add_executable(tuna_iir_tests tests/unit/iir_tests.cpp)
target_link_libraries(tuna_iir_tests PRIVATE tuna_core)
target_compile_features(tuna_iir_tests PRIVATE cxx_std_20)

add_executable(tuna_history_pruning_tests tests/unit/history_pruning_tests.cpp)
target_link_libraries(tuna_history_pruning_tests PRIVATE tuna_core)
target_compile_features(tuna_history_pruning_tests PRIVATE cxx_std_20)

add_executable(tuna_countermove_tests tests/unit/countermove_tests.cpp)
target_link_libraries(tuna_countermove_tests PRIVATE tuna_core)
target_compile_features(tuna_countermove_tests PRIVATE cxx_std_20)

add_executable(tuna_tuning_tests tests/unit/tuning_tests.cpp)
target_link_libraries(tuna_tuning_tests PRIVATE tuna_core)
target_compile_features(tuna_tuning_tests PRIVATE cxx_std_20)

add_executable(tuna_continuation_tests tests/unit/continuation_tests.cpp)
target_link_libraries(tuna_continuation_tests PRIVATE tuna_core)
target_compile_features(tuna_continuation_tests PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(tuna_core PRIVATE /W4 /permissive-)
  target_compile_options(tuna PRIVATE /W4 /permissive-)
  target_compile_options(tuna_perft PRIVATE /W4 /permissive-)
  target_compile_options(tuna_texel_tune PRIVATE /W4 /permissive-)
  target_compile_options(tuna_bench PRIVATE /W4 /permissive-)
  target_compile_options(tuna_time_manager_test PRIVATE /W4 /permissive-)
  target_compile_options(tuna_nnue_gen PRIVATE /W4 /permissive-)
  target_compile_options(tuna_nnue_train PRIVATE /W4 /permissive-)
  target_compile_options(tuna_datagen PRIVATE /W4 /permissive-)
  target_compile_options(tuna_dataset_report PRIVATE /W4 /permissive-)
  target_compile_options(tuna_nnue_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_datagen_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_nnue_train_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_core_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_movegen_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_slider_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_makeunmake_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_legal_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_fen_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_search_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_ordering_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_transposition_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_nullmove_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_uci_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_parallel_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_tablebase_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_book_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_lmr_table_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_move_ordering_opt_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_lmr_eligibility_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_lmr_depth_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_hard_timeout_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_aspiration_nodes_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_parallel_search_opt_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_search_cleanup_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_lmr_tuning_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_see_pruning_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_rfp_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_iir_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_history_pruning_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_countermove_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_continuation_tests PRIVATE /W4 /permissive-)
  target_compile_options(tuna_tuning_tests PRIVATE /W4 /permissive-)
else()
  target_compile_options(tuna_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_perft PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_texel_tune PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_bench PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_nnue_gen PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_nnue_train PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_datagen PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_dataset_report PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_nnue_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_datagen_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_nnue_train_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_core_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_movegen_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_slider_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_makeunmake_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_legal_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_fen_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_search_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_ordering_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_transposition_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_nullmove_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_uci_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_parallel_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_tablebase_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_book_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_lmr_table_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_move_ordering_opt_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_lmr_eligibility_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_lmr_depth_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_hard_timeout_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_aspiration_nodes_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_parallel_search_opt_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_search_cleanup_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_lmr_tuning_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_see_pruning_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_rfp_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_iir_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_history_pruning_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_countermove_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_continuation_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tuna_tuning_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(tuna_sanitize)
  if(MSVC)
    set(tuna_sanitize_flags /fsanitize=address)
    set(tuna_sanitize_link_flags /fsanitize=address)
  else()
    set(tuna_sanitize_flags -fsanitize=address,undefined -fno-omit-frame-pointer)
    set(tuna_sanitize_link_flags -fsanitize=address,undefined)
  endif()
  set(tuna_all_targets
    tuna_core tuna tuna_perft tuna_lmr_exp tuna_texel_tune tuna_bench tuna_nnue_gen tuna_datagen tuna_dataset_report
    tuna_nnue_train
    tuna_core_tests tuna_movegen_tests tuna_slider_tests tuna_makeunmake_tests
    tuna_legal_tests tuna_fen_tests tuna_search_tests tuna_ordering_tests
    tuna_transposition_tests tuna_nullmove_tests tuna_uci_tests
    tuna_parallel_tests tuna_tablebase_tests tuna_book_tests tuna_nnue_tests tuna_datagen_tests
    tuna_nnue_train_tests tuna_lmr_table_tests tuna_move_ordering_opt_tests tuna_lmr_eligibility_tests tuna_lmr_depth_tests tuna_hard_timeout_tests tuna_aspiration_nodes_tests tuna_parallel_search_opt_tests tuna_search_cleanup_tests tuna_lmr_tuning_tests tuna_see_pruning_tests tuna_rfp_tests tuna_iir_tests tuna_history_pruning_tests tuna_countermove_tests tuna_continuation_tests tuna_tuning_tests)
  foreach(tuna_target IN LISTS tuna_all_targets)
    target_compile_options(${tuna_target} PRIVATE ${tuna_sanitize_flags})
    target_link_options(${tuna_target} PRIVATE ${tuna_sanitize_link_flags})
  endforeach()
  if(MSVC)

    get_filename_component(tuna_msvc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(tuna_asan_runtime "${tuna_msvc_bin}/clang_rt.asan_dynamic-x86_64.dll")
    if(EXISTS "${tuna_asan_runtime}")
      foreach(tuna_target IN LISTS tuna_all_targets)
        add_custom_command(TARGET ${tuna_target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${tuna_asan_runtime}" "$<TARGET_FILE_DIR:${tuna_target}>"
          COMMENT "Copying ASan runtime next to ${tuna_target}")
      endforeach()
    endif()
  endif()
endif()

enable_testing()
add_test(NAME tuna_runs COMMAND tuna)
add_test(NAME tuna_core_tests COMMAND tuna_core_tests)
add_test(NAME tuna_movegen_tests COMMAND tuna_movegen_tests)
add_test(NAME tuna_slider_tests COMMAND tuna_slider_tests)
add_test(NAME tuna_makeunmake_tests COMMAND tuna_makeunmake_tests)
add_test(NAME tuna_legal_tests COMMAND tuna_legal_tests)
add_test(NAME tuna_fen_tests COMMAND tuna_fen_tests)
add_test(NAME tuna_search_tests COMMAND tuna_search_tests)
add_test(NAME tuna_ordering_tests COMMAND tuna_ordering_tests)
add_test(NAME tuna_transposition_tests COMMAND tuna_transposition_tests)
add_test(NAME tuna_nullmove_tests COMMAND tuna_nullmove_tests)
add_test(NAME tuna_uci_tests COMMAND tuna_uci_tests)
add_test(NAME tuna_parallel_tests COMMAND tuna_parallel_tests)
add_test(NAME tuna_tablebase_tests COMMAND tuna_tablebase_tests)
add_test(NAME tuna_book_tests COMMAND tuna_book_tests)
add_test(NAME tuna_nnue_tests COMMAND tuna_nnue_tests)
add_test(NAME tuna_datagen_tests COMMAND tuna_datagen_tests)
add_test(NAME tuna_nnue_train_tests COMMAND tuna_nnue_train_tests)
add_test(NAME tuna_lmr_table_tests COMMAND tuna_lmr_table_tests)
add_test(NAME tuna_move_ordering_opt_tests COMMAND tuna_move_ordering_opt_tests)
add_test(NAME tuna_lmr_eligibility_tests COMMAND tuna_lmr_eligibility_tests)
add_test(NAME tuna_lmr_depth_tests COMMAND tuna_lmr_depth_tests)
add_test(NAME tuna_hard_timeout_tests COMMAND tuna_hard_timeout_tests)
add_test(NAME tuna_aspiration_nodes_tests COMMAND tuna_aspiration_nodes_tests)
add_test(NAME tuna_parallel_search_opt_tests COMMAND tuna_parallel_search_opt_tests)
add_test(NAME tuna_search_cleanup_tests COMMAND tuna_search_cleanup_tests)
add_test(NAME tuna_lmr_tuning_tests COMMAND tuna_lmr_tuning_tests)
add_test(NAME tuna_see_pruning_tests COMMAND tuna_see_pruning_tests)
add_test(NAME tuna_rfp_tests COMMAND tuna_rfp_tests)
add_test(NAME tuna_iir_tests COMMAND tuna_iir_tests)
add_test(NAME tuna_history_pruning_tests COMMAND tuna_history_pruning_tests)
add_test(NAME tuna_countermove_tests COMMAND tuna_countermove_tests)
add_test(NAME tuna_continuation_tests COMMAND tuna_continuation_tests)
add_test(NAME tuna_tuning_tests COMMAND tuna_tuning_tests)
add_test(NAME tuna_perft_smoke COMMAND tuna_perft --suite 4)
add_test(NAME tuna_bench_smoke COMMAND tuna_bench 3)

include(GNUInstallDirs)
install(TARGETS tuna tuna_perft tuna_bench tuna_texel_tune tuna_nnue_gen tuna_datagen tuna_dataset_report tuna_nnue_train
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ../README.md DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME README.txt)