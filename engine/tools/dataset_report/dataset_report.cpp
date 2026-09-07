#include "datagen/dataset.hpp"
#include "movegen/movegen.hpp"
#include "tb/tablebase.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace tuna;
using namespace tuna::datagen;

static auto material_key(const position& pos) -> std::string {
  auto cnt=[&](color c, piece_type pt){ return std::popcount(pos.pieces(c,pt)); };
  char buf[128];
  std::snprintf(buf,sizeof(buf), "W:%d/%d/%d/%d/%d B:%d/%d/%d/%d/%d",
    (int)cnt(color::white,piece_type::pawn),(int)cnt(color::white,piece_type::knight),(int)cnt(color::white,piece_type::bishop),(int)cnt(color::white,piece_type::rook),(int)cnt(color::white,piece_type::queen),
    (int)cnt(color::black,piece_type::pawn),(int)cnt(color::black,piece_type::knight),(int)cnt(color::black,piece_type::bishop),(int)cnt(color::black,piece_type::rook),(int)cnt(color::black,piece_type::queen));
  return std::string(buf);
}

static auto non_pawn_material(const position& pos) -> int {
  auto c=[&](piece_type pt,int v){ auto bb=pos.pieces(color::white,pt)|pos.pieces(color::black,pt); return (int)std::popcount(bb)*v; };
  return c(piece_type::knight,3)+c(piece_type::bishop,3)+c(piece_type::rook,5)+c(piece_type::queen,9);
}

