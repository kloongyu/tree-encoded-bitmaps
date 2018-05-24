#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <bitset>
#include <functional>
#include <vector>

#include <dtl/dtl.hpp>
#include <dtl/math.hpp>
#include <dtl/tree.hpp>


namespace dtl {

//===----------------------------------------------------------------------===//
/// Encodes a bitmap of length N as a full binary tree.
/// The tree structure is encoded in pre-order.
template<std::size_t N>
class tree_mask_po {

  // TODO remove tailing 0
  // TODO reduce memory footprint - only one bitvector + lengths
  std::vector<$u1> structure_;
  std::vector<$u1> labels_;

  // helper structure to allow for faster navigation (requires two additional bits per word)
  sdsl::int_vector<2> skip_;

  using path_t = uint64_t;
  static constexpr path_t path_msb = path_t(1) << (sizeof(path_t) * 8 - 1);

  static constexpr uint64_t tree_height = dtl::ct::log_2<N>::value;
  static_assert(tree_height < 64, "Template parameter N must not exceed 2^(sizeof(path_t) * 8 - 1)."); // due to sentinel bit.


  //===----------------------------------------------------------------------===//
  // Helper functions
  //===----------------------------------------------------------------------===//
  __forceinline__ path_t
  OFF_toggle_highest_set_bit(const path_t i) const {
    return i & ~(path_t(1) << _bit_scan_reverse(i)); // 4 instructions
  }

  __forceinline__ path_t
  toggle_highest_set_bit(const path_t i) const {
    return i ^ (path_msb >> dtl::bits::lz_count(i)); // 3 instructions
  }

  /// Converts the given encoded binary tree path to a level-order node index.
  __forceinline__  static std::size_t
  get_node_idx(const path_t path) {
    std::size_t node_idx = dtl::binary_tree_structure<N>::root();
    const auto sentinel_pos = (sizeof(path_t) * 8) - dtl::bits::lz_count(path) - 1;
    if (sentinel_pos > 0) {
      for (std::size_t i = sentinel_pos - 1; i < sentinel_pos; i--) {
        const auto direction = (path & (path_t(1) << i)) != 0;
        node_idx = dtl::binary_tree_structure<N>::left_child_of(node_idx) + direction;
      }
    }
    return node_idx;
  }

  template<std::size_t M>
  __forceinline__ static std::size_t
  min_cntr(const std::bitset<M>& b) {
    int32_t c = 0;
    int32_t c_min = 0;
    for (std::size_t j = 0; j < M; j++) {
      c = b[j] ? c + 1 : c - 1;
      c_min = std::min(c_min, c);
    }
    c_min = c_min > 0 ? 0 : c_min;
    return (0 - c_min) + 1;
  }

  template<std::size_t M>
  __forceinline__ static std::size_t
  min_cntr_code(const std::bitset<M>& b) {
    auto c_min = min_cntr(b);
    if (c_min == 0) return 0;
    if (c_min == 1) return 1;
    if (c_min == 2) return 2;
    if (c_min <= 4) return 3;
    return 0;
  }
  //===----------------------------------------------------------------------===//

public:

  //===----------------------------------------------------------------------===//
  /// Helper structure to navigate within the tree structure.
  class traversal {
//  private:
  public:

    const std::vector<$u1>& structure_;
    const std::vector<$u1>& labels_;
    const sdsl::int_vector<2>& skip_;

    std::size_t s_pos_;
    std::size_t l_pos_;

    path_t path_; // encodes the path to the current node (the highest set bit is a sentinel bit)

  public:

    /// C'tor
    traversal(const std::vector<$u1>& structure,
              const std::vector<$u1>& labels,
              const sdsl::int_vector<2>& skip)
        : structure_(structure), labels_(labels), skip_(skip), s_pos_(0), l_pos_(0), path_(1) {}

    explicit
    traversal(const tree_mask_po& tm) : traversal(tm.structure_, tm.labels_, tm.skip_) {}

    ~traversal() = default;

    traversal(const traversal& other) = default;

    traversal(traversal&& other) noexcept = delete;

    traversal&
    operator=(const traversal& other) = delete;

    traversal&
    operator=(traversal&& other) noexcept = delete;

    /// Return 'true' if the current node is an inner node, 'false' otherwise.
    __forceinline__ bool
    is_inner_node() const { return structure_[s_pos_]; }

    /// Return 'true' if the current node is leaf node, 'false' otherwise.
    __forceinline__ bool
    is_leaf_node() const { return !is_inner_node(); }

