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

// ---------------- live_sample ----------------
static bool live_sample(float v) {
#include "build/live_sample.inc"
  return keep;
}

static void test_live_sample() {
  CHECK(live_sample(6.02f));
  CHECK(live_sample(0.0f));
  CHECK(!live_sample(NAN));        // unavailable / unknown
  CHECK(!live_sample(INFINITY));   // HA state "inf" would wreck the y-range for 24 h
  CHECK(!live_sample(-INFINITY));
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

// ---------------- ha_origin / history_url / backfill_accumulate / hist_parser ----------------
static std::string ha_origin(const std::string &base) {
#include "build/ha_origin.inc"
  return origin;
}

static std::string history_url(const std::string &origin, const char *entity) {
#include "build/history_url.inc"
  return url;
}

struct Acc {
  Slots sum{};
  Counts cnt{};
  float carry = NAN;
};
static void backfill_accumulate(Acc &acc, int32_t h_now, const std::vector<std::pair<int64_t, float>> &samples) {
  auto &sum = acc.sum;
  auto &cnt = acc.cnt;
  auto &carry = acc.carry;
#include "build/backfill_accumulate.inc"
  for (const auto &s : samples)
    add_sample(s.first, s.second);
}

#include "build/hist_parser.inc"

// Feed `json` in pieces of `chunk` bytes; collect the samples.
static std::vector<std::pair<int64_t, float>> parse_all(const std::string &json, size_t chunk, bool *done) {
  std::vector<std::pair<int64_t, float>> out;
  HistParser p;
  p.on_sample = [&](int64_t t, float v) { out.emplace_back(t, v); };
  for (size_t pos = 0; pos < json.size(); pos += chunk)
    p.feed(json.data() + pos, std::min(chunk, json.size() - pos));
  if (done)
    *done = p.done;
  CHECK(p.samples == out.size());
  return out;
}

static const int64_t T_10_00 = 1790244000;  // 2026-09-24T10:00:00Z
static const int32_t H_NOW = 994628;        // half-hour of 2026-09-25T10:00:00Z
static const std::string SAMPLE =
    "[[{\"entity_id\":\"sensor.greenhouse_growell_1_ph\",\"state\":\"6.10\",\"attributes\":{},"
    "\"last_changed\":\"2026-09-24T10:00:00+00:00\",\"last_reported\":\"2026-09-24T10:00:00+00:00\","
    "\"last_updated\":\"2026-09-24T10:00:00+00:00\"},"
    "{\"state\":\"6.20\",\"last_changed\":\"2026-09-24T10:14:59.5+00:00\"},"
    "{\"state\":\"unavailable\",\"last_changed\":\"2026-09-24T11:00:00+00:00\"},"
    "{\"state\":\"5.90\",\"last_changed\":\"2026-09-24T12:00:00.123456Z\"}]]";

static void test_ha_origin() {
  CHECK(ha_origin("http://ha:8123") == "http://ha:8123");
  CHECK(ha_origin("http://ha:8123/") == "http://ha:8123");  // trailing slash
  CHECK(ha_origin("http://ha:8123//") == "http://ha:8123");
  CHECK(ha_origin("http://ha:8123/lovelace/0") == "http://ha:8123");  // dashboard URL from the browser
  CHECK(ha_origin("https://ha.example.com/dashboard-garden/0?edit=1") == "https://ha.example.com");
  CHECK(ha_origin("http://192.168.1.10:8123?x=1") == "http://192.168.1.10:8123");  // query, no path
  CHECK(ha_origin("http://192.168.1.10:8123#top") == "http://192.168.1.10:8123");
}

static void test_history_url() {
  CHECK(history_url("http://ha:8123", "sensor.x") ==
        "http://ha:8123/api/history/period?filter_entity_id=sensor.x&minimal_response&no_attributes");
}

static void test_parse_iso() {
  int64_t t = -1;
  CHECK(HistParser::parse_iso("2026-09-24T22:15:00+00:00", t) && t == 1790288100);
  CHECK(HistParser::parse_iso("2026-09-24T22:15:00.123456+00:00", t) && t == 1790288100);
  CHECK(HistParser::parse_iso("2026-09-24T22:15:00-05:00", t) && t == 1790306100);
  CHECK(HistParser::parse_iso("2026-09-24T22:15:00Z", t) && t == 1790288100);
  CHECK(HistParser::parse_iso("2000-02-29T00:00:00+00:00", t) && t == 951782400);
  CHECK(HistParser::parse_iso("1970-01-01T00:00:00+00:00", t) && t == 0);
  CHECK(!HistParser::parse_iso("2026-09-24 22:15:00", t));
  CHECK(!HistParser::parse_iso("2026-09-24T22:15:00+0000", t));
  CHECK(!HistParser::parse_iso("unavailable", t));
}

static void test_parser() {
  bool done = false;
  auto s = parse_all(SAMPLE, SAMPLE.size(), &done);
  CHECK(done);
  CHECK(s.size() == 3);  // "unavailable" skipped
  if (s.size() == 3) {
    CHECK(s[0].first == T_10_00);
    CHECK_NEAR(s[0].second, 6.10f);
    CHECK(s[1].first == T_10_00 + 899);
    CHECK_NEAR(s[1].second, 6.20f);
    CHECK(s[2].first == T_10_00 + 7200);
    CHECK_NEAR(s[2].second, 5.90f);
  }
  for (size_t chunk = 1; chunk <= 64; chunk++) {  // every possible split of keys/values
    auto again = parse_all(SAMPLE, chunk, &done);
    CHECK(done && again == s);
  }
  parse_all(SAMPLE.substr(0, SAMPLE.size() - 1), 512, &done);  // cut off before the last ']'
  CHECK(!done);
  s = parse_all("[]", 512, &done);  // entity with no history
  CHECK(done && s.empty());
  s = parse_all("<html><body>Login required</body></html>", 512, &done);  // proxy page with HTTP 200
  CHECK(!done && s.empty());
  // HA's frontend (or an SSO page) answering with HTTP 200: braces in CSS/JS must not look like JSON.
  s = parse_all("<!DOCTYPE html><html><head><style>html{background:#111}</style>"
                "<script>window.x={a:[1]};</script></head><body></body></html>",
                512, &done);
  CHECK(!done && s.empty());
  s = parse_all("{\"message\":\"Entity not found.\"}", 512, &done);  // JSON error object with 200
  CHECK(!done && s.empty());
  s = parse_all(" \r\n[]", 512, &done);  // leading whitespace before the array is fine
  CHECK(done && s.empty());
  // Chatty sensor: 10,000 changes (~650 KB) streamed in 512-byte chunks.
  std::string big = "[[{\"entity_id\":\"sensor.x\",\"state\":\"6.00\",\"attributes\":{},"
                    "\"last_changed\":\"2026-09-24T10:00:00+00:00\"}";
  for (int n = 1; n < 10000; n++)
    big += ",{\"state\":\"6.01\",\"last_changed\":\"2026-09-24T10:30:00+00:00\"}";
  big += "]]";
  s = parse_all(big, 512, &done);
  CHECK(done && s.size() == 10000);
}

static void test_backfill_accumulate() {
  const int64_t slot0 = (int64_t) (H_NOW - 47) * 1800;  // 2026-09-24T10:30:00Z
  Acc acc;
  backfill_accumulate(acc, H_NOW,
                      {{T_10_00, 6.1f},            // before the window -> carry
                       {T_10_00 + 899, 6.2f},      // also before; latest wins
                       {slot0, 6.3f},              // first slot
                       {T_10_00 + 7200, 5.9f},     // 12:00 -> slot 3
                       {T_10_00 + 7800, 6.1f},     // 12:10 -> slot 3 (mean 6.0)
                       {(int64_t) H_NOW * 1800 + 3600, 7.0f}});  // HA clock ahead -> slot 47
  CHECK_NEAR(acc.carry, 6.2f);
  CHECK(acc.cnt[0] == 1);
  CHECK_NEAR(acc.sum[0], 6.3f);
  CHECK(acc.cnt[3] == 2);
  CHECK_NEAR(acc.sum[3] / acc.cnt[3], 6.0f);
  CHECK(acc.cnt[47] == 1);
  CHECK_NEAR(acc.sum[47], 7.0f);

  // Sensor unchanged all day: HA only returns its days-old start state.
  Acc flat;
  backfill_accumulate(flat, H_NOW, {{T_10_00 - 3 * 86400, 6.05f}});
  for (int k = 0; k < 48; k++)
    CHECK(flat.cnt[k] == 0);
  Slots pts = graph_points(flat.sum, flat.cnt, flat.carry);
  for (int k = 0; k < 48; k++)
    CHECK_NEAR(pts[k], 6.05f);
  Range r = graph_range(pts);
  CHECK(r.first == 0 && r.hi > r.lo);
}

// ---------------- camera_step / camera_url ----------------
static int camera_step(int cur, int dir, int n) {
#include "build/camera_step.inc"
  return next;
}

static std::string camera_url(const std::string &origin, const char *entity) {
#include "build/camera_url.inc"
  return url;
}

static void test_camera() {
  CHECK(camera_step(0, +1, 2) == 1);
  CHECK(camera_step(1, +1, 2) == 0);  // › on the last camera wraps to the first
  CHECK(camera_step(0, -1, 2) == 1);  // ‹ on the first wraps to the last
  CHECK(camera_step(2, +1, 3) == 0);  // a third camera added later
  CHECK(camera_step(0, -1, 3) == 2);
  CHECK(camera_step(1, -1, 3) == 0);
  CHECK(camera_url("http://ha:8123", "camera.tapo_cam_1") ==
        "http://ha:8123/api/camera_proxy/camera.tapo_cam_1?width=320&height=180");
}

int main() {
  test_value_color();
  test_live_sample();
  test_store_advance();
  test_graph_points_and_range();
  test_graph_band();
  test_segment_cuts();
  test_ha_origin();
  test_history_url();
  test_parse_iso();
  test_parser();
  test_backfill_accumulate();
  test_camera();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
