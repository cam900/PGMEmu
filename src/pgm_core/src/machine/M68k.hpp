#pragma once

// The 68000: Moira, connected to the board's bus and clock. Moira calls sync()
// before each bus cycle (MOIRA_PRECISE_TIMING), so every access reaches the bus
// at the emulated time the 68000 makes it, and devices that are caught up on
// access see the raster where the RTL's would be.

#include "Bus68k.hpp"

#include "pgm/machine/Time.hpp"

#include "Moira.h"

#include <cstdint>
#include <string>

namespace pgm::machine
{

class M68k : public moira::Moira
{
public:
  /// `time` is the machine's clock, which this CPU advances as it runs.
  M68k( Bus68k& bus, Time& time );

  /// Sets when the 68000's E clock started counting: the release of reset,
  /// which is when fx68k's E counter leaves zero.
  void startEClock( Time at );

  /// Whether a STOP instruction is waiting for an interrupt.
  [[nodiscard]] bool stopped() const;

  /// The instruction at `address` as text, and its length in bytes.
  [[nodiscard]] std::string disassembleAt( std::uint32_t address, int& length ) const;

protected:
  void sync( int cycles ) override;

  /// Lengthens the interrupt acknowledge cycle to the E clock, as VPA does.
  void willInterrupt( moira::u8 level ) override;

  [[nodiscard]] moira::u8 read8( moira::u32 addr ) const override;
  [[nodiscard]] moira::u16 read16( moira::u32 addr ) const override;
  [[nodiscard]] moira::u32 read32( moira::u32 addr ) const override;
  [[nodiscard]] moira::u16 read16Dasm( moira::u32 addr ) const override;
  void write8( moira::u32 addr, moira::u8 val ) const override;
  void write16( moira::u32 addr, moira::u16 val ) const override;
  void write32( moira::u32 addr, moira::u32 val ) const override;

private:
  // Moira's client interface is const, as a read of memory looks from the
  // CPU's side; the bus it reaches is not.
  Bus68k* mBus;
  Time* mTime;
  Time mEClockOrigin{};
};

} // namespace pgm::machine