    /// Return the label of the current leaf node.
    /// The result is undefined, if the current node is an inner node.
    __forceinline__ bool
    get_label() const { assert(is_leaf_node()); return labels_[l_pos_]; }

    /// Navigate to the next node (in pre-order).
    __forceinline__ bool
    next() {
      if (end()) return false;
      assert(l_pos_ < labels_.size());
      if (structure_[s_pos_] /* is inner node */) {
        // go to left child
        path_ <<= 1;
      }
      else {
        // is leaf node
        path_++;
        l_pos_++;
        path_ >>= dtl::bits::tz_count(path_);
      }
      s_pos_++;
      return true;
    }

    __forceinline__ bool
    end() const {
      return s_pos_ == structure_.size() - 1;
    }

    /// Return the level of the current node.
    __forceinline__ auto
    get_level() const {
      const auto lz_cnt_path = dtl::bits::lz_count(path_);
      const auto level = sizeof(path_t) * 8 - 1 - lz_cnt_path;
      return level;
    }

    /// Navigate to the left child.
    /// The current node must be an inner node.
    __forceinline__ void
    goto_left_child() {
      assert(is_inner_node());
      s_pos_++;
    }

    /// Navigate to the right child. (Naive implementation)
    /// The current node must be an inner node.
    __forceinline__ void
    goto_right_child_naive() {
      if (end()) return;
      assert(is_inner_node());
      // navigate to the left hand side child
      next();
      // determine the level of the left child
      const auto level = get_level();
      // traverse left sub tree until the level is reached again
      next();
      while (get_level() != level) {
        next();
      }
    }

    /// Navigate to the right child. (Semi-naive implementation)
    /// The current node must be an inner node.
    __forceinline__ void
    goto_right_child_semi_naive() {
      if (end()) {
        return;
      }
      assert(is_inner_node());
      path_ <<= 1;
      path_++;
      s_pos_++; // left child
      std::size_t cntr = 1;
      while (cntr != 0) {
        const auto is_inner_node = structure_[s_pos_++];
        cntr = is_inner_node ? cntr + 1 : cntr - 1;
        l_pos_ += !is_inner_node;
      }
    }

    /// Navigate to the right child.
    /// The current node must be an inner node.
    __forceinline__ void
    goto_right_child_word_skip() {
      if (end()) {
        return;
      }
      assert(is_inner_node());
      path_ <<= 1;
      path_++;
      s_pos_++; // left child

      std::size_t seq_cntr = 0;
      std::size_t skip_cntr = 0;

      std::size_t cntr = 1;

      constexpr std::size_t skip_width = 64; // [bits]

      while (cntr != 0) {
        if (s_pos_ % skip_width == 0 && (s_pos_ + skip_width) < structure_.size()) {
          // try to 'fast forward'

          uint64_t word = 0;
          std::size_t p = 0;
          for (std::size_t i = s_pos_; i < (s_pos_ + skip_width); i++) { // TODO optimize
            word |= uint64_t(structure_[i]) << p;
            p++;
          }

          const int64_t inner_node_cnt = dtl::bits::pop_count(word);
          const int64_t leaf_node_cnt = skip_width - inner_node_cnt;

//          std::cout << "skipping: "
//                    << std::bitset<64>(word)
//                    << ", spos=" << s_pos_
//                    << ", skip width=" << skip_width
//                    << ", |1|=" << inner_node_cnt
//                    << ", |0|=" << leaf_node_cnt
//                    << ", cntr=" << cntr
//                    << ", cntr_delta=" << (inner_node_cnt - leaf_node_cnt)
//                    << ", min_cntr=" << min_cntr(std::bitset<skip_width>(word))
//                    << ", min_cntr_code=" << min_cntr_code(std::bitset<skip_width>(word)) - 1
//                    << std::endl;


//          if (leaf_node_cnt < cntr) { // otherwise, fast forward is not safe
//          const auto min_c = 1ull << min_cntr_code(std::bitset<skip_width>(word)) - 1;
          const auto min_c = (1ull << skip_[s_pos_ / skip_width]) - 1;
          if (min_c <= cntr) { // otherwise, fast forward is not safe
            const auto cntr_delta = inner_node_cnt - leaf_node_cnt;
            if ((static_cast<int64_t>(cntr) + cntr_delta) >= 0) {
              s_pos_ += skip_width;
              l_pos_ += leaf_node_cnt;
              cntr += cntr_delta;
//              std::cout << " - skipped " << skip_width << std::endl;
              skip_cntr += 1;
              continue; // while loop
            }
          }
        }

        const std::size_t k = s_pos_ % skip_width == 0 ? skip_width : skip_width - s_pos_ % skip_width;
        for (std::size_t i = 0; i < k; i++) {
          const auto is_inner_node = structure_[s_pos_++];
          cntr = is_inner_node ? cntr + 1 : cntr - 1;
          l_pos_ += !is_inner_node;
          seq_cntr++;
          if (cntr == 0) break;
        }

      }
        std::cout << "seq_cntr: " << seq_cntr
                  << ", skip_cntr: " << skip_cntr
                  << " (" << (skip_cntr * skip_width) << ")"
                  << std::endl;
    }

