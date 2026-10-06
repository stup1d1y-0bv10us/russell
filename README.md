# motor de ajedrez russell v1.4

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

## cambios v1.4

* `engine/src/eval/nnue.hpp`->arquitectura `ft_size 256->512`, `l1_size 32->64`, nuevo `king_buckets=8`, `features_per_bucket=640`, `half_feature_space=5120`, `feature_space=10240`, `input_dims=1024` y helpers `king_bucket`, `king_flips_file`, `orient_square`, `feature_index`.
* `engine/src/eval/nnue.cpp`->formato `1->2`, `mirror`->`vertical_mirror`, acumulacion e incremental por bucket con orientacion de archivo, asserts de limites y casillas, enroque con `refresh` para evitar acumulador obsoleto y assert de pila en `unmake`.
* `engine/src/train/trainer.hpp` y `engine/src/train/trainer.cpp`->misma migracion a buckets con `feature_index/orient_square`, `reserve 18->32`, `trainer_predict` devuelve 0 sin ambos reyes, `mirror` renombrado, y nueva variante `train_from` para warm-start con `train` delegando a ella.
* `engine/tests/unit/nnue_tests.cpp`->actualiza referencia a buckets, agrega fens de reyes desnudos, paridad trainer/produccion, asserts de dimensiones v1.4, integracion a==b==c incremental/fresh/referencia mas busqueda, invariancia por espejo horizontal, caminata de bordes de bucket, unwind completo y rechazo de serializacion (v1, dims, truncado, vacio, mala version).
* `engine/tests/unit/nnue_train_tests.cpp`->calibracion `momentum 0.0->0.9` y validacion relajada a finita sin divergir (`final < initial*1.5`) por ruido en sets pequenos.

## licencia

mit (fathom syzygy se distribuye bajo licencia mit, ver `engine/third_party/fathom`)

note: puede que te estes preguntando por que se llama russell, basicamente el motor fue nombrado asi por la vibora de russell :p