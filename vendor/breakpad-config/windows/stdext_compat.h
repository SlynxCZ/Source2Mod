// Compatibility shim for breakpad's Windows client under MSVC 14.50+.
//
// minidump_generator.cc copies handle-operation records into a byte buffer
// through stdext::checked_array_iterator. That iterator was deprecated in
// VS 2022 17.8 and removed outright from the STL shipped with VS 2026
// (MSVC 14.50, _MSC_VER 1950), so the file no longer compiles there.
//
// This header is force-included into that one translation unit and provides
// a drop-in replacement with the same shape: a random-access iterator over a
// raw pointer that asserts the write stays inside the declared length. On
// older toolsets the real stdext::checked_array_iterator is still available
// and this header does nothing.
#pragma once

#if defined(_MSC_VER) && _MSC_VER >= 1950

#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace stdext {

template <class Ptr>
class checked_array_iterator {
 public:
  using iterator_category = std::random_access_iterator_tag;
  using value_type = std::remove_pointer_t<Ptr>;
  using difference_type = std::ptrdiff_t;
  using pointer = Ptr;
  using reference = value_type&;

  checked_array_iterator() = default;
  checked_array_iterator(Ptr array, size_t size, size_t index = 0)
      : array_(array), size_(size), index_(index) {
    assert(index <= size);
  }

  Ptr base() const { return array_ + index_; }

  reference operator*() const {
    assert(index_ < size_);
    return array_[index_];
  }
  pointer operator->() const { return &**this; }
  reference operator[](difference_type off) const { return *(*this + off); }

  checked_array_iterator& operator++() {
    assert(index_ < size_);
    ++index_;
    return *this;
  }
  checked_array_iterator operator++(int) {
    checked_array_iterator tmp = *this;
    ++*this;
    return tmp;
  }
  checked_array_iterator& operator--() {
    assert(index_ > 0);
    --index_;
    return *this;
  }
  checked_array_iterator operator--(int) {
    checked_array_iterator tmp = *this;
    --*this;
    return tmp;
  }
  checked_array_iterator& operator+=(difference_type off) {
    assert(off >= 0 ? index_ + off <= size_
                    : static_cast<size_t>(-off) <= index_);
    index_ += off;
    return *this;
  }
  checked_array_iterator& operator-=(difference_type off) {
    return *this += -off;
  }
  checked_array_iterator operator+(difference_type off) const {
    checked_array_iterator tmp = *this;
    return tmp += off;
  }
  checked_array_iterator operator-(difference_type off) const {
    checked_array_iterator tmp = *this;
    return tmp -= off;
  }
  difference_type operator-(const checked_array_iterator& other) const {
    assert(array_ == other.array_);
    return static_cast<difference_type>(index_) -
           static_cast<difference_type>(other.index_);
  }

  bool operator==(const checked_array_iterator& o) const {
    return array_ == o.array_ && index_ == o.index_;
  }
  bool operator!=(const checked_array_iterator& o) const { return !(*this == o); }
  bool operator<(const checked_array_iterator& o) const { return index_ < o.index_; }
  bool operator>(const checked_array_iterator& o) const { return o < *this; }
  bool operator<=(const checked_array_iterator& o) const { return !(o < *this); }
  bool operator>=(const checked_array_iterator& o) const { return !(*this < o); }

 private:
  Ptr array_ = nullptr;
  size_t size_ = 0;
  size_t index_ = 0;
};

}  // namespace stdext

#endif  // _MSC_VER >= 1950
