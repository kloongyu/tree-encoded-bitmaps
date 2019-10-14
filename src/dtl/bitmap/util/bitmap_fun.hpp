#pragma once
//===----------------------------------------------------------------------===//
#include <dtl/bits.hpp>
#include <dtl/dtl.hpp>
#include <dtl/simd.hpp>

#include <cassert>
//===----------------------------------------------------------------------===//
namespace dtl {
//===----------------------------------------------------------------------===//
/// Static functions to work with plain bitmaps.
template<typename _word_type>
struct bitmap_fun {
  using word_type = _word_type;
  static constexpr std::size_t word_bitlength = sizeof(word_type) * 8;

  /// Test the bit at position i.
  static u1 __forceinline__
  test(const word_type* bitmap, std::size_t i) noexcept {
    const auto block_idx = i / word_bitlength;
    const auto word = bitmap[block_idx];
    const auto bit_idx = i % word_bitlength;
    return (word & (word_type(1) << bit_idx)) != 0;
  }

  /// Set the bit at position i.
  static void __forceinline__
  set(word_type* bitmap, std::size_t i, u1 val) noexcept {
    if (val) {
      set(bitmap, i);
    }
    else {
      clear(bitmap, i);
    }
  }

  /// Set the bit at position i.
  static void __forceinline__
  set(word_type* bitmap, std::size_t i) noexcept {
    const auto block_idx = i / word_bitlength;
    const auto bit_idx = i % word_bitlength;
    bitmap[block_idx] |= word_type(1) << bit_idx;
  }

  /// Set the bits in [b,e).
  static void __forceinline__
  set(word_type* bitmap, std::size_t b, std::size_t e) noexcept {
    if (b >= e) { return; }
    assert(b < e);
    // The algorithm below has been adapted from the paper "Consistently faster
    // and smaller compressed bitmaps with Roaring" by Lemire et al.
    const auto x = b / word_bitlength;
    const auto y = (e - 1) / word_bitlength;
    const word_type Z = ~word_type(0);

    const word_type X = Z << (b % word_bitlength);
    const word_type Y = Z >> ((word_bitlength - (e % word_bitlength)) % word_bitlength);
    if (x == y) {
      bitmap[x] |= (X & Y);
    }
    else {
      bitmap[x] |= X;
      for (std::size_t k = x + 1; k < y; ++k) {
        bitmap[k] |= Z;
      }
      bitmap[y] |= Y;
    }
  }

  /// Clear the bit at position i.
  static void __forceinline__
  clear(word_type* bitmap, std::size_t i) noexcept {
    const auto block_idx = i / word_bitlength;
    const auto bit_idx = i % word_bitlength;
    bitmap[block_idx] &= ~(word_type(1) << bit_idx);
  }

  /// Clear the bits in [b,e).
  static void __forceinline__
  clear(word_type* bitmap, std::size_t b, std::size_t e) noexcept {
    if (e <= b) return;
    assert(b < e);
    // The algorithm below has been adapted from the paper "Consistently faster
    // and smaller compressed bitmaps with Roaring" by Lemire et al.
    const auto x = b / word_bitlength;
    const auto y = (e - 1) / word_bitlength;
    const word_type Z = ~word_type(0);

    const word_type X = Z << (b % word_bitlength);
    const word_type Y = Z >> ((word_bitlength - (e % word_bitlength)) % word_bitlength);
    if (x == y) {
      bitmap[x] &= ~(X & Y);
    }
    else {
      bitmap[x] &= ~X;
      for (std::size_t k = x + 1; k < y; ++k) {
        bitmap[k] &= ~Z;
      }
      bitmap[y] &= ~Y;
    }
  }

  /// Fetch up to size(word_type)*8 consecutive bits.
  static word_type __forceinline__
  fetch_bits(const word_type* bitmap,
      u64 bit_idx_begin,
      u64 bit_idx_end /* non-inclusive */) {
    assert(bit_idx_end > bit_idx_begin);
    static constexpr u64 word_bitlength = sizeof(word_type) * 8;
    const auto word_idx_begin = bit_idx_begin / word_bitlength;
    const auto word_idx_end = (bit_idx_end - 1) / word_bitlength;
    assert(word_idx_end - word_idx_begin <= 1);
    u64 cnt = bit_idx_end - bit_idx_begin;
    if (word_idx_begin == word_idx_end) {
      const auto word_idx = word_idx_begin;
      word_type bitmap_word = bitmap[word_idx];
      bitmap_word >>= (bit_idx_begin % word_bitlength);
      bitmap_word &= (~word_type(0)) >> (word_bitlength - cnt);
      return bitmap_word;
    }
    else {
      word_type bitmap_word_0 = bitmap[word_idx_begin];
      word_type bitmap_word_1 = bitmap[word_idx_end];
      bitmap_word_0 >>= (bit_idx_begin % word_bitlength);
      bitmap_word_1 &= (~word_type(0)) >> (word_bitlength - ((bit_idx_end % word_bitlength)));
      return bitmap_word_0 | (bitmap_word_1 << (word_bitlength - (bit_idx_begin % word_bitlength)));
    }
  }