int main(int argc, char** argv){
  if(argc<2){
    std::fprintf(stderr,"usage: tuna_dataset_report <dataset.bin> [validate_fraction=0.1]\n");
    return 2;
  }
  std::string path=argv[1];
  double vf=0.1;
  if(argc>=3) vf=std::strtod(argv[2],nullptr);
  dataset_reader reader(path);
  if(!reader.good()){
    std::fprintf(stderr,"failed to read %s\n",path.c_str());
    return 1;
  }
  auto hdr=reader.header();
  std::vector<dataset_record> records;
  reader.records(records);
  std::printf("=== Dataset Quality Report ===\n");
  std::printf("file: %s\n",path.c_str());
  std::printf("header: magic 0x%08x version %u seed 0x%llx games %llu positions %llu search_depth %d score_depth %d sample_interval %d max_plies %d\n",
    hdr.magic, hdr.version,(unsigned long long)hdr.seed,(unsigned long long)hdr.game_count,(unsigned long long)hdr.position_count,hdr.search_depth,hdr.score_depth,hdr.sample_interval,hdr.max_plies);
  std::printf("record count: %zu (header %llu)\n",records.size(),(unsigned long long)hdr.position_count);
  std::printf("binary format: header 64 bytes, record 48 bytes, total %zu bytes\n", (size_t)(64+records.size()*48));

  std::unordered_set<uint64_t> keys;
  keys.reserve(records.size()*2);
  std::unordered_map<uint64_t,int> key_counts;
  for(auto &r: records){
    auto pos=unpack_position(r);
    uint64_t k=pos.key();
    keys.insert(k);
    key_counts[k]++;
  }
  size_t unique=keys.size();
  double dup_rate = records.empty()?0.0:100.0*(records.size()-unique)/records.size();
  std::printf("unique positions: %zu / %zu (duplicate rate %.2f%%)\n",unique,records.size(),dup_rate);
  size_t dup_keys=0;
  for(auto &kv:key_counts) if(kv.second>1) dup_keys++;
  std::printf("duplicate keys (positions appearing >1): %zu\n",dup_keys);

  std::map<std::string,int> mat_dist;
  std::map<int,int> npm_hist;
  std::map<int,int> piece_count_hist;
  for(auto &r: records){
    auto pos=unpack_position(r);
    std::string mk=material_key(pos);
    mat_dist[mk]++;
    int npm=non_pawn_material(pos);
    npm_hist[npm]++;
    int pc=(int)std::popcount(pos.occupancy());
    piece_count_hist[pc]++;
  }
  std::printf("material distribution (top 10):\n");
  std::vector<std::pair<std::string,int>> mvec(mat_dist.begin(), mat_dist.end());
  std::sort(mvec.begin(), mvec.end(), [](auto &a, auto &b){return a.second>b.second;});
  for(size_t i=0;i<std::min<size_t>(10,mvec.size());++i) std::printf("  %s : %d (%.1f%%)\n",mvec[i].first.c_str(),mvec[i].second,100.0*mvec[i].second/records.size());
  std::printf("distinct material configs: %zu\n",mat_dist.size());
  std::printf("non-pawn material histogram:\n");
  for(auto &kv: npm_hist) std::printf("  npm=%d : %d\n",kv.first,kv.second);
  std::printf("piece-count histogram:\n");
  for(auto &kv: piece_count_hist) std::printf("  pieces=%d : %d\n",kv.first,kv.second);

  if(!records.empty()){
    int min_s=records[0].score, max_s=records[0].score;
    double sum=0, sumsq=0;
    std::map<int,int> score_bins;
    int cnt_gt800=0, cnt_mate=0, cnt_tb=0;
    for(auto &r: records){
      int s=r.score;
      if(s<min_s) min_s=s;
      if(s>max_s) max_s=s;
      sum+=s; sumsq+= (double)s*s;
      int b = (s/100)*100;
      score_bins[b]++;
      if(std::abs(s)>800) cnt_gt800++;
      if(std::abs(s)>20000) cnt_mate++;
      auto pos=unpack_position(r);
      if(tb::largest()>0 && (int)std::popcount(pos.occupancy()) <= tb::largest()){
        tb::probe_result pr;
        if(tb::probe_root(pos,pr)) cnt_tb++;
      }
    }
    double mean=sum/records.size();
    double var=sumsq/records.size() - mean*mean;
    double stddev= var>0?std::sqrt(var):0;
    std::printf("score distribution: min %d max %d mean %.1f std %.1f\n",min_s,max_s,mean,stddev);
    std::printf("  |score|>800 : %d / %zu (%.2f%%)\n",cnt_gt800,records.size(),100.0*cnt_gt800/records.size());
    std::printf("  mate scores |s|>20000 : %d\n",cnt_mate);
    std::printf("  tablebase hits (if TB loaded): %d\n",cnt_tb);
    std::printf("score histogram (bin 100cp):\n");
    for(auto &kv: score_bins) std::printf("  [%d,%d): %d\n",kv.first,kv.first+99,kv.second);
  }

  size_t wtm=0, btm=0;
  for(auto &r: records) (r.stm&1?btm:wtm)++;
  std::printf("side-to-move: white %zu (%.1f%%) black %zu (%.1f%%)\n",wtm,records.empty()?0:100.0*wtm/records.size(),btm,records.empty()?0:100.0*btm/records.size());

  std::map<int,int> ply_hist;
  std::map<int,int> full_hist;
  std::map<int,int> half_hist;
  for(auto &r: records){
    int ply = (r.fullmove_number-1)*2 + (r.stm&1);
    int fm=r.fullmove_number;
    int hm=r.halfmove_clock;
    ply_hist[ply]++;
    full_hist[fm]++;
    half_hist[hm]++;
  }
  std::printf("ply distribution (first 15):\n");
  int c=0;
  for(auto &kv: ply_hist){ if(c++>=15) break; std::printf("  ply %d : %d\n",kv.first,kv.second);}
  std::printf("fullmove distribution (first 15):\n");
  c=0; for(auto &kv: full_hist){ if(c++>=15) break; std::printf("  fullmove %d : %d\n",kv.first,kv.second);}
  std::printf("halfmove clock histogram:\n");
  for(auto &kv: half_hist) std::printf("  halfmove %d : %d\n",kv.first,kv.second);

  size_t rep_dup= records.size()-unique;
  std::printf("exclusions check (should be 0 in clean dataset): duplicate rate %.2f%%, mate 0? tb 0?\n",dup_rate);

  size_t vcount = (size_t)(records.size()*vf);
  size_t split = records.size() - vcount;
  std::unordered_set<uint64_t> train_keys, val_keys;
  for(size_t i=0;i<split;++i){ auto pos=unpack_position(records[i]); train_keys.insert(pos.key());}
  for(size_t i=split;i<records.size();++i){ auto pos=unpack_position(records[i]); val_keys.insert(pos.key());}
  size_t overlap=0;
  for(auto k: val_keys) if(train_keys.find(k)!=train_keys.end()) overlap++;
  double overlap_pct = val_keys.empty()?0:100.0*overlap/val_keys.size();
  std::printf("train/validation split (vf=%.2f): train %zu val %zu overlap %zu (%.2f%% of val)\n",vf, train_keys.size(), val_keys.size(), overlap, overlap_pct);
  if(overlap>0) std::printf("  NOTE: overlap indicates leakage (same position in both splits)\n");

  std::printf("\n=== Audit Findings ===\n");
  bool small = records.size() < 5000;
  if(small) std::printf("- BOTTLENECK: dataset too small (%zu < 5000) -> high variance, overfitting, poor generalization\n",records.size());
  else std::printf("- OK: dataset size %zu\n",records.size());
  if(dup_rate > 5.0) std::printf("- BOTTLENECK: high duplicate rate %.2f%% -> redundant samples, wasted capacity\n",dup_rate);
  else std::printf("- OK: duplicate rate %.2f%%\n",dup_rate);
  if(mat_dist.size() < 50) std::printf("- BOTTLENECK: low material diversity (%zu configs) -> poor coverage\n",mat_dist.size());
  else std::printf("- OK: material diversity %zu\n",mat_dist.size());
  double wtm_pct = records.empty()?0:100.0*wtm/records.size();
  if(wtm_pct < 40 || wtm_pct > 60) std::printf("- WARNING: side-to-move imbalance %.1f%% white\n",wtm_pct);
  else std::printf("- OK: side balance %.1f%% white\n",wtm_pct);
  if(overlap_pct > 1.0) std::printf("- BOTTLENECK: train/val overlap %.2f%% -> leakage\n",overlap_pct);
  else std::printf("- OK: train/val overlap %.2f%%\n",overlap_pct);
  std::printf("- NOTE: augmentation will ~double samples but black-to-move twins coincide and are skipped (redundancy, est %zu -> %zu with augment)\n", records.size(), records.size()*2);

  if(hdr.search_depth <=3) std::printf("- BOTTLENECK: shallow search_depth %d -> noisy verified scores (depth 2-3 insufficient)\n",hdr.search_depth);
  else std::printf("- OK: search_depth %d\n",hdr.search_depth);

  std::printf("- NOTE: diversity filter gap %d threshold %d min_non_pawn %d may remove useful midgame positions if too aggressive\n", 2,10,4);

  std::printf("- NOTE: tail split (last %.0f%%) may not be representative; shuffling or stratified split would be more representative\n",vf*100);
  std::printf("=== End Report ===\n");
  return 0;
}