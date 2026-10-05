# motor de ajedrez russell v1.3

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

## cambios v1.3

* `engine/src/search/ordering.cpp`->agrega constantes `see_bonus_winning=500`, `see_bonus_good=2000`, `see_demote_losing=-120000`, `see_demote_bad=-121500`, degrada fuertemente capturas perdedoras en el ordenamiento y limita el historial a +/-16384.
* `engine/src/search/search.hpp`->agrega `rfp_linear=120`, `history_prune_base=1024`, `see_prune_margin=100`, `tm_stable_factor=1.25`, `tm_aspire_factor=1.50`, `tm_panic_factor=2.00`, `tm_no_hard_cap_factor=2.00`, `tm_streak_min=2`, `tm_panic_drop=150`, `tm_hist_size=4`, `se_min_depth=6`, `se_tt_margin=3`, `recap_max_depth=8` y nueva sobrecarga `lmr_reduction` con see/pv/improving.
* `engine/src/search/search.cpp`->nueva sobrecarga `nmp_reduction` sensible a improving (r-1 si improving), rfp simplificado a `120*depth` sin ajuste por historial, `static_eval/improving` calculado antes del nmp, extension de jaque solo tactica con flag `node_extended`, singular extension con verificacion a `depth/2` y `se_beta=tablescore-depth`, recapture extension `+1` si recaptura mismo destino con `see>=0` y `depth<=8`, lmr no reduce primera jugada con ajuste pv-1/improving-1/captura buena-1/captura mala+1 y see perezoso, podas see (`100*depth`) e history (`-1024*depth`) escaladas por profundidad con cache de see, soporte `excluded_move` sin guardar tt en nodos excluidos/mate, y gestion de tiempo multi-factor (stable/unstable/aspire/panic con historial de 4 plies y tracking de turbulencia de aspiracion).
* `engine/src/search/transposition_table.cpp`->reemplazo simplificado a mas viejo primero con desempate por menor profundidad, sin bonus por bound exact ni score `age*32-depth`.
* `engine/src/uci/uci.cpp`->agrega `hash_mb_to_entries` y `default_hash_mb=16`, pre-dimensiona la tt en ambos constructores, usa el helper y hace `clear` en `setoption Hash`

## licencia

mit (fathom syzygy se distribuye bajo licencia mit, ver `engine/third_party/fathom`)

note: puede que te estes preguntando por que se llama russell, basicamente el motor fue nombrado asi por la vibora de russell :p