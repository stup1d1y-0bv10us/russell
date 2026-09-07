#include "datagen/selfplay.hpp"
#include "search/search.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

struct args {
  std::string path;
  std::uint64_t games = 100;
  std::uint64_t seed = 0;
  int depth = 6;
  int verified_depth = 0;
  int max_plies = 100;
  int opening_plies = 4;
  int diversity_gap = 2;
  int diversity_threshold = 8;
  int diversity_min_non_pawn = 0;
};

auto parse_args(int argc, char** argv) -> args
{
  auto out = args{};
  std::vector<std::string> positional;
  for(int i=1;i<argc;++i){
    std::string a=argv[i];
    if(a.rfind("--verified-depth=",0)==0) out.verified_depth=std::atoi(a.substr(17).c_str());
    else if(a=="--verified-depth" && i+1<argc) out.verified_depth=std::atoi(argv[++i]);
    else if(a.rfind("--score-depth=",0)==0) out.verified_depth=std::atoi(a.substr(14).c_str());
    else if(a.rfind("--diversity-gap=",0)==0) out.diversity_gap=std::atoi(a.substr(16).c_str());
    else if(a=="--diversity-gap" && i+1<argc) out.diversity_gap=std::atoi(argv[++i]);
    else if(a.rfind("--diversity-threshold=",0)==0) out.diversity_threshold=std::atoi(a.substr(22).c_str());
    else if(a=="--diversity-threshold" && i+1<argc) out.diversity_threshold=std::atoi(argv[++i]);
    else if(a.rfind("--diversity-min-non-pawn=",0)==0) out.diversity_min_non_pawn=std::atoi(a.substr(24).c_str());
    else if(a=="--diversity-min-non-pawn" && i+1<argc) out.diversity_min_non_pawn=std::atoi(argv[++i]);
    else if(a.rfind("--",0)==0){ std::fprintf(stderr,"unknown flag %s\n",a.c_str()); std::exit(2); }
    else positional.push_back(a);
  }
  if(positional.size()<1){
    std::fprintf(stderr,"usage: tuna_datagen <outfile> [games] [seed] [depth] [max_plies] [opening_plies] [--verified-depth N] [--diversity-gap N] [--diversity-threshold N] [--diversity-min-non-pawn N]\n");
    std::exit(2);
  }
  out.path = positional[0];
  if(positional.size()>=2) out.games = std::strtoull(positional[1].c_str(), nullptr, 0);
  if(positional.size()>=3) out.seed = std::strtoull(positional[2].c_str(), nullptr, 0);
  if(positional.size()>=4) out.depth = std::atoi(positional[3].c_str());
  if(positional.size()>=5) out.max_plies = std::atoi(positional[4].c_str());
  if(positional.size()>=6) out.opening_plies = std::atoi(positional[5].c_str());
  return out;
}

}

auto main(int argc, char** argv) -> int
{
  const auto a = parse_args(argc, argv);
  if(a.depth < 1 || a.depth > 16) {
    std::fprintf(stderr, "depth must be in 1..16\n");
    return 2;
  }

  auto opts = tuna::datagen::selfplay_options{};
  opts.seed = a.seed;
  opts.search_depth = a.depth;
  opts.score_depth = a.verified_depth;
  opts.max_plies = a.max_plies;
  opts.opening_plies = a.opening_plies;
  opts.diversity_min_ply_gap = a.diversity_gap;
  opts.diversity_eval_threshold = a.diversity_threshold;
  opts.diversity_min_non_pawn = a.diversity_min_non_pawn;

  const auto written = tuna::datagen::generate_selfplay(opts, a.path, a.games);
  if(written == 0) {
    std::fprintf(stderr, "failed to write dataset to %s\n", a.path.c_str());
    return 1;
  }
  std::printf("wrote %llu packed positions (%llu games, depth %d, seed 0x%llx) to %s\n",
              static_cast<unsigned long long>(written),
              static_cast<unsigned long long>(a.games),
              a.depth,
              static_cast<unsigned long long>(a.seed),
              a.path.c_str());
  return 0;
}