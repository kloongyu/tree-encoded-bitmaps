#pragma once

#include <cstddef>
#include <vector>

#include <dtl/dtl.hpp>
#include <dtl/math.hpp>

#include <boost/dynamic_bitset.hpp>

namespace dtl {

//===----------------------------------------------------------------------===//
/// Partitioned position list.
template<typename _block_type = $u32, typename _local_position_t = $u8>
struct partitioned_position_list {

  using position_t = uint32_t;
  using local_position_t = _local_position_t;

  /// Partition meta data.
  struct partition_info {
    /// The index of the first element in the partition.
    position_t begin;
    /// Offset within the concatenated partition vector.
    position_t offset;

    void
    print(std::ostream& os) const {
      os << "["
         << static_cast<u64>(begin) << ","
         << static_cast<u64>(offset)
         << "]";
    }
  };

  /// The number of entries per partition.
  static constexpr std::size_t partition_size =
      1ull << sizeof(local_position_t) * 8;

  /// The actual position list (all partitions concatenated).
  std::vector<local_position_t> positions_;
  /// Partition meta data.
  std::vector<partition_info> partitions_;

  /// The length of the range.
  $u64 n_;

  // TODO make private
  partitioned_position_list() = default;

  explicit
  partitioned_position_list(const boost::dynamic_bitset<_block_type>& in)
    : partitions_(), positions_(), n_(in.size()) {
    std::size_t current_pos = in.find_first();
    while (current_pos < in.size()) {
      push_back(static_cast<position_t>(current_pos));
      current_pos = in.find_next(current_pos);
    }
  }

  ~partitioned_position_list() = default;

  partitioned_position_list(const partitioned_position_list& other) = default;

  partitioned_position_list(partitioned_position_list&& other) noexcept = default;

  __forceinline__ partitioned_position_list&
  operator=(const partitioned_position_list& other) = default;

  __forceinline__ partitioned_position_list&
  operator=(partitioned_position_list&& other) noexcept = default;

  /// Return the size in bytes.
  std::size_t
  size_in_byte() const {
    return partitions_.size() * sizeof(partition_info) /* partitions */
        + sizeof(position_t) /* number of partitions */
        + positions_.size() * sizeof(local_position_t) /* positions */
        + sizeof(position_t) /* number of positions */
        + sizeof(n_) /* bit-length of the original bitmap */;
  }

  /// Return the length of the bitmap.
  std::size_t
  size() const {
    return n_;
  }

  /// Conversion to an boost::dynamic_bitset.
  boost::dynamic_bitset<_block_type>
  to_bitset() {
    boost::dynamic_bitset<_block_type> ret(n_);
    for (const partition_info& part : partitions_) {
      auto curr_local_pos = positions_[part.offset];
      ret[part.begin + curr_local_pos] = true;
      for (std::size_t i = part.offset + 1; i < positions_.size(); ++i) {
        if (positions_[i] <= curr_local_pos) {
          break;
        }
        curr_local_pos = positions_[i];
        ret[part.begin + curr_local_pos] = true;
      }
    }
    return ret;
  }

  /// Bitwise AND
  partitioned_position_list __forceinline__
  operator&(const partitioned_position_list& other) const {
    partitioned_position_list ret;
    ret.n_ = n_;
    auto this_it = positions_.begin();
    const auto this_it_end = positions_.end();
    auto other_it = other.positions_.begin();
    const auto other_it_end = other.positions_.end();
    while (!(this_it == this_it_end || other_it == other_it_end)) {
      if (*this_it == *other_it) {
        ret.positions_.push_back(*this_it);
        ++this_it;
        ++other_it;
      }
      else {
        if (*this_it < *other_it) {
          ++this_it;
        }
        else {
          ++other_it;
        }
      }
    }
    return ret;
  }

  /// Bitwise AND (range encoding)
  partitioned_position_list __forceinline__
  and_re(const partitioned_position_list& other) const {
    return *this & other; // nothing special here. fall back to AND
  }

  /// Bitwise XOR
  partitioned_position_list __forceinline__
  operator^(const partitioned_position_list& other) const {
    partitioned_position_list ret;
    ret.n_ = n_;

    auto this_it = positions_.begin();
    const auto this_it_end = positions_.end();
    auto other_it = other.positions_.begin();
    const auto other_it_end = other.positions_.end();
    while (!(this_it == this_it_end || other_it == other_it_end)) {
      if (*this_it < *other_it) {
        ret.positions_.push_back(*this_it);
        ++this_it;
      }
      else if (*other_it < *this_it) {
        ret.positions_.push_back(*other_it);
        ++other_it;
      }
      else {
        ++this_it;
        ++other_it;
      }
    }
    if (this_it != this_it_end) {
      while (this_it != this_it_end) {
        ret.positions_.push_back(*this_it);
        ++this_it;
      }
    }
    if (other_it != other_it_end) {
      while (other_it != other_it_end) {
        ret.positions_.push_back(*other_it);
        ++other_it;
      }
    }
    return ret;
  }

  /// Bitwise XOR (range encoding)
  partitioned_position_list __forceinline__
  xor_re(const partitioned_position_list& other) const {
    return *this ^ other; // nothing special here. fall back to XOR
  }

