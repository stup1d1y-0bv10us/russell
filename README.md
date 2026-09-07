# motor de ajedrez tuna v1.1

autor: dominic fuentes

un motor de ajedrez uci escrito en c++20.

## compilar

cmake --preset release
cmake --build build/release --config release

## ejecutar

./build/release/engine/release/tuna.exe

## uci

uci
isready
position startpos
go depth 15

## cambios v1.1

* `engine/src/datagen/selfplay.cpp`->elimina rama vacia relacionada con el muestreo de posiciones durante la grabacion.
* `engine/src/search/search.cpp`->corrige bucle de profundizacion iterativa para evitar comprobaciones innecesarias cuando no existe movimiento valido.
* `engine/src/search/ordering.cpp`->renombra `idx` por `cont_idx` en la historia de continuacion para mejorar la claridad del codigo.
* `engine/src/eval/evaluate.cpp`->reorganiza includes y elimina dependencias no utilizadas.
* `engine/src/train/trainer.cpp`->elimina cabeceras sin uso para reducir advertencias de compilacion.

## licencia

mit (fathom syzygy se distribuye bajo licencia mit, ver `engine/third_party/fathom`)
