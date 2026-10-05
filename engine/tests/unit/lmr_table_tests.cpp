#include "search/search.hpp"
#include <cstdlib>
#include <iostream>

namespace {
auto require(bool v, const char* msg) -> void {
  if(!v){ std::cerr<<msg<<"\n"; std::exit(1);}
}
}

int main(){
  using namespace russell::search;

  for(int d=3; d<32; ++d){
    for(int m=6; m<32; ++m){
      int r1 = lmr_base_reduction(d,m);
      int r2 = lmr_base_reduction(d+1,m);
      int r3 = lmr_base_reduction(d,m+1);
      require(r2 >= r1, "reduction non-decreasing with depth");
      require(r3 >= r1, "reduction non-decreasing with move_count");
      require(r1 >=0 && r1 <= lmr_max_reduction, "reduction bounds");
    }
  }

  {
    int r_neg = lmr_reduction(8, 10, false, -4096);
    int r_zero = lmr_reduction(8, 10, false, 0);
    int r_pos = lmr_reduction(8, 10, false, 4096);
    require(r_pos <= r_zero, "positive history reduces less");
    require(r_neg >= r_zero, "negative history reduces more");
  }

  {
    int r_big_pos = lmr_reduction(10, 10, false, 100000);
    int r_big_neg = lmr_reduction(10, 10, false, -100000);
    require(r_big_pos >=0 && r_big_pos <= lmr_max_reduction, "clamped positive history");
    require(r_big_neg >=0 && r_big_neg <= lmr_max_reduction, "clamped negative history");

    require(r_big_neg - r_big_pos <= 2*lmr_history_max_adjust+1, "history clamp limits range");
  }

  {
    int r_quiet = lmr_reduction(8, 10, false, 0);
    int r_cap = lmr_reduction(8, 10, true, 0);
    require(r_cap <= r_quiet, "capture reduction <= quiet");
    require(r_cap >=0, "capture reduction non-negative");
  }

  require(lmr_reduction(-5,0) >=0, "clamp negative depth");
  require(lmr_reduction(200,0) <= lmr_max_reduction, "clamp large depth");
  require(lmr_base_reduction(1,1) >=0, "base 1,1 bounds");
  require(lmr_base_reduction(64,64) <= lmr_max_reduction, "base max bounds");
  require(lmr_base_reduction(100,100) == lmr_base_reduction(63,63), "clamp large depth/move");

  {
    int r = lmr_reduction(6, 0);
    require(r >=0 && r <= lmr_max_reduction, "2-arg wrapper bounds");
  }

  std::cout<<"lmr table new checks passed\n";
  return 0;
}