    /// Navigate to the right child.
    /// The current node must be an inner node.
    __forceinline__ void
    goto_right_child_byte_skip() {
      if (end()) {
        return;
      }
      assert(is_inner_node());
      path_ <<= 1;
      path_++;
      s_pos_++; // left child

      std::size_t seq_cntr = 0;
      std::size_t skip_cntr = 0;

      std::size_t cntr = 1;

      constexpr std::size_t skip_width = 8; // [bits]

      while (cntr != 0) {
        if (s_pos_ % skip_width == 0 && (s_pos_ + skip_width) < structure_.size()) {
          // try to 'fast forward'

          uint64_t word = 0;
          std::size_t p = 0;
          for (std::size_t i = s_pos_; i < (s_pos_ + skip_width); i++) { // TODO optimize
            word |= uint64_t(structure_[i]) << p;
            p++;
          }

          const int64_t inner_node_cnt = dtl::bits::pop_count(word);
          const int64_t leaf_node_cnt = skip_width - inner_node_cnt;

//          std::cout << "skipping: "
//                    << std::bitset<8>(word)
//                    << ", spos=" << s_pos_
//                    << ", skip width=" << skip_width
//                    << ", |1|=" << inner_node_cnt
//                    << ", |0|=" << leaf_node_cnt
//                    << ", cntr=" << cntr
//                    << ", cntr_delta=" << (inner_node_cnt - leaf_node_cnt)
//                    << ", min_cntr=" << min_cntr(std::bitset<skip_width>(word))
//                    << ", min_cntr_code=" << min_cntr_code(std::bitset<skip_width>(word)) - 1
//                    << std::endl;

          if (leaf_node_cnt < cntr) { // otherwise, fast forward is not safe
            const auto cntr_delta = inner_node_cnt - leaf_node_cnt;
            if ((static_cast<int64_t>(cntr) + cntr_delta) >= 0) {
              s_pos_ += skip_width;
              l_pos_ += leaf_node_cnt;
              cntr += cntr_delta;
//              std::cout << " - skipped " << skip_width << std::endl;
              skip_cntr += 1;
              continue; // while loop
            }
          }
        }

        const std::size_t k = s_pos_ % skip_width == 0 ? skip_width : skip_width - s_pos_ % skip_width;
        for (std::size_t i = 0; i < k; i++) {
          const auto is_inner_node = structure_[s_pos_++];
          cntr = is_inner_node ? cntr + 1 : cntr - 1;
          l_pos_ += !is_inner_node;
          seq_cntr++;
          if (cntr == 0) break;
        }

      }
      std::cout << "seq_cntr: " << seq_cntr
                << ", skip_cntr: " << skip_cntr
                << " (" << (skip_cntr * skip_width) << ")"
                << std::endl;
    }

