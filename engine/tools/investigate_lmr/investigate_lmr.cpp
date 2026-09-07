#include "core/position.hpp"
#include "search/search.hpp"
#include <iostream>
#include <vector>
#include <string>

int main(){
  auto cases = std::vector<std::pair<std::string,int>>{
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4},
  };
  for(auto [fen,depth]: cases){
    auto p1 = tuna::position::from_fen(fen);
    auto p2 = tuna::position::from_fen(fen);
    auto plain = tuna::search::minimax(p1, depth);
    auto ab = tuna::search::alpha_beta(p2, depth, true, true, false, true, false);
    std::cout<<"FEN "<<fen<<" depth "<<depth<<"\n";
    std::cout<<" minimax score "<<plain.score<<" move "<<(int)plain.best_move.from<<","<<(int)plain.best_move.to<<" has "<<plain.has_move<<" nodes "<<plain.nodes<<"\n";
    std::cout<<" ab      score "<<ab.score<<" move "<<(int)ab.best_move.from<<","<<(int)ab.best_move.to<<" has "<<ab.has_move<<" nodes "<<ab.nodes<<"\n";
    std::cout<<" score diff "<<(ab.score-plain.score)<<" move equal "<<(ab.best_move==plain.best_move)<<"\n";

    if(ab.has_move && plain.has_move && !(ab.best_move==plain.best_move)){
      auto pos_after_ab = tuna::position::from_fen(fen);
      auto st = pos_after_ab.make_move(ab.best_move);
      auto pos_after_plain = tuna::position::from_fen(fen);
      auto st2 = pos_after_plain.make_move(plain.best_move);

      auto child_ab = tuna::position::from_fen(fen);
      child_ab.make_move(ab.best_move);
      auto child_plain = tuna::position::from_fen(fen);
      child_plain.make_move(plain.best_move);

      auto c1 = tuna::search::minimax(child_ab, depth-1);
      auto c2 = tuna::search::minimax(child_plain, depth-1);
      std::cout<<"  child minimax after ab move score "<<-c1.score<<" after plain move score "<<-c2.score<<"\n";
    }
    std::cout<<"---\n";
  }

  {
    auto fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    int depth=4;
    auto p = tuna::position::from_fen(fen);
    auto plain = tuna::search::minimax(p, depth);
    auto p2 = tuna::position::from_fen(fen);

    auto p3 = tuna::position::from_fen(fen);
    auto res = tuna::search::alpha_beta(p3, depth, true, true, false, true, false);
    std::cout<<"Ordering test: plain best "<<(int)plain.best_move.from<<","<<(int)plain.best_move.to<<" ab best "<<(int)res.best_move.from<<","<<(int)res.best_move.to<<"\n";
  }
  return 0;
}