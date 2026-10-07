#include "support/Program.hpp"

namespace pgm::test
{

Program::Program( std::uint32_t stack, std::uint32_t entry ) : mBytes( 0x20000, 0 )
{
  vector( 0, stack );
  vector( 1, entry );
}

Program& Program::at( std::uint32_t address, std::initializer_list<std::uint16_t> words )
{
  for ( std::uint16_t const word : words )
  {
    putWord( address, word );
    address += 2;
  }
  return *this;
}

Program& Program::vector( std::uint32_t number, std::uint32_t handler )
{
  putWord( number * 4, high( handler ) );
  putWord( ( number * 4 ) + 2, low( handler ) );
  return *this;
}

std::vector<std::uint8_t> const& Program::bytes() const
{
  return mBytes;
}

void Program::putWord( std::uint32_t address, std::uint16_t word )
{
  mBytes.at( address ) = static_cast<std::uint8_t>( word );
  mBytes.at( address + 1 ) = static_cast<std::uint8_t>( word >> 8U );
}

} // namespace pgm::test