    /// Navigate to the right child. (Semi-naive implementation)
    /// The current node must be an inner node.
    __forceinline__ void
    goto_right_child_lut() {
      if (end()) {
        return;
      }

      constexpr std::size_t skip_width_log2 = 4;
      constexpr std::size_t skip_width = 1ull << skip_width_log2; // [bits]

      constexpr std::size_t lut_size_log2 = 8;
      constexpr std::size_t lut_size = 1ull << lut_size_log2;
      constexpr std::size_t lut_mask = lut_size - 1;
      std::vector<std::size_t> lut(lut_size, 0);
      for (std::size_t i = 0; i < (1ull << skip_width) - 1; i++) {
        std::bitset<skip_width> s(i);
        int32_t c = 1;
        for (std::size_t j = skip_width - 2; j < skip_width; j--) {
          c = s[j] ? c - 1 : c + 1;
        }
        c = c < 0 ? 0 : c;
        const std::size_t lut_idx = i & lut_mask;
        lut[lut_idx] = std::max(lut[lut_idx], std::size_t(c));
      }

      std::cout << "LUT:" << std::endl;
      for (std::size_t i = 0; i < lut_size; i++) {
        std::cout << lut[i] << std::endl;
      }
      std::cout << "---" << std::endl;


      assert(is_inner_node());
      path_ <<= 1;
      path_++;
      s_pos_++; // left child

      std::size_t seq_cntr = 0;
      std::size_t skip_cntr = 0;

      std::size_t cntr = 1;

      while (cntr != 0) {

        // load one word
        uint64_t word = 0;
        std::size_t p = 0;
        for (std::size_t i = s_pos_; i < (s_pos_ + skip_width); i++) { // TODO optimize
          word |= uint32_t(structure_[i]) << p;
          p++;
        }

        if ((s_pos_ + skip_width) < structure_.size()) {
          // try to 'fast forward'
          const int64_t inner_node_cnt = dtl::bits::pop_count(word);
          const int64_t leaf_node_cnt = skip_width - inner_node_cnt;

//          std::cout << "skipping: "
//                    << std::bitset<skip_width>(word)
//                    << ", skip width=" << skip_width
//                    << ", |1|=" << inner_node_cnt
//                    << ", |0|=" << leaf_node_cnt
//                    << ", cntr=" << cntr
//                    << ", cntr_delta=" << (inner_node_cnt - leaf_node_cnt)
//                    << std::endl;


          if (lut[word & lut_mask] < cntr) { // otherwise, fast forward is not safe
            const auto cntr_delta = inner_node_cnt - leaf_node_cnt;
            if ((static_cast<int64_t>(cntr) + cntr_delta) >= 0) {
              s_pos_ += skip_width;
              l_pos_ += leaf_node_cnt;
              cntr += cntr_delta;
//              std::cout << " - skipped " << skip_width << std::endl;
              skip_cntr += 1;
              continue; // while loop
            }
          }
        }

        for (std::size_t i = 0; i < skip_width; i++) {
          const auto is_inner_node = structure_[s_pos_++];
          cntr = is_inner_node ? cntr + 1 : cntr - 1;
          l_pos_ += !is_inner_node;
          seq_cntr++;
          if (cntr == 0) break;
        }

      }
      if ((seq_cntr + skip_cntr) > 10)
        std::cout << "seq_cntr: " << seq_cntr
                  << ", skip_cntr: " << skip_cntr
                  << " (" << (skip_cntr * skip_width) << ")"
                  << std::endl;
    }

    /// Navigate to the right child. (Semi-naive implementation)
    /// The current node must be an inner node.
    __forceinline__ void
    goto_right_child() {
      goto_right_child_byte_skip();
    }

    /// Compute and return the level-order node index of the current node.
    __forceinline__ std::size_t
    get_node_idx() {
      return tree_mask_po::get_node_idx(path_);
    }
  };
  //===----------------------------------------------------------------------===//


  /// C'tor
  explicit
  tree_mask_po(const std::bitset<N>& bitmask) {

    using tree_t = dtl::binary_tree_structure<N>;

    static constexpr u64 length = tree_t::max_node_cnt;
    static constexpr u64 height = tree_t::height;

    tree_t tree_structure;
    std::bitset<length> labels;

    // initialize a complete binary tree
    // ... all the inner nodes have two children
    // ... the leaf nodes are labelled with the given bitmask
    for ($u64 i = length / 2; i < length; i++) {
      labels[i] = bitmask[i - length / 2];
    }
    // propagate the mask bits along the tree (bottom-up)
    for ($u64 i = 0; i < length - 1; i++) {
      u64 node_idx = length - i - 1;
      labels[tree_t::parent_of(node_idx)] = labels[tree_t::parent_of(node_idx)] | labels[node_idx];
    }

    // bottom-up pruning (loss-less)
    for ($u64 i = 0; i < length - 1; i += 2) {
      u64 left_node_idx = length - i - 2;
      u64 right_node_idx = left_node_idx + 1;

      u1 left_bit = labels[left_node_idx];
      u1 right_bit = labels[right_node_idx];

      u64 parent_node_idx = tree_t::parent_of(left_node_idx);

      u1 prune_causes_false_positives = left_bit ^ right_bit;
      u1 both_nodes_are_leaves = !tree_structure.is_inner_node(left_node_idx) & !tree_structure.is_inner_node(right_node_idx);
      u1 prune = both_nodes_are_leaves & !prune_causes_false_positives;
      if (prune) {
        tree_structure.set_leaf(parent_node_idx);
      }
    }

    // Encode (pruned) tree structure structure and labels.
    std::function<void(u64)> encode_recursively = [&](u64 idx) {
      u1 is_inner = tree_structure.is_inner_node(idx);
      if (is_inner) {
        structure_.push_back(true);
        encode_recursively(tree_t::left_child_of(idx));
        encode_recursively(tree_t::right_child_of(idx));
      } else {
        structure_.push_back(false);
        labels_.push_back(labels[idx]);
      }
    };
    encode_recursively(0);

    // initialize skip helper structure
    constexpr std::size_t skip_width = 64;
    sdsl::int_vector<2> skip((structure_.size() / skip_width) + 1, 0, 2 /*bits per element*/);
    for (std::size_t i = 0; i < structure_.size() / skip_width; i++) {
      // read on word from structure // TODO optimize
      uint64_t word = 0;
      std::size_t p = 0;
      for (std::size_t j = i * skip_width; j < ((i + 1) * skip_width); j++) {
        word |= uint64_t(structure_[j]) << p;
        p++;
      }
      skip[i] = min_cntr_code(std::bitset<skip_width>(word));
    }
    skip_ = std::move(skip);
  }

