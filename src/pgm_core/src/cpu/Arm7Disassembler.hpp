#pragma once

// Disassembly of the ARM7TDMI's two instruction sets, in ARM's assembler
// syntax, for the debugger and the control protocol's cpu.disassemble.

#include <cstdint>
#include <string>

namespace pgm::cpu
{

/// The ARM instruction `opcode` at `address`; branch targets are absolute.
[[nodiscard]] std::string disassembleArm( std::uint32_t address, std::uint32_t opcode );

/// The Thumb instruction `opcode` at `address`. A BL's first half names the
/// pair's target when `next`, the halfword after it, is its second half.
[[nodiscard]] std::string disassembleThumb( std::uint32_t address, std::uint16_t opcode, std::uint16_t next );

} // namespace pgm::cpu
