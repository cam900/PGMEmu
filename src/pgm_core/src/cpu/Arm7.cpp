#include "Arm7.hpp"

#include <bit>

namespace pgm::cpu
{

namespace
{

constexpr unsigned SP = 13;
constexpr unsigned LR = 14;
constexpr unsigned PC = 15;

// The exception vectors.
constexpr std::uint32_t VECTOR_RESET = 0x00;
constexpr std::uint32_t VECTOR_UNDEFINED = 0x04;
constexpr std::uint32_t VECTOR_SWI = 0x08;
constexpr std::uint32_t VECTOR_IRQ = 0x18;
constexpr std::uint32_t VECTOR_FIQ = 0x1c;

// Data-processing operations.
constexpr unsigned OP_AND = 0x0;
constexpr unsigned OP_EOR = 0x1;
constexpr unsigned OP_SUB = 0x2;
constexpr unsigned OP_RSB = 0x3;
constexpr unsigned OP_ADD = 0x4;
constexpr unsigned OP_ADC = 0x5;
constexpr unsigned OP_SBC = 0x6;
constexpr unsigned OP_RSC = 0x7;
constexpr unsigned OP_TST = 0x8;
constexpr unsigned OP_TEQ = 0x9;
constexpr unsigned OP_CMP = 0xa;
constexpr unsigned OP_CMN = 0xb;
constexpr unsigned OP_ORR = 0xc;
constexpr unsigned OP_MOV = 0xd;
constexpr unsigned OP_BIC = 0xe;

constexpr bool bit( std::uint32_t value, unsigned index )
{
  return ( ( value >> index ) & 1U ) != 0;
}

constexpr std::uint32_t field( std::uint32_t value, unsigned low, unsigned width )
{
  return ( value >> low ) & ( ( 1U << width ) - 1U );
}

constexpr std::uint32_t signExtend( std::uint32_t value, unsigned bits )
{
  std::uint32_t const sign = 1U << ( bits - 1 );
  return ( value ^ sign ) - sign;
}

/// The index into Arm7State::spsr of `mode`'s SPSR, or -1 for a mode without one.
constexpr int spsrIndex( std::uint32_t mode )
{
  switch ( mode )
  {
  case Arm7::MODE_FIQ:
    return 0;
  case Arm7::MODE_SUPERVISOR:
    return 1;
  case Arm7::MODE_ABORT:
    return 2;
  case Arm7::MODE_IRQ:
    return 3;
  case Arm7::MODE_UNDEFINED:
    return 4;
  default:
    return -1;
  }
}

} // namespace

Arm7::Arm7( Arm7Bus& bus ) : mBus{ &bus }
{
  bindRegisters();
}

void Arm7::reset()
{
  std::uint32_t const old = mState.cpsr;
  setCpsr( MODE_SUPERVISOR | DISABLE_IRQ | DISABLE_FIQ );
  mState.spsr.at( 1 ) = old;
  *mRegisters[PC] = VECTOR_RESET;
  refill();
}

void Arm7::step()
{
  if ( mState.fiqLine && ( mState.cpsr & DISABLE_FIQ ) == 0 )
  {
    std::uint32_t const next = thumb() ? *mRegisters[PC] : *mRegisters[PC] - 4;
    prefetch();
    enterException( MODE_FIQ, VECTOR_FIQ, next );
    return;
  }
  if ( mState.irqLine && ( mState.cpsr & DISABLE_IRQ ) == 0 )
  {
    std::uint32_t const next = thumb() ? *mRegisters[PC] : *mRegisters[PC] - 4;
    prefetch();
    enterException( MODE_IRQ, VECTOR_IRQ, next );
    return;
  }

  std::uint32_t const opcode = mState.pipeline[0];
  mState.pipeline[0] = mState.pipeline[1];
  if ( thumb() )
  {
    executeThumb( opcode & 0xffffU );
    return;
  }
  if ( !conditionHolds( opcode >> 28U ) )
  {
    prefetch();
    return;
  }
  executeArm( opcode );
}

void Arm7::setFiq( bool asserted )
{
  mState.fiqLine = asserted;
}

void Arm7::setIrq( bool asserted )
{
  mState.irqLine = asserted;
}

Arm7State const& Arm7::state() const
{
  return mState;
}

void Arm7::setState( Arm7State const& state )
{
  mState = state;
  bindRegisters();
}

std::uint32_t Arm7::reg( unsigned index ) const
{
  return *mRegisters.at( index );
}

std::uint32_t Arm7::pc() const
{
  return *mRegisters[PC] - ( thumb() ? 4U : 8U );
}

// ---------------------------------------------------------------------------
// Registers and modes

void Arm7::bindRegisters()
{
  for ( unsigned i = 0; i < 16; ++i )
  {
    mRegisters.at( i ) = &mState.r.at( i );
  }
  switch ( mState.cpsr & MODE_MASK )
  {
  case MODE_FIQ:
    for ( unsigned i = 8; i < 15; ++i )
    {
      mRegisters.at( i ) = &mState.fiq.at( i - 8 );
    }
    break;
  case MODE_SUPERVISOR:
    mRegisters[SP] = &mState.svc.front();
    mRegisters[LR] = &mState.svc.back();
    break;
  case MODE_ABORT:
    mRegisters[SP] = &mState.abt.front();
    mRegisters[LR] = &mState.abt.back();
    break;
  case MODE_IRQ:
    mRegisters[SP] = &mState.irq.front();
    mRegisters[LR] = &mState.irq.back();
    break;
  case MODE_UNDEFINED:
    mRegisters[SP] = &mState.und.front();
    mRegisters[LR] = &mState.und.back();
    break;
  default:
    break;
  }
}

void Arm7::setCpsr( std::uint32_t value )
{
  mState.cpsr = value;
  bindRegisters();
}

std::uint32_t* Arm7::spsr()
{
  int const index = spsrIndex( mState.cpsr & MODE_MASK );
  return index < 0 ? nullptr : &mState.spsr.at( static_cast<std::size_t>( index ) );
}

bool Arm7::thumb() const
{
  return ( mState.cpsr & THUMB ) != 0;
}

bool Arm7::conditionHolds( std::uint32_t condition ) const
{
  std::uint32_t const cpsr = mState.cpsr;
  bool const n = ( cpsr & FLAG_N ) != 0;
  bool const z = ( cpsr & FLAG_Z ) != 0;
  bool const c = ( cpsr & FLAG_C ) != 0;
  bool const v = ( cpsr & FLAG_V ) != 0;
  switch ( condition )
  {
  case 0x0:
    return z;
  case 0x1:
    return !z;
  case 0x2:
    return c;
  case 0x3:
    return !c;
  case 0x4:
    return n;
  case 0x5:
    return !n;
  case 0x6:
    return v;
  case 0x7:
    return !v;
  case 0x8:
    return c && !z;
  case 0x9:
    return !c || z;
  case 0xa:
    return n == v;
  case 0xb:
    return n != v;
  case 0xc:
    return !z && n == v;
  case 0xd:
    return z || n != v;
  case 0xe:
    return true;
  default:
    return false;
  }
}

void Arm7::setNz( std::uint32_t result )
{
  mState.cpsr = ( mState.cpsr & ~( FLAG_N | FLAG_Z ) ) | ( result & FLAG_N ) | ( result == 0 ? FLAG_Z : 0 );
}

// ---------------------------------------------------------------------------
// The pipeline and the bus

void Arm7::prefetch()
{
  unsigned const size = thumb() ? 2 : 4;
  std::uint32_t& pc = *mRegisters[PC];
  mState.pipeline[1] =
      mBus->read( pc & ~( size - 1 ), size, Arm7Bus::CODE | ( mState.fetchSequential ? Arm7Bus::SEQUENTIAL : 0 ) );
  ++mState.cycles;
  mState.fetchSequential = true;
  pc += size;
}

void Arm7::refill()
{
  unsigned const size = thumb() ? 2 : 4;
  std::uint32_t& pc = *mRegisters[PC];
  mState.pipeline[0] = mBus->read( pc & ~( size - 1 ), size, Arm7Bus::CODE );
  ++mState.cycles;
  pc += size;
  mState.pipeline[1] = mBus->read( pc & ~( size - 1 ), size, Arm7Bus::CODE | Arm7Bus::SEQUENTIAL );
  ++mState.cycles;
  pc += size;
  mState.fetchSequential = true;
}

void Arm7::internalCycle()
{
  ++mState.cycles;
  mState.fetchSequential = false;
}

std::uint32_t Arm7::readData( std::uint32_t address, unsigned size, unsigned access )
{
  std::uint32_t const value = mBus->read( address, size, access );
  ++mState.cycles;
  mState.fetchSequential = false;
  return value;
}

void Arm7::writeData( std::uint32_t address, unsigned size, std::uint32_t value, unsigned access )
{
  std::uint32_t const mask = size == 4 ? 0xffffffffU : ( 1U << ( size * 8 ) ) - 1;
  mBus->write( address, size, value & mask, access );
  ++mState.cycles;
  mState.fetchSequential = false;
}

void Arm7::writeRegister( unsigned index, std::uint32_t value )
{
  *mRegisters.at( index ) = value;
  if ( index == PC )
  {
    refill();
  }
}

void Arm7::writeBase( unsigned index, std::uint32_t value )
{
  // R15 written back takes the value the fetch in the instruction's first
  // cycle then moves on, as the suite's generator has it.
  writeRegister( index, index == PC ? value + 4 : value );
}

void Arm7::enterException( std::uint32_t mode, std::uint32_t vector, std::uint32_t returnAddress )
{
  std::uint32_t const old = mState.cpsr;
  std::uint32_t cpsr = ( old & ~( MODE_MASK | THUMB ) ) | mode | DISABLE_IRQ;
  if ( mode == MODE_FIQ )
  {
    cpsr |= DISABLE_FIQ;
  }
  setCpsr( cpsr );
  *spsr() = old;
  *mRegisters[LR] = returnAddress;
  *mRegisters[PC] = vector;
  refill();
}

// ---------------------------------------------------------------------------
// Shared by both instruction sets

Arm7::Shifted Arm7::shiftByImmediate( unsigned type, std::uint32_t value, unsigned amount ) const
{
  bool const carry = ( mState.cpsr & FLAG_C ) != 0;
  switch ( type )
  {
  case 0: // LSL
    if ( amount == 0 )
    {
      return { .value = value, .carry = carry };
    }
    return { .value = value << amount, .carry = bit( value, 32 - amount ) };
  case 1: // LSR; #0 means #32
    if ( amount == 0 )
    {
      return { .value = 0, .carry = bit( value, 31 ) };
    }
    return { .value = value >> amount, .carry = bit( value, amount - 1 ) };
  case 2: // ASR; #0 means #32
    if ( amount == 0 )
    {
      return { .value = bit( value, 31 ) ? 0xffffffffU : 0U, .carry = bit( value, 31 ) };
    }
    return { .value = static_cast<std::uint32_t>( static_cast<std::int32_t>( value ) >> amount ),
             .carry = bit( value, amount - 1 ) };
  default: // ROR; #0 means RRX
    if ( amount == 0 )
    {
      return { .value = ( carry ? 0x80000000U : 0U ) | ( value >> 1U ), .carry = bit( value, 0 ) };
    }
    return { .value = std::rotr( value, static_cast<int>( amount ) ), .carry = bit( value, amount - 1 ) };
  }
}

Arm7::Shifted Arm7::shiftByRegister( unsigned type, std::uint32_t value, unsigned amount ) const
{
  bool const carry = ( mState.cpsr & FLAG_C ) != 0;
  if ( amount == 0 )
  {
    return { .value = value, .carry = carry };
  }
  switch ( type )
  {
  case 0: // LSL
    if ( amount < 32 )
    {
      return { .value = value << amount, .carry = bit( value, 32 - amount ) };
    }
    return { .value = 0, .carry = amount == 32 && bit( value, 0 ) };
  case 1: // LSR
    if ( amount < 32 )
    {
      return { .value = value >> amount, .carry = bit( value, amount - 1 ) };
    }
    return { .value = 0, .carry = amount == 32 && bit( value, 31 ) };
  case 2: // ASR
    if ( amount < 32 )
    {
      return { .value = static_cast<std::uint32_t>( static_cast<std::int32_t>( value ) >> amount ),
               .carry = bit( value, amount - 1 ) };
    }
    return { .value = bit( value, 31 ) ? 0xffffffffU : 0U, .carry = bit( value, 31 ) };
  default: // ROR
    if ( ( amount & 31U ) == 0 )
    {
      return { .value = value, .carry = bit( value, 31 ) };
    }
    return { .value = std::rotr( value, static_cast<int>( amount & 31U ) ),
             .carry = bit( value, ( amount & 31U ) - 1 ) };
  }
}

std::uint32_t Arm7::alu( unsigned operation, std::uint32_t first, Shifted second, bool setFlags )
{
  bool const carryIn = ( mState.cpsr & FLAG_C ) != 0;
  std::uint32_t const b = second.value;
  bool logical = false;
  std::uint32_t result = 0;
  bool carry = false;
  bool overflow = false;
  // a + b + carry, its carry out and its overflow.
  auto const add = [&]( std::uint32_t x, std::uint32_t y, bool in )
  {
    std::uint64_t const sum = std::uint64_t{ x } + y + ( in ? 1U : 0U );
    result = static_cast<std::uint32_t>( sum );
    carry = ( sum >> 32U ) != 0;
    overflow = ( ( ~( x ^ y ) & ( x ^ result ) ) >> 31U ) != 0;
  };
  switch ( operation )
  {
  case OP_AND:
  case OP_TST:
    result = first & b;
    logical = true;
    break;
  case OP_EOR:
  case OP_TEQ:
    result = first ^ b;
    logical = true;
    break;
  case OP_SUB:
  case OP_CMP:
    add( first, ~b, true );
    break;
  case OP_RSB:
    add( b, ~first, true );
    break;
  case OP_ADD:
  case OP_CMN:
    add( first, b, false );
    break;
  case OP_ADC:
    add( first, b, carryIn );
    break;
  case OP_SBC:
    add( first, ~b, carryIn );
    break;
  case OP_RSC:
    add( b, ~first, carryIn );
    break;
  case OP_ORR:
    result = first | b;
    logical = true;
    break;
  case OP_MOV:
    result = b;
    logical = true;
    break;
  case OP_BIC:
    result = first & ~b;
    logical = true;
    break;
  default: // MVN
    result = ~b;
    logical = true;
    break;
  }
  if ( setFlags )
  {
    setNz( result );
    std::uint32_t cpsr = mState.cpsr & ~FLAG_C;
    if ( logical )
    {
      cpsr |= second.carry ? FLAG_C : 0;
    }
    else
    {
      cpsr = ( cpsr & ~FLAG_V ) | ( carry ? FLAG_C : 0 ) | ( overflow ? FLAG_V : 0 );
    }
    mState.cpsr = cpsr;
  }
  return result;
}

int Arm7::multiplyCycles( std::uint32_t multiplier, bool isSigned )
{
  // The multiplier is consumed 8 bits a cycle, until what is left of it is
  // all zeros, or, signed, all ones.
  for ( int cycles = 1; cycles < 4; ++cycles )
  {
    std::uint32_t const rest = multiplier >> ( cycles * 8 );
    std::uint32_t const ones = 0xffffffffU >> ( cycles * 8 );
    if ( rest == 0 || ( isSigned && rest == ones ) )
    {
      return cycles;
    }
  }
  return 4;
}

void Arm7::transferBlock( Block const& block )
{
  std::uint16_t list = block.list;
  std::uint32_t bytes = static_cast<std::uint32_t>( std::popcount( list ) ) * 4;
  if ( list == 0 )
  {
    // An empty list moves R15 alone, and the base as if all sixteen moved.
    list = 1U << PC;
    bytes = 0x40;
  }
  std::uint32_t const base = *mRegisters.at( block.base );
  std::uint32_t address = 0;
  std::uint32_t final = 0;
  if ( block.up )
  {
    address = base + ( block.preIndex ? 4U : 0U );
    final = base + bytes;
  }
  else
  {
    address = base - bytes + ( block.preIndex ? 0U : 4U );
    final = base - bytes;
  }
  bool const loadsPc = block.load && bit( list, PC );
  // S moves the user bank's registers, except where an LDM loads R15: that
  // returns from an exception instead.
  bool const userBank = block.userBank && !loadsPc;
  auto const target = [&]( unsigned index ) -> std::uint32_t&
  { return userBank ? mState.r.at( index ) : *mRegisters.at( index ); };

  prefetch();
  if ( block.load )
  {
    // A loaded base wins over the written-back one.
    if ( block.writeBack )
    {
      target( block.base ) = final;
    }
    unsigned access = 0;
    for ( unsigned i = 0; i < 16; ++i )
    {
      if ( bit( list, i ) )
      {
        target( i ) = readData( address, 4, access );
        access = Arm7Bus::SEQUENTIAL;
        address += 4;
      }
    }
    internalCycle();
    if ( loadsPc )
    {
      if ( block.userBank && spsr() != nullptr )
      {
        setCpsr( *spsr() );
      }
      if ( block.alignPc )
      {
        *mRegisters[PC] &= ~1U;
      }
      refill();
    }
    else if ( block.writeBack && block.base == PC )
    {
      refill();
    }
    return;
  }

  // A store writes the base back after its first transfer, so that a base
  // stored later in the list is stored written back.
  unsigned access = 0;
  bool first = true;
  for ( unsigned i = 0; i < 16; ++i )
  {
    if ( bit( list, i ) )
    {
      std::uint32_t const value = target( i );
      writeData( address, 4, value, access );
      access = Arm7Bus::SEQUENTIAL;
      address += 4;
      if ( first && block.writeBack )
      {
        target( block.base ) = final;
      }
      first = false;
    }
  }
  if ( block.writeBack && block.base == PC )
  {
    refill();
  }
}

std::uint32_t Arm7::loadedWord( std::uint32_t data, std::uint32_t address )
{
  return std::rotr( data, static_cast<int>( ( address & 3U ) * 8 ) );
}

std::uint32_t Arm7::loadedHalfword( std::uint32_t data, std::uint32_t address, bool isSigned )
{
  data &= 0xffffU;
  if ( ( address & 1U ) == 0 )
  {
    return isSigned ? signExtend( data, 16 ) : data;
  }
  // Misaligned: LDRH rotates the halfword, LDRSH takes its high byte.
  return isSigned ? signExtend( data >> 8U, 8 ) : std::rotr( data, 8 );
}

std::uint32_t Arm7::loadedByte( std::uint32_t data, bool isSigned )
{
  data &= 0xffU;
  return isSigned ? signExtend( data, 8 ) : data;
}

// ---------------------------------------------------------------------------
// ARM state

void Arm7::executeArm( std::uint32_t opcode )
{
  if ( ( opcode & 0x0ffffff0U ) == 0x012fff10U )
  {
    branchExchange( opcode );
  }
  else if ( ( opcode & 0x0fb00ff0U ) == 0x01000090U )
  {
    singleDataSwap( opcode );
  }
  else if ( ( opcode & 0x0fc000f0U ) == 0x00000090U )
  {
    multiply( opcode );
  }
  else if ( ( opcode & 0x0f8000f0U ) == 0x00800090U )
  {
    multiplyLong( opcode );
  }
  else if ( ( opcode & 0x0e000090U ) == 0x00000090U && ( opcode & 0x60U ) != 0 )
  {
    halfwordTransfer( opcode );
  }
  else if ( ( opcode & 0x0d900000U ) == 0x01000000U )
  {
    psrTransfer( opcode );
  }
  else if ( ( opcode & 0x0c000000U ) == 0 )
  {
    dataProcessing( opcode );
  }
  else if ( ( opcode & 0x0c000000U ) == 0x04000000U && ( opcode & 0x02000010U ) != 0x02000010U )
  {
    singleDataTransfer( opcode );
  }
  else if ( ( opcode & 0x0e000000U ) == 0x08000000U )
  {
    blockDataTransfer( opcode );
  }
  else if ( ( opcode & 0x0e000000U ) == 0x0a000000U )
  {
    branch( opcode );
  }
  else if ( ( opcode & 0x0f000000U ) == 0x0f000000U )
  {
    softwareInterrupt();
  }
  else
  {
    // The undefined encodings among LDR's and STR's, and a coprocessor's:
    // there is none.
    undefined();
  }
}

void Arm7::dataProcessing( std::uint32_t opcode )
{
  unsigned const operation = field( opcode, 21, 4 );
  bool const setFlags = bit( opcode, 20 );
  unsigned const rn = field( opcode, 16, 4 );
  unsigned const rd = field( opcode, 12, 4 );

  Shifted operand{};
  std::uint32_t first = 0;
  if ( bit( opcode, 25 ) )
  {
    unsigned const rotate = field( opcode, 8, 4 ) * 2;
    std::uint32_t const value = std::rotr( field( opcode, 0, 8 ), static_cast<int>( rotate ) );
    operand = { .value = value, .carry = rotate == 0 ? ( mState.cpsr & FLAG_C ) != 0 : bit( value, 31 ) };
    first = *mRegisters.at( rn );
    prefetch();
  }
  else if ( !bit( opcode, 4 ) )
  {
    operand = shiftByImmediate( field( opcode, 5, 2 ), *mRegisters.at( field( opcode, 0, 4 ) ), field( opcode, 7, 5 ) );
    first = *mRegisters.at( rn );
    prefetch();
  }
  else
  {
    // The shift amount is read with the fetch, R15 8 ahead; the operands in
    // a cycle of their own after it, R15 12 ahead.
    unsigned const amount = *mRegisters.at( field( opcode, 8, 4 ) ) & 0xffU;
    prefetch();
    internalCycle();
    operand = shiftByRegister( field( opcode, 5, 2 ), *mRegisters.at( field( opcode, 0, 4 ) ), amount );
    first = *mRegisters.at( rn );
  }

  bool const compare = operation >= OP_TST && operation <= OP_CMN;
  // S with R15 returns from an exception: the SPSR becomes the CPSR. In user
  // and system modes, which have none, the flags are set as usual.
  bool const restoresCpsr = setFlags && rd == PC && spsr() != nullptr;
  std::uint32_t const result = alu( operation, first, operand, setFlags && !restoresCpsr );
  if ( restoresCpsr )
  {
    setCpsr( *spsr() );
  }
  if ( !compare )
  {
    writeRegister( rd, result );
  }
}

void Arm7::psrTransfer( std::uint32_t opcode )
{
  bool const useSpsr = bit( opcode, 22 );
  if ( !bit( opcode, 21 ) )
  {
    // MRS. Into R15 it moves the pipeline on without refilling it: R15
    // reads the PSR and 4, as the suite's generator has it.
    std::uint32_t const* const saved = useSpsr ? spsr() : nullptr;
    std::uint32_t const value = saved != nullptr ? *saved : mState.cpsr;
    unsigned const rd = field( opcode, 12, 4 );
    prefetch();
    *mRegisters.at( rd ) = rd == PC ? value + 4 : value;
    return;
  }

  // MSR
  std::uint32_t value = 0;
  if ( bit( opcode, 25 ) )
  {
    value = std::rotr( field( opcode, 0, 8 ), static_cast<int>( field( opcode, 8, 4 ) * 2 ) );
  }
  else
  {
    value = *mRegisters.at( field( opcode, 0, 4 ) );
  }
  prefetch();
  std::uint32_t mask = 0;
  for ( unsigned i = 0; i < 4; ++i )
  {
    if ( bit( opcode, 16 + i ) )
    {
      mask |= 0xffU << ( i * 8 );
    }
  }
  if ( useSpsr )
  {
    if ( std::uint32_t* const saved = spsr() )
    {
      *saved = ( *saved & ~mask ) | ( value & mask );
    }
    return;
  }
  if ( ( mState.cpsr & MODE_MASK ) == MODE_USER )
  {
    mask &= 0xff000000U;
  }
  // The mode's top bit is always set: the ARMv4T has no 26-bit modes.
  setCpsr( ( mState.cpsr & ~mask ) | ( value & mask ) | 0x10U );
}

void Arm7::multiply( std::uint32_t opcode )
{
  bool const accumulate = bit( opcode, 21 );
  unsigned const rd = field( opcode, 16, 4 );
  // The operands are read after the fetch: R15 reads 12 ahead.
  prefetch();
  std::uint32_t const multiplier = *mRegisters.at( field( opcode, 8, 4 ) );
  std::uint32_t result = *mRegisters.at( field( opcode, 0, 4 ) ) * multiplier;
  if ( accumulate )
  {
    result += *mRegisters.at( field( opcode, 12, 4 ) );
  }
  int const cycles = multiplyCycles( multiplier, true ) + ( accumulate ? 1 : 0 );
  for ( int i = 0; i < cycles; ++i )
  {
    internalCycle();
  }
  if ( bit( opcode, 20 ) )
  {
    setNz( result );
  }
  writeRegister( rd, result );
}

void Arm7::multiplyLong( std::uint32_t opcode )
{
  bool const isSigned = bit( opcode, 22 );
  bool const accumulate = bit( opcode, 21 );
  unsigned const high = field( opcode, 16, 4 );
  unsigned const low = field( opcode, 12, 4 );
  prefetch();
  std::uint32_t const multiplier = *mRegisters.at( field( opcode, 8, 4 ) );
  std::uint32_t const multiplicand = *mRegisters.at( field( opcode, 0, 4 ) );
  std::uint64_t result = 0;
  if ( isSigned )
  {
    result = static_cast<std::uint64_t>( static_cast<std::int64_t>( static_cast<std::int32_t>( multiplicand ) ) *
                                         static_cast<std::int32_t>( multiplier ) );
  }
  else
  {
    result = std::uint64_t{ multiplicand } * multiplier;
  }
  if ( accumulate )
  {
    result += ( std::uint64_t{ *mRegisters.at( high ) } << 32U ) | *mRegisters.at( low );
  }
  int const cycles = multiplyCycles( multiplier, isSigned ) + ( accumulate ? 2 : 1 );
  for ( int i = 0; i < cycles; ++i )
  {
    internalCycle();
  }
  if ( bit( opcode, 20 ) )
  {
    mState.cpsr = ( mState.cpsr & ~( FLAG_N | FLAG_Z ) ) | ( static_cast<std::uint32_t>( result >> 32U ) & FLAG_N ) |
                  ( result == 0 ? FLAG_Z : 0 );
  }
  // Both halves are written before R15 is, if either is it, refilled from.
  *mRegisters.at( low ) = static_cast<std::uint32_t>( result );
  *mRegisters.at( high ) = static_cast<std::uint32_t>( result >> 32U );
  if ( low == PC || high == PC )
  {
    refill();
  }
}

void Arm7::singleDataSwap( std::uint32_t opcode )
{
  unsigned const size = bit( opcode, 22 ) ? 1 : 4;
  prefetch();
  std::uint32_t const address = *mRegisters.at( field( opcode, 16, 4 ) );
  std::uint32_t const source = *mRegisters.at( field( opcode, 0, 4 ) );
  std::uint32_t const data = readData( address, size, 0 );
  writeData( address, size, source, Arm7Bus::LOCK );
  internalCycle();
  writeRegister( field( opcode, 12, 4 ), size == 1 ? loadedByte( data, false ) : loadedWord( data, address ) );
}

void Arm7::branchExchange( std::uint32_t opcode )
{
  std::uint32_t const target = *mRegisters.at( field( opcode, 0, 4 ) );
  prefetch();
  setCpsr( ( mState.cpsr & ~THUMB ) | ( bit( target, 0 ) ? THUMB : 0 ) );
  writeRegister( PC, target & ~1U );
}

void Arm7::halfwordTransfer( std::uint32_t opcode )
{
  bool const preIndex = bit( opcode, 24 );
  bool const up = bit( opcode, 23 );
  bool const writeBack = bit( opcode, 21 ) || !preIndex;
  bool const load = bit( opcode, 20 );
  unsigned const rn = field( opcode, 16, 4 );
  unsigned const rd = field( opcode, 12, 4 );
  unsigned const kind = field( opcode, 5, 2 ); // 1 H, 2 SB, 3 SH

  std::uint32_t const offset = bit( opcode, 22 ) ? ( field( opcode, 8, 4 ) << 4U ) | field( opcode, 0, 4 )
                                                 : *mRegisters.at( field( opcode, 0, 4 ) );
  std::uint32_t const base = *mRegisters.at( rn );
  std::uint32_t const moved = up ? base + offset : base - offset;
  std::uint32_t const address = preIndex ? moved : base;
  prefetch();

  if ( !load )
  {
    writeData( address, 2, *mRegisters.at( rd ), 0 );
    if ( writeBack )
    {
      writeBase( rn, moved );
    }
    return;
  }
  std::uint32_t value = 0;
  if ( kind == 2 )
  {
    value = loadedByte( readData( address, 1, 0 ), true );
  }
  else
  {
    value = loadedHalfword( readData( address, 2, 0 ), address, kind == 3 );
  }
  internalCycle();
  if ( writeBack && rn != rd )
  {
    writeBase( rn, moved );
  }
  writeRegister( rd, value );
}

void Arm7::singleDataTransfer( std::uint32_t opcode )
{
  bool const preIndex = bit( opcode, 24 );
  bool const up = bit( opcode, 23 );
  unsigned const size = bit( opcode, 22 ) ? 1 : 4;
  bool const writeBack = bit( opcode, 21 ) || !preIndex;
  bool const load = bit( opcode, 20 );
  unsigned const rn = field( opcode, 16, 4 );
  unsigned const rd = field( opcode, 12, 4 );

  std::uint32_t offset = field( opcode, 0, 12 );
  if ( bit( opcode, 25 ) )
  {
    offset =
        shiftByImmediate( field( opcode, 5, 2 ), *mRegisters.at( field( opcode, 0, 4 ) ), field( opcode, 7, 5 ) ).value;
  }
  std::uint32_t const base = *mRegisters.at( rn );
  std::uint32_t const moved = up ? base + offset : base - offset;
  std::uint32_t const address = preIndex ? moved : base;
  prefetch();

  if ( !load )
  {
    writeData( address, size, *mRegisters.at( rd ), 0 );
    if ( writeBack )
    {
      writeBase( rn, moved );
    }
    return;
  }
  std::uint32_t const data = readData( address, size, 0 );
  internalCycle();
  // A base loaded wins over the base written back.
  if ( writeBack && rn != rd )
  {
    writeBase( rn, moved );
  }
  writeRegister( rd, size == 1 ? loadedByte( data, false ) : loadedWord( data, address ) );
}

void Arm7::blockDataTransfer( std::uint32_t opcode )
{
  transferBlock( Block{ .base = field( opcode, 16, 4 ),
                        .list = static_cast<std::uint16_t>( opcode ),
                        .load = bit( opcode, 20 ),
                        .preIndex = bit( opcode, 24 ),
                        .up = bit( opcode, 23 ),
                        .writeBack = bit( opcode, 21 ),
                        .userBank = bit( opcode, 22 ),
                        .alignPc = false } );
}

void Arm7::branch( std::uint32_t opcode )
{
  std::uint32_t const pc = *mRegisters[PC];
  std::uint32_t const target = pc + ( signExtend( field( opcode, 0, 24 ), 24 ) << 2U );
  prefetch();
  if ( bit( opcode, 24 ) )
  {
    *mRegisters[LR] = pc - 4;
  }
  writeRegister( PC, target );
}

void Arm7::undefined()
{
  std::uint32_t const next = *mRegisters[PC] - ( thumb() ? 2U : 4U );
  prefetch();
  enterException( MODE_UNDEFINED, VECTOR_UNDEFINED, next );
}

void Arm7::softwareInterrupt()
{
  std::uint32_t const next = *mRegisters[PC] - ( thumb() ? 2U : 4U );
  prefetch();
  enterException( MODE_SUPERVISOR, VECTOR_SWI, next );
}

} // namespace pgm::cpu
