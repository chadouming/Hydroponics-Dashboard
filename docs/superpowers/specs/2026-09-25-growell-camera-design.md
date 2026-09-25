# Growell display — camera view design

Date: 2026-09-25
Status: approved in conversation (parts 1–2); pending written-spec review
Extends: `2026-09-25-growell-display-design.md` (everything there still applies unless changed here)

## Goal

Show the greenhouse cameras below the Health Index gauge and let the user step between them with
two touch buttons, as a lightweight stand-in for the HA `custom:advanced-camera-card`
(`camera.tapo_cam_1`, `camera.tapo_cam_2`).

**Success** = the picture refreshes about every 5 s, the ‹ › buttons switch cameras on the first
tap, and the gauge, graphs and touch stay responsive while pictures download.

## Decisions (made with the user)

| Topic | Decision |
|---|---|
| Video | No live video (the ESP32-S3 cannot decode the H.264/WebRTC stream): snapshots every **5 s** |
| Touch | Board is the **8048S070C** (capacitive GT911) |
| Layout | Gauge shrinks; picture below it; **buttons in a row under the picture** with the camera name between |
| Fetching | **Background task** downloads snapshots (UI never blocks on HA); main loop only decodes |

## Facts this design relies on (verified in ESPHome 2026.9.0 source / a compile spike)

- `image: - platform: online_image` publicly inherits `runtime_image::RuntimeImage`, whose
  `begin_decode(size, runtime_image::JPEG)` / `feed_data(buf, len)` / `end_decode()` decode a
  JPEG from memory into the configured `type: RGB565` + `resize` buffer; `get_lv_image_dsc()` feeds
  `lv_image_set_src`. Baseline JPEG only (progressive is rejected).
- ESP-IDF's `esp_http_client_*`, `heap_caps_malloc`, `xTaskCreatePinnedToCore` (captureless lambda
  as the task function) and `std::atomic` are all usable from YAML lambdas (spike compiled, 0 warnings).
- HA `GET /api/camera_proxy/<entity>?width=W&height=H` accepts `Authorization: Bearer <token>` and
  down-scales JPEGs server-side (turbojpeg DCT factors, aspect kept): 1440p/720p → 320×180,
  1080p → 480×270. A bad token returns 401 and can count toward HA's IP ban (if enabled).
- GT911 on this board: SDA GPIO19, SCL GPIO20, reset GPIO38; its INT line is **not connected**, and
  declaring `interrupt_pin` stops ESPHome's polling (touch would be dead). No calibration/transform
  needed for 800×480 landscape (community configs; confirm on the device).
- LVGL symbols (‹ › as `LV_SYMBOL_LEFT` `` / `LV_SYMBOL_RIGHT` ``) exist only in the built-in
  Montserrat fonts; a single touchscreen is bound to LVGL automatically.

## 1. Layout (left column x 14–304, y 14–466)

