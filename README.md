# motor de ajedrez russell v1.5

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

## cambios v1.5

* `engine/src/movegen/movegen.hpp` y `engine/src/movegen/movegen.cpp`->nuevo `generate_quiet_moves` que filtra `capture/en_passant/promotion_capture` con `castling_legal` y prueba `make/unmake` con `is_square_attacked`, nuevo `is_pseudo_legal` por igualdad en `generate_pseudo_legal` y nuevo `is_legal` con `castling_legal` y prueba `make/unmake` de exposicion de rey.
* `engine/src/search/search.cpp`->negamax por etapas `tt move validado con is_legal->capturas/promociones->quiet moves->perdedoras SEE<0 diferidas`, generacion perezosa con `generate_captures_promotions/generate_quiet_moves`, `has_searchable` y `have_cap/quiet_moves`, refactor del bucle a `search_stage_move/run_stage` con `move_idx` para LMP/LMR/PVS/futility/SEE/history/recaptura (`continue->return 0`, `i->idx`, retornos `0/1/2`), mate solo si `cap_moves` y `quiet_moves` vacias y `return -inf` si solo queda `excluded_move`.
* `engine/tests/unit/movegen_tests.cpp`->helper `make_mv` y 13 tests nuevos: `is_legal/is_pseudo_legal` basico, auto-jaque por clavada, mal movimiento, pieza rival, enroque legal/atacado, en passant legal/clavada de fila, promocion tranquila/subpromocion/captura, quietas en inicio (`20`), exclusion de capturas, promociones tranquilas y enroque en quietas, exclusion de ilegales/bloqueadas, paridad quiet/captura/legal en 10 fens (`quiets+caps==all+quiet_promotions`) y preservacion de posicion/key/fen tras las nuevas apis.

## licencia

mit (fathom syzygy se distribuye bajo licencia mit, ver `engine/third_party/fathom`)

note: puede que te estes preguntando por que se llama russell, basicamente el motor fue nombrado asi por la vibora de russell :p