# Growell Camera View Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a camera picture under the Health Index gauge of `growell-display.yaml`, refreshed from Home Assistant every 5 s, with ‹ › touch buttons to step between `camera.tapo_cam_1` and `camera.tapo_cam_2`.

**Architecture:** The gauge shrinks to make room. A GT911 touchscreen drives two LVGL buttons. One script, `camera_service(op)`, owns all camera state in function-`static` data shared with a FreeRTOS task: the task downloads `/api/camera_proxy/<entity>?width=320&height=180` (Bearer token) into a PSRAM buffer; the main loop polls every 100 ms and decodes finished downloads through `online_image`'s `RuntimeImage::begin_decode/feed_data/end_decode` into an LVGL `image`. `ha_url` is reduced once at boot to its origin, shared by the history and camera URLs. Pure logic stays in `// BEGIN unit` blocks tested on the host.

**Tech Stack:** ESPHome 2026.9.0 (ESP-IDF 5.5.5, LVGL 9.5), `online_image` (decode target only), `gt911`, ESP-IDF `esp_http_client` + FreeRTOS, existing Docker tooling (`tools/esphome.ps1`, `tools/test.ps1`).

**Spec:** `docs/superpowers/specs/2026-09-25-growell-camera-design.md` (extends `docs/superpowers/specs/2026-09-25-growell-display-design.md`)

## Global Constraints

- Work on branch `growell-display` in `C:\Users\chadc\Logs\01196\growell-display`; commands in PowerShell from that folder; Docker Desktop running.
- Deliverable stays ONE file `growell-display.yaml` (no includes, no external components). `min_version: 2026.9.0` stays.
- Every run of `.\tools\test.ps1` must end `N checks, 0 failures` with `config policy: 0 problem(s)`; every `.\tools\esphome.ps1 compile` must succeed with **no** `warning:` lines from our code.
- Cameras are substitutions: `cam_1: camera.tapo_cam_1`, `cam_1_name: Tapo cam 1`, `cam_2: camera.tapo_cam_2`, `cam_2_name: Tapo cam 2`; start on index 0.
- Layout (left column x 14–304): meter 200×200 at (59, 24), arcs width 30, needle width 5 / `radial_offset` 56 / `length` 44; `f_gauge_value` 44 px, label y 70; gauge name y 132; picture `image` (14, 176) 290×163, bg `0x232325`, radius 12, `clip_corner`; status label x 14 w 290 y 243, `0xE1E1E1` on black 50 %; ‹ button (14, 349) and › button (248, 349) 56×40, radius 10, `0x2A2A2C`, pressed `0x3A3A3C`, symbol `montserrat_20`; camera name label x 70 w 178 y 358, `f_note`, `0x9B9B9B`; HA note unchanged at y 420.
- Touch: `i2c` SDA GPIO19, SCL GPIO20; `gt911` with `reset_pin: GPIO38` and **no** `interrupt_pin`.
- Snapshot fetch: `{origin}/api/camera_proxy/{entity}?width=320&height=180`, header `Authorization: Bearer ${ha_token}`, 10 s timeout, 256 KB PSRAM buffer, every 5 s (or immediately after a switch), only while HA is connected and auth hasn't failed. 401/403 pause fetching until HA reconnects.
- The download task never calls LVGL or `ESP_LOG*`; the main loop never does camera network I/O.
- Status texts are ASCII: `Loading...`, `Camera unavailable`.
- `// BEGIN unit` code is plain C++17 (no `id()`, no LVGL, no ESP-IDF, no logging).

## Review Focus