  tree_mask_po(const bool b) : structure_({false}), labels_({b})  {}

  ~tree_mask_po() = default;

  tree_mask_po(const tree_mask_po& other) = default;

  tree_mask_po(tree_mask_po&& other) noexcept = default;

  tree_mask_po&
  operator=(const tree_mask_po& other) = default;

  tree_mask_po&
  operator=(tree_mask_po&& other) noexcept = default;

  /// Return the size in bytes.
  __forceinline__ std::size_t
  size() {
    return ((structure_.size() + labels_.size()) + 7) / 8; // TODO + labels offset skip
  }

  /// Conversion to a std::bitset.
  std::bitset<N>
  to_bitset() {
    std::bitset<N> ret; // the resulting bitset
    std::size_t label_pos = 0; // the current read position in the labels
    path_t path = 1; // a stack replacement, the highest set bit is a sentinel bit
    constexpr uint64_t tree_height = dtl::ct::log_2<N>::value;
    assert(tree_height < 64); // due to sentinel bit.
    for (std::size_t i = 0; i < structure_.size(); i++) {
//      std::cout << "path: " << std::bitset<8>(path)
//                << ", level: " << (sizeof(decltype(path)) * 8 - dtl::bits::lz_count(path)) - 1
//                << ", #bits to set: " << (N >> (sizeof(decltype(path)) * 8 - dtl::bits::lz_count(path) - 1))
//                << std::endl;
      if (structure_[i] /* is inner node */) {
        // go to left child
        path <<= 1;
      }
      else {
        // is leaf node
        const bool label = labels_[label_pos++]; // read the label
        if (unlikely(label)) {
          // produce output (a 1-fill)
          const auto lz_cnt_path = dtl::bits::lz_count(path);
          const auto level = sizeof(path_t) * 8 - 1 - lz_cnt_path;
          const auto pos = (path ^ (path_msb >> lz_cnt_path)) << (tree_height - level); // toggle sentinel bit (= highest bit set) and add offset
          const auto len = N >> level; // the length of the 1-fill
          for (std::size_t j = pos; j < pos + len; j++) {
            ret[j] = true;
          }
        }
        path++;
        path >>= dtl::bits::tz_count(path);
        // is "left hand side" node?
//        const bool is_lhs = (path & 1ull) == 0;
//        path++;
//        if (!is_lhs) {
//          path >>= __builtin_ctz(path);
//        }
      }
    }
    std::cout << std::endl;
    return ret;
  }

  /// Bitwise XOR
  tree_mask_po
  operator^(const tree_mask_po& other) const {
    tree_mask_po ret(false);

    return ret;
  }

  /// Computes (a XOR b) & this
  /// Note: this, a and b must be different instances. Otherwise, the behavior is undefined.
  tree_mask_po&
  fused_xor_and(const tree_mask_po& a, const tree_mask_po& b) {
    // TODO
  }


  void
  print(std::ostream& os) const {
    // print tree structure
//    for ($i64 i = structure_.size() - 1; i >= 0; i--) {
    for ($i64 i = 0; i < structure_.size(); i++) {
      os << (structure_[i] ? "1" : "0");
    }
    os << " | ";
//    for ($i64 i = labels_.size() - 1; i >= 0; i--) {
    for ($i64 i = 0; i < labels_.size(); i++) {
      os << (labels_[i] ? "1" : "0");
    }
  }

};
//===----------------------------------------------------------------------===//

} // namespace dtl

