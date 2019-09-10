#include <atomic>
#include <vector>

#include <dtl/dtl.hpp>
#include <dtl/env.hpp>
#include <dtl/bitmap/util/convert.hpp>
#include <dtl/bitmap/teb.hpp>
#include <dtl/bitmap/dynamic_bitmap.hpp>
#include <dtl/bitmap/dynamic_roaring_bitmap.hpp>
#include <dtl/bitmap/dynamic_wah.hpp>

#include <boost/algorithm/string.hpp>
#include <dtl/bitmap/util/random.hpp>
#include <fstream>

#include "../util/bitmap_db.hpp"
#include "../util/threading.hpp"

#include "boost/filesystem.hpp"

//===----------------------------------------------------------------------===//
namespace fs = boost::filesystem;
//===----------------------------------------------------------------------===//
void run(const std::string& dir, std::ostream& result_out) {
  result_out
      << "//===----------------------------------------------------------------------===//"
      << "\n" << dir
      << std::endl;

  std::size_t min_val = std::numeric_limits<std::size_t>::max();
  std::size_t max_val = 0;
  std::size_t file_cnt = 0;

  std::vector<std::string> filenames;
  fs::directory_iterator end_it;
  for (fs::directory_iterator dir_it(dir); dir_it != end_it; ++dir_it ) {
    const auto file = dir_it->path().string();
    filenames.push_back(file);
    ++file_cnt;
    std::cout << "reading file: " << file << std::endl;
    std::ifstream is(file);
    std::string token;
    while (std::getline(is, token, ',')) {
      const auto val = std::stoull(token);
      if (val < min_val) min_val = val;
      if (val > max_val) max_val = val;
    }
    is.close();
  }
  std::sort(filenames.begin(), filenames.end());

  std::cout << "min=" << min_val
      << ", max_val=" << max_val
      << ", file_cnt=" << file_cnt
      << std::endl;

  const auto n = max_val + 1;
  const auto n_pow2 = dtl::next_power_of_two(n);
  const auto c = file_cnt;

  std::atomic<std::size_t> bytes_roaring { 0 };
  std::atomic<std::size_t> bytes_wah { 0 };
  std::atomic<std::size_t> bytes_wah64 { 0 };
  std::atomic<std::size_t> bytes_teb { 0 };

  std::vector<dtl::bitmap> bitmaps;
  std::vector<dtl::bitmap> bitmaps_pow2;

  std::size_t total_bit_cnt = 0;
  for (auto& file : filenames) {
//    std::cout << "reading file: " << file << std::endl;
    std::ifstream is(file);
    std::string token;

    dtl::bitmap bm(n);
    dtl::bitmap bm_pow2(n_pow2);

    while (std::getline(is, token, ',')) {
      const auto val = std::stoull(token);
      bm[val] = true;
      bm_pow2[val] = true;
    }
    is.close();

    bitmaps.push_back(bm);
    bitmaps_pow2.push_back(bm_pow2);
    std::cout << dtl::determine_bit_density(bm)
        << "," << dtl::determine_clustering_factor(bm)
        << std::endl;
    total_bit_cnt += bm.count();
  }
  std::cout << "total bit cnt: " << total_bit_cnt << std::endl;
//std::exit(0);
  u1 range_encoding = false;
  if (range_encoding) {
    for (std::size_t i = 1; i < bitmaps.size(); ++i) {
      bitmaps[i] |= bitmaps[i-1];
      bitmaps_pow2[i] |= bitmaps_pow2[i-1];
    }
  }

  auto thread_fn = [&](const std::size_t bid, std::ostream& os) {
    auto& bm = bitmaps[bid];
    auto& bm_pow2 = bitmaps_pow2[bid];

    std::size_t r = 0;
    std::size_t t = 0;
    std::size_t w = 0;
    std::size_t w64 = 0;
//    {
//      dtl::dynamic_roaring_bitmap roaring(bm);
//      r= roaring.size_in_byte();
//    }
//    {
//      dtl::dynamic_wah32 wah(bm);
//      w = wah.size_in_byte();
//    }
//    {
//      dtl::dynamic_wah64 wah(bm);
//      w64 = wah.size_in_byte();
//    }
    {
      const auto fpr = 0.0001;
      dtl::teb<> teb(bm_pow2, fpr);
//      dtl::teb<> teb(bm_pow2);
      t = teb.size_in_byte();
      const auto dec = dtl::to_bitmap_using_iterator(teb);
      if ((bm_pow2 & dec) != bm_pow2) {
        std::cerr << "Validation failed." << std::endl;
        std::exit(1);
      }
      u64 max_fp_cnt = static_cast<u64>(teb.size() * fpr);
      u64 fp_cnt = (bm_pow2 ^ dec).count();
      std::cout << "fp_cnt=" << fp_cnt << std::endl;
      if (fp_cnt > max_fp_cnt) {
        std::cerr << "Validation failed. Max FP count exceeded." << std::endl;
        std::exit(1);
      }
      os << teb.info() << std::endl;
    }

    bytes_roaring += r;
    bytes_wah += w;
    bytes_wah64 += w64;
    bytes_teb += t;

    os << "d=" << dtl::determine_bit_density(bm)
        << ", d_pow2=" << dtl::determine_bit_density(bm_pow2)
        << ", f=" << dtl::determine_clustering_factor(bm)
        << ", pop_cnt=" << bm.count()
        << std::endl;
    os << "roaring: " << std::setw(15) << bytes_roaring
        << " / " << std::setw(10) << r << std::endl;
    os << "teb:     " << std::setw(15) << bytes_teb
        << " / " << std::setw(10) << t << std::endl;
    os << "wah:     " << std::setw(15) << bytes_wah
        << " / " << std::setw(10) << w << std::endl;
    os << "wah64:     " << std::setw(15) << bytes_wah64
        << " / " << std::setw(10) << w64 << std::endl;

  };

  dispatch(0, bitmaps.size(), thread_fn);

  result_out << "roaring: " << std::setw(15) << bytes_roaring << std::endl;
  result_out << "teb:     " << std::setw(15) << bytes_teb << std::endl;
  result_out << "wah:     " << std::setw(15) << bytes_wah << std::endl;
  result_out << "wah64:   " << std::setw(15) << bytes_wah64 << std::endl;
}
//===----------------------------------------------------------------------===//
$i32 main() {
  const auto basedir = dtl::env<std::string>::get("DIR",
      "/home/hl/git/storage/RoaringBitmap/real-roaring-dataset/src/main/resources/real-roaring-dataset/");
  std::vector<std::string> dirs {
    basedir + "/census1881/",
    basedir + "/census1881_srt/",
    basedir + "/census-income/",
    basedir + "/census-income_srt/",
    basedir + "/weather_sept_85/",
    basedir + "/weather_sept_85_srt/",
    basedir + "/wikileaks-noquotes/",
    basedir + "/wikileaks-noquotes_srt/",
  };

  std::stringstream results;
  for (auto& dir : dirs) {
    run(dir, results);
  }
  std::cout << results.str() << std::endl;
}
//===----------------------------------------------------------------------===//