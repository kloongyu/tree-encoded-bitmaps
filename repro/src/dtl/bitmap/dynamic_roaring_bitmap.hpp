#pragma once
//===----------------------------------------------------------------------===//
#include <roaring/roaring.hh>

#include <dtl/dtl.hpp>

#include <boost/dynamic_bitset.hpp>

#include <cstddef>
//===----------------------------------------------------------------------===//
namespace dtl {
//===----------------------------------------------------------------------===//
/// Compressed representation of a bitmap of length N.
/// Wraps a Roaring bitmap.
struct dynamic_roaring_bitmap {
  Roaring bitmap_;
  std::size_t size_;

  dynamic_roaring_bitmap() = default; // NOLINT

  explicit dynamic_roaring_bitmap(const boost::dynamic_bitset<$u32>& in)
      : size_(in.size()) {
    auto i = in.find_first();
    while (i != boost::dynamic_bitset<$u32>::npos) {
      bitmap_.add(i);
      i = in.find_next(i);
    }
    bitmap_.runOptimize();
    bitmap_.shrinkToFit();
  }

  /// Constructs an empty bitmap of size n. This kind of constructor is only
  /// available when the current type is suitable as a differential data
  /// structure.
  explicit dynamic_roaring_bitmap(std::size_t n)
      : size_(n) { };

  ~dynamic_roaring_bitmap() = default;
  dynamic_roaring_bitmap(const dynamic_roaring_bitmap& other) = default;
  dynamic_roaring_bitmap(dynamic_roaring_bitmap&& other) noexcept = default;

  __forceinline__ dynamic_roaring_bitmap&
  operator=(const dynamic_roaring_bitmap& other) = default;

  __forceinline__ dynamic_roaring_bitmap&
  operator=(dynamic_roaring_bitmap&& other) noexcept = default;

  /// Return the size in bytes.
  std::size_t
  size_in_bytes() const {
    return bitmap_.getSizeInBytes(false) /* size of the compressed bitmap */
        + sizeof(size_) /* bit-length of the original bitmap */;
  }

  /// Conversion to an std::bitset.
  boost::dynamic_bitset<$u32>
  to_bitset() {
    boost::dynamic_bitset<$u32> ret(size_);
    for (Roaring::const_iterator i = bitmap_.begin(); i != bitmap_.end(); i++) {
      ret[*i] = true;
    }
    return ret;
  }

  /// Bitwise AND
  dynamic_roaring_bitmap __forceinline__
  operator&(const dynamic_roaring_bitmap& other) const {
    dynamic_roaring_bitmap ret(*this);
    ret.bitmap_ &= other.bitmap_;
    return ret;
  }

  /// Bitwise AND (range encoding)
  dynamic_roaring_bitmap __forceinline__
  and_re(const dynamic_roaring_bitmap& other) const {
    return *this & other; // nothing special here. fall back to AND
  }

  /// Bitwise XOR
  dynamic_roaring_bitmap __forceinline__
  operator^(const dynamic_roaring_bitmap& other) const {
    dynamic_roaring_bitmap ret;
    ret.bitmap_ = bitmap_;
    ret.size_ = size_;
    ret.bitmap_ ^= other.bitmap_;
    return ret;
  }

  /// Bitwise XOR (assignment)
  __forceinline__ dynamic_roaring_bitmap&
  operator^=(const dynamic_roaring_bitmap& other) {
    assert(size() == other.size());
    bitmap_ ^= other.bitmap_;
    return *this;
  }

  /// Bitwise XOR (range encoding)
  __forceinline__ dynamic_roaring_bitmap
  xor_re(const dynamic_roaring_bitmap& other) const {
    return *this ^ other; // nothing special here. fall back to XOR
  }

  /// Computes (a XOR b) & this
  /// Note: this, a and b must be different instances. Otherwise, the behavior
  /// is undefined.
  __forceinline__ dynamic_roaring_bitmap&
  fused_xor_and(const dynamic_roaring_bitmap& a, const dynamic_roaring_bitmap& b) {
    auto x = a ^ b;
    bitmap_ &= x.bitmap_;
    return *this;
  }

