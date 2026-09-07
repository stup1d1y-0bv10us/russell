#pragma once

#include "core/position.hpp"
#include "core/types.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace tuna::datagen {

constexpr auto dataset_magic = std::uint32_t{0x544E4141};
constexpr auto dataset_version = std::uint32_t{1};

#pragma pack(push, 1)
struct dataset_header {
  std::uint32_t magic = dataset_magic;
  std::uint32_t version = dataset_version;
  std::uint64_t seed = 0;
  std::int32_t search_depth = 0;
  std::int32_t score_depth = 0;
  std::int32_t sample_interval = 1;
  std::int32_t max_plies = 0;
  std::uint64_t game_count = 0;
  std::uint64_t position_count = 0;
  std::uint64_t reserved0 = 0;
  std::uint64_t reserved1 = 0;
};

struct dataset_record {
  std::uint8_t stm = 0;
  std::uint8_t castling = 0;
  std::uint8_t ep_square = 0xff;
  std::uint8_t reserved = 0;
  std::uint16_t halfmove_clock = 0;
  std::uint16_t fullmove_number = 1;

  std::array<std::uint8_t, 32> piece_nibbles{};

  std::int8_t result = 0;
  std::int32_t score = 0;
  std::array<std::uint8_t, 3> padding{};
};
#pragma pack(pop)

static_assert(sizeof(dataset_header) == 64, "dataset header must be 64 bytes");
static_assert(sizeof(dataset_record) == 48, "dataset record must be 48 bytes");

class dataset_writer {
public:
  dataset_writer(const std::string& path, const dataset_header& header);
  ~dataset_writer();

  dataset_writer(const dataset_writer&) = delete;
  auto operator=(const dataset_writer&) = delete;

  auto write(const dataset_record& record) -> bool;
  auto finish() -> bool;

  [[nodiscard]] auto good() const noexcept -> bool { return good_; }
  [[nodiscard]] auto position_count() const noexcept -> std::uint64_t { return count_; }

private:
  std::string path_;
  dataset_header header_;
  std::ofstream out_{};
  std::uint64_t count_ = 0;
  bool good_ = false;
  bool finished_ = false;
};

class dataset_reader {
public:
  dataset_reader() = default;
  explicit dataset_reader(const std::string& path);

  auto open(const std::string& path) -> bool;

  [[nodiscard]] auto good() const noexcept -> bool { return good_; }
  [[nodiscard]] auto header() const noexcept -> const dataset_header& { return header_; }
  [[nodiscard]] auto position_count() const noexcept -> std::uint64_t
  {
    return header_.position_count;
  }

  auto record(std::uint64_t index, dataset_record& out) const -> bool;

  auto records(std::vector<dataset_record>& out) const -> bool;

private:
  bool load();

  std::string path_;
  dataset_header header_{};
  std::vector<std::uint8_t> bytes_{};
  bool good_ = false;
};

[[nodiscard]] auto pack_position(const position& pos) -> dataset_record;

[[nodiscard]] auto unpack_position(const dataset_record& record) -> position;

}