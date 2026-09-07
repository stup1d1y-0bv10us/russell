#include "eval/nnue.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

namespace tuna::eval::nnue {

namespace {

constexpr auto king_stride = 640;
constexpr auto square_stride = 64;
constexpr auto mirror = 56;

constexpr auto l1_inputs = input_dims;
constexpr auto l2_inputs = l1_size;

constexpr auto magic = std::to_array<char>({'T', 'U', 'N', 'N', 'U', '1'});
constexpr auto format_version = std::uint32_t{1};

auto write_u32(std::ostream& out, std::uint32_t v) -> void
{
  out.put(static_cast<char>(v & 0xFF));
  out.put(static_cast<char>((v >> 8) & 0xFF));
  out.put(static_cast<char>((v >> 16) & 0xFF));
  out.put(static_cast<char>((v >> 24) & 0xFF));
}

auto read_u32(std::istream& in) -> std::uint32_t
{
  auto bytes = std::array<unsigned char, 4>{};
  in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return bytes[0] | (std::uint32_t{bytes[1]} << 8) | (std::uint32_t{bytes[2]} << 16)
         | (std::uint32_t{bytes[3]} << 24);
}

auto make_piece(color c, piece_type pt) noexcept -> piece
{
  const auto offset = c == color::white ? 1 : 7;
  return static_cast<piece>(offset + piece_type_index(pt));
}

auto color_of(piece p) noexcept -> color
{
  return p >= piece::bp ? color::black : color::white;
}

auto mirrored(const position& pos) -> position
{
  auto out = position::empty();
  for(auto c = 0; c < color_count; ++c) {
    for(auto pt = 0; pt < piece_type_count; ++pt) {
      auto bb = pos.pieces(static_cast<color>(c), static_cast<piece_type>(pt));
      while(bb != 0) {
        const auto sq = static_cast<int>(std::countr_zero(bb));
        bb &= bb - 1;
        out.set_piece(opposite(static_cast<color>(c)),
                      static_cast<piece_type>(pt), sq ^ mirror);
      }
    }
  }
  out.set_side_to_move(opposite(pos.side_to_move()));
  return out;
}

auto clamp_quantized(int value) noexcept -> int
{
  return std::clamp(value, 0, 127);
}

auto apply_column(std::array<std::int16_t, ft_size>& acc, const std::int8_t* column,
                  int sign) noexcept -> void
{
  if(sign > 0) {
    for(auto i = 0; i < ft_size; ++i) {
      acc[i] = static_cast<std::int16_t>(acc[i] + column[i]);
    }
  } else {
    for(auto i = 0; i < ft_size; ++i) {
      acc[i] = static_cast<std::int16_t>(acc[i] - column[i]);
    }
  }
}

auto king_square(const position& pos, color c) noexcept -> int
{
  const auto bb = pos.pieces(c, piece_type::king);
  if(bb == 0) {
    return no_square;
  }
  return static_cast<int>(std::countr_zero(bb));
}

}

auto network::load(const std::string& path) -> bool
{
  std::ifstream in(path, std::ios::binary);
  if(!in) {
    return false;
  }
  auto tag = std::array<char, magic.size()>{};
  in.read(tag.data(), static_cast<std::streamsize>(tag.size()));
  if(!in || tag != magic) {
    return false;
  }
  const auto version = read_u32(in);
  const auto ft = read_u32(in);
  const auto l1 = read_u32(in);
  const auto l2 = read_u32(in);
  const auto fspace = read_u32(in);
  const auto idims = read_u32(in);
  if(!in || version != format_version || ft != ft_size || l1 != l1_size || l2 != l2_size
     || fspace != feature_space || idims != input_dims) {
    return false;
  }

  auto w1 = std::vector<std::int8_t>(static_cast<std::size_t>(ft_size) * feature_space);
  auto bias1 = std::vector<std::int16_t>(static_cast<std::size_t>(ft_size));
  auto w2 = std::vector<std::int8_t>(static_cast<std::size_t>(l1_size) * input_dims);
  auto bias2 = std::vector<std::int16_t>(static_cast<std::size_t>(l1_size));
  auto w3 = std::vector<std::int8_t>(static_cast<std::size_t>(l2_size) * l1_size);
  auto bias3 = std::vector<std::int16_t>(static_cast<std::size_t>(l2_size));
  auto w4 = std::vector<std::int8_t>(static_cast<std::size_t>(l2_size));
  auto bias4 = std::vector<std::int16_t>(static_cast<std::size_t>(1));

  const auto read_raw = [&in](auto* data, std::size_t count) -> bool {
    if(count == 0) {
      return true;
    }
    in.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(count));
    return static_cast<bool>(in);
  };

