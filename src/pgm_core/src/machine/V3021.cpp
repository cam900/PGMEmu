#include "V3021.hpp"

#include "ClockEnables.hpp"

namespace pgm::machine
{

namespace
{

/// bcd_counter's increment: wraps to `minimum` past `maximum`, and says so.
bool incrementBcd( std::uint8_t& count, std::uint8_t maximum, std::uint8_t minimum )
{
  if ( count == maximum )
  {
    count = minimum;
    return true;
  }
  count = ( count & 0x0fU ) == 9 ? static_cast<std::uint8_t>( ( count & 0xf0U ) + 0x10U )
                                 : static_cast<std::uint8_t>( count + 1U );
  return false;
}

/// bcd_counter's clamping, which it applies on every clock whatever else
/// happens: a count below its minimum becomes the minimum, one above its
/// maximum becomes the maximum.
void clampBcd( std::uint8_t& count, std::uint8_t maximum, std::uint8_t minimum )
{
  if ( count < minimum )
  {
    count = minimum;
  }
  else if ( count > maximum )
  {
    count = maximum;
  }
}

} // namespace

bool V3021::access( Time now, bool write, bool bit )
{
  advanceTo( now );

  std::uint8_t const state = mState;
  mState = state == 11 ? 0 : static_cast<std::uint8_t>( state + 1 );

  if ( state <= 3 )
  {
    // The address phase: four written bits, least significant first. A read
    // here abandons the transfer.
    if ( !write )
    {
      mState = 0;
      return mOut;
    }
    mAddress = static_cast<std::uint8_t>( ( mAddress >> 1U ) | ( bit ? 0x08U : 0x00U ) );
    if ( state == 3 )
    {
      if ( mAddress == 0x0e )
      {
        mState = 0;
        loadCountersFromRam();
      }
      else if ( mAddress == 0x0f )
      {
        // Latches the time into RAM 2..9, and into RAM 0 which counters
        // differ from what RAM held.
        std::array<std::uint8_t, 8> const counters{ mCounters.second,  mCounters.minute, mCounters.hour,
                                                    mCounters.day,     mCounters.month,  mCounters.year,
                                                    mCounters.weekday, mCounters.week };
        std::uint8_t changed = 0;
        for ( std::size_t i = 0; i < counters.size(); ++i )
        {
          if ( counters[i] != mRam[2 + i] )
          {
            changed = static_cast<std::uint8_t>( changed | ( 1U << i ) );
          }
          mRam[2 + i] = counters[i];
        }
        mRam[0] = changed;
        mState = 0;
      }
      else
      {
        mData = mRam[mAddress];
      }
    }
    return mOut;
  }

  // The data phase: eight bits, least significant first, out and in at once.
  mOut = ( mData & 1U ) != 0;
  mData = static_cast<std::uint8_t>( ( mData >> 1U ) | ( write && bit ? 0x80U : 0x00U ) );
  if ( write && state == 11 )
  {
    mRam[mAddress] = mData;
  }
  return mOut;
}

void V3021::advanceTo( Time now )
{
  // A second passes on every 65536th pulse of the RTC clock, the first of them
  // included: that is when the RTL's 16-bit prescaler is at zero.
  std::int64_t const pulses = rtcPulses( masterTicks( now ) );
  std::int64_t const seconds = ( pulses + 65535 ) / 65536;
  while ( mSecondsApplied < seconds )
  {
    tickSecond();
    ++mSecondsApplied;
  }
}

void V3021::tickSecond()
{
  Counters& c = mCounters;
  if ( !incrementBcd( c.second, 0x59, 0x00 ) || !incrementBcd( c.minute, 0x59, 0x00 ) ||
       !incrementBcd( c.hour, 0x23, 0x00 ) )
  {
    return;
  }
  bool const newWeek = incrementBcd( c.weekday, 0x07, 0x01 );
  bool const newMonth = incrementBcd( c.day, lastDayOfMonth(), 0x00 );
  if ( newWeek )
  {
    incrementBcd( c.week, 0x52, 0x00 );
  }
  if ( newMonth && incrementBcd( c.month, 0x12, 0x01 ) )
  {
    incrementBcd( c.year, 0x99, 0x00 );
  }
}

void V3021::loadCountersFromRam()
{
  mCounters = Counters{ .second = mRam[2],
                        .minute = mRam[3],
                        .hour = mRam[4],
                        .day = mRam[5],
                        .month = mRam[6],
                        .year = mRam[7],
                        .weekday = mRam[8],
                        .week = mRam[9] };
  // What the RTL's counters do to such values on the clock after the load.
  clampBcd( mCounters.second, 0x59, 0x00 );
  clampBcd( mCounters.minute, 0x59, 0x00 );
  clampBcd( mCounters.hour, 0x23, 0x00 );
  clampBcd( mCounters.day, lastDayOfMonth(), 0x00 );
  clampBcd( mCounters.month, 0x12, 0x01 );
  clampBcd( mCounters.year, 0x99, 0x00 );
  clampBcd( mCounters.weekday, 0x07, 0x01 );
  clampBcd( mCounters.week, 0x52, 0x00 );
}

std::uint8_t V3021::lastDayOfMonth() const
{
  std::uint8_t const year = mCounters.year;
  bool const leap =
      ( ( year & 0x10U ) == 0 && ( ( year & 0x0fU ) == 0 || ( year & 0x0fU ) == 4 || ( year & 0x0fU ) == 8 ) ) ||
      ( ( year & 0x10U ) != 0 && ( ( year & 0x0fU ) == 2 || ( year & 0x0fU ) == 6 ) );
  switch ( mCounters.month )
  {
  case 0x02:
    return leap ? 0x29 : 0x28;
  case 0x04:
  case 0x06:
  case 0x09:
  case 0x11:
    return 0x30;
  default:
    return 0x31;
  }
}

} // namespace pgm::machine
