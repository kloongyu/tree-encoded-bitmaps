#pragma once

#include <list>
#include <queue>

#include <dtl/tree_mask.hpp>

#include <sdsl/int_vector.hpp>

#include "boost/dynamic_bitset.hpp"


namespace dtl {

/// Encodes a bitmap of length N as a binary tree.
/// The tree structure is encoded in level-order.
template<std::size_t _N>
class tree_mask_lo {
public:
  static constexpr auto N = _N;

  // level-order encoding
  sdsl::bit_vector lo_struc;
  sdsl::bit_vector lo_label;

  sdsl::rank_support_v5<> rank_support;

  /// C'tor
  explicit
  tree_mask_lo(const std::bitset<N>& bitmask){

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

    // Encode the tree into level-order

    std::queue<$u64> fifo; // explored nodes set

    // Allocate enough space for the structure and labels
    lo_struc.resize(tree_structure.max_node_cnt);
    lo_label.resize(labels.size());
    $u64 struct_cnt = 0;
    $u64 label_cnt = 0;

    std::function<void(u64)> add_node = [&](u64 idx){
      u1 is_inner = tree_structure.is_inner_node(idx);
      if(is_inner){
        // add the children of the current node to the structure
        u64 l_child = tree_structure.left_child_of(idx);
        u64 r_child = tree_structure.right_child_of(idx);

        u1 l_child_is_inner = tree_structure.is_inner_node(l_child);
        u1 r_child_is_inner = tree_structure.is_inner_node(r_child);

        lo_struc[struct_cnt++] = l_child_is_inner;
        lo_struc[struct_cnt++] = r_child_is_inner;

        // push them to the fifo queue to traverse them later
        fifo.push(l_child);
        fifo.push(r_child);

        // done for this node

      } else { // leaf node
        // add the label of the leaf node
        lo_label[label_cnt++] = labels[idx];

        // no children, nothing to add to the fifo queue, done
      }
    };

    // Special case for the root node: if the tree is only the root, structure is 0
    {
      bool root_is_inner = tree_structure.is_inner_node(0);

      // add the root to the tree structure
      lo_struc[struct_cnt++] = root_is_inner;

      if(root_is_inner) {
        // add the root to the fifo to add the rest of the tree
        fifo.push(0);
      }
      else {
        lo_label[label_cnt++] = labels[0];
        // tree is only the root, we are done
      }
    }

    while(!fifo.empty()){
      u64 next_node = fifo.front();
      add_node(next_node);
      fifo.pop();
    }

    lo_struc.resize(struct_cnt);
    lo_label.resize(label_cnt);

    rank_support.set_vector(&lo_struc);
  }

  explicit
  tree_mask_lo(const std::vector<$u1>& structure, const std::vector<$u1>& labels){

    lo_struc = sdsl::bit_vector();
    lo_label = sdsl::bit_vector();

    lo_struc.resize(structure.size());
    lo_label.resize(labels.size());

    for(auto i = 0; i < structure.size(); i++){
      lo_struc[i] = structure[i];
    }

    for(auto i = 0; i < labels.size(); i++){
      lo_label[i] = labels[i];
    }

    rank_support.set_vector(&lo_struc);
  }

  __forceinline__ u1
  is_inner_node(u64 node_idx) const {
    return lo_struc[node_idx] == 1 ? true : false;
  }

  __forceinline__ u1
  is_leaf_node(u64 node_idx) const {
    return lo_struc[node_idx] == 0 ? true : false;
  }

  // naive rank for tests
  u64 rank(u64 node_idx){
    $u64 rank = 0;

    for(auto i = 0; i <= node_idx; i++)
      if(lo_struc[i])
        rank++;

    return rank;
  }

  /// Important: rank_supportv5.rank() calculates the rank of the prefix -> we need idx + 1
  __forceinline__ u64
  left_child(u64 node_idx) const {

    return 2* rank_support.rank(node_idx+1) -1;
  }

  __forceinline__ u64
  right_child(u64 node_idx) const {
    return 2* rank_support.rank(node_idx+1);
  }

  __forceinline__ bool
  get_label(u64 node_idx) const {

    u64 label_idx = node_idx - this->rank_support.rank(node_idx);
    return this->lo_label[label_idx];
  }