  /// Find the first set bit.  Returns the index of the first set bit. If no
  /// bits are set, the length of the bitmap is returned.
  static std::size_t
  find_first(const word_type* bitmap_begin, const word_type* bitmap_end) {
    const std::size_t word_cnt = bitmap_end - bitmap_begin;
    std::size_t word_idx = 0;
    while (word_idx < word_cnt && bitmap_begin[word_idx] == word_type(0)) {
      ++word_idx;
    }
    if (word_idx < word_cnt) {
      return dtl::bits::tz_count(bitmap_begin[word_idx])
          + (word_idx * sizeof(word_type) * 8);
    }
    return word_idx * sizeof(word_type) * 8;
  }

  /// Find the last set bit.  Returns the index of the last set bit. If no bits
  /// are set, the length of the bitmap is returned.
  static std::size_t
  find_last(const word_type* bitmap_begin, const word_type* bitmap_end) {
    const std::size_t word_cnt = bitmap_end - bitmap_begin;
    std::size_t word_idx = word_cnt;
    while (word_idx > 0 && bitmap_begin[word_idx - 1] == word_type(0)) {
      --word_idx;
    }
    if (word_idx > 0) {
      return (word_idx * sizeof(word_type) * 8)
          - (dtl::bits::lz_count(bitmap_begin[word_idx - 1]) + 1);
    }
    return word_cnt * sizeof(word_type) * 8;
  }

  /// Find the first set bit [b,e). Returns 'e' if all bits in [b,e) are 0.
  static std::size_t
  find_first(const word_type* bitmap,
      const std::size_t b,
      const std::size_t e) {
    if (e <= b) return e;
    const auto x = b / word_bitlength;
    const auto y = (e - 1) / word_bitlength;

    const auto X_off = (b % word_bitlength);
    const word_type X = ~word_type(0) << X_off;
    const word_type Y = ~word_type(0) >> ((word_bitlength - (e % word_bitlength)) % word_bitlength);

    if (likely(x == y)) {
      const word_type w = bitmap[x] & (X & Y);
      if (w == 0) return e;
      return b + dtl::bits::tz_count(w >> X_off);
    }
    else {
      const word_type w_b = bitmap[x] >> X_off;
      if (w_b != 0) {
        return b + dtl::bits::tz_count(w_b);
      }
      for (std::size_t k = x + 1; k < y; ++k) {
        const word_type w = bitmap[k];
        if (w != 0) {
          return b
              + (word_bitlength - X_off)
              + ((k - (x + 1)) * word_bitlength)
              + dtl::bits::tz_count(w);
        }
      }
      const word_type w_e = bitmap[y] & Y;
      if (w_e != 0) {
        return b
            + ((y - x) * word_bitlength)
            - X_off
            + dtl::bits::tz_count(w_e);
      }
    }
    return e;
  }

  /// Find the first set bit [b,e). Returns 'e' if all bits in [b,e) are 0.
  /// This function is supposed to be called, when the underlying bitmap is
  /// dense.
  static std::size_t
  find_first_dense(const word_type* bitmap,
      const std::size_t b,
      const std::size_t e) {
    for (std::size_t i = b; i < e; ++i) {
      if (test(bitmap, i)) {
        return i;
      }
    }
    return e;
  }

  /// Scans the bitmap (that consists of a single word) for set bits and produces
  /// a position list. The positions are written to the given destination
  /// pointer. The function returns the number for values written to 'dst_ptr'.
  ///
  /// Note: Based on the implementation of Song and Chen described in the paper
  /// 'Exploiting SIMD for Complex Numerical Predicates'. This implementation
  /// works well when only few bits are set.
  static std::size_t
  to_positions(word_type bitmap_word, $u32* dst_ptr, $u32 offset) {
    $u32* writer = dst_ptr;
    for ($u32 m = dtl::bits::pop_count(bitmap_word); m > 0; m--) {
      u32 bit_pos = dtl::bits::tz_count(bitmap_word);
      *writer = bit_pos + offset;
      bitmap_word = dtl::bits::blsr(bitmap_word);
      writer++;
    }
    return writer - dst_ptr;
  }

  /// Scans the (word aligned) bitmap set bits and produces a position list. The
  /// positions are written to the given destination pointer. The function
  /// returns the number for values written to 'dst_ptr'.
  static inline std::size_t
  to_positions(
      const word_type* bitmap_begin,
      const word_type* bitmap_end,
      $u32* dst_ptr,
      u32 offset) {
#ifdef __AVX512F__
    return to_positions_avx512(bitmap_begin, bitmap_end, dst_ptr, offset);
#elif __AVX2__
    return to_positions_avx2(bitmap_begin, bitmap_end, dst_ptr, offset);
#else
    return to_positions_x86(bitmap_begin, bitmap_end, dst_ptr, offset);
#endif
  }

