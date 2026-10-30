# motor de ajedrez russell v1.9

autor: dominic fuentes

un motor de ajedrez uci escrito en c++20.

## compilar

cmake --preset release
cmake --build build/release --config release

## ejecutar

./build/release/engine/release/russell.exe

## uci

uci
isready
position startpos
go depth 15

## cambios v1.9

* `engine/src/search/search.hpp` y `engine/src/search/search.cpp`->reuso de eval estatica TT con `tt_eval_usable/tt_no_static_eval` y contador `tt_eval_reused`, guarda `tt_search_result_valid` que omite store si `best==-mate_value`, seleccion `select_completed_result` y agregacion `aggregate_worker_nodes` saturante, Lazy-SMP con `start_depth=1+slot` y slots bajo mutex mas autoridad central de tiempo (`helper_limits` sin soft/hard) y suma de nodos, e `id name russell v1.9` via uci.
* `engine/src/search/transposition_table.hpp` y `engine/src/search/transposition_table.cpp`->campo `static_eval int32` en hueco de alineacion (`32B` entry/`128B` bucket con `static_assert`), `probe/store` con `static_eval`, reemplazo con claim CAS acotado `4` intentos, fast-path misma-key (mas profundo gana, `EXACT` conserva, preserva move/eval) y victima por edad/profundidad.
* `engine/src/uci/uci.hpp` y `engine/src/uci/uci.cpp`->nuevo `tt_hashfull()` observable e invalidacion TT en `EvalFile` (clear en exito y en `<empty>`, conserva evaluador y TT en fallo de load).
* `engine/src/search/ordering.hpp`->solo comentario: tamano real `147456 int16` (`~287KiB` por tabla, `~574KiB` total).
* `engine/tests/unit/transposition_tests.cpp`, `engine/tests/unit/uci_tests.cpp`, `engine/tests/unit/parallel_tests.cpp`, `engine/tests/unit/search_tests.cpp`, `engine/tests/unit/improving_tests.cpp` y `engine/tests/unit/nullmove_tests.cpp`->roundtrip `static_eval`, politica keep-deepest/EXACT/move/eval y concurrencia, `EvalFile` limpia TT y paridad `bestmove/score` vs referencia neta, Lazy-SMP seleccion/agregacion/publicacion/stop/reuso y `TT` compartida, `test_absurd_depth_bound`, e `improving/nullmove` relajados a invariantes (`pruned<=nodes`, `nodes<=2x` y signo preservado).

## licencia

mit (fathom syzygy se distribuye bajo licencia mit, ver `engine/third_party/fathom`)

note: puede que te estes preguntando por que se llama russell, basicamente el motor fue nombrado asi por la vibora de russell :p