  if(!read_raw(w1.data(), w1.size()) || !read_raw(bias1.data(), bias1.size() * 2)
     || !read_raw(w2.data(), w2.size()) || !read_raw(bias2.data(), bias2.size() * 2)
     || !read_raw(w3.data(), w3.size()) || !read_raw(bias3.data(), bias3.size() * 2)
     || !read_raw(w4.data(), w4.size()) || !read_raw(bias4.data(), bias4.size() * 2)) {
    return false;
  }

  w1_ = std::move(w1);
  bias1_ = std::move(bias1);
  w2_ = std::move(w2);
  bias2_ = std::move(bias2);
  w3_ = std::move(w3);
  bias3_ = std::move(bias3);
  w4_ = std::move(w4);
  bias4_ = std::move(bias4);
  return true;
}

auto network::save(const std::string& path) const -> bool
{
  if(empty()) {
    return false;
  }
  std::ofstream out(path, std::ios::binary);
  if(!out) {
    return false;
  }
  out.write(magic.data(), static_cast<std::streamsize>(magic.size()));
  write_u32(out, format_version);
  write_u32(out, static_cast<std::uint32_t>(ft_size));
  write_u32(out, static_cast<std::uint32_t>(l1_size));
  write_u32(out, static_cast<std::uint32_t>(l2_size));
  write_u32(out, static_cast<std::uint32_t>(feature_space));
  write_u32(out, static_cast<std::uint32_t>(input_dims));

  const auto write_raw = [&out](auto* data, std::size_t count) {
    if(count == 0) {
      return;
    }
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(count));
  };

  write_raw(const_cast<std::int8_t*>(w1_.data()), w1_.size());
  write_raw(const_cast<std::int16_t*>(bias1_.data()), bias1_.size() * 2);
  write_raw(const_cast<std::int8_t*>(w2_.data()), w2_.size());
  write_raw(const_cast<std::int16_t*>(bias2_.data()), bias2_.size() * 2);
  write_raw(const_cast<std::int8_t*>(w3_.data()), w3_.size());
  write_raw(const_cast<std::int16_t*>(bias3_.data()), bias3_.size() * 2);
  write_raw(const_cast<std::int8_t*>(w4_.data()), w4_.size());
  write_raw(const_cast<std::int16_t*>(bias4_.data()), bias4_.size() * 2);
  return static_cast<bool>(out);
}

auto network::make_random(std::uint64_t seed) -> network
{
  auto rng = seed | std::uint64_t{1};
  const auto next = [&rng]() -> std::uint64_t {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
  };

  auto net = network{};
  net.w1_.resize(static_cast<std::size_t>(ft_size) * feature_space);
  net.bias1_.resize(static_cast<std::size_t>(ft_size));
  net.w2_.resize(static_cast<std::size_t>(l1_size) * input_dims);
  net.bias2_.resize(static_cast<std::size_t>(l1_size));
  net.w3_.resize(static_cast<std::size_t>(l2_size) * l1_size);
  net.bias3_.resize(static_cast<std::size_t>(l2_size));
  net.w4_.resize(static_cast<std::size_t>(l2_size));
  net.bias4_.resize(static_cast<std::size_t>(1));

  for(auto& v : net.w1_) { v = static_cast<std::int8_t>(next() & 0xFF); }
  for(auto& v : net.bias1_) { v = static_cast<std::int16_t>(next() & 0xFFFF); }
  for(auto& v : net.w2_) { v = static_cast<std::int8_t>(next() & 0xFF); }
  for(auto& v : net.bias2_) { v = static_cast<std::int16_t>(next() & 0xFFFF); }
  for(auto& v : net.w3_) { v = static_cast<std::int8_t>(next() & 0xFF); }
  for(auto& v : net.bias3_) { v = static_cast<std::int16_t>(next() & 0xFFFF); }
  for(auto& v : net.w4_) { v = static_cast<std::int8_t>(next() & 0xFF); }
  for(auto& v : net.bias4_) { v = static_cast<std::int16_t>(next() & 0xFFFF); }
  return net;
}

