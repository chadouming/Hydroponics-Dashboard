// Host tests for the pure-logic blocks embedded in growell-display.yaml.
// Each wrapper #includes one "// BEGIN unit <name>" block extracted by tools/extract_units.py.
// Run: .\tools\test.ps1
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

static int checks = 0, failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    checks++;                                                          \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      failures++;                                                      \
    }                                                                  \
  } while (0)
#define CHECK_NEAR(a, b) CHECK(std::fabs((double) (a) - (double) (b)) < 1e-3)

// ---------------- value_color ----------------
static uint32_t value_color(float v, float gLo, float gHi, float aLo, float aHi) {
  uint32_t col;
#include "build/value_color.inc"
  return col;
}

static void test_value_color() {
  const uint32_t G = 0x10B981, A = 0xF59E0B, R = 0xEF4444, GREY = 0x9B9B9B;
  auto ph = [](float v) { return value_color(v, 5.8f, 6.2f, 5.5f, 6.5f); };
  CHECK(ph(6.0f) == G);
  CHECK(ph(5.8f) == G);
  CHECK(ph(6.2f) == G);
  CHECK(ph(6.21f) == A);
  CHECK(ph(6.5f) == A);
  CHECK(ph(6.51f) == R);
  CHECK(ph(5.79f) == A);
  CHECK(ph(5.5f) == A);
  CHECK(ph(5.49f) == R);
  CHECK(ph(NAN) == GREY);
  auto ec = [](float v) { return value_color(v, 2000, 2500, 1800, 2700); };
  CHECK(ec(2500) == G);
  CHECK(ec(2500.5f) == A);
  CHECK(ec(2700) == A);
  CHECK(ec(2701) == R);
  CHECK(ec(1800) == A);
  CHECK(ec(1799) == R);
  CHECK(ec(0) == R);
}

// ---------------- store_advance / graph_points / graph_range / graph_band ----------------
using Slots = std::array<float, 48>;
using Counts = std::array<uint16_t, 48>;

static void store_advance(Slots &sum, Counts &cnt, int32_t &head, float &carry, int32_t h) {
#include "build/store_advance.inc"
}

static Slots graph_points(const Slots &sum, const Counts &cnt, float carry) {
  Slots pts;
#include "build/graph_points.inc"
  return pts;
}

struct Range {
  float lo, hi;
  int first;
};
static Range graph_range(const Slots &pts) {
#include "build/graph_range.inc"
  return {lo, hi, first};
}

static uint32_t graph_band(int i, float val) {
#include "build/graph_band.inc"
  return band(val);
}

static void test_store_advance() {
  Slots sum{};
  Counts cnt{};
  int32_t head = 0;
  float carry = NAN;
  store_advance(sum, cnt, head, carry, 100);  // first call only starts the clock
  CHECK(head == 100);
  CHECK(std::isnan(carry));
  sum[0] = 10; cnt[0] = 2;  // oldest slot: mean 5
  sum[47] = 7; cnt[47] = 1; // newest slot: 7
  store_advance(sum, cnt, head, carry, 100);  // same half-hour: no-op
  CHECK(head == 100 && cnt[0] == 2 && cnt[47] == 1);
  store_advance(sum, cnt, head, carry, 102);  // two slots drop out
  CHECK(head == 102);
  CHECK_NEAR(carry, 5.0f);  // mean of the newest non-empty dropped slot
  CHECK(cnt[45] == 1);
  CHECK_NEAR(sum[45], 7.0f);  // old slot 47 moved two places
  CHECK(cnt[46] == 0 && cnt[47] == 0);
  store_advance(sum, cnt, head, carry, 99);  // clock went backwards: ignored
  CHECK(head == 102 && cnt[45] == 1);
  store_advance(sum, cnt, head, carry, 102 + 60);  // more than 24 h: everything drops
  CHECK(head == 162);
  CHECK_NEAR(carry, 7.0f);  // newest known value survives as the carry
  for (int k = 0; k < 48; k++) CHECK(cnt[k] == 0);
}

