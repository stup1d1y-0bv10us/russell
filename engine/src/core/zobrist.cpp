#include "core/zobrist.hpp"

namespace tuna::zobrist {

namespace {

constexpr auto next(std::uint64_t value) noexcept -> std::uint64_t
{
  value += 0x9e3779b97f4a7c15ull;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
  return value ^ (value >> 31);
}

constexpr auto make_piece_keys() noexcept -> piece_keys
{
  auto keys = piece_keys{};
  auto seed = std::uint64_t{0x4d595df4d0f33173ull};
  for(auto c = 0; c < color_count; ++c) {
    for(auto pt = 0; pt < piece_type_count; ++pt) {
      for(auto sq = 0; sq < square_count; ++sq) {
        seed = next(seed);
        keys[c][pt][sq] = seed;
      }
    }
  }
  return keys;
}

constexpr auto make_castling_keys() noexcept -> castling_keys
{
  auto keys = castling_keys{};
  auto seed = std::uint64_t{0x6a09e667f3bcc909ull};
  for(auto i = 0; i < 16; ++i) {
    seed = next(seed);
    keys[i] = seed;
  }
  return keys;
}

constexpr auto make_en_passant_keys() noexcept -> en_passant_keys
{
  auto keys = en_passant_keys{};
  auto seed = std::uint64_t{0xbb67ae8584caa73bull};
  for(auto i = 0; i < 8; ++i) {
    seed = next(seed);
    keys[i] = seed;
  }
  return keys;
}

constexpr auto piece_keys_value = make_piece_keys();
constexpr auto castling_keys_value = make_castling_keys();
constexpr auto en_passant_keys_value = make_en_passant_keys();
constexpr auto side_key_value = next(0x3c6ef372fe94f82bull);

}

auto piece_square(color c, piece_type pt, int sq) noexcept -> std::uint64_t
{
  return piece_keys_value[color_index(c)][piece_type_index(pt)][sq];
}

auto castling(std::uint8_t rights) noexcept -> std::uint64_t
{
  return castling_keys_value[rights & all_castling];
}

auto en_passant(int file) noexcept -> std::uint64_t
{
  return en_passant_keys_value[file];
}

auto side() noexcept -> std::uint64_t
{
  return side_key_value;
}

}