- `ha_url` saved with a query string but no path (`http://192.168.1.10:8123?x=1`) → the origin must drop it, or every REST URL breaks (host test in Task 1, unit `ha_origin`).
- Home Assistant takes 1–3 s to produce a Tapo snapshot → the gauge, graphs and buttons must stay responsive because the fetch runs in the background task (device checklist item "UI responsive during downloads"; reviewer to check no blocking call sits on the main loop, Task 3).
- The user switches cameras while the old camera's download is still in flight → the old picture must never appear under the new name, and the new camera must load at once rather than 5 s later (Task 3: result tagged with its camera index and discarded when stale; the task waits for the poll to consume a result instead of skipping a cycle).
- HA returns an unscaled full-size JPEG (no libturbojpeg) larger than 256 KB → "Camera unavailable" and a log line, never a buffer overflow (Task 3 read loop stops at capacity).
- A wrong `ha_token` → 401 every 5 s could get the board's IP banned by HA → fetching must pause after the first 401/403 until HA reconnects (Task 3 `auth_failed`).

---

### Task 1: Shared Home Assistant origin

**Files:**
- Modify: `test/logic_test.cpp` (replace the `history_url` wrapper and `test_history_url`; add `ha_origin`; update `main`)
- Modify: `growell-display.yaml` (`esphome: on_boot` lambda, `globals:`, backfill `url` lambda)

**Interfaces:**
- Produces: global `g_ha_origin` (`std::string`, set in `on_boot` before anything uses it); unit `ha_origin` (input `std::string base` → output `std::string origin`); unit `history_url` now takes `std::string origin` (not `base`) and `const char *entity` → `std::string url`.

- [ ] **Step 1: Write the failing tests** — in `test/logic_test.cpp` replace the block from `// ---------------- history_url / backfill_accumulate / hist_parser ----------------` down to (not including) `struct Acc {` with:

```cpp
// ---------------- ha_origin / history_url / backfill_accumulate / hist_parser ----------------
static std::string ha_origin(const std::string &base) {
#include "build/ha_origin.inc"
  return origin;
}

static std::string history_url(const std::string &origin, const char *entity) {
#include "build/history_url.inc"
  return url;
}

```

and replace the whole `test_history_url` function with:

```cpp
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
```

and in `main()` replace the line `  test_history_url();` with:

```cpp
  test_ha_origin();
  test_history_url();
```

- [ ] **Step 2: Run to verify it fails**

Run: `.\tools\test.ps1`
Expected: non-zero exit; g++ error `build/ha_origin.inc: No such file or directory`.

- [ ] **Step 3: Add the origin global** — append to the end of the `globals:` list (after `g_bf_next`):

```yaml
  # Home Assistant origin (scheme://host:port), derived from ha_url at boot.
  - id: g_ha_origin
    type: std::string
```

- [ ] **Step 4: Compute the origin at boot** — replace the `esphome:` block's `on_boot` lambda with:

```yaml
  on_boot:
    priority: -100
    then:
      - lambda: |-
          // Canvas buffers start uninitialised: paint them the tile colour.
          for (lv_obj_t *c : {id(ph_graph), id(ec_graph), id(orp_graph), id(temp_graph)})
            lv_canvas_fill_bg(c, lv_color_hex(0x232325), LV_OPA_COVER);
          // Home Assistant origin for REST calls: ha_url without any path or query.
          const std::string base = "${ha_url}";
          // BEGIN unit ha_origin
          std::string origin = base;
          const size_t scheme = origin.find("://");
          const size_t cut = origin.find_first_of("/?#", scheme == std::string::npos ? 0 : scheme + 3);
          if (cut != std::string::npos)
            origin.erase(cut);
          // END unit ha_origin
          id(g_ha_origin) = origin;
```

- [ ] **Step 5: Build the history URL from the origin** — in `backfill_all`, replace the `url: !lambda |-` body (from `static const char *const ENTITY[4]` through `return url;`) with:

```yaml
                      url: !lambda |-
                        static const char *const ENTITY[4] = {"${ent_ph}", "${ent_ec}", "${ent_orp}", "${ent_temp}"};
                        const std::string &origin = id(g_ha_origin);
                        const char *entity = ENTITY[iteration];
                        // BEGIN unit history_url
                        std::string url = origin;
                        url += "/api/history/period?filter_entity_id=";
                        url += entity;
                        url += "&minimal_response&no_attributes";  // default window: last 24 h
                        // END unit history_url
                        return url;
```