  /// decodes the level-order encoding to a bitmap
  __forceinline__ std::bitset<N>
  to_bitset(){

    std::bitset<N> ret; // the resulting bitset

    // special case if the tree is only a root node
    if(lo_struc.size() == 1){
      //std::cout << "to_bitset() only root case" << std::endl;
      if(lo_label[0]){
        ret.set();
      } else {
        ret.reset();
      }

      return ret;
    }

    // normal cases: construct the tree using the lo_construction and a level counter
    $u64 level = 0; //current height
    $u64 write_pointer = 0;
    constexpr u64 tree_height = dtl::ct::log_2<N>::value;

    // a doubly linked list<node_idx, level> to manage the decoding for all nodes
    std::list<std::pair<$u64, $u64>> nodes;

    std::function<void(std::pair<u64, u64>)> decode_tree = [&](std::pair<u64, u64> n){
      u64 idx = n.first;
      u64 level = n.second;

      if(is_inner_node(idx)){
        //std::cout << "Inner node idx:" << idx << std::endl;

        u64 l_child = this->left_child(idx);
        u64 r_child = this->right_child(idx);
        u64 child_level = level+1;

        //std::cout << "Left child: " << l_child << " Right_child: " << r_child << std::endl;

        nodes.push_front(std::make_pair(r_child, child_level)); // first push the right child to the front
        nodes.push_front(std::make_pair(l_child, child_level)); // then push the left child to the front
        // resulting list: {left child, right child, ...}

      } else {
        //std::cout << "Leaf node" << std::endl;
        // write the label to the bitset
        u1 label = lo_label[idx - rank_support.rank(idx+1)];
        u64 to_write = 1 << (tree_height - level) ;// number of tuples represented by the label

        for(auto i = 0; i < to_write; ++i){
          ret[i + write_pointer] = label;
        }

        //std::cout << "Written: " << ret << " Level: " << level << " Tree_Height: " << tree_height << std::endl;

        write_pointer += to_write;
      }
    };

    nodes.push_front(std::make_pair(0,0)); // push the root node to the list

    while(!nodes.empty()){
      /*
      for(auto e : nodes){
        std::cout << e.first << "|" << e.second << "  ,  ";
      }*/
      //std::cout << std::endl;
      // the function is always called for the first node in the list
      //remove the node for which this function is executed
      std::pair<$u64,$u64> node = nodes.front();
      nodes.pop_front();
      decode_tree(node);
    }

    return ret;
  }

  /// Return the size in bytes.
  __forceinline__ std::size_t
  size_in_byte() {
    // TODO check: currently the size is calculated like in the sdsl_cheat_sheet
    u64 lo_struct_size = lo_struc.bit_size();
    u64 lo_labels_size = lo_label.bit_size();

    // the required space of an int_vector with n bits: 64*ceil(n/64+1) bit = 8*ceil(n/64+1) byte
    u64 lo_struct_bytes = 8 * std::ceil((lo_struct_size*1.0 / 64) +1);
    u64 lo_labels_bytes = 8 * std::ceil((lo_labels_size*1.0 / 64) +1);

    // the additional required space for the rank_support_v5 is: 0.0625*n bit
    u64 rank_supp_bytes = ((rank_support.size() * 0.0625) + 7) / 8;

    // std::cout << "Struct Bits: " << lo_struct_size << " Size: " << lo_struct_bytes << std::endl;
    // std::cout << "Labels Bits: " << lo_labels_size << " Size: " << lo_labels_bytes << std::endl;
    // std::cout << "R_Supp Size: " << rank_supp_bytes << std::endl;

    return lo_struct_bytes + lo_labels_bytes + rank_supp_bytes;
  }

  bool operator!=(tree_mask_lo& other) const {
    return (this->lo_struc != other.lo_struc || this->lo_label != other.lo_label);
  }

  bool operator==(tree_mask_lo& other) const {
    return (this->lo_struc == other.lo_struc && this->lo_label == other.lo_label);
  }