auto forward(const network& net, const std::array<std::int16_t, ft_size>& own,
             const std::array<std::int16_t, ft_size>& enemy) -> int
{
  auto hidden0 = std::array<int, input_dims>{};
  for(auto i = 0; i < ft_size; ++i) {
    hidden0[i] = clamp_quantized(own[i]);
    hidden0[ft_size + i] = clamp_quantized(enemy[i]);
  }

  auto hidden1 = std::array<int, l1_size>{};
  for(auto j = 0; j < l1_size; ++j) {
    auto sum = static_cast<int>(net.bias2_[j]);
    for(auto k = 0; k < l1_inputs; ++k) {
      sum += net.w2_[static_cast<std::size_t>(j) * l1_inputs + k] * hidden0[k];
    }
    hidden1[j] = clamp_quantized(sum >> weight_scale_bits);
  }

  auto hidden2 = std::array<int, l2_size>{};
  for(auto j = 0; j < l2_size; ++j) {
    auto sum = static_cast<int>(net.bias3_[j]);
    for(auto k = 0; k < l2_inputs; ++k) {
      sum += net.w3_[static_cast<std::size_t>(j) * l2_inputs + k] * hidden1[k];
    }
    hidden2[j] = clamp_quantized(sum >> weight_scale_bits);
  }

  auto out = static_cast<int>(net.bias4_[0]);
  for(auto k = 0; k < l2_size; ++k) {
    out += net.w4_[k] * hidden2[k];
  }
  return out / output_scale;
}

auto reference_accumulate(const network& net, const position& pos,
                          std::array<std::int16_t, ft_size>& own,
                          std::array<std::int16_t, ft_size>& enemy) -> bool
{
  const auto wk = king_square(pos, color::white);
  const auto bk = king_square(pos, color::black);
  if(wk == no_square || bk == no_square) {
    return false;
  }
  own = {};
  enemy = {};
  for(auto i = 0; i < ft_size; ++i) {
    own[i] = net.bias1_[i];
    enemy[i] = net.bias1_[i];
  }

  for(auto c = 0; c < color_count; ++c) {
    for(auto pt = 0; pt < piece_type_count; ++pt) {
      const auto ptype = static_cast<piece_type>(pt);
      if(ptype == piece_type::king) {
        continue;
      }
      auto bb = pos.pieces(static_cast<color>(c), ptype);
      while(bb != 0) {
        const auto sq = static_cast<int>(std::countr_zero(bb));
        bb &= bb - 1;
        const auto enc = piece_encoding(static_cast<color>(c), ptype);
        const auto own_index = wk * king_stride + enc * square_stride + sq;
        const auto enemy_index = half_feature_space + bk * king_stride + enc * square_stride + sq;
        for(auto i = 0; i < ft_size; ++i) {
          own[i] = static_cast<std::int16_t>(
              own[i] + net.w1_[static_cast<std::size_t>(own_index) * ft_size + i]);
          enemy[i] = static_cast<std::int16_t>(
              enemy[i] + net.w1_[static_cast<std::size_t>(enemy_index) * ft_size + i]);
        }
      }
    }
  }
  return true;
}

auto evaluate(const network& net, const position& pos) -> int
{
  auto own = std::array<std::int16_t, ft_size>{};
  auto enemy = std::array<std::int16_t, ft_size>{};
  if(pos.side_to_move() == color::white) {
    if(!reference_accumulate(net, pos, own, enemy)) {
      return 0;
    }
    return forward(net, own, enemy);
  }
  if(!reference_accumulate(net, mirrored(pos), own, enemy)) {
    return 0;
  }
  return forward(net, own, enemy);
}

evaluator::evaluator(const network& net) noexcept : net_(net) {}

auto evaluator::apply_piece(piece p, int sq, int sign) -> void
{
  if(p == piece::none || sq < 0 || sq >= 64) {
    return;
  }
  const auto pt = piece_type_of(p);
  if(pt == piece_type::king) {
    return;
  }
  const auto pc = color_of(p);
  const auto enc = piece_encoding(pc, pt);
  const auto enc_flipped = piece_encoding(opposite(pc), pt);
  const auto sq_flipped = sq ^ mirror;
  const auto wksq_flipped = wk_ ^ mirror;
  const auto bksq_flipped = bk_ ^ mirror;

  const auto column = [&](int index) -> const std::int8_t* {
    return net_.w1_.data() + static_cast<std::size_t>(index) * ft_size;
  };

  apply_column(acc_[0], column(wk_ * king_stride + enc * square_stride + sq), sign);
  apply_column(acc_[1], column(half_feature_space + bk_ * king_stride + enc * square_stride + sq), sign);
  apply_column(acc_[2], column(bksq_flipped * king_stride + enc_flipped * square_stride + sq_flipped), sign);
  apply_column(acc_[3], column(half_feature_space + wksq_flipped * king_stride + enc_flipped * square_stride + sq_flipped), sign);
}

