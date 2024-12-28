#pragma once
#include <algorithm>  // std::reverse()
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

class MallocStream {
  unsigned char* _data;
  unsigned char* _end;
  unsigned char* capacity_end;

public:
  MallocStream(size_t size);
  // ~MallocStream()
  // {
  //   if (this->_data) {
  //     throw std::runtime_error("MallocStream data must be release");
  //   }
  // }

  void write(unsigned char const* value, size_t size);

  template <typename T, size_t Size>
  void write(const T (&bytes)[Size]) {
    this->write(reinterpret_cast<unsigned char const*>(bytes), Size);
  }

  template <typename T>
  void write(T const value) {
    static_assert(std::is_integral<T>::value, "Integral required.");
    this->write(reinterpret_cast<unsigned char const*>(&value), sizeof(T));
  }

  unsigned char* const data() {
    return this->_data;
  }

  size_t size() {
    return this->_end - this->_data;
  }

private:
  void ensure_have_n_free_bytes(unsigned int n);
};

namespace thismsgpack {

void pack_array_header(size_t const n, MallocStream& out);

// pack `double`
void pack(double const val, MallocStream& out);

// pack `float`
void pack(float const val, MallocStream& out);

// pack `int64`
void pack(int64_t const val, MallocStream& out);
}  // namespace thismsgpack