  /// Bitwise XOR without compression of the resulting tree
  tree_mask_lo
  operator^(const tree_mask_lo& other) const{

    struct node{
        $u64 node_idx;
        $u64 node_pos; // position in the more full of both trees
        bool xor_bit;

        node() : node_idx(0), node_pos(0), xor_bit(false) {};

        node(u64 idx, u64 pos , bool bit) : node_idx(idx), node_pos(pos), xor_bit(bit) {};

        void operator=(node& other){
          this->node_idx = other.node_idx;
          this->node_pos = other.node_pos;
          this->xor_bit = other.xor_bit;
        }
    };

    std::vector<$u1> structure;
    std::vector<$u1> labels;
    std::vector<std::pair<$u64, $u64>> level_offset; // to keep track of the different level begins

    // TODO optimize: determine and skip common prefix

    // a XOR b : a = this, b = other
    std::queue<node> fifo_a; // nodes of the next level of a
    std::queue<node> fifo_b; // nodes of the next level of b

    $u64 node_pos = 1; // needed to keep track of the corresponding nodes of tree a & b

    // push the root-nodes into the queues
    fifo_a.push(node(0, node_pos, this->is_inner_node(0) ? false : this->get_label(0)));
    fifo_b.push(node(0, node_pos, other.is_inner_node(0) ? false : other.get_label(0)));
    //frontier_level_nodes = fifo_a.size(), fifo_b.size();
    node_pos++;

    while(fifo_a.size() > 0 || fifo_b.size() > 0){

      //std::cout << std::endl << "Size queue_a: " << fifo_a.size() << " queue_b: " << fifo_b.size() << std::endl;

      node curr_a;
      node curr_b;

      if(fifo_a.size() > 0){
        curr_a = fifo_a.front();
      }

      if(fifo_b.size() > 0){
        curr_b = fifo_b.front();
      }

      if(curr_a.node_pos == curr_b.node_pos && curr_a.node_pos != 0){

        fifo_a.pop();
        fifo_b.pop();

        // both nodes are no fillers
        u8 c = static_cast<u8>(this->is_inner_node(curr_a.node_idx) |
                               (static_cast<u8>(other.is_inner_node(curr_b.node_idx)) << 1));
        $u1 bit;

        switch (c) {
          case 0b00: // a and b: leaf
            //std::cout << "Case: 0b00" << std::endl;

            bit = this->get_label(curr_a.node_idx) ^ other.get_label(curr_b.node_idx);

            structure.push_back(false); // insert the leaf
            labels.push_back(bit);
            break;

          case 0b01: // a: inner, b: leaf
          {
            //std::cout << "Case: 0b01" << std::endl;

            // add the inner node from a
            structure.push_back(true);

            u1 bit_b = other.get_label(curr_b.node_idx);

            // add the children of a to the queue
            node left_child(this->left_child(curr_a.node_idx), node_pos++, bit_b);
            node right_child(this->right_child(curr_a.node_idx), node_pos++, bit_b);

            fifo_a.push(left_child);
            fifo_a.push(right_child);

            break;
          }
          case 0b10: // a: leaf, b: inner
          {
            //std::cout << "Case: 0b10" << std::endl;

            // add the inner node from b
            structure.push_back(true);

            u1 bit_a = this->get_label(curr_a.node_idx);

            // add the children of b to the queue
            node left_child(other.left_child(curr_b.node_idx), node_pos++, bit_a);
            node right_child(other.right_child(curr_b.node_idx), node_pos++, bit_a);

            fifo_b.push(left_child);
            fifo_b.push(right_child);

            break;
          }
          case 0b11: // a and b: inner
          {
            //std::cout << "Case: 0b11" << std::endl;

            // add the inner node
            structure.push_back(true);

            // add the left child of a and b
            node left_child_a(this->left_child(curr_a.node_idx), node_pos, false);
            node left_child_b(other.left_child(curr_b.node_idx), node_pos, false);
            node_pos++;

            node right_child_a(this->right_child(curr_a.node_idx), node_pos, false);
            node right_child_b(other.right_child(curr_b.node_idx), node_pos, false);
            node_pos++;

            // add the children of a
            fifo_a.push(left_child_a);
            fifo_a.push(right_child_a);

            // add the children of b
            fifo_b.push(left_child_b);
            fifo_b.push(right_child_b);

            break;
          }
        }

      } else { // if we are in a subtree of one of the both nodes

        //std::cout << "Node a: " << curr_a.node_idx << " | " << curr_a.node_pos << " | " << curr_a.xor_bit << std::endl;
        //std::cout << "Node b: " << curr_b.node_idx << " | " << curr_b.node_pos << " | " << curr_b.xor_bit << std::endl;

        if((curr_a.node_pos < curr_b.node_pos && curr_a.node_pos != 0) || curr_b.node_pos == 0){ // add part of the subtree of a

          //std::cout << "Case: subtree_a" << std::endl;

          fifo_a.pop();

          if(this->is_inner_node(curr_a.node_idx)){ // current node is an inner node -> add children to the queue

            //std::cout << "Is inner node" << std::endl;
            // add the inner node to the structure
            structure.push_back(true);

            // add the children of a to the queue
            node left_child(this->left_child(curr_a.node_idx), node_pos++, curr_a.xor_bit);
            node right_child(this->right_child(curr_a.node_idx), node_pos++, curr_a.xor_bit);

            fifo_a.push(left_child);
            fifo_a.push(right_child);

          } else { // node is a leaf, add it to the structure

            //std::cout << "Is leaf node" << std::endl;
            // add the leaf to the structure
            structure.push_back(false);
            labels.push_back(this->get_label(curr_a.node_idx) ^ curr_a.xor_bit);
          }

        } else { // add part of the subtree of b

          //std::cout << "Case: subtree_b" << std::endl;

          fifo_b.pop();

          if(other.is_inner_node(curr_b.node_idx)){ // current node is an inner node -> add children to the queue

            //std::cout << "Is inner node" << std::endl;

            // add the inner node to the structure
            structure.push_back(true);

            // add the children of b to the queue
            node left_child(other.left_child(curr_b.node_idx), node_pos++, curr_b.xor_bit);
            node right_child(other.right_child(curr_b.node_idx), node_pos++, curr_b.xor_bit);

            fifo_b.push(left_child);
            fifo_b.push(right_child);

          } else { // node is a leaf, add it to the structure

            //std::cout << "Is leaf node" << std::endl;

            // add the leaf to the structure
            structure.push_back(false);
            labels.push_back(other.get_label(curr_b.node_idx) ^ curr_b.xor_bit);
          }
        }

        if(curr_a.node_pos == 0 && fifo_a.size() == 1){
          fifo_a.pop();
        }

        if(curr_b.node_pos == 0 && fifo_b.size() == 1 ){
          fifo_b.pop();
        }
      }

      //tree_mask_lo<N> tmp(structure, labels);
      //std::cout << tmp << std::endl;
    }

    tree_mask_lo<N> ret(structure, labels);
    return ret;
  }