auto evaluator::fresh_accumulate(const position& pos) -> bool
{
  wk_ = king_square(pos, color::white);
  bk_ = king_square(pos, color::black);
  if(wk_ == no_square || bk_ == no_square) {
    return false;
  }
  for(auto& half : acc_) {
    half = {};
  }
  for(auto i = 0; i < ft_size; ++i) {
    for(auto& half : acc_) {
      half[i] = net_.bias1_[i];
    }
  }
  for(auto c = 0; c < color_count; ++c) {
    for(auto pt = 0; pt < piece_type_count; ++pt) {
      const auto ptype = static_cast<piece_type>(pt);
      if(ptype == piece_type::king) {
        continue;
      }
      auto bb = pos.pieces(static_cast<color>(c), ptype);
      while(bb != 0) {
        const auto sq = static_cast<int>(std::countr_zero(bb));
        bb &= bb - 1;
        apply_piece(make_piece(static_cast<color>(c), ptype), sq, +1);
      }
    }
  }
  return true;
}

auto evaluator::refresh(const position& pos) -> void
{
  if(!fresh_accumulate(pos)) {
    ready_ = false;
    return;
  }
  sig_ = pos.key();
  ready_ = true;
}

auto evaluator::make_move(const position& after, move mv, const move_state& st) -> void
{
  undo_.push_back(undo{acc_, wk_, bk_, sig_, ready_});
  if(!ready_) {

    return;
  }

  const auto us = opposite(after.side_to_move());
  const auto promo = mv.flag == move_flag::promotion || mv.flag == move_flag::promotion_capture;
  const auto moving_after = after.piece_on(mv.to);

  const auto king_move = !promo && piece_type_of(moving_after) == piece_type::king;
  if(king_move) {
    refresh(after);
    return;
  }

  const auto moved_before = promo ? piece_type::pawn : piece_type_of(moving_after);
  const auto piece_before = make_piece(us, moved_before);
  const auto from = static_cast<int>(mv.from);
  const auto to = static_cast<int>(mv.to);

  switch(mv.flag) {
  case move_flag::quiet:
  case move_flag::double_push:
  case move_flag::promotion:
    apply_piece(piece_before, from, -1);
    apply_piece(moving_after, to, +1);
    break;
  case move_flag::capture:
  case move_flag::promotion_capture:
    apply_piece(piece_before, from, -1);
    apply_piece(st.captured, to, -1);
    apply_piece(moving_after, to, +1);
    break;
  case move_flag::en_passant: {
    const auto ep = to + (us == color::white ? -8 : 8);
    apply_piece(piece_before, from, -1);
    apply_piece(make_piece(opposite(us), piece_type::pawn), ep, -1);
    apply_piece(moving_after, to, +1);
    break;
  }
  case move_flag::castling:

    break;
  }
  sig_ = after.key();
}

auto evaluator::make_null_move(const position& after) -> void
{

  undo_.push_back(undo{acc_, wk_, bk_, sig_, ready_});
  if(ready_) {
    sig_ = after.key();
  }
}

auto evaluator::unmake_move() -> void
{
  if(undo_.empty()) {
    return;
  }
  const auto u = undo_.back();
  undo_.pop_back();
  acc_ = u.acc;
  wk_ = u.wk;
  bk_ = u.bk;
  sig_ = u.sig;
  ready_ = u.ready;
}

auto evaluator::evaluate(const position& pos) -> int
{

  if(!ready_ || sig_ != pos.key()) {
    refresh(pos);
  }
  if(!ready_) {
    return 0;
  }
  const auto stm_white = pos.side_to_move() == color::white;
  return forward(net_, stm_white ? acc_[0] : acc_[2], stm_white ? acc_[1] : acc_[3]);
}

namespace {

auto active_mutex() -> std::mutex&
{
  static std::mutex mutex;
  return mutex;
}

auto active_net() -> std::shared_ptr<const network>&
{
  static std::shared_ptr<const network> net;
  return net;
}

}

auto set_active(std::shared_ptr<const network> net) -> void
{
  std::lock_guard<std::mutex> lock(active_mutex());
  active_net() = std::move(net);
}

auto active() -> std::shared_ptr<const network>
{
  std::lock_guard<std::mutex> lock(active_mutex());
  return active_net();
}

}