  /// Fall back implementation of 'to_positions()'; when neither AVX2 nor AVX512
  /// is available.
  static std::size_t
  to_positions_x86(
      const word_type* bitmap_begin,
      const word_type* bitmap_end,
      $u32* dst_ptr,
      u32 offset) {
    if (bitmap_begin >= bitmap_end) {
      return 0;
    }

    const std::size_t word_cnt = bitmap_end - bitmap_begin;
    auto* writer = dst_ptr;

    $u32 off = offset;
    for (std::size_t i = 0; i < word_cnt; ++i) {
      const auto bits = bitmap_begin[i];
      writer += to_positions(bits, writer, off);
      off += word_bitlength;
    }
    return (writer - dst_ptr);
  }

#ifdef __AVX2__
  /// AVX2 implementation of 'to_positions()'
  static std::size_t
  to_positions_avx2(
      const word_type* bitmap_begin,
      const word_type* bitmap_end,
      $u32* dst_ptr,
      u32 offset) {
    if (bitmap_begin >= bitmap_end) {
      return 0;
    }

    using simd_mask_type = $u8; // Process 8 bits at a time.
    static_assert(
        sizeof(simd_mask_type) <= sizeof(word_type),
        "The size of the SIMD mask must not exceed the size of a word.");

    auto* begin = reinterpret_cast<const simd_mask_type*>(bitmap_begin);
    const std::size_t cnt = (bitmap_end - bitmap_begin)
        * (sizeof(word_type) / sizeof(simd_mask_type));
    auto* writer = dst_ptr;

    __m256i offset_v = _mm256_set1_epi32(offset);
    const __m256i eight_v = _mm256_set1_epi32(8);
    for (std::size_t i = 0; i < cnt; ++i) {
      const auto bits = begin[i];
      const __m256i local_pos_v =
          _mm256_cvtepi16_epi32(dtl::simd::lut_match_pos[bits].i);
      const __m256i pos_v = _mm256_add_epi32(offset_v, local_pos_v);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(writer), pos_v);
      writer += dtl::simd::lut_match_cnt[bits];
      offset_v = _mm256_add_epi32(offset_v, eight_v);
    }
    return (writer - dst_ptr);
  }
#endif

#ifdef __AVX512F__
  /// AVX-512 implementation of 'to_positions()'
  static std::size_t
  to_positions_avx512(
      const word_type* bitmap_begin,
      const word_type* bitmap_end,
      $u32* dst_ptr,
      u32 offset) {
    if (bitmap_begin >= bitmap_end) {
      return 0;
    }

    using simd_mask_type = $u16; // Process 16 bits at a time.
    static_assert(
        sizeof(simd_mask_type) <= sizeof(word_type),
        "The size of the SIMD mask must not exceed the size of a word.");

    auto* begin = reinterpret_cast<const simd_mask_type*>(bitmap_begin);
    const std::size_t cnt = (bitmap_end - bitmap_begin)
        * (sizeof(word_type) / sizeof(simd_mask_type));
    auto* writer = dst_ptr;

    const __m512i sequence = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m512i sixteen_v = _mm512_set1_epi32(16);
    __m512i pos_v = _mm512_add_epi32(sequence, _mm512_set1_epi32(offset));
    for (std::size_t i = 0; i < cnt; ++i) {
      const auto bits = begin[i];
      _mm512_mask_compressstoreu_epi32(writer, bits, pos_v);
      writer += dtl::bits::pop_count(static_cast<u32>(bits));
      pos_v = _mm512_add_epi32(pos_v, sixteen_v);
    }
    return (writer - dst_ptr);
  }
#endif

  /// Scans the given range [b,e) of the bitmap and produce a position list.
  /// The positions are written to the given destination pointer.
  /// The function returns the number for values written to 'dst_ptr'.
  static std::size_t
  to_positions(const word_type* bitmap,
      const std::size_t b,
      const std::size_t e,
      $u32* dst_ptr) {
    // The algorithm below has been adapted from the paper "Consistently faster
    // and smaller compressed bitmaps with Roaring" by Lemire et al.
    const auto word_idx_begin = b / word_bitlength;
    const auto word_idx_end = (e - 1) / word_bitlength;
    const word_type Z = ~word_type(0);

    const word_type X = Z << (b % word_bitlength);
    const word_type Y = Z >> ((word_bitlength - (e % word_bitlength)) % word_bitlength);
    if (word_idx_begin == word_idx_end) {
      const word_type w = bitmap[word_idx_begin] & X & Y;
      return to_positions(w, dst_ptr, b - ((b % word_bitlength)));
    }
    else {
      auto* writer = dst_ptr;
      // Process the first word.
      writer +=
          to_positions(bitmap[word_idx_begin] & X, writer,
              b - ((b % word_bitlength)));
      // Process the intermediate words.
      writer += to_positions(
          &bitmap[word_idx_begin + 1],
          &bitmap[word_idx_end],
          writer,
          b + word_bitlength - (b % word_bitlength));
      // Process the last word.
      writer +=
          to_positions(bitmap[word_idx_end] & Y, writer,
              b + word_bitlength - (b % word_bitlength)
                  + ((word_idx_end - 1 - word_idx_begin) * word_bitlength));
      return (writer - dst_ptr);
    }
  }

private:
  // Construction not allowed.
  bitmap_fun() = delete;
};
//===----------------------------------------------------------------------===//
} // namespace dtl
