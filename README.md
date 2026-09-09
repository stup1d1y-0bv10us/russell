# motor de ajedrez russell v1.2 (formerly tuna)

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

## cambios v1.2

* `engine/src/search/ordering.cpp` y `engine/src/search/ordering.hpp`->agrega historial de capturas y nuevos contadores see para mejorar el ordenamiento de movimientos y promociones.
* `engine/src/search/search.hpp` y `engine/src/search/search.cpp`->ajusta podas, reducciones y gestion de tiempo, incluyendo mejoras en null move, lmp, rfp y lmr.
* `engine/src/search/search.cpp`->mejora la ventana de aspiracion con reintentos automaticos y estadisticas de fallos altos y bajos.
* `engine/src/search/search.cpp` y `search.cpp`->introduce control de tiempo adaptativo y soporte adicional para busqueda paralela.
* `engine/src/search/transposition_table.cpp` y `engine/src/search/transposition_table.hpp`->reorganiza la tabla de transposicion usando grupos de entradas y seguimiento por generacion.
* `engine/src/uci/uci.cpp`->recalcula presupuestos de tiempo, mejora el manejo de limites de nodos y actualiza la version mostrada a `russell v1.2`.
* `engine/tests/unit/uci_tests.cpp` y `engine/tests/unit/lmr_table_tests.cpp`->actualiza pruebas relacionadas con la version del motor y las reducciones lmr.

## licencia

mit (fathom syzygy se distribuye bajo licencia mit, ver `engine/third_party/fathom`)

note: puede que te estes preguntando por que se llama russell, basicamente el motor fue nombrado asi por la vibora de russell :p