  /// Computes (a XOR b) & this
  /// Note: this, a and b must be different instances. Otherwise, the behavior
  /// is undefined.
  __forceinline__ partitioned_position_list&
  fused_xor_and(const partitioned_position_list& a, const partitioned_position_list& b) {
    const auto x = a ^ b;
    auto y = *this & x;
    std::swap(positions_, y.positions_);
    return *this;
  }

  void
  print(std::ostream& os) const {
    os << "part: [";
    if (!partitions_.empty()) {
      os << partitions_[0];
      for (std::size_t i = 1; i < partitions_.size(); ++i) {
        os << "," << partitions_[i];
      }
    }
    os << "]";
    os << ", pos: [";
    if (!positions_.empty()) {
      os << static_cast<u64>(positions_[0]);
      for (std::size_t i = 1; i < positions_.size(); ++i) {
        os << "," << static_cast<u64>(positions_[i]);
      }
    }
    os << "]";
  }

  static std::string
  name() {
    return "partitioned_position_list";
  }

  /// Returns the value of the bit at the position pos.
  u1
  test(const std::size_t pos) const {
    auto it = std::lower_bound(positions_.begin(), positions_.end(), pos);
    return *it == pos;
  }


  //===--------------------------------------------------------------------===//
  /// Iterator, with skip support.
  class iter {

    const partitioned_position_list& outer_;

    //===------------------------------------------------------------------===//
    // Iterator state
    //===------------------------------------------------------------------===//
    /// Read position within the list.
    $u64 partitions_read_pos_;
    /// Read position within the list.
    $u64 positions_read_pos_;
    /// Points to the beginning of the current range.
    $u64 range_begin_;
    /// The length of the current range.
    $u64 range_length_;
    //===------------------------------------------------------------------===//

  public:

    explicit __forceinline__
    iter(const partitioned_position_list& outer)
        : outer_(outer),
          partitions_read_pos_(0),
          positions_read_pos_(0),
          range_begin_(outer.positions_.empty()
                       ? outer_.n_
                       : outer_.positions_[0] + outer_.partitions_[0].begin),
          range_length_(outer.positions_.empty() ? 0 : 1) {

      if (!outer.positions_.empty()) {
        range_begin_ =
          outer_.partitions_[partitions_read_pos_].begin + outer_.positions_[0];
        range_length_ = 1;
        ++positions_read_pos_;
        // Determine the length of the current range.
        while (positions_read_pos_ < outer_.positions_.size()
            && outer_.positions_[positions_read_pos_] == range_begin_
                + range_length_
                - outer_.partitions_[partitions_read_pos_].begin) {
          ++positions_read_pos_;
          ++range_length_;
        }
        if (outer_.positions_[positions_read_pos_]
            < outer_.positions_[positions_read_pos_ - 1]) {
          ++partitions_read_pos_;
        }
      }
      else {
        range_begin_ = outer_.n_;
        range_length_ = 0;
      }
    }

    void __forceinline__
    next() {
      if (positions_read_pos_ < outer_.positions_.size()) {
        range_begin_ = outer_.partitions_[partitions_read_pos_].begin
            + outer_.positions_[positions_read_pos_];
        range_length_ = 1;
        ++positions_read_pos_;
        while (positions_read_pos_ < outer_.positions_.size()
            && outer_.positions_[positions_read_pos_] == range_begin_
                + range_length_
                - outer_.partitions_[partitions_read_pos_].begin) {
          ++positions_read_pos_;
          ++range_length_;
        }
      }
      else {
        range_begin_ = outer_.n_;
        range_length_ = 0;
      }
    }

    void __forceinline__
    skip_to(const std::size_t to_pos) {
      auto search = std::lower_bound(
          outer_.positions_.begin(), outer_.positions_.end(), to_pos);
      if (search != outer_.positions_.end()) {
        range_begin_ = *search;
        positions_read_pos_ = std::distance(outer_.positions_.begin(), search) + 1ull;
        while (positions_read_pos_ < outer_.positions_.size()
            && outer_.positions_[positions_read_pos_] == range_begin_
                + range_length_
                - outer_.partitions_[partitions_read_pos_].begin) {
          ++positions_read_pos_;
          ++range_length_;
        }
      }
      else {
        range_begin_ = outer_.n_;
        range_length_ = 0;
      }
    }

    u1 __forceinline__
    end() const noexcept {
      return range_length_ == 0;
    }

    u64 __forceinline__
    pos() const noexcept {
      return range_begin_;
    }

    u64 __forceinline__
    length() const noexcept {
      return range_length_;
    }

  };
  //===--------------------------------------------------------------------===//

  iter __forceinline__
  it() const {
    return iter(*this);
  }


private:

  //===--------------------------------------------------------------------===//
  // Helper functions.
  //===--------------------------------------------------------------------===//
  /// Appends a new position. - Used during construction.
  inline void
  push_back(const position_t pos) {
    if (partitions_.empty()) {
      create_partition(pos);
    }
    if (pos - partitions_.back().begin >= partition_size) {
      create_partition(pos);
    }
    partition_info& part_info = partitions_.back();
    positions_.push_back(pos - part_info.begin);
  }

  /// Creates a new partition.
  inline void
  create_partition(const position_t pos) {
    partitions_.emplace_back();
    partition_info& part_info = partitions_.back();
    part_info.begin = pos;
    part_info.offset = positions_.size();
  }
  //===--------------------------------------------------------------------===//

};
//===----------------------------------------------------------------------===//


} // namespace dtl