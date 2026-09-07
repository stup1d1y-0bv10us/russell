#include "search/search.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
auto require(bool v, const char* msg) -> void {
  if(!v){ std::cerr<<msg<<"\n"; std::exit(1);}
}
}

int main(){
  auto old_formula = [](int depth, int history)->int{
    auto r = 1 + static_cast<int>(std::log(static_cast<double>(std::max(1,depth))) / 2.0);
    auto bonus = std::max(0, history) / 1024;
    r -= std::min(bonus, r);
    if(r < 1) r = 1;
    return r;
  };
  const int depths[] = {1,2,3,4,5,6,8,10,12,16,20,30,64,100,128};
  const int histories[] = {-100,0,512,1024,2047,2048,4096,8192,16384};
  for(int d: depths){
    for(int h: histories){
      int expected = old_formula(d,h);
      int actual = tuna::search::lmr_reduction(d,h);
      if(expected != actual){
        std::cerr<<"mismatch depth "<<d<<" history "<<h<<" expected "<<expected<<" actual "<<actual<<"\n";
        return 1;
      }
    }
  }

  require(tuna::search::lmr_reduction(-5,0)==old_formula(-5,0),"clamp negative depth");
  require(tuna::search::lmr_reduction(200,0)==old_formula(128,0),"clamp large depth");
  std::cout<<"lmr table matches formula\n";
  return 0;
}