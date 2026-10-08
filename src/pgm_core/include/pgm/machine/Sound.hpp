#pragma once

// What the sound side shows of itself: the ICS2115's output and voices, and
// the Z80's registers.

#include <cstdint>

namespace pgm::machine
{

/// One stereo sample of the ICS2115's output, and the pulse of its clock
/// (ce_33m, 615/908 of the master clock) at which the sample period began.
struct AudioFrame
{
  std::int64_t at{};
  std::int16_t left{};
  std::int16_t right{};
};

/// An ICS2115 voice's registers.
struct Ics2115Voice
{
  std::uint32_t oscAcc{};   // 29 bits, 20.9 fixed point
  std::uint16_t oscFc{};    // bit 0 unused
  std::uint32_t oscStart{}; // 29 bits
  std::uint32_t oscEnd{};   // 29 bits
  std::uint8_t oscSaddr{};
  std::uint8_t oscConf{};
  std::uint8_t oscCtl{};
  std::uint32_t volAcc{};   // 26 bits
  std::uint32_t volStart{}; // 26 bits
  std::uint32_t volEnd{};   // 26 bits
  std::uint8_t volIncr{};
  std::uint8_t volPan{};
  std::uint8_t volCtrl{};
  std::uint8_t volMode{};
};

struct Z80Registers
{
  std::uint16_t af{};
  std::uint16_t bc{};
  std::uint16_t de{};
  std::uint16_t hl{};
  std::uint16_t ix{};
  std::uint16_t iy{};
  std::uint16_t sp{};
  std::uint16_t pc{};
  std::uint16_t wz{};
  std::uint16_t af2{};
  std::uint16_t bc2{};
  std::uint16_t de2{};
  std::uint16_t hl2{};
  std::uint8_t i{};
  std::uint8_t r{};
  std::uint8_t im{};
  bool iff1{};
  bool iff2{};
};

} // namespace pgm::machine