  void
  print(std::ostream& os) const {

    for ($i64 i = 0; i < lo_struc.size(); i++) {
      os << (lo_struc[i] ? "1" : "0");
    }
    os << " | ";
    for ($i64 i = 0; i < lo_label.size(); i++) {
      os << (lo_label[i] ? "1" : "0");
    }
  }

  /// Bitwise AND without compression of the resulting tree
  tree_mask_lo
  operator&(const tree_mask_lo& other) const {

    struct node{
      $u64 node_idx;
      $u64 node_pos; // position in the more full of both trees

      node() : node_idx(0), node_pos(0) {};

      node(u64 idx, u64 pos) : node_idx(idx), node_pos(pos) {};

      void operator=(node& other){
        this->node_idx = other.node_idx;
        this->node_pos = other.node_pos;
      }
    };

    std::vector<$u1> structure;
    std::vector<$u1> labels;

    // needed for compression
    // store for each level the offset of the first node of the level and the max_rank of all previous nodes
    //std::vector<std::pair<$u64,$u64>> level_meta;

    // TODO optimize: determine and skip common prefix

    // a XOR b : a = this, b = other
    std::queue<node> fifo_a; // nodes of the next level of a
    std::queue<node> fifo_b; // nodes of the next level of b

    $u64 node_pos = 1;

    // push the root-nodes into the queues
    fifo_a.push(node(0, node_pos));
    fifo_b.push(node(0, node_pos));
    node_pos++;

    while(fifo_a.size() > 0 || fifo_b.size() > 0){

      //std::cout << std::endl << "Size queue_a: " << fifo_a.size() << " queue_b: " << fifo_b.size() << std::endl;

      node curr_a;
      node curr_b;

      if(fifo_a.size() > 0){
        curr_a = fifo_a.front();
      }

      if(fifo_b.size() > 0){
        curr_b = fifo_b.front();
      }

      if(curr_a.node_pos == curr_b.node_pos && curr_a.node_pos != 0){

        fifo_a.pop();
        fifo_b.pop();

        // both nodes are no fillers
        u8 c = static_cast<u8>(this->is_inner_node(curr_a.node_idx) |
                               (static_cast<u8>(other.is_inner_node(curr_b.node_idx)) << 1));

        $u1 bit;

        switch (c) {
          case 0b00: // a and b: leaf
            //std::cout << "Case: 0b00" << std::endl;

            bit = this->get_label(curr_a.node_idx) & other.get_label(curr_b.node_idx);

            structure.push_back(false); // insert the leaf
            labels.push_back(bit);
            break;

            // TODO compress

          case 0b01: // a: inner, b: leaf
          {
            //std::cout << "Case: 0b01" << std::endl;

            if(!other.get_label(curr_b.node_idx)){ // a & 0 -> leaf 0
              structure.push_back(false);
              labels.push_back(false);

              // TODO compression
            } else {
              // add the inner node from a
              structure.push_back(true);

              // add the children of a to the queue
              node left_child(this->left_child(curr_a.node_idx), node_pos++);
              node right_child(this->right_child(curr_a.node_idx), node_pos++);

              fifo_a.push(left_child);
              fifo_a.push(right_child);
            }

            break;
          }
          case 0b10: // a: leaf, b: inner
          {
            //std::cout << "Case: 0b10" << std::endl;

            if(!this->get_label(curr_a.node_idx)){ // a & 0 -> leaf 0
              structure.push_back(false);
              labels.push_back(false);

              // TODO compression
            } else {
              // add the inner node from a
              structure.push_back(true);

              // add the children of a to the queue
              node left_child(other.left_child(curr_b.node_idx), node_pos++);
              node right_child(other.right_child(curr_b.node_idx), node_pos++);

              fifo_b.push(left_child);
              fifo_b.push(right_child);
            }

            break;
          }
          case 0b11: // a and b: inner
          {
            //std::cout << "Case: 0b11" << std::endl;

            // add the inner node
            structure.push_back(true);

            // add the left child of a and b
            node left_child_a(this->left_child(curr_a.node_idx), node_pos);
            node left_child_b(other.left_child(curr_b.node_idx), node_pos);
            node_pos++;

            node right_child_a(this->right_child(curr_a.node_idx), node_pos);
            node right_child_b(other.right_child(curr_b.node_idx), node_pos);
            node_pos++;

            // add the children of a
            fifo_a.push(left_child_a);
            fifo_a.push(right_child_a);

            // add the children of b
            fifo_b.push(left_child_b);
            fifo_b.push(right_child_b);

            break;
          }
        }

      } else { // if we are in a subtree of one of the both nodes

        //std::cout << "Node a: " << curr_a.node_idx << " | " << curr_a.node_pos << std::endl;
        //std::cout << "Node b: " << curr_b.node_idx << " | " << curr_b.node_pos << std::endl;

        if((curr_a.node_pos < curr_b.node_pos && curr_a.node_pos != 0) || curr_b.node_pos == 0){ // add part of the subtree of a

          //std::cout << "Case: subtree_a" << std::endl;

          fifo_a.pop();

          if(this->is_inner_node(curr_a.node_idx)){ // current node is an inner node -> add children to the queue

            //std::cout << "Is inner node" << std::endl;
            // add the inner node to the structure
            structure.push_back(true);

            // add the children of a to the queue
            node left_child(this->left_child(curr_a.node_idx), node_pos++);
            node right_child(this->right_child(curr_a.node_idx), node_pos++);

            fifo_a.push(left_child);
            fifo_a.push(right_child);

          } else { // node is a leaf, add it to the structure

            //std::cout << "Is leaf node" << std::endl;
            // add the leaf to the structure
            structure.push_back(false);
            labels.push_back(this->get_label(curr_a.node_idx));
          }

        } else { // add part of the subtree of b

          //std::cout << "Case: subtree_b" << std::endl;

          fifo_b.pop();

          if(other.is_inner_node(curr_b.node_idx)){ // current node is an inner node -> add children to the queue

            //std::cout << "Is inner node" << std::endl;

            // add the inner node to the structure
            structure.push_back(true);

            // add the children of b to the queue
            node left_child(other.left_child(curr_b.node_idx), node_pos++);
            node right_child(other.right_child(curr_b.node_idx), node_pos++);

            fifo_b.push(left_child);
            fifo_b.push(right_child);

          } else { // node is a leaf, add it to the structure

            //std::cout << "Is leaf node" << std::endl;

            // add the leaf to the structure
            structure.push_back(false);
            labels.push_back(other.get_label(curr_b.node_idx));
          }
        }

        if(curr_a.node_pos == 0 && fifo_a.size() == 1){
          fifo_a.pop();
        }

        if(curr_b.node_pos == 0 && fifo_b.size() == 1 ){
          fifo_b.pop();
        }
      }

      //tree_mask_lo tmp(structure, labels);
      //std::cout << tmp << std::endl;
    }

    tree_mask_lo ret(structure, labels);
    return ret;
  }

