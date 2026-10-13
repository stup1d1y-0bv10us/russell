# motor de ajedrez russell v1.6

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

## cambios v1.6

* `engine/CMakeLists.txt`->nuevo target `russell_improving_tests` con flags `/W4` y `-Wall -Wextra -Wpedantic`, registro en `russell_all_targets` y `add_test` mas `russell_improving_tests`.
* `engine/src/search/ordering.hpp` y `engine/src/search/ordering.cpp`->nuevo `is_countermove(prev,cur)` que compara `counter_[prev.from][prev.to]==cur` con guarda `no_move`, sin tocar almacenamiento ni puntuacion.
* `engine/src/search/search.hpp` y `engine/src/search/search.cpp`->expone `nmp_reduction` (3 y 5 args) y `rfp_margin` fuera del anonimo, NMP 2.0 con tiers `margin<64->R-1` y `margin>128->R+1` solo si `depth>3` mas `npm<=6->R-1`, `rfp_margin` con ajuste `history/4096` saturado `+/-20`, RFP/razor consultan historial del bando previo (`opposite(us)`), futility desacoplada de quiescencia (`use_futility && depth==1`), raiz siembra `static_eval_stack[0]`, LMR con exencion countermove (`return 0`) y `+1` si `!captura && depth>=5 && history<-4096`, quita `idx>0` y clamp muerto `depth>0 && depth<1`, cachea `nmp_material`.
* `engine/tests/unit/countermove_tests.cpp`, `engine/tests/unit/lmr_table_tests.cpp`, `engine/tests/unit/nullmove_tests.cpp`, `engine/tests/unit/rfp_tests.cpp`, `engine/tests/unit/search_tests.cpp` y nuevo `engine/tests/unit/improving_tests.cpp`->`is_countermove` tras `update_counter`, LMR 2.0 exencion y `+1` con bordes `-4096`/`depth 4/5` y capturas intactas, NMP tiers/bordes/clamps/material bajo/gates jaque/poca profundidad/verificacion, RFP `360+hist/4096` saturacion `+/-20` e `improving+history`, futility desacoplada con y sin quiescencia y escape con jaque-mate, e `improving_tests` con `improving_count>0` en `4k3/8/8/8/8/8/4P3/4K3` y conteos pinnados `rfp_pruned==3416` y `razor_pruned==1022` en kiwipete depth 5.

## licencia

mit (fathom syzygy se distribuye bajo licencia mit, ver `engine/third_party/fathom`)

note: puede que te estes preguntando por que se llama russell, basicamente el motor fue nombrado asi por la vibora de russell :p