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

int main() {
  test_value_color();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
