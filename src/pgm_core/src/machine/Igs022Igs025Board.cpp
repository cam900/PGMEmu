#include "Igs022Igs025Board.hpp"

namespace pgm::machine
{

namespace
{

/// The IGS022's RAM, 0x300000-0x303FFF, which the board takes from ROM.
constexpr std::uint32_t IGS022_RAM = 0x300000;
constexpr std::uint32_t IGS022_RAM_MASK = 0xffc000;

} // namespace

Igs022Igs025Board::Igs022Igs025Board( std::span<std::uint8_t const> igs022Rom,
                                      cart::Igs025Table const& igs025Table,
                                      std::uint32_t igs025Address )
    : mIgs022{ igs022Rom }, mIgs025{ igs025Table }, mIgs025Address{ igs025Address }
{
}

std::vector<std::uint8_t> Igs022Igs025Board::pages() const
{
  return { static_cast<std::uint8_t>( IGS022_RAM >> 16U ), static_cast<std::uint8_t>( mIgs025Address >> 16U ) };
}

bool Igs022Igs025Board::decodes( std::uint32_t address ) const
{
  return ( address & IGS022_RAM_MASK ) == IGS022_RAM || isIgs025( address );
}

std::uint16_t Igs022Igs025Board::read( Time& /*time*/, std::uint32_t address, bool /*upper*/, bool /*lower*/ )
{
  if ( isIgs025( address ) )
  {
    return mIgs025.read( ( address >> 1U ) & 1U );
  }
  return mIgs022.read( ( address & ~IGS022_RAM_MASK ) >> 1U );
}

void Igs022Igs025Board::write( Time& time, std::uint32_t address, std::uint16_t value, bool upper, bool lower )
{
  if ( !isIgs025( address ) )
  {
    mIgs022.write( ( address & ~IGS022_RAM_MASK ) >> 1U, value, upper, lower );
    return;
  }
  if ( mIgs025.write( ( address >> 1U ) & 1U, value ) )
  {
    // The write's DTACK waits until the IGS022 has finished the command, in
    // whole 68000 cycles.
    Time const busy = mIgs022.execute() * UNITS_PER_MASTER_TICK;
    time += ( busy + UNITS_PER_M68K_CYCLE - 1 ) / UNITS_PER_M68K_CYCLE * UNITS_PER_M68K_CYCLE;
  }
}

std::uint16_t Igs022Igs025Board::peek( std::uint32_t address ) const
{
  if ( isIgs025( address ) )
  {
    return mIgs025.peek( ( address >> 1U ) & 1U );
  }
  return mIgs022.read( ( address & ~IGS022_RAM_MASK ) >> 1U );
}

void Igs022Igs025Board::reset( Time /*now*/ )
{
  mIgs025.reset();
  mIgs022.reset();
}

void Igs022Igs025Board::serialize( StateWriter& archive )
{
  mIgs022.serialize( archive );
  mIgs025.serialize( archive );
}

void Igs022Igs025Board::serialize( StateReader& archive )
{
  mIgs022.serialize( archive );
  mIgs025.serialize( archive );
}

bool Igs022Igs025Board::isIgs025( std::uint32_t address ) const
{
  return ( address & ~3U ) == mIgs025Address;
}

} // namespace pgm::machine
