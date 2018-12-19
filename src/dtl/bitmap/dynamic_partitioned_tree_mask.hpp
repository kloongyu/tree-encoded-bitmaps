#pragma once

#include <list>
#include <queue>
#include <vector>

#include <boost/dynamic_bitset.hpp>

#include "dynamic_tree_mask_lo.hpp"

namespace dtl {


//===----------------------------------------------------------------------===//
class dynamic_partitioned_tree_mask {
public:

  u64 N;
  u64 partition_cnt; // must be a power of two
  u64 part_n;
  u64 part_n_log2;
  u64 part_n_mask;

  using tree_mask_t = dynamic_tree_mask_lo;

  std::vector<tree_mask_t> tree_masks_;

private:

  dynamic_partitioned_tree_mask(u64 n, u64 partition_cnt, u64 part_n, u64 part_n_log2, u64 part_n_mask)
      : N(n), partition_cnt(partition_cnt),
        part_n(part_n), part_n_log2(part_n_log2), part_n_mask(part_n_mask) { }

public:

  /// C'tor
  explicit
  dynamic_partitioned_tree_mask(const boost::dynamic_bitset<$u32>& bitmask, u64 partition_cnt = 2)
      : N(bitmask.size()), partition_cnt(partition_cnt),
        part_n(N / partition_cnt), part_n_log2(dtl::log_2(part_n)),
        part_n_mask((part_n ==1) ?  u64(0) : u64(-1) >> (64 - dtl::log_2(part_n))) {

    if (!dtl::is_power_of_two(N)) {
      throw std::invalid_argument("The length of the bitmask must be a power of two.");
    }

    if (!dtl::is_power_of_two(partition_cnt)) {
      throw std::invalid_argument("The numbers of partitions must be a power of two.");
    }

    if (N < partition_cnt) {
      throw std::invalid_argument("The number of partitions must be less than or equal to N.");
    }

    for ($u64 pid = 0; pid < partition_cnt; pid++) {
      u64 offset = part_n * pid;
      boost::dynamic_bitset<$u32> part_bitmask(part_n);
      for ($u64 i = 0; i < part_n; i++) { // TODO optimize later
        part_bitmask[i] = bitmask[i + offset];
      }
      tree_masks_.emplace_back(part_bitmask);
//      std::cout << "is true: " << tree_masks_.back().all()
//                << ", is false: " << tree_masks_.back().none()
//                << ", size: " << tree_masks_.back().size_in_byte()
//                << std::endl;
    }
  }


  /// Decodes the level-order encoding to a bitmap.
  boost::dynamic_bitset<$u32>
  to_bitset() const {
    boost::dynamic_bitset<$u32> ret_val(N);
    for ($u64 pid = 0; pid < partition_cnt; pid++) {
      u64 offset = part_n * pid;
      auto part_bitmask = tree_masks_[pid].to_bitset();
      for ($u64 i = 0; i < part_n; i++) { // TODO optimize later
        ret_val[i + offset] = part_bitmask[i];
      }
    }
    return ret_val;
  }

  /// Return the size in bytes.
  std::size_t
  size_in_byte() const {
    $u64 size = 0;
    for ($u64 pid = 0; pid < partition_cnt; pid++) {
      size += tree_masks_[pid].size_in_byte();
    }
    size += partition_cnt * 8; // pointer to tree masks
    return size;
  }

  bool operator!=(dynamic_partitioned_tree_mask& other) const {
    // TODO
  }

  bool operator==(dynamic_partitioned_tree_mask& other) const {
    // TODO
  }

  /// Bitwise XOR without compression of the resulting tree
  dynamic_partitioned_tree_mask
  operator^(const dynamic_partitioned_tree_mask& other) const{
    // TODO
  }

  /// Bitwise XOR (range encoding)
  dynamic_partitioned_tree_mask
  xor_re(const dynamic_partitioned_tree_mask& other) const {
    // TODO
  }

  void
  print(std::ostream& os) const {
    for ($u64 pid = 0; pid < partition_cnt; pid++) {
      os << pid << ":" << tree_masks_[pid] << " ";
    }
  }

  /// Bitwise AND without compression of the resulting tree
  dynamic_partitioned_tree_mask
  operator&(const dynamic_partitioned_tree_mask& other) const {
    dynamic_partitioned_tree_mask ret_val(N, partition_cnt, part_n, part_n_log2, part_n_mask);
    for ($u64 pid = 0; pid < partition_cnt; pid++) {
      ret_val.tree_masks_.emplace_back(tree_masks_[pid] & other.tree_masks_[pid]);
    }
    return ret_val;
  }

