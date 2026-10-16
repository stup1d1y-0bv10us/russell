# motor de ajedrez russell v1.7

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

## cambios v1.7

* `engine/src/search/search.hpp` y `engine/src/search/search.cpp`->nuevo `singular_gate` con salvaguardas v1.6 y verificacion `se_beta=value-depth` en `verify_depth=depth/2` con contadores `singular_considered/extensions`, nuevo ProbCut `probcut_min_depth=5/reduction=4/margin=200` con `probcut_gate/probcut_see_ok` solo capturas/promociones `SEE>=0` y corte `value>=beta+margin` con flag `in_probcut`, nueva sonda `threat_gate/detect_immediate_threat` (`SEE>=100` o jaque inmediato, sin NNUE) con `threat_now` que bloquea NMP/RFP/LMP y contadores `threat_considered/detected`, mas `include transposition_table.hpp` y plomeria de contadores en `root/iterative/emergency`.
* `engine/tests/unit/search_tests.cpp`->~50 tests nuevos: `singular_gate` (tt move, exclusion, check/extended, mate, profundidad TT, upper bound) y contadores/invariantes/determinismo, ProbCut gate/SEE/lista solo capturas/idle depth 5/intento kiwipete depth 6 (`7/0`) y corte depth 7 (`330/57`)/aborto/preservacion/determinismo, amenaza gate/bloqueos/detector (tranquila, colgada, perdedora, promocion con/sin jaque, captura con jaque, clavada) y preservacion key/eval/determinismo, mas bloqueos NMP/RFP/LMP por amenaza, recap, raiz sin extension, depth 8, verificaciones conjuntas e invariantes `cutoff<=considered`/`extensions<=nodes`.

## licencia

mit (fathom syzygy se distribuye bajo licencia mit, ver `engine/third_party/fathom`)

note: puede que te estes preguntando por que se llama russell, basicamente el motor fue nombrado asi por la vibora de russell :p