- [ ] **Step 6: Run the tests**

Run: `.\tools\test.ps1`
Expected: `config policy: 0 problem(s)`, extracted units include `ha_origin` and `history_url`, `N checks, 0 failures`, exit 0.

- [ ] **Step 7: Validate and build**

Run: `.\tools\esphome.ps1 config` → `Configuration is valid!`
Run: `.\tools\esphome.ps1 compile` → `Successfully compiled program.`, no `warning:` lines.

- [ ] **Step 8: Commit**

```powershell
git add growell-display.yaml test/logic_test.cpp
git commit -m "Derive the Home Assistant origin once at boot for all REST URLs"
```

---

### Task 2: Camera layout and touch

**Files:**
- Modify: `test/config_policy.py` (optional path argument; GT911 `interrupt_pin` rule)
- Modify: `growell-display.yaml` (substitutions; `f_gauge_value`; gauge widgets; new camera widgets; `image:`, `i2c:`, `touchscreen:` blocks)

**Interfaces:**
- Consumes: `g_ha_origin` is not used yet.
- Produces: LVGL ids `cam_img` (image), `cam_status` (label), `cam_prev`, `cam_next` (buttons), `cam_name` (label); image id `cam_snapshot` (`online_image`, JPEG, RGB565, `resize: 290x163`); touchscreen `touch`; substitutions `cam_1`, `cam_1_name`, `cam_2`, `cam_2_name`.

- [ ] **Step 1: Extend the config policy** — replace `test/config_policy.py` with:

```python
"""Static checks on growell-display.yaml that the C++ tests can't see.

- No open fallback hotspot: `captive_portal` pulls in an unauthenticated firmware
  upload page, and the board carries a Home Assistant token.
- `min_version` must be the ESPHome release the file is actually built with
  (the image tag in tools/esphome.ps1), not an older one we never compiled.
- The GT911 must not declare `interrupt_pin`: the 8048S070C leaves INT unconnected
  and ESPHome stops polling once an interrupt pin is set, so touch would be dead.

Usage: python3 test/config_policy.py [path/to/config.yaml]
"""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
config = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "growell-display.yaml"
yaml_text = config.read_text(encoding="utf-8-sig")
runner = (root / "tools" / "esphome.ps1").read_text(encoding="utf-8")
problems = []

if re.search(r"^captive_portal:", yaml_text, re.M):
    problems.append("captive_portal is enabled (unauthenticated firmware upload on the fallback hotspot)")
if re.search(r"^\s+ap:", yaml_text, re.M):
    problems.append("wifi fallback hotspot (ap:) is enabled")

image = re.search(r"ghcr\.io/esphome/esphome:([\d.]+)", runner)
min_version = re.search(r"^\s+min_version:\s*([\d.]+)", yaml_text, re.M)
if not image or not min_version:
    problems.append("could not find the ESPHome image tag or min_version")
elif image.group(1) != min_version.group(1):
    problems.append(f"min_version {min_version.group(1)} != built-with ESPHome {image.group(1)}")

touch = re.search(r"^touchscreen:\n((?:[ \t].*\n?|\n)*)", yaml_text, re.M)
if touch and re.search(r"^\s+interrupt_pin:", touch.group(1), re.M):
    problems.append("gt911 declares interrupt_pin (INT is not connected on the 8048S070C; touch would never fire)")

for p in problems:
    print("POLICY FAIL:", p)
print(f"config policy: {len(problems)} problem(s)")
sys.exit(1 if problems else 0)
```

- [ ] **Step 2: Add the camera substitutions** — insert after the `ent_temp:` line in `substitutions:`:

```yaml
  # Cameras shown under the gauge (the ‹ › buttons step through them)
  cam_1: camera.tapo_cam_1
  cam_1_name: Tapo cam 1
  cam_2: camera.tapo_cam_2
  cam_2_name: Tapo cam 2
```

