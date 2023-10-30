#include "thismsgpack.hpp"
#include <cstring>

MallocStream::MallocStream(size_t size)
{
this->_data = this->_end = static_cast<unsigned char *>(malloc(size));
this->capacity_end = this->_end + size;
}

void MallocStream::write(unsigned char const *value, size_t size)
{
  ensure_have_n_free_bytes(size);
  memcpy(this->_end, value, size);
  this->_end += size;
}

void MallocStream::ensure_have_n_free_bytes(unsigned int n)
{
  size_t const bytes_left = this->capacity_end - this->_end;
  if (bytes_left < n)
  {
    size_t const cur_capacity = this->capacity_end - this->_data;
    size_t const cur_size = cur_capacity - bytes_left;
    size_t const new_size = cur_capacity * 2;
    this->_data = static_cast<unsigned char *>(realloc(this->_data, new_size));
    if (!this->_data) {
      throw std::runtime_error("MallocStream out of memory");  // ToDo: cleanup
    }
    this->_end = this->_data + cur_size;
    this->capacity_end = this->_data + new_size;
  }
}

namespace {
template <typename T>
constexpr inline T htonT(T value) noexcept
{
#ifndef __BIG_ENDIAN__
  char* ptr = reinterpret_cast<char*>(&value);
  std::reverse(ptr, ptr + sizeof(T));
#endif
  return value;
}

template <typename T>
void inline _write_raw(const T val, MallocStream& out)
{
  const T net_val = htonT(val);
  out.write(reinterpret_cast<uint8_t const*>(&net_val), sizeof(net_val));
}

template <>
void inline _write_raw<uint8_t>(const uint8_t val, MallocStream& out)
{
  out.write(&val, sizeof(uint8_t));
}

}

namespace thismsgpack {

void pack_array_header(size_t const n, MallocStream& out)
{
  if (n <= 0x0f) {
    out.write(static_cast<uint8_t>(0x90 + n));
  }
  else if (n <= 0xffff) {
    out.write(static_cast<uint8_t>(0xdc));
    _write_raw(static_cast<uint16_t>(n), out);
  }
  else if (n <= 0xffffffff) {
    out.write('\xdd');
    _write_raw(static_cast<uint32_t>(n), out);
  }
  else {
    throw std::runtime_error("Array is too large");
  }
}


// pack `double`
void pack(double const val, MallocStream& out)
{
  out.write('\xcb');
  _write_raw(val, out);
}

// pack `float`
void pack(float const val, MallocStream& out)
{
  out.write('\xca');
  _write_raw(val, out);
}

// pack `int64`
void pack(int64_t const val, MallocStream& out) {
  if (0 <= val && val < 0x80) {
    out.write(static_cast<uint8_t>(val));
  }
  else if (-0x20 <= val && val < 0) {
    out.write(static_cast<uint8_t>(val));
  }
  else if (0x80 <= val && val <= std::numeric_limits<uint8_t>::max()) {
    out.write('\xcc');  // uint 8
    out.write(static_cast<uint8_t>(val));
  }
  else if (std::numeric_limits<int8_t>::min() <= val && val < 0) {
    out.write('\xd0');  // int 8
    out.write(static_cast<uint8_t>(val));
  }
  else if (0xff < val && val <= std::numeric_limits<uint16_t>::max()) {
    out.write('\xcd');  // uint 16
    _write_raw(static_cast<uint16_t>(val), out);
  }
  else if (std::numeric_limits<int16_t>::min() <= val && val < -0x80) {
    out.write('\xd1'); // int 16
    _write_raw(static_cast<uint16_t>(val), out);
  }
  else if (0xffff < val && val <= std::numeric_limits<uint32_t>::max()) {
    out.write('\xce');  // uint 32
    _write_raw(static_cast<uint32_t>(val), out);
  }  //     0x80000000
  else if (std::numeric_limits<int32_t>::min() <= val && val < -0x8000) {
    out.write('\xd2');  // int32
    _write_raw(static_cast<uint32_t>(val), out);
  }
  else if (0xffffffff < val /*&& val <= 0xffffffffffffffff*/) {
    out.write('\xcf');  // uint 64
    _write_raw(static_cast<uint64_t>(val), out);
  }
  else if (/*-0x8000000000000000 <= val &&*/ val < -0x80000000LL) {
    out.write('\xd3');  // int 64
    _write_raw(static_cast<uint64_t>(val), out);
  }
  else {
    throw std::runtime_error("Integer value out of range (impossible)");
  }
}
}