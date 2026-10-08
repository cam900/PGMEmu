#include "Arm7Disassembler.hpp"

#include <spdlog/fmt/fmt.h>

#include <array>
#include <bit>
#include <string_view>
#include <utility>

namespace pgm::cpu
{

namespace
{

constexpr std::array<std::string_view, 16> CONDITIONS{ "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
                                                       "hi", "ls", "ge", "lt", "gt", "le", "",   "nv" };
constexpr std::array<std::string_view, 16> OPERATIONS{ "and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc",
                                                       "tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn" };
constexpr std::array<std::string_view, 4> SHIFTS{ "lsl", "lsr", "asr", "ror" };
constexpr std::array<std::string_view, 16> THUMB_ALU{ "and", "eor", "lsl", "lsr", "asr", "adc", "sbc", "ror",
                                                      "tst", "neg", "cmp", "cmn", "orr", "mul", "bic", "mvn" };

std::uint32_t field( std::uint32_t value, unsigned low, unsigned width )
{
  return ( value >> low ) & ( ( 1U << width ) - 1U );
}

bool bit( std::uint32_t value, unsigned index )
{
  return ( ( value >> index ) & 1U ) != 0;
}

std::string_view reg( std::uint32_t index )
{
  constexpr std::array<std::string_view, 16> names{ "r0", "r1", "r2",  "r3",  "r4",  "r5", "r6", "r7",
                                                    "r8", "r9", "r10", "r11", "r12", "sp", "lr", "pc" };
  return names.at( index & 15U );
}

std::string registerList( std::uint32_t list )
{
  std::string text;
  for ( unsigned i = 0; i < 16; ++i )
  {
    if ( !bit( list, i ) )
    {
      continue;
    }
    unsigned last = i;
    while ( last + 1 < 16 && bit( list, last + 1 ) )
    {
      ++last;
    }
    text += fmt::format( "{}{}", text.empty() ? "" : ",", reg( i ) );
    if ( last > i )
    {
      text += fmt::format( "{}{}", last > i + 1 ? "-" : ",", reg( last ) );
    }
    i = last;
  }
  return "{" + text + "}";
}

std::string signedHex( std::uint32_t value, bool up )
{
  return fmt::format( "#{}0x{:x}", up ? "" : "-", value );
}

/// An addressing mode `[rn, offset]{!}` or `[rn], offset`.
std::string address( std::uint32_t rn, std::string const& offset, bool preIndex, bool writeBack )
{
  if ( !preIndex )
  {
    return fmt::format( "[{}], {}", reg( rn ), offset );
  }
  return fmt::format( "[{}, {}]{}", reg( rn ), offset, writeBack ? "!" : "" );
}

std::string shiftedRegister( std::uint32_t opcode )
{
  std::uint32_t const rm = field( opcode, 0, 4 );
  std::uint32_t const type = field( opcode, 5, 2 );
  if ( bit( opcode, 4 ) )
  {
    return fmt::format( "{}, {} {}", reg( rm ), SHIFTS.at( type ), reg( field( opcode, 8, 4 ) ) );
  }
  std::uint32_t amount = field( opcode, 7, 5 );
  if ( amount == 0 )
  {
    if ( type == 0 )
    {
      return std::string{ reg( rm ) };
    }
    if ( type == 3 )
    {
      return fmt::format( "{}, rrx", reg( rm ) );
    }
    amount = 32;
  }
  return fmt::format( "{}, {} #{}", reg( rm ), SHIFTS.at( type ), amount );
}

std::string dataProcessing( std::uint32_t opcode, std::string_view cond )
{
  std::uint32_t const operation = field( opcode, 21, 4 );
  std::uint32_t const rn = field( opcode, 16, 4 );
  std::uint32_t const rd = field( opcode, 12, 4 );
  bool const compare = operation >= 8 && operation <= 11;
  std::string const operand =
      bit( opcode, 25 )
          ? fmt::format( "#0x{:x}", std::rotr( field( opcode, 0, 8 ), static_cast<int>( field( opcode, 8, 4 ) * 2 ) ) )
          : shiftedRegister( opcode );
  std::string const mnemonic =
      fmt::format( "{}{}{}", OPERATIONS.at( operation ), cond, bit( opcode, 20 ) && !compare ? "s" : "" );
  if ( compare )
  {
    return fmt::format( "{}{} {}, {}", mnemonic, rd == 15 ? "p" : "", reg( rn ), operand );
  }
  if ( operation == 13 || operation == 15 )
  {
    return fmt::format( "{} {}, {}", mnemonic, reg( rd ), operand );
  }
  return fmt::format( "{} {}, {}, {}", mnemonic, reg( rd ), reg( rn ), operand );
}

std::string psrTransfer( std::uint32_t opcode, std::string_view cond )
{
  std::string_view const psr = bit( opcode, 22 ) ? "spsr" : "cpsr";
  if ( !bit( opcode, 21 ) )
  {
    return fmt::format( "mrs{} {}, {}", cond, reg( field( opcode, 12, 4 ) ), psr );
  }
  std::string fields;
  for ( auto const [index, name] :
        { std::pair{ 19U, 'f' }, std::pair{ 18U, 's' }, std::pair{ 17U, 'x' }, std::pair{ 16U, 'c' } } )
  {
    if ( bit( opcode, index ) )
    {
      fields += name;
    }
  }
  std::string const source =
      bit( opcode, 25 )
          ? fmt::format( "#0x{:x}", std::rotr( field( opcode, 0, 8 ), static_cast<int>( field( opcode, 8, 4 ) * 2 ) ) )
          : std::string{ reg( field( opcode, 0, 4 ) ) };
  return fmt::format( "msr{} {}_{}, {}", cond, psr, fields, source );
}

std::string halfwordTransfer( std::uint32_t opcode, std::string_view cond )
{
  constexpr std::array<std::string_view, 4> kinds{ "", "h", "sb", "sh" };
  bool const up = bit( opcode, 23 );
  std::string const offset = bit( opcode, 22 )
                                 ? signedHex( ( field( opcode, 8, 4 ) << 4U ) | field( opcode, 0, 4 ), up )
                                 : fmt::format( "{}{}", up ? "" : "-", reg( field( opcode, 0, 4 ) ) );
  return fmt::format( "{}{}{} {}, {}",
                      bit( opcode, 20 ) ? "ldr" : "str",
                      cond,
                      kinds.at( field( opcode, 5, 2 ) ),
                      reg( field( opcode, 12, 4 ) ),
                      address( field( opcode, 16, 4 ), offset, bit( opcode, 24 ), bit( opcode, 21 ) ) );
}

std::string singleDataTransfer( std::uint32_t opcode, std::string_view cond )
{
  bool const up = bit( opcode, 23 );
  std::string const offset = bit( opcode, 25 ) ? fmt::format( "{}{}", up ? "" : "-", shiftedRegister( opcode ) )
                                               : signedHex( field( opcode, 0, 12 ), up );
  bool const preIndex = bit( opcode, 24 );
  return fmt::format( "{}{}{}{} {}, {}",
                      bit( opcode, 20 ) ? "ldr" : "str",
                      cond,
                      bit( opcode, 22 ) ? "b" : "",
                      !preIndex && bit( opcode, 21 ) ? "t" : "",
                      reg( field( opcode, 12, 4 ) ),
                      address( field( opcode, 16, 4 ), offset, preIndex, bit( opcode, 21 ) ) );
}

std::string blockDataTransfer( std::uint32_t opcode, std::string_view cond )
{
  constexpr std::array<std::string_view, 4> modes{ "da", "ia", "db", "ib" };
  return fmt::format( "{}{}{} {}{}, {}{}",
                      bit( opcode, 20 ) ? "ldm" : "stm",
                      cond,
                      modes.at( field( opcode, 23, 2 ) ),
                      reg( field( opcode, 16, 4 ) ),
                      bit( opcode, 21 ) ? "!" : "",
                      registerList( field( opcode, 0, 16 ) ),
                      bit( opcode, 22 ) ? "^" : "" );
}

std::uint32_t signExtend( std::uint32_t value, unsigned bits )
{
  std::uint32_t const sign = 1U << ( bits - 1 );
  return ( value ^ sign ) - sign;
}

} // namespace

std::string disassembleArm( std::uint32_t address, std::uint32_t opcode )
{
  std::string_view const cond = CONDITIONS.at( opcode >> 28U );
  if ( ( opcode & 0x0ffffff0U ) == 0x012fff10U )
  {
    return fmt::format( "bx{} {}", cond, reg( field( opcode, 0, 4 ) ) );
  }
  if ( ( opcode & 0x0fb00ff0U ) == 0x01000090U )
  {
    return fmt::format( "swp{}{} {}, {}, [{}]",
                        cond,
                        bit( opcode, 22 ) ? "b" : "",
                        reg( field( opcode, 12, 4 ) ),
                        reg( field( opcode, 0, 4 ) ),
                        reg( field( opcode, 16, 4 ) ) );
  }
  if ( ( opcode & 0x0fc000f0U ) == 0x00000090U )
  {
    std::string_view const s = bit( opcode, 20 ) ? "s" : "";
    if ( bit( opcode, 21 ) )
    {
      return fmt::format( "mla{}{} {}, {}, {}, {}",
                          cond,
                          s,
                          reg( field( opcode, 16, 4 ) ),
                          reg( field( opcode, 0, 4 ) ),
                          reg( field( opcode, 8, 4 ) ),
                          reg( field( opcode, 12, 4 ) ) );
    }
    return fmt::format( "mul{}{} {}, {}, {}",
                        cond,
                        s,
                        reg( field( opcode, 16, 4 ) ),
                        reg( field( opcode, 0, 4 ) ),
                        reg( field( opcode, 8, 4 ) ) );
  }
  if ( ( opcode & 0x0f8000f0U ) == 0x00800090U )
  {
    return fmt::format( "{}{}{}{} {}, {}, {}, {}",
                        bit( opcode, 22 ) ? "s" : "u",
                        bit( opcode, 21 ) ? "mlal" : "mull",
                        cond,
                        bit( opcode, 20 ) ? "s" : "",
                        reg( field( opcode, 12, 4 ) ),
                        reg( field( opcode, 16, 4 ) ),
                        reg( field( opcode, 0, 4 ) ),
                        reg( field( opcode, 8, 4 ) ) );
  }
  if ( ( opcode & 0x0e000090U ) == 0x00000090U && ( opcode & 0x60U ) != 0 )
  {
    return halfwordTransfer( opcode, cond );
  }
  if ( ( opcode & 0x0d900000U ) == 0x01000000U )
  {
    return psrTransfer( opcode, cond );
  }
  if ( ( opcode & 0x0c000000U ) == 0 )
  {
    return dataProcessing( opcode, cond );
  }
  if ( ( opcode & 0x0e000010U ) == 0x06000010U )
  {
    return "undefined";
  }
  if ( ( opcode & 0x0c000000U ) == 0x04000000U )
  {
    return singleDataTransfer( opcode, cond );
  }
  if ( ( opcode & 0x0e000000U ) == 0x08000000U )
  {
    return blockDataTransfer( opcode, cond );
  }
  if ( ( opcode & 0x0e000000U ) == 0x0a000000U )
  {
    std::uint32_t const target = address + 8 + ( signExtend( field( opcode, 0, 24 ), 24 ) << 2U );
    return fmt::format( "b{}{} 0x{:08x}", bit( opcode, 24 ) ? "l" : "", cond, target );
  }
  if ( ( opcode & 0x0f000000U ) == 0x0f000000U )
  {
    return fmt::format( "swi{} 0x{:06x}", cond, field( opcode, 0, 24 ) );
  }
  return "undefined (coprocessor)";
}

std::string disassembleThumb( std::uint32_t address, std::uint16_t opcode, std::uint16_t next )
{
  std::uint32_t const op = opcode;
  auto const low = [&]( unsigned at ) { return reg( field( op, at, 3 ) ); };
  switch ( op >> 12U )
  {
  case 0x0:
  case 0x1:
    if ( field( op, 11, 2 ) == 3 )
    {
      std::string const operand = bit( op, 10 ) ? fmt::format( "#{}", field( op, 6, 3 ) ) : std::string{ low( 6 ) };
      return fmt::format( "{} {}, {}, {}", bit( op, 9 ) ? "sub" : "add", low( 0 ), low( 3 ), operand );
    }
    return fmt::format( "{} {}, {}, #{}", SHIFTS.at( field( op, 11, 2 ) ), low( 0 ), low( 3 ), field( op, 6, 5 ) );
  case 0x2:
  case 0x3:
  {
    constexpr std::array<std::string_view, 4> names{ "mov", "cmp", "add", "sub" };
    return fmt::format( "{} {}, #0x{:x}", names.at( field( op, 11, 2 ) ), low( 8 ), field( op, 0, 8 ) );
  }
  case 0x4:
    if ( bit( op, 11 ) )
    {
      std::uint32_t const target = ( ( address + 4 ) & ~2U ) + ( field( op, 0, 8 ) << 2U );
      return fmt::format( "ldr {}, [pc, #0x{:x}] ; 0x{:08x}", low( 8 ), field( op, 0, 8 ) << 2U, target );
    }
    if ( bit( op, 10 ) )
    {
      std::uint32_t const rd = field( op, 0, 3 ) | ( bit( op, 7 ) ? 8U : 0U );
      std::uint32_t const rs = field( op, 3, 3 ) | ( bit( op, 6 ) ? 8U : 0U );
      if ( field( op, 8, 2 ) == 3 )
      {
        return fmt::format( "bx {}", reg( rs ) );
      }
      constexpr std::array<std::string_view, 3> names{ "add", "cmp", "mov" };
      return fmt::format( "{} {}, {}", names.at( field( op, 8, 2 ) ), reg( rd ), reg( rs ) );
    }
    return fmt::format( "{} {}, {}", THUMB_ALU.at( field( op, 6, 4 ) ), low( 0 ), low( 3 ) );
  case 0x5:
  {
    constexpr std::array<std::string_view, 8> names{ "str", "strh", "strb", "ldsb", "ldr", "ldrh", "ldrb", "ldsh" };
    return fmt::format( "{} {}, [{}, {}]", names.at( field( op, 9, 3 ) ), low( 0 ), low( 3 ), low( 6 ) );
  }
  case 0x6:
  case 0x7:
  {
    bool const byte = bit( op, 12 );
    return fmt::format( "{}{} {}, [{}, #0x{:x}]",
                        bit( op, 11 ) ? "ldr" : "str",
                        byte ? "b" : "",
                        low( 0 ),
                        low( 3 ),
                        field( op, 6, 5 ) * ( byte ? 1U : 4U ) );
  }
  case 0x8:
    return fmt::format(
        "{} {}, [{}, #0x{:x}]", bit( op, 11 ) ? "ldrh" : "strh", low( 0 ), low( 3 ), field( op, 6, 5 ) << 1U );
  case 0x9:
    return fmt::format( "{} {}, [sp, #0x{:x}]", bit( op, 11 ) ? "ldr" : "str", low( 8 ), field( op, 0, 8 ) << 2U );
  case 0xa:
    return fmt::format( "add {}, {}, #0x{:x}", low( 8 ), bit( op, 11 ) ? "sp" : "pc", field( op, 0, 8 ) << 2U );
  case 0xb:
    if ( field( op, 8, 4 ) == 0 )
    {
      return fmt::format( "add sp, #{}0x{:x}", bit( op, 7 ) ? "-" : "", field( op, 0, 7 ) << 2U );
    }
    if ( field( op, 9, 2 ) == 2 )
    {
      bool const pop = bit( op, 11 );
      std::uint32_t list = field( op, 0, 8 );
      if ( bit( op, 8 ) )
      {
        list |= 1U << ( pop ? 15U : 14U );
      }
      return fmt::format( "{} {}", pop ? "pop" : "push", registerList( list ) );
    }
    return "undefined";
  case 0xc:
    return fmt::format( "{}ia {}!, {}", bit( op, 11 ) ? "ldm" : "stm", low( 8 ), registerList( field( op, 0, 8 ) ) );
  case 0xd:
    if ( field( op, 8, 4 ) == 0xf )
    {
      return fmt::format( "swi #0x{:x}", field( op, 0, 8 ) );
    }
    return fmt::format( "b{} 0x{:08x}",
                        CONDITIONS.at( field( op, 8, 4 ) ),
                        address + 4 + ( signExtend( field( op, 0, 8 ), 8 ) << 1U ) );
  case 0xe:
    if ( bit( op, 11 ) )
    {
      return "undefined";
    }
    return fmt::format( "b 0x{:08x}", address + 4 + ( signExtend( field( op, 0, 11 ), 11 ) << 1U ) );
  default:
    if ( !bit( op, 11 ) )
    {
      if ( ( next & 0xf800U ) == 0xf800U )
      {
        std::uint32_t const target =
            address + 4 + ( signExtend( field( op, 0, 11 ), 11 ) << 12U ) + ( field( next, 0, 11 ) << 1U );
        return fmt::format( "bl 0x{:08x}", target );
      }
      return fmt::format( "bl (first half) lr = pc + 0x{:x}", signExtend( field( op, 0, 11 ), 11 ) << 12U );
    }
    return fmt::format( "bl (second half) lr + 0x{:x}", field( op, 0, 11 ) << 1U );
  }
}

} // namespace pgm::cpu
