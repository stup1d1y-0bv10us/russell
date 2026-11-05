# motor de ajedrez russell v2.0

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

## cambios v2.0

* `engine/src/search/diagnostics.hpp` (nuevo) y `engine/src/search/search.hpp`/`search.cpp`->nuevo `search_diagnostics` con fases `main/qsearch/probcut/threat/see`, `diagnostic_scope/depth_scope/binding` y `active_diagnostics`, `evaluate_instrumented`, `checked` hoisteado una vez por nodo, contadores TT/NMP/RFP/razor/singular/ProbCut/LMR/moves y `elapsed_ms` en resultado, LMR `+1` si `!captura && !pv && depth>=4 && move_count>=10`, y plomeria `diagnostics` en `alpha_beta/iterative/search_impl/emergency`.
* `engine/src/movegen/pin_state.hpp` (nuevo), `engine/src/movegen/movegen.cpp` y `engine/src/search/ordering.cpp`->`pin_state/compute_pin_state/pin_between` como fuente unica, fast-path sin `make/unmake` en `generate_captures_promotions/quiet/evasions/legal` con `fast_accepted/rejected/fallback` y `cycle_timer`, y `see_attackers_to` solo atacantes a `target` con mismo orden/flags que pseudo-legal mas `least_valuable_capture_to` filtrado por seguridad de rey.
* `engine/src/core/position.cpp` y `engine/src/eval/evaluate.cpp`->solo instrumentacion: conteo `make/unmake/null` por fase/profundidad/SEE/validacion y `classical_eval_calls`, sin cambio de reglas.
* `engine/CMakeLists.txt`, `engine/src/uci/uci.cpp`, `engine/tools/bench/bench.cpp` y nuevas `engine/tools/legal_oracle`, `see_oracle`, `eval_oracle`, `pruning_audit`->targets `russell_legal/see/eval_oracle` y `russell_pruning_audit`, `info` con `time/nps` y `info string diag ...`, `bench` agrega `merge` y reportes `diag/rates/see_sites/gen/validacion/fastpath/check/fases/profundidad`.
* `engine/src/book/polyglot.cpp`->ampliación del repertorio de aperturas integrado de 20 a 26 líneas, selección ponderada de aperturas, validación legal mejorada, gestión de movimientos duplicados y corrección de la codificación polyglot del enroque. se mantiene la compatibilidad con archivos externos `.bin`.

## licencia

mit (fathom syzygy se distribuye bajo licencia mit, ver `engine/third_party/fathom`)

note: puede que te estes preguntando por que se llama russell, basicamente el motor fue nombrado asi por la vibora de russell :p