  /// Bitwise AND (range encoding)
  dynamic_partitioned_tree_mask
  and_re(const dynamic_partitioned_tree_mask& other) const {
    // TODO
  }

  /// Computes (a XOR b) & this
  /// Note: this, a and b must be different instances. Otherwise, the behavior is undefined.
  dynamic_partitioned_tree_mask&
  fused_xor_and(const dynamic_partitioned_tree_mask& a, const dynamic_partitioned_tree_mask& b) const {
    // TODO
  }

  /// Bitwise XOR with compression of the resulting tree
  dynamic_partitioned_tree_mask
  xor_compressed(const dynamic_partitioned_tree_mask& other) const {
    // TODO
  }

  /// Bitwise AND with compression of the resulting tree
  dynamic_partitioned_tree_mask
  and_compressed(const dynamic_partitioned_tree_mask& other) const {
    // TODO
  }

  /// Return the name of the implementation.
  std::string
  name() const {
    return "dynamic_partitioned_tree_mask_" + std::to_string(partition_cnt);
  }

  std::size_t
  size() const {
    return N;
  }

  /// Returns the value of the bit at the position pos.
  u1
  test(const std::size_t pos) const {
    u64 tree_mask_idx = pos >> part_n_log2;
    u64 in_part_pos = pos & part_n_mask;
    return tree_masks_[tree_mask_idx].test(in_part_pos);
  }

  //===----------------------------------------------------------------------===//
  /// 1-fill iterator, with skip support.
  class iter {

    const dynamic_partitioned_tree_mask& tm_;
    using iter_t = dynamic_partitioned_tree_mask::tree_mask_t::iter;

    //===----------------------------------------------------------------------===//
    // Iterator state
    //===----------------------------------------------------------------------===//
    /// points to the beginning of a 1-fill
    $u64 pos_;
    /// the current partition number
    $u64 part_no_;
    /// the iterator of the current partition
    iter_t iter_;
    //===----------------------------------------------------------------------===//

  public:

    void __forceinline__
    next() {
      iter_.next();
      while (iter_.end() && part_no_ < (tm_.partition_cnt - 1)) {
        part_no_++;
        new(&iter_) iter_t(tm_.tree_masks_[part_no_]);
      }
      if (iter_.end()) {
        pos_ = tm_.N;
      }
      else {
        pos_ = iter_.pos() + tm_.part_n * part_no_;
      }
    }

    explicit
    iter(const dynamic_partitioned_tree_mask& tm)
        : tm_(tm), part_no_(0), iter_(tm_.tree_masks_[0]) {
      while (iter_.end() && part_no_ < (tm_.partition_cnt  - 1)) {
        part_no_++;
        new(&iter_) iter_t(tm_.tree_masks_[part_no_]);
      }
      if (iter_.end()) {
        pos_ = tm_.N;
      }
      else {
        pos_ = iter_.pos() + tm_.part_n * part_no_;
      }
    }

    void __forceinline__
    nav_to(const std::size_t to_pos) {
      skip_to(to_pos);
    }

    void __forceinline__
    skip_to(const std::size_t to_pos) {
      const auto to_part_no = to_pos >> tm_.part_n_log2;
      if (to_part_no >= tm_.partition_cnt) {
        part_no_ = tm_.partition_cnt;
        pos_ = tm_.N;
        return;
      }
      if (to_part_no == part_no_) {
        iter_.skip_to(to_pos & tm_.part_n_mask);
        if (iter_.end()) {
          next();
        }
        pos_ = iter_.pos() + tm_.part_n * part_no_;
      }
      else {
        part_no_ = to_part_no;
        new(&iter_) iter_t(tm_.tree_masks_[part_no_]);
        while (iter_.end() && part_no_ < (tm_.partition_cnt  - 1)) {
          part_no_++;
          new(&iter_) iter_t(tm_.tree_masks_[part_no_]);
        }
        if (iter_.end()) {
          pos_ = tm_.N;
        }
        else {
          pos_ = iter_.pos() + tm_.part_n * part_no_;
        }
      }
    }

    u1 __forceinline__
    end() const noexcept {
      return pos_ == tm_.N;
    }

    u64 __forceinline__
    pos() const noexcept {
      return pos_;
    }

    u64 __forceinline__
    length() const noexcept {
      return (pos_ == tm_.N) ? 0 : iter_.length();
    }

  };
  //===----------------------------------------------------------------------===//

  iter __forceinline__
  it() const {
    return iter(*this);
  }


};
//===----------------------------------------------------------------------===//

}; // namespace dtl