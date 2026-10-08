#pragma once

// The two directions of a save state. Each part of the machine names its state
// once, in a serialize() that takes either archive: the writer copies each field
// out, the reader copies it back in, in the same order. Fields are copied as
// their bytes, so only trivially copyable ones are taken, and a state is read
// back by the build that wrote it: Machine puts a format version in front.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

namespace pgm::machine
{

class StateWriter
{
public:
  template <class T>
    requires std::is_trivially_copyable_v<T>
  void operator()( T const& value )
  {
    std::size_t const at = mBytes.size();
    mBytes.resize( at + sizeof( T ) );
    std::memcpy( mBytes.data() + at, &value, sizeof( T ) );
  }

  template <class T>
    requires std::is_trivially_copyable_v<T>
  void operator()( std::vector<T> const& values )
  {
    ( *this )( static_cast<std::uint64_t>( values.size() ) );
    std::size_t const at = mBytes.size();
    mBytes.resize( at + ( values.size() * sizeof( T ) ) );
    if ( !values.empty() )
    {
      std::memcpy( mBytes.data() + at, values.data(), values.size() * sizeof( T ) );
    }
  }

  [[nodiscard]] std::vector<std::uint8_t> const& bytes() const
  {
    return mBytes;
  }

private:
  std::vector<std::uint8_t> mBytes;
};

class StateReader
{
public:
  explicit StateReader( std::span<std::uint8_t const> bytes ) : mBytes{ bytes } {}

  template <class T>
    requires std::is_trivially_copyable_v<T>
  void operator()( T& value )
  {
    if ( take( sizeof( T ) ) )
    {
      std::memcpy( &value, mBytes.data() + mAt - sizeof( T ), sizeof( T ) );
    }
  }

  template <class T>
    requires std::is_trivially_copyable_v<T>
  void operator()( std::vector<T>& values )
  {
    std::uint64_t size = 0;
    ( *this )( size );
    if ( !mGood || size > ( mBytes.size() - mAt ) / sizeof( T ) )
    {
      mGood = false;
      return;
    }
    values.resize( static_cast<std::size_t>( size ) );
    if ( take( values.size() * sizeof( T ) ) && !values.empty() )
    {
      std::memcpy( values.data(), mBytes.data() + mAt - ( values.size() * sizeof( T ) ), values.size() * sizeof( T ) );
    }
  }

  /// Whether every field read was there.
  [[nodiscard]] bool good() const
  {
    return mGood;
  }

  /// Whether every byte was read.
  [[nodiscard]] bool atEnd() const
  {
    return mAt == mBytes.size();
  }

private:
  bool take( std::size_t size )
  {
    if ( !mGood || size > mBytes.size() - mAt )
    {
      mGood = false;
      return false;
    }
    mAt += size;
    return true;
  }

  std::span<std::uint8_t const> mBytes;
  std::size_t mAt{};
  bool mGood{ true };
};

} // namespace pgm::machine