  /// Try to reduce the memory consumption. This function is supposed to be
  /// called after the bitmap has been modified.
  __forceinline__ void
  shrink() {
    bitmap_.runOptimize();
    bitmap_.shrinkToFit();
  }

  void
  print(std::ostream& os) const {
    os << "n/a";
  }

  static std::string
  name() {
    return "roaring";
  }

  /// Set the i-th bit to the given value. This function is only available when
  /// the current type is suitable as a differential data structure.
  void __forceinline__
  set(std::size_t i, u1 val) noexcept {
    if (val) {
      bitmap_.add(i);
    }
    else {
      bitmap_.remove(i);
    }
  }

  /// Returns the value of the bit at the position pos.
  u1 __forceinline__
  test(const std::size_t pos) const {
    return bitmap_.contains(pos);
  }

  /// Returns the size of the bitmap.
  std::size_t
  size() const {
    return size_;
  }

  //===--------------------------------------------------------------------===//
  /// 1-fill iterator, with skip support.
  class iter {
    const dynamic_roaring_bitmap& rbm_;

    //===------------------------------------------------------------------===//
    // Iterator state
    //===------------------------------------------------------------------===//
    Roaring::const_iterator roaring_iter;
    Roaring::const_iterator& roaring_iter_end;
    /// Points to the beginning of a 1-fill.
    $u64 pos_;
    /// The length of the current 1-fill
    $u64 length_;
    //===------------------------------------------------------------------===//

  public:
    explicit iter(const dynamic_roaring_bitmap& rbm)
        : rbm_(rbm),
          roaring_iter(rbm.bitmap_.begin()),
          roaring_iter_end(rbm.bitmap_.end()) {
      if (roaring_iter == roaring_iter_end) {
        pos_ = rbm_.size_;
        length_ = 0;
        return;
      }

      pos_ = *roaring_iter;
      ++roaring_iter;
      $u64 end = pos_ + 1;
      while (end == *roaring_iter
          && roaring_iter != roaring_iter_end) {
        ++roaring_iter;
        ++end;
      }
      length_ = end - pos_;
    }

    iter(iter&&) = default;

    void __forceinline__
    next() {
      if (roaring_iter == roaring_iter_end) {
        pos_ = rbm_.size_;
        length_ = 0;
        return;
      }
      pos_ = *roaring_iter;
      ++roaring_iter;
      $u64 end = pos_ + 1;
      while (end == *roaring_iter
          && roaring_iter != roaring_iter_end) {
        ++roaring_iter;
        ++end;
      }
      length_ = end - pos_;
    }

    void __forceinline__
    nav_to(const std::size_t to_pos) {
      if (to_pos >= rbm_.size_) {
        pos_ = rbm_.size_;
        length_ = 0;
        return;
      }
      roaring_iter.equalorlarger(to_pos);
      next();
    }

    void __forceinline__
    skip_to(const std::size_t to_pos) {
      nav_to(to_pos);
    }

    u1 __forceinline__
    end() const noexcept {
      return pos_ == rbm_.size_;
    }

    u64 __forceinline__
    pos() const noexcept {
      return pos_;
    }

    u64 __forceinline__
    length() const noexcept {
      return (length_ > rbm_.size_ ? 0 : length_);
    }
  };

  using skip_iter_type = iter;
  using scan_iter_type = iter;

  skip_iter_type __forceinline__
  it() const {
    return skip_iter_type(*this);
  }

  scan_iter_type __forceinline__
  scan_it() const {
    return scan_iter_type(*this);
  }
  //===--------------------------------------------------------------------===//

  /// Returns the name of the instance including the most important parameters
  /// in JSON.
  std::string
  info() const {
    return "{\"name\":\"" + name() + "\""
        + ",\"n\":" + std::to_string(size_)
        + ",\"size\":" + std::to_string(size_in_bytes())
        + "}";
  }
};
//===----------------------------------------------------------------------===//
} // namespace dtl
