#include <dtl/dtl.hpp>
#include <dtl/env.hpp>
#include <dtl/bitmap/util/convert.hpp>
#include <dtl/bitmap/teb.hpp>
#include <boost/algorithm/string.hpp>

#include "../util/bitmap_db.hpp"
#include "../util/gen.hpp"
#include "../util/threading.hpp"

#include "thirdparty/perfevent/PerfEvent.hpp"
//===----------------------------------------------------------------------===//
const std::string DB_FILE =
    dtl::env<std::string>::get("DB_FILE", "./random_bitmaps.sqlite3");
bitmap_db db(DB_FILE);

constexpr auto opt_level = 2;

auto now_nanos = []() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
};

std::vector<$f64> clustering_factors = {8,4,2,1};
std::vector<$f64> bit_densities = {0.001, 0.01, 0.1, 0.2};
std::vector<$u64> n_values {
//      1ull << 16,
//      1ull << 17,
//      1ull << 18,
//      1ull << 19,
    1ull << 20
};
//===----------------------------------------------------------------------===//
void run(u64 n, f64 f, f64 d, i64 bitmap_id) {
  const auto bid = bitmap_id;
  const auto plain_bitmap = db.load_bitmap(bid);

  using T = dtl::teb<0>;

  T enc_bitmap(plain_bitmap);

  std::vector<std::size_t> probe_positions;
  std::vector<std::size_t> probe_up_steps;
  std::size_t up_step_sum = 0;
  {
    auto it = enc_bitmap.it();
    while (!it.end()) {
      probe_positions.push_back(it.pos());
      auto up_steps = it.bench_nav_upwards_get_stack_size();
      up_step_sum += up_steps;
      probe_up_steps.push_back(up_steps);
      it.next();
    }
  }
  if (up_step_sum == 0) return;

  auto it = enc_bitmap.it();
  auto tsc_cntr = 0ull;
  for (std::size_t i = 0; i < probe_positions.size(); ++i) {
    std::size_t to_pos = probe_positions[i];
    it.nav_from_root_to(to_pos);
    const auto tsc_begin = _rdtsc();
    it.bench_nav_upwards(1, 0);
    const auto tsc_end = _rdtsc();
    tsc_cntr += tsc_end - tsc_begin;
  }
  std::cout << n << "," << f << "," << d
      << "," << (tsc_cntr * 1.0 / up_step_sum)
      << "," << enc_bitmap.info()
      << std::endl;
}
//===----------------------------------------------------------------------===//
$i32 main() {
  std::vector<$i64> bitmap_ids;

  for (auto n : n_values) {
    for (auto d : bit_densities) {
      for (auto f : clustering_factors) {
        const auto ids = db.find_bitmaps(n, f, d);
        if (ids.empty()) {
          const auto bitmap = gen_random_bitmap_markov(n, f, d);
          const auto id = db.store_bitmap(n,f,d,bitmap);
          bitmap_ids.push_back(id);
        }
      }
    }
  }

  std::cerr << "n,f,d,cycles,info" << std::endl;
  for (auto n : n_values) {
    for (auto d : bit_densities) {
      for (auto f : clustering_factors) {
        const auto ids = db.find_bitmaps(n, f, d);
        if (!ids.empty()) {
          run(n, f, d, ids[0]);
          run(n, f, d, ids[0]);
          run(n, f, d, ids[0]);
        }
      }
    }
  }
}
//===----------------------------------------------------------------------===//