- [ ] **Step 3: Shrink the gauge value font** — in `font:`, change the `f_gauge_value` entry's `size: 54` to `size: 44`.

- [ ] **Step 4: Replace the gauge widgets** — replace everything from `        # ---------------- Health Index gauge (left column 14,14 290x452) ----------------` down to (not including) `        - label:` + `            id: ha_note` with:

```yaml
        # ---------------- Health Index gauge (left column 14,14 290x452) ----------------
        - meter:
            id: kpi_meter
            x: 59  # square 200x200, centre at (159,124), arc outer radius 100
            y: 24
            width: 200
            height: 200
            bg_opa: TRANSP
            border_width: 0
            pad_all: 0
            indicator:  # the pivot dot: hidden
              bg_opa: TRANSP
            scales:
              - range_from: 0
                range_to: 100
                angle_range: 180
                rotation: 180  # top semicircle, 0 on the left
                indicators:  # no ticks: key => no ticks or labels
                  - arc: {color: 0xEF4444, width: 30, start_value: 0, end_value: 65}
                  - arc: {color: 0xF59E0B, width: 30, start_value: 65, end_value: 85}
                  - arc: {color: 0x10B981, width: 30, start_value: 85, end_value: 100}
                  - line:
                      id: kpi_needle
                      color: 0xE1E1E1
                      width: 5
                      rounded: true
                      radial_offset: 56  # from r=56 ...
                      length: 44         # ... to the arc's outer edge (r=100)
                      value: 0
                      opa: 0%            # hidden until the first value arrives
        - label:
            id: kpi_value
            x: 14
            y: 70
            width: 290
            text_align: CENTER
            text_font: f_gauge_value
            text_color: 0xE1E1E1
            text: "—"
        - label:
            x: 14
            y: 132
            width: 290
            text_align: CENTER
            text_font: f_gauge_name
            text_color: 0xE1E1E1
            text: "Growell 1 Health Index"
        # ---------------- Camera (under the gauge) ----------------
        - image:
            id: cam_img
            x: 14
            y: 176
            width: 290
            height: 163
            src: cam_snapshot
            bg_color: 0x232325
            bg_opa: COVER
            radius: 12
            clip_corner: true
        - label:
            id: cam_status  # "Loading..." / "Camera unavailable" over the picture
            x: 14
            y: 243
            width: 290
            text_align: CENTER
            text_font: f_note
            text_color: 0xE1E1E1
            bg_color: 0x000000
            bg_opa: 50%
            pad_all: 4
            text: "Loading..."
        - button:
            id: cam_prev
            x: 14
            y: 349
            width: 56
            height: 40
            radius: 10
            bg_color: 0x2A2A2C
            bg_opa: COVER
            border_width: 0
            shadow_width: 0
            text: "\uF053"  # LV_SYMBOL_LEFT
            text_font: montserrat_20
            text_color: 0xE1E1E1
            pressed:
              bg_color: 0x3A3A3C
        - button:
            id: cam_next
            x: 248
            y: 349
            width: 56
            height: 40
            radius: 10
            bg_color: 0x2A2A2C
            bg_opa: COVER
            border_width: 0
            shadow_width: 0
            text: "\uF054"  # LV_SYMBOL_RIGHT
            text_font: montserrat_20
            text_color: 0xE1E1E1
            pressed:
              bg_color: 0x3A3A3C
        - label:
            id: cam_name
            x: 70
            y: 358
            width: 178
            text_align: CENTER
            text_font: f_note
            text_color: 0x9B9B9B
            text: "${cam_1_name}"
```

- [ ] **Step 5: Add the decode target, I²C bus and touchscreen** — insert these blocks directly before the `font:` block:

```yaml
image:
  # Decode target for camera snapshots. The camera_service script downloads the
  # JPEGs itself in a background task; this component never fetches its own URL.
  - platform: online_image
    id: cam_snapshot
    url: "${ha_url}/api/camera_proxy/${cam_1}"
    format: JPEG
    type: RGB565
    resize: 290x163
    buffer_size: 256  # the minimum: its own downloader is never used

i2c:
  - id: touch_bus
    sda: GPIO19
    scl: GPIO20

touchscreen:
  - platform: gt911
    id: touch
    i2c_id: touch_bus
    reset_pin: GPIO38  # no interrupt_pin: GT911 INT is not connected on the 8048S070C
```

- [ ] **Step 6: Prove the new policy rule catches an interrupt pin**

```powershell
New-Item -ItemType Directory -Force test\build | Out-Null
(Get-Content growell-display.yaml -Raw) -replace 'reset_pin: GPIO38', "reset_pin: GPIO38`n    interrupt_pin: GPIO18" | Set-Content -Encoding utf8 test\build\bad-touch.yaml
docker run --rm -v "${PWD}:/config" --entrypoint python3 ghcr.io/esphome/esphome:2026.9.0 /config/test/config_policy.py /config/test/build/bad-touch.yaml
```
Expected: `POLICY FAIL: gt911 declares interrupt_pin ...`, `config policy: 1 problem(s)`, exit 1.

- [ ] **Step 7: Run the suite on the real file**

Run: `.\tools\test.ps1`
Expected: `config policy: 0 problem(s)`, `N checks, 0 failures`, exit 0.

- [ ] **Step 8: Validate and build**

Run: `.\tools\esphome.ps1 config` → `Configuration is valid!` (warnings that GPIO19/GPIO20 are USB-JTAG pins are expected: the logger uses UART0).
Run: `.\tools\esphome.ps1 compile` → `Successfully compiled program.`, no `warning:` lines.

- [ ] **Step 9: Commit**

```powershell
git add growell-display.yaml test/config_policy.py
git commit -m "Make room for a camera under the gauge; add GT911 touch and camera buttons"
```

---

### Task 3: Camera engine

**Files:**
- Modify: `test/logic_test.cpp` (add `camera_step`, `camera_url` wrappers and `test_camera`; update `main`)
- Modify: `growell-display.yaml` (new script `camera_service`; start it in `on_boot`; buttons' `on_click`; 100 ms poll interval)

**Interfaces:**
- Consumes: `g_ha_origin`, `g_ha_up` (existing, maintained by the 2 s interval), `cam_snapshot`, `cam_img`, `cam_status`, `cam_name`, `cam_prev`, `cam_next`, substitutions `cam_*`, `ha_token`.
- Produces: script `camera_service(op: int)` — 0 start (boot), 1 poll (every 100 ms), 2 next, 3 previous; units `camera_step` (inputs `int cur, dir, n` → `int next`) and `camera_url` (inputs `std::string origin`, `const char *entity` → `std::string url`).

- [ ] **Step 1: Write the failing tests** — insert above `int main()` in `test/logic_test.cpp`:

```cpp
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
```

and in `main()` add `  test_camera();` after `  test_backfill_accumulate();`.

- [ ] **Step 2: Run to verify it fails**

Run: `.\tools\test.ps1`
Expected: non-zero exit; g++ error `build/camera_step.inc: No such file or directory`.

- [ ] **Step 3: Add the `camera_service` script** — append to the end of the `script:` list (after `backfill_all`, before `globals:`):

```yaml
  # Camera under the gauge. op: 0 start (boot), 1 poll (every 100 ms), 2 next, 3 previous.
  # A background task downloads snapshots; this lambda (main loop) decodes and shows them.
  - id: camera_service
    parameters:
      op: int
    then:
      - lambda: |-
          static const char *const CAM[] = {"${cam_1}", "${cam_2}"};
          static const char *const CAM_NAME[] = {"${cam_1_name}", "${cam_2_name}"};
          constexpr int N = sizeof(CAM) / sizeof(CAM[0]);
          constexpr size_t CAP = 256 * 1024;  // snapshot buffer (HA scales to ~320x180, ~10-35 KB)
          enum : int { IDLE = 0, READY = 1, FAILED = 2 };
          struct Shared {                      // main loop <-> download task
            std::atomic<int> want{0};          // selected camera index
            std::atomic<bool> ha_up{false};    // mirror of g_ha_up
            std::atomic<bool> auth_failed{false};
            std::atomic<int> state{IDLE};      // task sets READY/FAILED; the poll resets it to IDLE
            int cam = 0;                       // camera the result belongs to
            int http = 0;                      // HTTP status; 0 = network error, -1 = too large
            size_t len = 0;                    // JPEG bytes in buf when READY
            uint8_t *buf = nullptr;
            std::string origin;                // Home Assistant origin, fixed before the task starts
          };
          static Shared s;
          static TaskHandle_t task = nullptr;

          if (op == 0) {  // start: allocate the buffer and launch the download task (once)
            if (task != nullptr)
              return;
            s.buf = (uint8_t *) heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM);
            if (s.buf == nullptr) {
              ESP_LOGE("camera", "No PSRAM for the snapshot buffer; camera disabled");
              return;
            }
            s.origin = id(g_ha_origin);
            xTaskCreatePinnedToCore(
                [](void *) {
                  for (;;) {
                    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));  // every 5 s, or sooner when woken
                    while (s.state.load() != IDLE)                 // previous result not consumed yet
                      vTaskDelay(pdMS_TO_TICKS(50));
                    if (!s.ha_up.load() || s.auth_failed.load())
                      continue;
                    const int cam = s.want.load();
                    const std::string &origin = s.origin;
                    const char *entity = CAM[cam];
                    // BEGIN unit camera_url
                    std::string url = origin;
                    url += "/api/camera_proxy/";
                    url += entity;
                    url += "?width=320&height=180";  // HA shrinks the JPEG server-side (aspect kept)
                    // END unit camera_url
                    esp_http_client_config_t cfg = {};
                    cfg.url = url.c_str();
                    cfg.timeout_ms = 10000;
                    esp_http_client_handle_t c = esp_http_client_init(&cfg);
                    int http = 0;
                    size_t got = 0;
                    if (c != nullptr) {
                      esp_http_client_set_header(c, "Authorization", "Bearer ${ha_token}");
                      if (esp_http_client_open(c, 0) == ESP_OK) {
                        const int64_t n = esp_http_client_fetch_headers(c);
                        http = esp_http_client_get_status_code(c);
                        if (http == 200) {
                          for (;;) {
                            if (got == CAP) {  // bigger than the buffer: give up, never overflow
                              http = -1;
                              break;
                            }
                            const int r = esp_http_client_read(c, (char *) s.buf + got, CAP - got);
                            if (r < 0) {  // network error / timeout
                              http = 0;
                              break;
                            }
                            if (r == 0)  // end of body
                              break;
                            got += r;
                          }
                          if (http == 200 && n > 0 && got != (size_t) n)  // cut short
                            http = 0;
                        }
                      }
                      esp_http_client_cleanup(c);
                    }
                    if (http == 401 || http == 403)
                      s.auth_failed.store(true);
                    s.cam = cam;
                    s.http = http;
                    s.len = got;
                    s.state.store(http == 200 && got > 0 ? READY : FAILED);
                  }
                },
                "cam_dl", 8192, nullptr, 1, &task, 0);
            return;
          }

          if (op == 1) {  // poll: show a finished download, or report why it failed
            if (task == nullptr)
              return;
            const bool up = id(g_ha_up);
            if (up && !s.ha_up.load())  // fresh HA connection: allow the token to be tried again
              s.auth_failed.store(false);
            s.ha_up.store(up);
            const int st = s.state.load();
            if (st == IDLE)
              return;
            if (s.cam == s.want.load()) {  // results for a camera the user left are discarded
              bool shown = false;
              if (st == READY) {
                auto *img = id(cam_snapshot);
                if (img->begin_decode(s.len, esphome::runtime_image::JPEG)) {
                  img->feed_data(s.buf, s.len);
                  shown = img->end_decode();
                }
                if (shown) {
                  lv_image_set_src(id(cam_img), img->get_lv_image_dsc());
                  lv_obj_add_flag(id(cam_status), LV_OBJ_FLAG_HIDDEN);
                } else {
                  ESP_LOGW("camera", "%s: snapshot could not be decoded (progressive or corrupt JPEG?)", CAM[s.cam]);
                }
              } else if (s.http == 401 || s.http == 403) {
                ESP_LOGW("camera", "%s: HTTP %d - check the ha_token secret; paused until HA reconnects",
                         CAM[s.cam], s.http);
              } else if (s.http == -1) {
                ESP_LOGW("camera", "%s: snapshot larger than %u KB", CAM[s.cam], (unsigned) (CAP / 1024));
              } else {
                ESP_LOGW("camera", "%s: snapshot failed (HTTP %d; 0 = network error or timeout)", CAM[s.cam], s.http);
              }
              if (!shown) {
                lv_label_set_text(id(cam_status), "Camera unavailable");
                lv_obj_remove_flag(id(cam_status), LV_OBJ_FLAG_HIDDEN);
              }
            }
            s.state.store(IDLE);
            return;
          }

          // op 2 / 3: next / previous camera
          const int cur = s.want.load();
          const int dir = op == 2 ? 1 : -1;
          const int n = N;
          // BEGIN unit camera_step
          const int next = ((cur + dir) % n + n) % n;
          // END unit camera_step
          s.want.store(next);
          lv_label_set_text(id(cam_name), CAM_NAME[next]);
          lv_label_set_text(id(cam_status), "Loading...");
          lv_obj_remove_flag(id(cam_status), LV_OBJ_FLAG_HIDDEN);
          if (task != nullptr)
            xTaskNotifyGive(task);  // fetch the new camera now, not at the next 5 s tick