| Element | Geometry |
|---|---|
| Gauge `meter` | 280×280 at (19, 24) → centre (159, 164), arc radius 140, arc width 43 (the HA card's proportions); no line needle |
| Gauge pointer | HA-card-style teardrop drawn per pixel (signed distance, anti-aliased) into a 64×64 transparent canvas moved onto the pointer: head centre 0.62 R (the round head and its outline stay inside the band's inner edge, r = 97), head radius 0.046 R, tip 0.888 R (radius 0.0057 R) so only the arrow enters the coloured band; fill `0xE1E1E1`, 0.016 R outline in `0x1C1C1E` that notches the arc; hidden when unavailable |
| Gauge value | `f_gauge_value` Roboto 400 **52 px** with `%`, e.g. "87%"; label x 14 w 290, y 108, centred |
| Gauge name | "Growell 1 Health Index", `f_note` 16 px `0xCFCFCF`, y 179, centred |
| Picture `image` | (14, 253), 290×163, background `0x232325`, radius 12, `clip_corner`; snapshot decoded to 289×163 |
| Status overlay | label over the picture, x 14 w 290, y 320, centred, text `0xE1E1E1` on black 50 %: "Loading..." / "Camera unavailable" (ASCII, so the default glyph set covers it); hidden when a fresh picture is shown |
| ‹ button | (14, 426), 56×40, radius 10, `0x2A2A2C`, pressed `0x3A3A3C`, symbol `` in `montserrat_20` `0xE1E1E1` |
| › button | (248, 426), same style, `` |
| Camera name | label x 70 w 178, y 435, centred, `f_note`, secondary `0x9B9B9B` |
| HA note | band over the top of the picture: x 14 w 290, y 257, amber on black 50 % (only while HA is disconnected) |

Cameras are substitutions: `cam_1: camera.tapo_cam_1`, `cam_1_name: Tapo cam 1`,
`cam_2: camera.tapo_cam_2`, `cam_2_name: Tapo cam 2`. Start on camera 1 (index 0).

## 2. Touch

```yaml
i2c: [{id: touch_bus, sda: GPIO19, scl: GPIO20}]
touchscreen: [{platform: gt911, id: touch, i2c_id: touch_bus, reset_pin: GPIO38}]  # no interrupt_pin
```

## 3. Camera engine

One script `camera_service(op: int)` owns all camera state as function-`static` data shared with a
background task (ops: 0 start, 1 poll, 2 next, 3 previous). The task never touches LVGL; the main
loop never does network I/O for the camera.

- **Origin**: at boot, `ha_url` is reduced once to `scheme://host:port` (new unit `ha_origin`) and
  stored in a global; the history URL and the camera URL both start from it (the history backfill's
  existing trailing-slash / path handling moves into `ha_origin`).
- **Task** (core 0, priority 1, 12 KB stack so an `https://` origin's TLS handshake fits; the ESP-IDF
  CA bundle is attached via `esphome: includes: [<esp_crt_bundle.h>]`, a system header, not a file;
  started by op 0 from `on_boot`): loops on
  `ulTaskNotifyTake(…, 5 s)`; when HA is up, auth hasn't failed and no decoded-but-unshown picture is
  pending, it GETs `{origin}/api/camera_proxy/{entity}?width=320&height=180` with the Bearer token
  (10 s timeout) into a 256 KB PSRAM buffer, then publishes `{camera index, length}` or a failure
  reason (HTTP status / too large / network) through atomics.
- **Poll** (op 1, every 100 ms from an interval): mirrors `g_ha_up` into the shared state (clearing a
  previous auth failure on a fresh HA connection), then if a download finished:
  - for a camera no longer selected → discard;
  - otherwise read the frame size from the JPEG header (unit `jpeg_info`); refuse non-JPEG,
    progressive, or frames over 640×360 (an unscaled snapshot would stall the screen) with
    "Camera unavailable" + a specific log line;
  - else decode (`begin_decode`, `feed_data` until every byte is consumed, then `end_decode` —
    `end_decode()` alone does not report success), `lv_image_set_src`, hide the overlay; a failed
    decode releases the half-painted buffer, clears the picture and shows "Camera unavailable" + log;
  - failure reason → keep the last picture, show "Camera unavailable", log the reason; 401/403 →
    log "check ha_token", stop fetching until HA reconnects.
- **Next / previous** (ops 2/3, from the buttons' `on_click`): index = wrap(index ± 1) (new unit
  `camera_step`), clear the picture (never the old camera under the new name), update the name
  label, show "Loading...", wake the task (`xTaskNotifyGive`).
- **Result classification** (unit `fetch_result`): the real HTTP status when one arrived; 0 for no
  or partial response (timeout, dropped connection, short or incomplete chunked body); -1 when the
  snapshot is larger than the buffer (refused from its Content-Length before reading).
- `online_image` is used only as the decode target: `update_interval` stays `never`, its own URL is
  never fetched, `buffer_size: 256` (the minimum) so it doesn't hold a 64 KB download buffer.

## 4. Error handling

| Situation | Behaviour |
|---|---|
| Timeout / network error / non-200 / body > 256 KB | Last picture stays, "Camera unavailable", log reason, retry next 5 s tick |
| 401 / 403 | "Camera unavailable", log "check ha_token", no more requests until HA reconnects |
| Progressive, non-JPEG, or frame over 640×360 | Refused before decoding → "Camera unavailable", specific log |
| Corrupt JPEG | Decode fails → picture cleared, "Camera unavailable", log |
| HA disconnected | No requests |
| Download finishes for a camera the user already left | Discarded |
| Before the first picture | Tile colour + "Loading…" |

## 5. Testing

- Host tests first (existing harness): `ha_origin` (paths, trailing slashes, query strings),
  `history_url` and `camera_url` built from an origin, `camera_step` wrap-around for 2 and 3 cameras.
- `test/config_policy.py`: fail if the GT911 declares `interrupt_pin`.
- Existing suite stays green; `esphome config` + `esphome compile` clean (no warnings).
- On-device checklist additions: taps land on the buttons; picture refreshes ~5 s; UI responsive
  during downloads; a powered-off camera shows "Camera unavailable".

## Out of scope

Live video, tapping the picture to enlarge it, remembering the selected camera across reboots,
PTZ or other camera controls.
