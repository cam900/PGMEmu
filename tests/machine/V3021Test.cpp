#include <catch2/catch_test_macros.hpp>

#include "machine/ClockEnables.hpp"
#include "machine/V3021.hpp"

#include <cstdint>

using pgm::machine::Time;
using pgm::machine::V3021;

namespace
{

/// The first time at which `pulses` pulses of the RTC clock have passed.
Time timeOfRtcPulse( std::int64_t pulses )
{
  // One pulse per 228 ce_8m, one ce_8m per 4 ce_33m, one ce_33m per 908/615
  // master ticks: an estimate from below, then the exact count by stepping.
  std::int64_t ticks = ( pulses * 228 * 4 * 908 / 615 ) - 8;
  ticks = ticks < 0 ? 0 : ticks;
  while ( pgm::machine::rtcPulses( ticks ) < pulses )
  {
    ++ticks;
  }
  return ticks * pgm::machine::UNITS_PER_MASTER_TICK;
}

/// The serial protocol, as a 68000 program drives it through bit 0 of 0xC00006.
class Port
{
public:
  explicit Port( V3021& rtc ) : mRtc{ rtc } {}

  void selectAddress( Time now, std::uint8_t address )
  {
    for ( unsigned bit = 0; bit < 4; ++bit )
    {
      mRtc.access( now, true, ( ( address >> bit ) & 1U ) != 0 );
    }
  }

  void writeRegister( Time now, std::uint8_t address, std::uint8_t value )
  {
    selectAddress( now, address );
    for ( unsigned bit = 0; bit < 8; ++bit )
    {
      mRtc.access( now, true, ( ( value >> bit ) & 1U ) != 0 );
    }
  }

  std::uint8_t readRegister( Time now, std::uint8_t address )
  {
    selectAddress( now, address );
    std::uint8_t value = 0;
    for ( unsigned bit = 0; bit < 8; ++bit )
    {
      if ( mRtc.access( now, false, false ) )
      {
        value = static_cast<std::uint8_t>( value | ( 1U << bit ) );
      }
    }
    return value;
  }

private:
  V3021& mRtc;
};

constexpr std::uint8_t SECONDS = 2;
constexpr std::uint8_t MINUTES = 3;
constexpr std::uint8_t LATCH_TIME = 0x0f;
constexpr std::uint8_t LOAD_TIME = 0x0e;

} // namespace

TEST_CASE( "a register written over the serial port reads back", "[machine][rtc]" )
{
  V3021 rtc;
  Port port{ rtc };

  port.writeRegister( 0, 0x0a, 0x5a );

  REQUIRE( port.readRegister( 0, 0x0a ) == 0x5a );
}

TEST_CASE( "the clock counts from what it was loaded with, a second per 65536 RTC pulses", "[machine][rtc]" )
{
  V3021 rtc;
  Port port{ rtc };
  port.writeRegister( 0, SECONDS, 0x58 );
  port.writeRegister( 0, MINUTES, 0x07 );
  port.selectAddress( 0, LOAD_TIME );

  SECTION( "before the first pulse" )
  {
    port.selectAddress( 0, LATCH_TIME );
    REQUIRE( port.readRegister( 0, SECONDS ) == 0x58 );
  }

  SECTION( "the first pulse is a second, as the RTL's prescaler starts at zero" )
  {
    Time const now = timeOfRtcPulse( 1 );
    port.selectAddress( now, LATCH_TIME );
    REQUIRE( port.readRegister( now, SECONDS ) == 0x59 );
  }

  SECTION( "the second after it carries into the minutes, in BCD" )
  {
    Time const now = timeOfRtcPulse( 65537 );
    port.selectAddress( now, LATCH_TIME );
    REQUIRE( port.readRegister( now, SECONDS ) == 0x00 );
    REQUIRE( port.readRegister( now, MINUTES ) == 0x08 );
  }
}

TEST_CASE( "a read in the address phase abandons the transfer", "[machine][rtc]" )
{
  V3021 rtc;
  Port port{ rtc };
  port.writeRegister( 0, 0x0a, 0x81 );

  // Two address bits, then a read: the next write starts a new address.
  rtc.access( 0, true, true );
  rtc.access( 0, true, true );
  rtc.access( 0, false, false );

  REQUIRE( port.readRegister( 0, 0x0a ) == 0x81 );
}