  /// Computes (a XOR b) & this
  /// Note: this, a and b must be different instances. Otherwise, the behavior is undefined.
  tree_mask_lo&
  fused_xor_and(const tree_mask_lo& a, const tree_mask_lo& b) const {

    // get (a XOR b)
    tree_mask_lo tree_mask_xor = a ^ b;

    // get this & (a XOR b)
    tree_mask_lo tree_mask_and = this & tree_mask_xor;

    return tree_mask_and;
  }

  /// Bitwise XOR without compression of the resulting tree
  tree_mask_lo
  xor_compressed(const tree_mask_lo& other) const {

    struct node{
      $u64 node_idx;
      $u64 node_pos; // position in the more full of both trees
      bool xor_bit;

      node() : node_idx(0), node_pos(0), xor_bit(false) {};

      node(u64 idx, u64 pos , bool bit) : node_idx(idx), node_pos(pos), xor_bit(bit) {};

      void operator=(node& other){
        this->node_idx = other.node_idx;
        this->node_pos = other.node_pos;
        this->xor_bit = other.xor_bit;
      }
    };

    struct comp_leafs{
      $u64 level;
      $u64 struct_idx;
      $u64 label_idx;

      comp_leafs(u64 level, u64 idx, u64 label_idx) : level(level), struct_idx(idx), label_idx(label_idx) {};

      void print() const {
        std::cout << "Leaf for compression; level: " << level << " struct_idx: " << struct_idx
                  << " label_idx: " << label_idx << std::endl;
      }

      bool operator<(comp_leafs& other) const {
        return this->struct_idx > other.struct_idx;
      }
    };

    std::vector<$u1> structure;
    std::vector<$u1> labels;
    // keep track of the different level begins <structure_idx, rank>
    std::vector<std::pair<$u64, $u64>> level_offset;
    $u64 next_level = 0;
    // one vector per level <idx of the node, leaf position>
    std::unordered_multimap<$u64, comp_leafs> compression_candidates;
    compression_candidates.clear();

    // TODO optimize: determine and skip common prefix

    // a XOR b : a = this, b = other
    std::queue<node> fifo_a; // nodes of the next level of a
    std::queue<node> fifo_b; // nodes of the next level of b

    $u64 node_pos = 1; // needed to keep track of the corresponding nodes of tree a & b

    // push the root-nodes into the queues
    fifo_a.push(node(0, node_pos, this->is_inner_node(0) ? false : this->get_label(0)));
    fifo_b.push(node(0, node_pos, other.is_inner_node(0) ? false : other.get_label(0)));
    level_offset.push_back(std::make_pair(0, structure.size()-labels.size()));
    node_pos++;
    next_level = node_pos;
    std::cout << "   next_level: " << next_level << std::endl;


    while(fifo_a.size() > 0 || fifo_b.size() > 0){

      std::cout << std::endl << "Size queue_a: " << fifo_a.size() << " queue_b: " << fifo_b.size() << std::endl;
      std::cout << "node_pos: " << node_pos << std::endl;

      node curr_a;
      node curr_b;

      if(fifo_a.size() > 0){
        curr_a = fifo_a.front();
      }

      if(fifo_b.size() > 0){
        curr_b = fifo_b.front();
      }

      if(curr_a.node_pos == curr_b.node_pos && curr_a.node_pos != 0){

        fifo_a.pop();
        fifo_b.pop();

        // both nodes are no fillers
        u8 c = static_cast<u8>(this->is_inner_node(curr_a.node_idx) |
                               (static_cast<u8>(other.is_inner_node(curr_b.node_idx)) << 1));
        $u1 bit;

        // update the level-helper variables
        if(curr_a.node_pos == next_level || curr_b.node_pos == next_level){

          level_offset.push_back(std::make_pair(structure.size(), structure.size()-labels.size()));
          next_level = node_pos;
          std::cout << "   a:" << curr_a.node_idx << " b: " << curr_b.node_idx << " next_level: " << next_level
                    << " Stored tuple: " << level_offset.back().first << " | " << level_offset.back().second << std::endl;
        }

        switch (c) {
          case 0b00: // a and b: leaf
            std::cout << "Case: 0b00" << std::endl;

            bit = this->get_label(curr_a.node_idx) ^ other.get_label(curr_b.node_idx);

            // check if we later need to compress it
            // therefore check: is the last node also a leaf && the lable is identical && right child -> idx is even
            if (structure.size() > 1 && labels.size() > 0 &&
                !structure.back() && labels.back() == bit &&
                !(structure.size() & 1)) {

              auto tmp = comp_leafs(level_offset.size()-1, structure.size(), labels.size());
              compression_candidates.insert({tmp.level, tmp});
              //tmp.print();
            }

            structure.push_back(false); // insert the leaf
            labels.push_back(bit);

            break;

          case 0b01: // a: inner, b: leaf
          {
            std::cout << "Case: 0b01" << std::endl;

            // add the inner node from a
            structure.push_back(true);

            u1 bit_b = other.get_label(curr_b.node_idx);

            // add the children of a to the queue
            node left_child(this->left_child(curr_a.node_idx), node_pos++, bit_b);
            node right_child(this->right_child(curr_a.node_idx), node_pos++, bit_b);

            fifo_a.push(left_child);
            fifo_a.push(right_child);

            break;
          }
          case 0b10: // a: leaf, b: inner
          {
            std::cout << "Case: 0b10" << std::endl;

            // add the inner node from b
            structure.push_back(true);

            u1 bit_a = this->get_label(curr_a.node_idx);

            // add the children of b to the queue
            node left_child(other.left_child(curr_b.node_idx), node_pos++, bit_a);
            node right_child(other.right_child(curr_b.node_idx), node_pos++, bit_a);

            fifo_b.push(left_child);
            fifo_b.push(right_child);

            break;
          }
          case 0b11: // a and b: inner
          {
            std::cout << "Case: 0b11" << std::endl;

            // add the inner node
            structure.push_back(true);

            // add the left child of a and b
            node left_child_a(this->left_child(curr_a.node_idx), node_pos, false);
            node left_child_b(other.left_child(curr_b.node_idx), node_pos, false);
            node_pos++;

            node right_child_a(this->right_child(curr_a.node_idx), node_pos, false);
            node right_child_b(other.right_child(curr_b.node_idx), node_pos, false);
            node_pos++;

            // add the children of a
            fifo_a.push(left_child_a);
            fifo_a.push(right_child_a);

            // add the children of b
            fifo_b.push(left_child_b);
            fifo_b.push(right_child_b);

            break;
          }
        }

      } else { // if we are in a subtree of one of the both nodes

        //std::cout << "Node a: " << curr_a.node_idx << " | " << curr_a.node_pos << " | " << curr_a.xor_bit << std::endl;
        //std::cout << "Node b: " << curr_b.node_idx << " | " << curr_b.node_pos << " | " << curr_b.xor_bit << std::endl;

        if ((curr_a.node_pos < curr_b.node_pos && curr_a.node_pos != 0) || curr_b.node_pos == 0) { // add part of the subtree of a

          std::cout << "Case: subtree_a" << std::endl;

          fifo_a.pop();

          if (curr_a.node_pos == next_level) {
            level_offset.push_back(std::make_pair(structure.size(), structure.size()-labels.size()));
            next_level = node_pos;
            std::cout << "   a:" << curr_a.node_idx << " next_level: " << next_level
                      << " Stored tuple: " << level_offset.back().first << " | " << level_offset.back().second << std::endl;
          }

          if (this->is_inner_node(curr_a.node_idx)) { // current node is an inner node -> add children to the queue

            std::cout << "Is inner node" << std::endl;
            // add the inner node to the structure
            structure.push_back(true);

            // add the children of a to the queue
            node left_child(this->left_child(curr_a.node_idx), node_pos++, curr_a.xor_bit);
            node right_child(this->right_child(curr_a.node_idx), node_pos++, curr_a.xor_bit);

            fifo_a.push(left_child);
            fifo_a.push(right_child);

          } else { // node is a leaf, add it to the structure

            std::cout << "Is leaf node" << std::endl;
            // add the leaf to the structure
            structure.push_back(false);
            labels.push_back(this->get_label(curr_a.node_idx) ^ curr_a.xor_bit);
          }

        } else { // add part of the subtree of b

          std::cout << "Case: subtree_b" << std::endl;

          fifo_b.pop();

          if (curr_b.node_pos == next_level) {
            level_offset.push_back(std::make_pair(structure.size(), structure.size()-labels.size()));
            next_level = node_pos;
            std::cout << "   b: " << curr_b.node_idx << " next_level: " << next_level
                      << " Stored tuple: " << level_offset.back().first << " | " << level_offset.back().second << std::endl;
          }

          if (other.is_inner_node(curr_b.node_idx)) { // current node is an inner node -> add children to the queue

            std::cout << "Is inner node" << std::endl;

            // add the inner node to the structure
            structure.push_back(true);

            // add the children of b to the queue
            node left_child(other.left_child(curr_b.node_idx), node_pos++, curr_b.xor_bit);
            node right_child(other.right_child(curr_b.node_idx), node_pos++, curr_b.xor_bit);

            fifo_b.push(left_child);
            fifo_b.push(right_child);

          } else { // node is a leaf, add it to the structure

            std::cout << "Is leaf node" << std::endl;

            // add the leaf to the structure
            structure.push_back(false);
            labels.push_back(other.get_label(curr_b.node_idx) ^ curr_b.xor_bit);
          }
        }

        if (curr_a.node_pos == 0 && fifo_a.size() == 1) {
          fifo_a.pop();
        }

        if (curr_b.node_pos == 0 && fifo_b.size() == 1 ) {
          fifo_b.pop();
        }
      }

      tree_mask_lo<N> tmp(structure, labels);
      std::cout << tmp << std::endl;
    }

    //---- Compression ----
    // iterate bottom up -> start with the last one -> if compressed, check if we can compress the new leaf = old parent
    // as well and add it to the vector -> need to insert it at the right slot

    // always just take the deepest level l and sort this level

    if(compression_candidates.size() > 0) {

      std::cout << std::endl << "Apply compression" << std::endl;


      for ($u16 l = level_offset.size()-1; l > 0; l--) {

        std::cout << std::endl;

        using iter = typename std::unordered_multimap<$u64, comp_leafs>::iterator;
        std::pair<iter, iter> range = compression_candidates.equal_range(l);

        if (range.first == range.second) {
          std::cout << "Level " << l << " has no leaves to compress" << std::endl;
          continue;
        }

        std::cout << "Level " << l << ", compressing" << std::endl;
        std::cout << "Index offset: " << level_offset[l].first << " Rank before: " << level_offset[l].second << std::endl;

        // get all elements of this level
        std::vector<comp_leafs> level_queue;

        for (iter it = range.first; it != range.second; it++) {
          level_queue.push_back(it->second);
        }

        std::cout << "Number of siblings to compress: " << level_queue.size() << std::endl;

        for(comp_leafs e: level_queue){
          e.print();
        }

        std::sort(&level_queue[0], &level_queue[level_queue.size()]);


        for (auto i = 0; i < level_queue.size(); i++) {

          comp_leafs& r_leaf = level_queue[i];

          std::cout << "Compress node: " << r_leaf.struct_idx << std::endl;
          r_leaf.print();

          // store the label of the two identical leafs, which will be the label of their parent, which becomes a leaf
          u1 label = labels[r_leaf.label_idx + i];
          std::cout << "Label: " << label << std::endl;
          std::cout << "Leafes before delete: ";
          for(auto i = 0; i < labels.size(); i++){
            std::cout << labels[i];
          }
          std::cout << std::endl;

          // delete the right leaf from the structure and the labels
          structure.erase(structure.begin() + r_leaf.struct_idx);
          labels.erase(labels.begin() + r_leaf.label_idx + i);

          // delete the sibling, which is a left child, from the structure and the labels
          structure.erase(structure.begin() + r_leaf.struct_idx - 1);
          labels.erase(labels.begin() + r_leaf.label_idx - 1 + i);

          std::cout << "Leafes after delete: ";
          for(auto i = 0; i < labels.size(); i++){
            std::cout << labels[i];
          }
          std::cout << std::endl;

          // now we need to update their parent to an leaf node and set its label
          // get the position of their parent by first calculating its rank

          u64 p_rank = r_leaf.struct_idx / 2;
          std::cout << "Number of levels: " << level_offset.size() << std::endl;
          std::cout << "r_leaf.level: " << r_leaf.level << std::endl;
          std::cout << "Level: " << r_leaf.level - 1;
          std::cout << " Idx: " << level_offset[r_leaf.level - 1].first
                    << " Rank: " << level_offset[r_leaf.level - 1].second << std::endl;

          $u64 current_idx = level_offset[r_leaf.level - 1].first;
          $u64 current_rank = level_offset[r_leaf.level - 1].second;


          for (; current_idx < level_offset[r_leaf.level].first; current_idx++) {
            current_rank += structure[current_idx];
            std::cout << "Current rank: " << current_rank << " Current idx: " << current_idx <<  std::endl;

            if (current_rank == p_rank) {
              structure[current_idx] = 0;
              u64 label_pos = current_idx + 1 - current_rank;
              std::cout << "Change the node: " << current_idx << " with label: " << label
                        << " at position: " << label_pos <<  std::endl;
              labels.emplace(labels.begin() + label_pos, label);

              // update all labels on the layer above
              auto level_above = compression_candidates.equal_range(l-1);

              for (iter j = level_above.first; j != level_above.second; ++j)
              {
                if(j->second.label_idx >= label_pos){
                  std::cout << "Apply new: " << j->second.struct_idx << " > " << current_idx << std::endl;
                  j->second.print();
                  j->second.label_idx = j->second.label_idx + 1;
                  j->second.print();
                }
              }

              // check if the new leaf also needs to be compressed
              // is the parent not the root
              if (structure.size() > 1) {
                // check if we are a left or a right child
                if (!(current_idx & 1)) { // right child

                  // left sibling is also an leaf and the labels are identical
                  if (!structure[current_idx - 1] && labels[label_pos] == labels[label_pos - 1]) {
                    comp_leafs tmp(r_leaf.level - 1, current_idx, label_pos);
                    compression_candidates.insert({tmp.level, tmp});
                    std::cout << "New candidate: ";
                    tmp.print();
                  }

                } else { // left child
                  // right sibling is also an leaf and the labels are identical
                  if (!structure[current_idx + 1] && labels[label_pos] == labels[label_pos + 1]) {
                    comp_leafs tmp(r_leaf.level - 1, current_idx + 1, label_pos + 1);
                    compression_candidates.insert({tmp.level, tmp});
                    std::cout << "New candidate: ";
                    tmp.print();
                  }
                }
              }
              tree_mask_lo<N> tmp(structure, labels);
              std::cout << tmp << std::endl;
              break;
            }
          }

        }
      }
    }
    compression_candidates.clear();
    tree_mask_lo<N> ret(structure, labels);
    std::cout << std::endl << "Final Tree: " << ret << std::endl;
    return ret;
  }

};

//TODO:
// 1. XOR
// 2. Fused XOR
// 3. BP

}; // namespace dtl