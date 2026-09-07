#include "eval/nnue.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

auto main(int argc, char** argv) -> int
{
  if(argc < 2) {
    std::cerr << "usage: tuna_nnue_gen <outfile> [seed]\n";
    return 1;
  }
  const auto path = std::string{argv[1]};
  auto seed = std::uint64_t{0x9E3779B97F4A7C15ULL};
  if(argc >= 3) {
    seed = std::strtoull(argv[2], nullptr, 0);
  }
  auto net = tuna::eval::nnue::network::make_random(seed);
  if(!net.save(path)) {
    std::cerr << "failed to write " << path << '\n';
    return 1;
  }
  std::cout << "wrote placeholder halfKP network (" << net.feature_space_size() << " features, "
            << tuna::eval::nnue::ft_size << " -> " << tuna::eval::nnue::l1_size << " -> "
            << tuna::eval::nnue::l2_size << " -> 1, int16 quantized) to " << path << '\n';
  return 0;
}