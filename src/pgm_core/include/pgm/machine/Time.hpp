#pragma once

#include <cstdint>

namespace pgm::machine
{

/// Emulated time, in units of 10 ns.
///
/// The RTL clocks the board from one 50 MHz clock, and derives the 68000's
/// 20 MHz (2/5) and the pixel clock's 10 MHz (1/5) from it. Half a master
/// period is the largest unit in which both are whole numbers, so that is the
/// unit: a 68000 cycle is 5, a dot is 10, a master tick is 2. Clocks the RTL
/// derives by fractions that do not divide evenly (the ICS2115's 615/908) are
/// counted from master ticks, as the RTL counts them.
using Time = std::int64_t;

inline constexpr Time UNITS_PER_SECOND = 100'000'000;
inline constexpr Time UNITS_PER_MASTER_TICK = 2;
inline constexpr Time UNITS_PER_M68K_CYCLE = 5;
inline constexpr Time UNITS_PER_DOT = 10;

// The IGS023's raster, igs023.sv: 640 dots by 264 lines, of which the first
// 192 dots and the first 40 lines are blanking.
inline constexpr int DOTS_PER_LINE = 640;
inline constexpr int LINES_PER_FRAME = 264;
inline constexpr int HBLANK_DOTS = 192;
inline constexpr int VBLANK_LINES = 40;
inline constexpr int HSYNC_START_DOT = 63;
inline constexpr int HSYNC_DOTS = 63;
inline constexpr int VSYNC_START_LINE = 14;
inline constexpr int VSYNC_LINES = 8;

inline constexpr Time UNITS_PER_LINE = DOTS_PER_LINE * UNITS_PER_DOT;
inline constexpr Time UNITS_PER_FRAME = LINES_PER_FRAME * UNITS_PER_LINE;

/// Master ticks elapsed at `time`, rounded down: the unit the RTL simulator
/// counts in, and the one the control protocol reports.
constexpr std::int64_t masterTicks( Time time )
{
  return time / UNITS_PER_MASTER_TICK;
}

} // namespace pgm::machine
