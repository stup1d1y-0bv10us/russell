#include "datagen/dataset.hpp"

#include <cstring>
#include <fstream>
#include <iterator>

namespace tuna::datagen {

namespace {

auto ep_store(int sq) noexcept -> std::uint8_t
{
  return static_cast<std::uint8_t>(sq == no_square ? 0xff : sq);
}

auto ep_load(std::uint8_t stored) noexcept -> int
{
  return stored == 0xff ? no_square : static_cast<int>(stored);
}

auto piece_nibble(piece p) noexcept -> std::uint8_t
{
  return static_cast<std::uint8_t>(p);
}

auto nibble_piece(std::uint8_t v) noexcept -> piece
{
  return static_cast<piece>(v & 0x0f);
}

}

dataset_writer::dataset_writer(const std::string& path, const dataset_header& header)
  : path_(path), header_(header)
{
  out_.open(path_.c_str(), std::ios::binary | std::ios::trunc);
  if(!out_) {
    good_ = false;
    return;
  }
  out_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
  good_ = static_cast<bool>(out_);
}

dataset_writer::~dataset_writer()
{
  if(!finished_) {
    static_cast<void>(finish());
  }
}

auto dataset_writer::write(const dataset_record& record) -> bool
{
  if(finished_ || !good_ || !out_) {
    return false;
  }
  out_.write(reinterpret_cast<const char*>(&record), sizeof(record));
  if(!out_) {
    good_ = false;
    return false;
  }
  ++count_;
  return true;
}

auto dataset_writer::finish() -> bool
{
  if(finished_) {
    return good_;
  }
  finished_ = true;
  if(!good_ || !out_) {
    good_ = false;
    return good_;
  }

  header_.position_count = count_;
  out_.seekp(0, std::ios::beg);
  out_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
  out_.flush();
  good_ = static_cast<bool>(out_);
  out_.close();
  return good_;
}

dataset_reader::dataset_reader(const std::string& path) : path_(path)
{
  static_cast<void>(load());
}

auto dataset_reader::open(const std::string& path) -> bool
{
  path_ = path;
  bytes_.clear();
  good_ = false;
  return load();
}

auto dataset_reader::load() -> bool
{
  std::ifstream in(path_, std::ios::binary);
  if(!in) {
    return false;
  }
  bytes_ = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
  if(!in.eof() && in.fail()) {
    return false;
  }
  if(bytes_.size() < sizeof(dataset_header)) {
    return false;
  }
  std::memcpy(&header_, bytes_.data(), sizeof(dataset_header));
  if(header_.magic != dataset_magic || header_.version != dataset_version) {
    return false;
  }
  const auto expected = sizeof(dataset_header) + header_.position_count * sizeof(dataset_record);
  if(bytes_.size() != expected) {
    return false;
  }
  good_ = true;
  return true;
}

auto dataset_reader::record(std::uint64_t index, dataset_record& out) const -> bool
{
  if(!good_ || index >= header_.position_count) {
    return false;
  }
  const auto offset = sizeof(dataset_header) + index * sizeof(dataset_record);
  std::memcpy(&out, bytes_.data() + offset, sizeof(dataset_record));
  return true;
}

auto dataset_reader::records(std::vector<dataset_record>& out) const -> bool
{
  if(!good_) {
    return false;
  }
  out.resize(static_cast<std::size_t>(header_.position_count));
  const auto first = bytes_.data() + sizeof(dataset_header);
  std::memcpy(out.data(), first, out.size() * sizeof(dataset_record));
  return true;
}

auto pack_position(const position& pos) -> dataset_record
{
  auto record = dataset_record{};
  record.stm = static_cast<std::uint8_t>(color_index(pos.side_to_move()));
  record.castling = pos.castling_rights();
  record.ep_square = ep_store(pos.en_passant_square());
  record.halfmove_clock = static_cast<std::uint16_t>(pos.halfmove_clock());
  record.fullmove_number = static_cast<std::uint16_t>(pos.fullmove_number());
  for(auto sq = 0; sq < square_count; ++sq) {
    const auto p = piece_nibble(pos.piece_on(sq));
    if((sq & 1) == 0) {
      record.piece_nibbles[sq / 2] = static_cast<std::uint8_t>(
          (record.piece_nibbles[sq / 2] & 0xf0) | p);
    } else {
      record.piece_nibbles[sq / 2] = static_cast<std::uint8_t>(
          (record.piece_nibbles[sq / 2] & 0x0f) | (p << 4));
    }
  }
  return record;
}

auto unpack_position(const dataset_record& record) -> position
{
  auto pos = position::empty();
  for(auto sq = 0; sq < square_count; ++sq) {
    const auto raw = record.piece_nibbles[sq / 2];
    const auto v = (sq & 1) == 0 ? raw & 0x0f : raw >> 4;
    const auto p = nibble_piece(static_cast<std::uint8_t>(v));
    if(p == piece::none) {
      continue;
    }
    const auto c = static_cast<int>(p) <= 6 ? color::white : color::black;
    pos.set_piece(c, piece_type_of(p), sq);
  }
  pos.set_side_to_move(static_cast<color>(record.stm & 1));
  pos.set_castling_rights(record.castling & all_castling);
  pos.set_en_passant_square(ep_load(record.ep_square));
  pos.set_halfmove_clock(record.halfmove_clock);
  pos.set_fullmove_number(record.fullmove_number);
  return pos;
}

}