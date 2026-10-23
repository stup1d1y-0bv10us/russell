# motor de ajedrez russell v1.8

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

## cambios v1.8

* `engine/src/core/position.cpp` y `engine/src/uci/uci.cpp`->hardening `STEP 12`: guarda de rangos en placement FEN y casilla EP, `try/catch` en `stoi` de halfmove/fullmove, clamp `threads [1,64]`.
* `engine/src/search/ordering.hpp` y `engine/src/search/ordering.cpp`->`killer_min_depth=2` y `update_killers` solo quietas, `score` con `prev_moved/prev2_move/prev2_moved` y bandas countermove `30000+` y continuacion `20000+`, `update_history` firmada con gravedad, countermove puntuada en heap `16.7M int16` con umbral `>=1` y `update_countermove_history` bonus/malus, continuaciones por pieza 1ply/2ply `6x64x6x64` con gravedad `16384` y clamp `+-10000`, captura con semantica promocion captura/EP, y nuevo `move_picker` por etapas `TT->capturas buenas->quietas->perdedoras` con ventana SEE `32` y `pick_best` estable.
* `engine/src/search/search.hpp` y `engine/src/search/search.cpp`->nuevo `SearchStackEntry` (`staticEval/improving/prevMove/movedPiece/ply`) y `kSearchStackSize=256` con `init_search_stack/publish_child_stack/publish_null_child`, `ss` enhebrado en `quiescence/negamax_ab/root/aspiration`, `improving` desde `ss-2`, sonda amenaza intacta, `searched_quiets` con malus `-depth^2`, aprendizaje continuacion 1ply/2ply y countermove bajo disciplina buscada, piso killer `depth>=2`, `move_picker` interior y en raiz con `prev_best` encadenado, tope `depth>max_ply=128`, y `capture_history_lmr_scale=4096` sin cambio conductual.
* `engine/tests/unit/continuation_tests.cpp`, `engine/tests/unit/countermove_tests.cpp`, `engine/tests/unit/ordering_tests.cpp` y `engine/tests/unit/search_tests.cpp`->unidades continuacion (cero/bonus/malus/gravedad/clamp/independencia pieza/alias/pawn-identity/reset), countermove puntuada (`0->positivo->revocado`), refinamiento killer (orden/duplicado/eviccion/rechazo captura/EP/promocion), y `test_absurd_depth_bound` (`1000->max_ply` en `alpha_beta/iterative/search` mate con posicion intacta).

## licencia

mit (fathom syzygy se distribuye bajo licencia mit, ver `engine/third_party/fathom`)

note: puede que te estes preguntando por que se llama russell, basicamente el motor fue nombrado asi por la vibora de russell :p