static void test_graph_points_and_range() {
  Slots sum{};
  Counts cnt{};
  sum[10] = 12; cnt[10] = 2;  // mean 6
  sum[20] = 5; cnt[20] = 1;   // 5
  Slots pts = graph_points(sum, cnt, NAN);
  for (int k = 0; k < 10; k++) CHECK(std::isnan(pts[k]));  // before the first value: blank
  for (int k = 10; k < 20; k++) CHECK_NEAR(pts[k], 6.0f);  // empty slots repeat the carry
  for (int k = 20; k < 48; k++) CHECK_NEAR(pts[k], 5.0f);
  Range r = graph_range(pts);
  CHECK(r.first == 10);
  CHECK_NEAR(r.lo, 5.0f);
  CHECK_NEAR(r.hi, 6.0f);
  pts = graph_points(sum, cnt, 4.0f);  // known starting value fills the prefix
  CHECK_NEAR(pts[0], 4.0f);
  CHECK(graph_range(pts).first == 0);
  Slots empty{};
  Counts none{};
  pts = graph_points(empty, none, NAN);
  CHECK(graph_range(pts).first == -1);  // no data at all
  pts = graph_points(empty, none, 6.1f);  // flat line gets a ±0.5 window
  r = graph_range(pts);
  CHECK(r.first == 0);
  CHECK_NEAR(r.lo, 5.6f);
  CHECK_NEAR(r.hi, 6.6f);
}

static void test_graph_band() {
  const uint32_t G = 0x10B981, A = 0xF59E0B, R = 0xEF4444;
  CHECK(graph_band(0, 4.9f) == R);  // below the first threshold: first colour
  CHECK(graph_band(0, 5.0f) == R);
  CHECK(graph_band(0, 5.49f) == R);
  CHECK(graph_band(0, 5.5f) == A);
  CHECK(graph_band(0, 5.8f) == G);
  CHECK(graph_band(0, 6.2f) == G);
  CHECK(graph_band(0, 6.21f) == A);
  CHECK(graph_band(0, 6.5f) == A);
  CHECK(graph_band(0, 6.51f) == R);
  CHECK(graph_band(1, 2500) == G);
  CHECK(graph_band(1, 2501) == A);
  CHECK(graph_band(2, 400) == G);
  CHECK(graph_band(2, 451) == R);
  CHECK(graph_band(3, 17.99f) == A);
  CHECK(graph_band(3, 21.0f) == G);
  CHECK(graph_band(3, 21.01f) == A);
  CHECK(graph_band(3, 24.01f) == R);
}

// Fractions along a segment a->b where the line changes colour, framed by 0 and 1.
static std::vector<float> segment_cuts(int i, float a, float b) {
#include "build/graph_band.inc"
  (void) band;
#include "build/segment_cuts.inc"
  return std::vector<float>(cut, cut + n);
}

static bool cuts_are(const std::vector<float> &got, const std::vector<float> &want) {
  if (got.size() != want.size())
    return false;
  for (size_t k = 0; k < got.size(); k++)
    if (std::fabs(got[k] - want[k]) > 1e-3f)
      return false;
  return true;
}

static void test_segment_cuts() {
  CHECK(cuts_are(segment_cuts(0, 5.9f, 6.0f), {0, 1}));                   // stays green
  CHECK(cuts_are(segment_cuts(0, 5.8f, 6.0f), {0, 1}));                   // starts exactly on 5.8
  CHECK(cuts_are(segment_cuts(0, 5.7f, 6.3f), {0, 0.16667f, 0.85f, 1}));  // rising: 5.8 then 6.21
  CHECK(cuts_are(segment_cuts(0, 6.3f, 5.7f), {0, 0.15f, 0.83333f, 1}));  // falling: 6.21 then 5.8
  CHECK(cuts_are(segment_cuts(3, 10, 30), {0, 0.3f, 0.4f, 0.5505f, 0.7005f, 1}));  // all four
  CHECK(cuts_are(segment_cuts(3, 30, 10), {0, 0.2995f, 0.4495f, 0.6f, 0.7f, 1}));
}

int main() {
  test_value_color();
  test_store_advance();
  test_graph_points_and_range();
  test_graph_band();
  test_segment_cuts();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