```

- [ ] **Step 4: Start the engine at boot** — at the end of the `on_boot` lambda (after `id(g_ha_origin) = origin;`) add:

```cpp
          id(camera_service)->execute(0);  // after g_ha_origin is set
```

- [ ] **Step 5: Wire the buttons** — add to the `cam_prev` button (after its `pressed:` block):

```yaml
            on_click:
              - script.execute: {id: camera_service, op: 3}
```

and to the `cam_next` button:

```yaml
            on_click:
              - script.execute: {id: camera_service, op: 2}
```

- [ ] **Step 6: Poll for finished downloads** — append to the end of the `interval:` list:

```yaml
  - interval: 100ms
    then:
      - script.execute: {id: camera_service, op: 1}
```

- [ ] **Step 7: Run the tests**

Run: `.\tools\test.ps1`
Expected: extracted units include `camera_step` and `camera_url`; `N checks, 0 failures`; exit 0.

- [ ] **Step 8: Validate and build**

Run: `.\tools\esphome.ps1 config` → `Configuration is valid!`
Run: `.\tools\esphome.ps1 compile` → `Successfully compiled program.`, no `warning:` lines. If the compiler rejects a detail inside the lambda (e.g. an enumerator or `constexpr` used inside the captureless task lambda), fix it in place in the YAML with the smallest change (e.g. `static constexpr`) and re-run `.\tools\test.ps1`.

- [ ] **Step 9: Final checks**

```powershell
(Select-String -Path growell-display.yaml -Pattern 'BEGIN unit').Count
$y = Get-Content growell-display.yaml -Raw
$start = $y.IndexOf('xTaskCreatePinnedToCore(')
([regex]::Matches($y.Substring($start, $y.IndexOf('"cam_dl"') - $start), 'lv_|ESP_LOG')).Count
```
Expected: `13` (10 existing units + `ha_origin`, `camera_url`, `camera_step`), then `0` (the download task calls no LVGL or logging).

- [ ] **Step 10: Commit**

```powershell
git add growell-display.yaml test/logic_test.cpp
git commit -m "Fetch camera snapshots in the background and switch cameras with the buttons"
```
