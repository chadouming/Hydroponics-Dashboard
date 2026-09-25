# Growell 1 Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One ESPHome YAML that turns a Sunton ESP32-8048S070 into a live copy of the HA "Growell 1" card (needle gauge + four 24 h threshold-coloured mini-graphs, backfilled from HA history).

**Architecture:** A single `growell-display.yaml`. LVGL 9 draws a static layout (meter gauge, four tiles with labels and 207×105 canvases). Home Assistant values arrive through `homeassistant` sensors; C++ in ESPHome scripts keeps 48 half-hour slots per graph and repaints the canvases (per-pixel fade fill + `lv_draw_line` segments split at thresholds). On each HA connection a script streams `/api/history/period` through a tiny JSON state machine to rebuild the slots. Pure-logic C++ blocks are fenced with `// BEGIN unit <name>` / `// END unit <name>` so a host test program can compile them straight out of the YAML.

**Tech Stack:** ESPHome 2026.9.0 (ESP-IDF, LVGL 9.5), `mipi_rgb` board preset, Docker image `ghcr.io/esphome/esphome:2026.9.0` (validation, firmware build, and its `g++` for host tests), PowerShell wrappers, Python 3 (inside the image) for unit extraction.

**Spec:** `docs/superpowers/specs/2026-09-25-growell-display-design.md`

## Global Constraints

- All commands run from `C:\Users\chadc\Logs\01196\growell-display` (PowerShell). Docker Desktop must be running.
- Validation/builds only through `ghcr.io/esphome/esphome:2026.9.0`; the YAML declares `min_version: 2026.5.0`.
- Deliverable is ONE file, `growell-display.yaml`: no `includes:`, no external components, no extra files for the user.
- Hardware: `mipi_rgb` `model: ESP32-8048S070`, `update_interval: never`, `auto_clear_enabled: false`; `esp32` `variant: esp32s3`, `flash_size: 16MB`, `framework: esp-idf`, `advanced: execute_from_psram: true`, sdkconfig `CONFIG_ESP32S3_DATA_CACHE_64KB: y` and `CONFIG_ESP32S3_DATA_CACHE_LINE_64B: y`; `psram: mode: octal, speed: 80MHz`; `logger: hardware_uart: UART0`; backlight `ledc` GPIO2 1220Hz → monochromatic light "Backlight" `ALWAYS_ON`.
- Secrets used (dummy values in the local `secrets.yaml`): `wifi_ssid`, `wifi_password`, `growell_api_key`, `ha_url`, `ha_token`.
- Entities are substitutions: `ent_kpi: sensor.growell_1_kpi_score`, `ent_ph: sensor.greenhouse_growell_1_ph`, `ent_ec: sensor.greenhouse_growell_1_conductivity_value`, `ent_orp: sensor.greenhouse_growell_1_orp_value`, `ent_temp: sensor.greenhouse_growell_1_temperature`.
- Colours: screen `0x1C1C1E`, tile `0x232325`, tile border `0x2A2A2C`, primary text `0xE1E1E1`, secondary text `0x9B9B9B`, icon `0x44739E`, red `0xEF4444`, amber `0xF59E0B`, green `0x10B981`.
- Graph index `i`: 0 pH, 1 EC, 2 ORP, 3 Temp. Decimals: pH 2, EC 0, ORP 0, Temp 1, Health Index 0. Units: "", "µS/cm", "mV", "°C".
- Value colour (card_mod): green `gLo ≤ v ≤ gHi`; amber `aLo ≤ v < gLo` or `gHi < v ≤ aHi`; else red; NaN → grey `0x9B9B9B` with text "—". Limits (gLo, gHi, aLo, aHi): pH (5.8, 6.2, 5.5, 6.5), EC (2000, 2500, 1800, 2700), ORP (300, 400, 250, 450), Temp (18, 21, 16, 24).
- Graph bands (mini-graph `color_thresholds`, hard): colour of highest threshold ≤ v, below first → first. pH 5/5.5/5.8/6.21/6.51, EC 1000/1800/2000/2501/2701, ORP 150/250/300/401/451, Temp 12/16/18/21.01/24.01, colours R/A/G/A/R.
- LVGL facts (verified in 2026.9.0 source): `id(widget)` is `lv_obj_t*`; meter needle `id(kpi_needle)` is `IndicatorLine*` with `set_value(int)` (clamped) and `->obj`; `lvgl.indicator.update` ignores `opa`, so hide/show the needle with `lv_obj_set_style_opa(id(kpi_needle)->obj, …, LV_PART_MAIN)`; canvas `transparent: false` = RGB565, stride = w·2, buffer in PSRAM; `lv_value_precise_t` is `int32_t`; `lv_color_mix(c1, c2, mix)` → 255 = c1; quote `scrollbar_mode: "OFF"`; `pad_column` only inside `layout:`; `obj` carries a theme card style that must be overridden.
- Font rules (verified): if `glyphs` is given, only those glyphs are included (no default glyphset); a duplicate character across `glyphs`/`extras` is an error; `µ` is not in GF_Latin_Kernel/Core; `–`, `—`, `°` are in GF_Latin_Kernel.
- Code in `// BEGIN unit` blocks must be plain C++17 (no `id()`, no `ESP_LOG*`, no LVGL, no `return` of values) so `test/logic_test.cpp` can `#include` it.

## Review Focus

- `ha_url` saved with a trailing slash (`http://ha:8123/`) → the request URL must still be `…:8123/api/history/period?…` (test in Task 5, unit `history_url`).
- A reverse proxy/login page answers the history request with HTTP 200 and HTML → must be treated as an incomplete download (graph untouched, retried), not as empty history (test in Task 5, unit `hist_parser`).
- A chatty sensor with ~10,000 state changes in 24 h (≈650 KB response) → must stream in 512-byte chunks and count every sample (test in Task 5, unit `hist_parser`).
- A sensor that hasn't changed all day (HA returns only a days-old start state) → a full, centred flat line, not a blank graph (test in Task 5, units `backfill_accumulate` + `graph_points` + `graph_range`).
- HA timestamps a little ahead of the board's clock → the sample lands in the newest slot instead of being dropped (test in Task 5, unit `backfill_accumulate`).

---

### Task 1: Tooling and hardware base

**Files:**
- Create: `.gitignore`, `secrets.yaml` (dummy values, local validation only), `tools/esphome.ps1`, `growell-display.yaml`

**Interfaces:**
- Produces: `.\tools\esphome.ps1 config|compile`; YAML ids `api_server`, `ha_time`, `backlight_pwm`, `backlight`, `main_display`, sensors `kpi`, `ph`, `ec`, `orp`, `temp`; substitutions `ent_*`, `ha_url`, `ha_token`.

- [ ] **Step 1: Make sure Docker is up and the image is present**

```powershell
docker info *> $null
if ($LASTEXITCODE -ne 0) {
  Start-Process 'C:\Program Files\Docker\Docker\Docker Desktop.exe'
  for ($n = 0; $n -lt 90; $n++) { Start-Sleep 2; docker info *> $null; if ($LASTEXITCODE -eq 0) { break } }
}
docker pull ghcr.io/esphome/esphome:2026.9.0
```
Expected: `Status: Downloaded newer image for ghcr.io/esphome/esphome:2026.9.0` (or "Image is up to date").

- [ ] **Step 2: Create `.gitignore`**

```gitignore
.esphome/
test/build/
```

- [ ] **Step 3: Create `secrets.yaml` (dummy values; the user's real ones live in ESPHome Builder)**

```yaml
# Generated by tools/esphome.ps1 with random throwaway values (git-ignored, never committed).
wifi_ssid: <random>
wifi_password: <random>
growell_api_key: <random 32-byte base64 key>
ha_url: "http://192.168.1.10:8123"
ha_token: <random>
```

- [ ] **Step 4: Create `tools/esphome.ps1`**

```powershell
# Run the ESPHome 2026.9.0 CLI (Docker) on growell-display.yaml.
#   .\tools\esphome.ps1 config    validate the YAML
#   .\tools\esphome.ps1 compile   build the firmware (first run downloads the ESP-IDF toolchain)
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('config', 'compile')]
    [string]$Command
)
$root = Split-Path -Parent $PSScriptRoot
# /cache keeps toolchains between runs; /build keeps the build tree off the slow Windows mount.
docker run --rm `
    -v "${root}:/config" `
    -v growell-esphome-cache:/cache `
    -v growell-esphome-build:/build `
    ghcr.io/esphome/esphome:2026.9.0 $Command growell-display.yaml
exit $LASTEXITCODE
```

- [ ] **Step 5: Run it before the YAML exists (expected failure)**

Run: `.\tools\esphome.ps1 config`
Expected: non-zero exit, error mentioning `growell-display.yaml` not found.

- [ ] **Step 6: Create `growell-display.yaml` (hardware + connectivity + sensors)**

```yaml
# ============================================================================
# Growell 1 display: Sunton ESP32-8048S070 (7", 800x480, ESP32-S3), ESPHome 2026.5+
# Shows the Home Assistant "Growell 1" card: Health Index gauge plus 24 h
# mini-graphs for pH, EC, ORP and water temperature.
#
# One-time setup
#   1. Home Assistant: create a non-admin user for this display, log in as it,
#      then Profile > Security > Long-lived access tokens > Create token.
#   2. ESPHome Builder: New device > ESP32-S3 > Skip installation. Open its YAML
#      and copy the generated `api: encryption: key:` value.
#   3. ESPHome Builder > Secrets, add (wifi_ssid / wifi_password too if missing):
#        growell_api_key: "<the key from step 2>"
#        ha_url: "http://192.168.1.10:8123"     # your Home Assistant address
#        ha_token: "<the token from step 1>"
#   4. Replace the device's whole YAML with this file and Install
#      (the first time over USB, afterwards over Wi-Fi).
# ============================================================================
substitutions:
  name: growell-display
  friendly_name: Growell Display
  # Home Assistant entities shown on the screen
  ent_kpi: sensor.growell_1_kpi_score
  ent_ph: sensor.greenhouse_growell_1_ph
  ent_ec: sensor.greenhouse_growell_1_conductivity_value
  ent_orp: sensor.greenhouse_growell_1_orp_value
  ent_temp: sensor.greenhouse_growell_1_temperature
  # Used by the history backfill (Home Assistant REST API)
  ha_url: !secret ha_url
  ha_token: !secret ha_token

esphome:
  name: ${name}
  friendly_name: ${friendly_name}
  min_version: 2026.5.0

esp32:
  variant: esp32s3
  flash_size: 16MB
  framework:
    type: esp-idf
    advanced:
      # Run code from PSRAM so flash writes (OTA, Wi-Fi config) don't starve the panel.
      execute_from_psram: true
    sdkconfig_options:
      # Bigger data cache: stops the RGB panel image from drifting under load.
      CONFIG_ESP32S3_DATA_CACHE_64KB: y
      CONFIG_ESP32S3_DATA_CACHE_LINE_64B: y

psram:
  mode: octal
  speed: 80MHz

logger:
  hardware_uart: UART0  # the board's USB port goes through a CH340 on UART0

api:
  id: api_server
  encryption:
    key: !secret growell_api_key

ota:
  - platform: esphome

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:

captive_portal:

time:
  - platform: homeassistant
    id: ha_time

output:
  - platform: ledc
    id: backlight_pwm
    pin: GPIO2
    frequency: 1220Hz

light:
  - platform: monochromatic
    id: backlight
    name: Backlight
    output: backlight_pwm
    restore_mode: ALWAYS_ON

display:
  - platform: mipi_rgb
    id: main_display
    model: ESP32-8048S070
    update_interval: never  # LVGL drives the panel
    auto_clear_enabled: false

sensor:
  - platform: homeassistant
    id: kpi
    entity_id: ${ent_kpi}
    internal: true
  - platform: homeassistant
    id: ph
    entity_id: ${ent_ph}
    internal: true
  - platform: homeassistant
    id: ec
    entity_id: ${ent_ec}
    internal: true
  - platform: homeassistant
    id: orp
    entity_id: ${ent_orp}
    internal: true
  - platform: homeassistant
    id: temp
    entity_id: ${ent_temp}
    internal: true
```

- [ ] **Step 7: Validate**

Run: `.\tools\esphome.ps1 config`
Expected: ends with `Configuration is valid!`, exit 0. (If `ap:` with no keys is rejected, change it to `ap: {}` and re-run.)

- [ ] **Step 8: Build the firmware (first build downloads ESP-IDF; allow up to ~30 min, run in background)**

Run: `.\tools\esphome.ps1 compile`
Expected: ends with `Successfully compiled program.`, exit 0.

- [ ] **Step 9: Commit**

```powershell
git add .gitignore secrets.yaml tools/esphome.ps1 growell-display.yaml
git commit -m "Add hardware base config and ESPHome Docker tooling"
```

---

### Task 2: Static screen (fonts, gauge, tiles, canvases)

**Files:**
- Modify: `growell-display.yaml` (replace the `esphome:` block; append `font:` and `lvgl:` blocks at the end)

**Interfaces:**
- Consumes: `main_display` (Task 1).
- Produces: LVGL ids `kpi_meter`, `kpi_needle` (IndicatorLine), `kpi_value`, `ha_note`, `ph_value`/`ph_unit`/`ph_graph`, `ec_value`/`ec_unit`/`ec_graph`, `orp_value`/`orp_unit`/`orp_graph`, `temp_value`/`temp_unit`/`temp_graph`; fonts `f_name`, `f_value`, `f_unit`, `f_icon`, `f_gauge_value`, `f_gauge_name`, `f_note`; styles `tile_style`, `bare_style`.

- [ ] **Step 1: Replace the `esphome:` block (adds canvas clearing at boot)**

```yaml
esphome:
  name: ${name}
  friendly_name: ${friendly_name}
  min_version: 2026.5.0
  on_boot:
    priority: -100
    then:
      - lambda: |-
          // Canvas buffers start uninitialised: paint them the tile colour.
          for (lv_obj_t *c : {id(ph_graph), id(ec_graph), id(orp_graph), id(temp_graph)})
            lv_canvas_fill_bg(c, lv_color_hex(0x232325), LV_OPA_COVER);
```

- [ ] **Step 2: Append the fonts**

```yaml
font:
  - id: f_name  # tile titles, e.g. "pH (5.8 – 6.2)" (default GF_Latin_Kernel has "–")
    file: "gfonts://Roboto@400"
    size: 17
    bpp: 4
  - id: f_value  # tile values
    file: "gfonts://Roboto@700"
    size: 38
    bpp: 4
    glyphs: "0123456789.-—"
  - id: f_unit
    file: "gfonts://Roboto@700"
    size: 18
    bpp: 4
    glyphs: "µS/cmV°C"
  - id: f_icon  # Material Design Icons used by the HA card
    file: "https://github.com/Templarian/MaterialDesign-Webfont/raw/v7.4.47/fonts/materialdesignicons-webfont.ttf"
    size: 24
    bpp: 4
    glyphs:
      - "\U000F124B"  # mdi:flask-round-bottom
      - "\U000F0241"  # mdi:flash
      - "\U000F1855"  # mdi:water-opacity
      - "\U000F1A80"  # mdi:thermometer-water
  - id: f_gauge_value
    file: "gfonts://Roboto@500"
    size: 54
    bpp: 4
    glyphs: "0123456789.-—"
  - id: f_gauge_name
    file: "gfonts://Roboto@400"
    size: 20
    bpp: 4
  - id: f_note
    file: "gfonts://Roboto@400"
    size: 16
    bpp: 4
```

- [ ] **Step 3: Append the LVGL screen**

```yaml
lvgl:
  displays: main_display
  log_level: WARN
  default_font: f_note
  style_definitions:
    - id: tile_style  # card_mod: rgba(255,255,255,.03) fill, .06 border, 16px radius
      bg_color: 0x232325
      bg_opa: COVER
      border_color: 0x2A2A2C
      border_width: 1
      radius: 16
      pad_all: 0
    - id: bare_style  # invisible container
      bg_opa: TRANSP
      border_width: 0
      radius: 0
      pad_all: 0
  pages:
    - id: main_page
      bg_color: 0x1C1C1E
      bg_opa: COVER
      pad_all: 0
      scrollable: false
      scrollbar_mode: "OFF"
      widgets:
        # ---------------- Health Index gauge (left column 14,14 290x452) ----------------
        - meter:
            id: kpi_meter
            x: 34  # square 250x250, centre at (159,264), arc outer radius 125
            y: 139
            width: 250
            height: 250
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
                  - arc: {color: 0xEF4444, width: 38, start_value: 0, end_value: 65}
                  - arc: {color: 0xF59E0B, width: 38, start_value: 65, end_value: 85}
                  - arc: {color: 0x10B981, width: 38, start_value: 85, end_value: 100}
                  - line:
                      id: kpi_needle
                      color: 0xE1E1E1
                      width: 6
                      rounded: true
                      radial_offset: 70  # from r=70 ...
                      length: 55         # ... to the arc's outer edge (r=125)
                      value: 0
                      opa: 0%            # hidden until the first value arrives
        - label:
            id: kpi_value
            x: 14
            y: 200
            width: 290
            text_align: CENTER
            text_font: f_gauge_value
            text_color: 0xE1E1E1
            text: "—"
        - label:
            x: 14
            y: 284
            width: 290
            text_align: CENTER
            text_font: f_gauge_name
            text_color: 0xE1E1E1
            text: "Growell 1 Health Index"
        - label:
            id: ha_note  # hidden while Home Assistant is connected
            x: 14
            y: 420
            width: 290
            text_align: CENTER
            text_font: f_note
            text_color: 0xF59E0B
            text: "Home Assistant disconnected"
        # ---------------- pH tile ----------------
        - obj:
            styles: tile_style
            x: 316
            y: 14
            width: 229
            height: 220
            scrollable: false
            scrollbar_mode: "OFF"
            widgets:
              - label: {x: 12, y: 12, text: "pH (5.8 – 6.2)", text_font: f_name, text_color: 0x9B9B9B}
              - label: {align: TOP_RIGHT, x: -12, y: 10, text: "\U000F124B", text_font: f_icon, text_color: 0x44739E}
              - obj:
                  styles: bare_style
                  x: 12
                  y: 40
                  width: SIZE_CONTENT
                  height: SIZE_CONTENT
                  scrollable: false
                  scrollbar_mode: "OFF"
                  layout: {type: flex, flex_flow: ROW, flex_align_cross: END, pad_column: 6}
                  widgets:
                    - label: {id: ph_value, text: "—", text_font: f_value, text_color: 0x9B9B9B}
                    - label: {id: ph_unit, text: "", text_font: f_unit, text_color: 0x9B9B9B, pad_bottom: 5}
              - canvas: {id: ph_graph, x: 10, y: 104, width: 207, height: 105, transparent: false}
        # ---------------- EC tile ----------------
        - obj:
            styles: tile_style
            x: 557
            y: 14
            width: 229
            height: 220
            scrollable: false
            scrollbar_mode: "OFF"
            widgets:
              - label: {x: 12, y: 12, text: "EC (2000 – 2500)", text_font: f_name, text_color: 0x9B9B9B}
              - label: {align: TOP_RIGHT, x: -12, y: 10, text: "\U000F0241", text_font: f_icon, text_color: 0x44739E}
              - obj:
                  styles: bare_style
                  x: 12
                  y: 40
                  width: SIZE_CONTENT
                  height: SIZE_CONTENT
                  scrollable: false
                  scrollbar_mode: "OFF"
                  layout: {type: flex, flex_flow: ROW, flex_align_cross: END, pad_column: 6}
                  widgets:
                    - label: {id: ec_value, text: "—", text_font: f_value, text_color: 0x9B9B9B}
                    - label: {id: ec_unit, text: "µS/cm", text_font: f_unit, text_color: 0x9B9B9B, pad_bottom: 5}
              - canvas: {id: ec_graph, x: 10, y: 104, width: 207, height: 105, transparent: false}
        # ---------------- ORP tile ----------------
        - obj:
            styles: tile_style
            x: 316
            y: 246
            width: 229
            height: 220
            scrollable: false
            scrollbar_mode: "OFF"
            widgets:
              - label: {x: 12, y: 12, text: "ORP (300 – 400)", text_font: f_name, text_color: 0x9B9B9B}
              - label: {align: TOP_RIGHT, x: -12, y: 10, text: "\U000F1855", text_font: f_icon, text_color: 0x44739E}
              - obj:
                  styles: bare_style
                  x: 12
                  y: 40
                  width: SIZE_CONTENT
                  height: SIZE_CONTENT
                  scrollable: false
                  scrollbar_mode: "OFF"
                  layout: {type: flex, flex_flow: ROW, flex_align_cross: END, pad_column: 6}
                  widgets:
                    - label: {id: orp_value, text: "—", text_font: f_value, text_color: 0x9B9B9B}
                    - label: {id: orp_unit, text: "mV", text_font: f_unit, text_color: 0x9B9B9B, pad_bottom: 5}
              - canvas: {id: orp_graph, x: 10, y: 104, width: 207, height: 105, transparent: false}
        # ---------------- Temp tile ----------------
        - obj:
            styles: tile_style
            x: 557
            y: 246
            width: 229
            height: 220
            scrollable: false
            scrollbar_mode: "OFF"
            widgets:
              - label: {x: 12, y: 12, text: "Temp (18 – 21)", text_font: f_name, text_color: 0x9B9B9B}
              - label: {align: TOP_RIGHT, x: -12, y: 10, text: "\U000F1A80", text_font: f_icon, text_color: 0x44739E}
              - obj:
                  styles: bare_style
                  x: 12
                  y: 40
                  width: SIZE_CONTENT
                  height: SIZE_CONTENT
                  scrollable: false
                  scrollbar_mode: "OFF"
                  layout: {type: flex, flex_flow: ROW, flex_align_cross: END, pad_column: 6}
                  widgets:
                    - label: {id: temp_value, text: "—", text_font: f_value, text_color: 0x9B9B9B}
                    - label: {id: temp_unit, text: "°C", text_font: f_unit, text_color: 0x9B9B9B, pad_bottom: 5}
              - canvas: {id: temp_graph, x: 10, y: 104, width: 207, height: 105, transparent: false}
```

- [ ] **Step 4: Validate**

Run: `.\tools\esphome.ps1 config`
Expected: `Configuration is valid!`. Fix any schema error it names (keys above were checked against 2026.9.0 source; the likeliest nits are option spellings, e.g. an opacity written as `0%` vs `TRANSP`).

- [ ] **Step 5: Build**

Run: `.\tools\esphome.ps1 compile`
Expected: `Successfully compiled program.`

- [ ] **Step 6: Commit**

```powershell
git add growell-display.yaml
git commit -m "Add LVGL layout: gauge, four tiles, fonts and graph canvases"
```

---

### Task 3: Host test harness + live values

**Files:**
- Create: `tools/extract_units.py`, `tools/test.ps1`, `test/logic_test.cpp`
- Modify: `growell-display.yaml` (replace the `sensor:` block; append a `script:` block)

**Interfaces:**
- Consumes: label ids from Task 2, sensors from Task 1.
- Produces: script `tile_update(i: int, v: float)`; unit `value_color` (inputs `float v, gLo, gHi, aLo, aHi`; output `uint32_t col`); `.\tools\test.ps1`; test wrappers pattern (`#include "build/<unit>.inc"`).

- [ ] **Step 1: Create `tools/extract_units.py`**

```python
"""Copy every '// BEGIN unit <name>' ... '// END unit <name>' block out of
growell-display.yaml into test/build/<name>.inc so test/logic_test.cpp can
compile exactly the code that ships in the YAML."""
import pathlib
import re
import textwrap

root = pathlib.Path(__file__).resolve().parent.parent
source = (root / "growell-display.yaml").read_text(encoding="utf-8")
out = root / "test" / "build"
out.mkdir(parents=True, exist_ok=True)
for stale in out.glob("*.inc"):
    stale.unlink()

pattern = re.compile(r"^[ \t]*// BEGIN unit (\w+)[^\n]*\n(.*?)^[ \t]*// END unit \1\b", re.S | re.M)
names = []
for match in pattern.finditer(source):
    name, body = match.group(1), textwrap.dedent(match.group(2))
    (out / f"{name}.inc").write_text(body, encoding="utf-8")
    names.append(name)
print("extracted units:", ", ".join(sorted(names)) or "(none)")
```

- [ ] **Step 2: Create `tools/test.ps1`**

```powershell
# Extract the "// BEGIN unit" blocks from growell-display.yaml and run test/logic_test.cpp
# using the g++ and python3 that ship in the ESPHome image.
$root = Split-Path -Parent $PSScriptRoot
docker run --rm -v "${root}:/config" --entrypoint bash ghcr.io/esphome/esphome:2026.9.0 -c `
    'set -e; cd /config; python3 tools/extract_units.py; g++ -std=gnu++17 -Wall -Wextra -O1 -o test/build/logic_test test/logic_test.cpp; test/build/logic_test'
exit $LASTEXITCODE
```

- [ ] **Step 3: Write the failing test `test/logic_test.cpp`**

```cpp
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
```

- [ ] **Step 4: Run it to verify it fails**

Run: `.\tools\test.ps1`
Expected: non-zero exit; `extracted units: (none)` then a g++ error `build/value_color.inc: No such file or directory`.

- [ ] **Step 5: Replace the `sensor:` block (values now drive the screen)**

```yaml
sensor:
  - platform: homeassistant
    id: kpi
    entity_id: ${ent_kpi}
    internal: true
    on_value:
      - lambda: |-
          lv_obj_t *needle = id(kpi_needle)->obj;
          if (std::isnan(x)) {  // unavailable / unknown
            lv_label_set_text(id(kpi_value), "—");
            lv_obj_set_style_opa(needle, LV_OPA_TRANSP, LV_PART_MAIN);
          } else {
            lv_label_set_text_fmt(id(kpi_value), "%.0f", x);
            id(kpi_needle)->set_value((int) lroundf(x));  // clamped to 0..100
            lv_obj_set_style_opa(needle, LV_OPA_COVER, LV_PART_MAIN);
          }
  - platform: homeassistant
    id: ph
    entity_id: ${ent_ph}
    internal: true
    on_value:
      - script.execute: {id: tile_update, i: 0, v: !lambda "return x;"}
  - platform: homeassistant
    id: ec
    entity_id: ${ent_ec}
    internal: true
    on_value:
      - script.execute: {id: tile_update, i: 1, v: !lambda "return x;"}
  - platform: homeassistant
    id: orp
    entity_id: ${ent_orp}
    internal: true
    on_value:
      - script.execute: {id: tile_update, i: 2, v: !lambda "return x;"}
  - platform: homeassistant
    id: temp
    entity_id: ${ent_temp}
    internal: true
    on_value:
      - script.execute: {id: tile_update, i: 3, v: !lambda "return x;"}
```

- [ ] **Step 6: Append the `script:` block with `tile_update`**

```yaml
script:
  # New reading for tile i (0 pH, 1 EC, 2 ORP, 3 Temp): value text + colour.
  - id: tile_update
    parameters:
      i: int
      v: float
    then:
      - lambda: |-
          lv_obj_t *const VALUE[4] = {id(ph_value), id(ec_value), id(orp_value), id(temp_value)};
          lv_obj_t *const UNIT[4] = {id(ph_unit), id(ec_unit), id(orp_unit), id(temp_unit)};
          static const char *const FORMAT[4] = {"%.2f", "%.0f", "%.0f", "%.1f"};
          // card_mod colour rules: green lo, green hi, amber lo, amber hi
          static const float LIMITS[4][4] = {
              {5.8f, 6.2f, 5.5f, 6.5f}, {2000, 2500, 1800, 2700}, {300, 400, 250, 450}, {18, 21, 16, 24}};
          const float gLo = LIMITS[i][0], gHi = LIMITS[i][1], aLo = LIMITS[i][2], aHi = LIMITS[i][3];
          uint32_t col;
          // BEGIN unit value_color
          constexpr uint32_t GREY = 0x9B9B9B, RED = 0xEF4444, AMBER = 0xF59E0B, GREEN = 0x10B981;
          if (std::isnan(v))
            col = GREY;
          else if (v >= gLo && v <= gHi)
            col = GREEN;
          else if ((v >= aLo && v < gLo) || (v > gHi && v <= aHi))
            col = AMBER;
          else
            col = RED;
          // END unit value_color
          if (std::isnan(v))
            lv_label_set_text(VALUE[i], "—");
          else
            lv_label_set_text_fmt(VALUE[i], FORMAT[i], v);
          lv_obj_set_style_text_color(VALUE[i], lv_color_hex(col), LV_PART_MAIN);
          lv_obj_set_style_text_color(UNIT[i], lv_color_hex(col), LV_PART_MAIN);
```

- [ ] **Step 7: Run the tests**

Run: `.\tools\test.ps1`
Expected: `extracted units: value_color`, then `17 checks, 0 failures`, exit 0.

- [ ] **Step 8: Validate and build**

Run: `.\tools\esphome.ps1 config` → `Configuration is valid!`
Run: `.\tools\esphome.ps1 compile` → `Successfully compiled program.`

- [ ] **Step 9: Commit**

```powershell
git add tools/extract_units.py tools/test.ps1 test/logic_test.cpp growell-display.yaml
git commit -m "Show live values: gauge needle, tile values coloured by card_mod rules"
```

---

### Task 4: History store and graph drawing

**Files:**
- Modify: `growell-display.yaml` (replace the `tile_update` script; add scripts `graph_advance`, `graph_redraw`; append `globals:` and `interval:` blocks)
- Modify: `test/logic_test.cpp` (add wrappers/tests; replace `main`)

**Interfaces:**
- Consumes: `tile_update(i, v)` (Task 3), canvases `*_graph` (Task 2), `ha_time` (Task 1).
- Produces: globals `g_sum` (`std::array<std::array<float, 48>, 4>`), `g_cnt` (`std::array<std::array<uint16_t, 48>, 4>`), `g_head` (`std::array<int32_t, 4>`, half-hour number of slot 47, 0 = not started), `g_carry` (`std::array<float, 4>`, NaN = unknown); scripts `graph_advance(i: int)`, `graph_redraw(i: int)`; units `store_advance` (refs `sum, cnt, head, carry`, input `int32_t h`), `graph_points` (inputs `sum, cnt, carry`; output `std::array<float,48> pts`), `graph_range` (input `pts`; outputs `float lo, hi; int first`), `graph_band` (input `int i`; defines `TH[4][5]` and `band(float) -> uint32_t`).

- [ ] **Step 1: Add the failing tests to `test/logic_test.cpp`** — insert above `int main()`:

```cpp
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
```

and replace `main` with:

```cpp
int main() {
  test_value_color();
  test_store_advance();
  test_graph_points_and_range();
  test_graph_band();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `.\tools\test.ps1`
Expected: non-zero exit; g++ error `build/store_advance.inc: No such file or directory`.

- [ ] **Step 3: Append the `globals:` block**

```yaml
globals:
  # 48 half-hour slots per graph (slot 47 = the current half-hour): reading sum and count.
  - id: g_sum
    type: std::array<std::array<float, 48>, 4>
  - id: g_cnt
    type: std::array<std::array<uint16_t, 48>, 4>
  # Half-hour number (unix time / 1800) of slot 47; 0 = not started yet.
  - id: g_head
    type: std::array<int32_t, 4>
  # Value in effect before slot 0 (NaN = unknown).
  - id: g_carry
    type: std::array<float, 4>
    initial_value: "std::array<float, 4>{NAN, NAN, NAN, NAN}"
```

- [ ] **Step 4: Replace the whole `script:` block (tile_update now records; adds graph_advance and graph_redraw)**

```yaml
script:
  # New reading for tile i (0 pH, 1 EC, 2 ORP, 3 Temp): value text + colour, then history.
  - id: tile_update
    parameters:
      i: int
      v: float
    then:
      - lambda: |-
          lv_obj_t *const VALUE[4] = {id(ph_value), id(ec_value), id(orp_value), id(temp_value)};
          lv_obj_t *const UNIT[4] = {id(ph_unit), id(ec_unit), id(orp_unit), id(temp_unit)};
          static const char *const FORMAT[4] = {"%.2f", "%.0f", "%.0f", "%.1f"};
          // card_mod colour rules: green lo, green hi, amber lo, amber hi
          static const float LIMITS[4][4] = {
              {5.8f, 6.2f, 5.5f, 6.5f}, {2000, 2500, 1800, 2700}, {300, 400, 250, 450}, {18, 21, 16, 24}};
          const float gLo = LIMITS[i][0], gHi = LIMITS[i][1], aLo = LIMITS[i][2], aHi = LIMITS[i][3];
          uint32_t col;
          // BEGIN unit value_color
          constexpr uint32_t GREY = 0x9B9B9B, RED = 0xEF4444, AMBER = 0xF59E0B, GREEN = 0x10B981;
          if (std::isnan(v))
            col = GREY;
          else if (v >= gLo && v <= gHi)
            col = GREEN;
          else if ((v >= aLo && v < gLo) || (v > gHi && v <= aHi))
            col = AMBER;
          else
            col = RED;
          // END unit value_color
          if (std::isnan(v))
            lv_label_set_text(VALUE[i], "—");
          else
            lv_label_set_text_fmt(VALUE[i], FORMAT[i], v);
          lv_obj_set_style_text_color(VALUE[i], lv_color_hex(col), LV_PART_MAIN);
          lv_obj_set_style_text_color(UNIT[i], lv_color_hex(col), LV_PART_MAIN);

          // History: only real readings, and only once the clock is set.
          if (std::isnan(v) || !id(ha_time)->now().is_valid())
            return;
          id(graph_advance)->execute(i);
          id(g_sum)[i][47] += v;
          if (id(g_cnt)[i][47] < 65535)
            id(g_cnt)[i][47]++;
          id(graph_redraw)->execute(i);

  # Move graph i's window so slot 47 is the current half-hour.
  - id: graph_advance
    parameters:
      i: int
    then:
      - lambda: |-
          const auto now = id(ha_time)->now();
          if (!now.is_valid())
            return;
          const int32_t h = (int32_t) (now.timestamp / 1800);
          auto &sum = id(g_sum)[i];
          auto &cnt = id(g_cnt)[i];
          int32_t &head = id(g_head)[i];
          float &carry = id(g_carry)[i];
          // BEGIN unit store_advance
          if (head == 0) {
            head = h;
          } else if (h > head) {
            const int32_t d = std::min<int32_t>(h - head, 48);
            for (int s = 0; s < d; s++)  // slots falling off the left edge
              if (cnt[s] > 0)
                carry = sum[s] / cnt[s];
            for (int s = 0; s < 48; s++) {
              sum[s] = s + d < 48 ? sum[s + d] : 0.0f;
              cnt[s] = s + d < 48 ? cnt[s + d] : 0;
            }
            head = h;
          }
          // END unit store_advance

  # Repaint graph i's canvas like mini-graph-card: fade fill + threshold-coloured line.
  - id: graph_redraw
    parameters:
      i: int
    then:
      - lambda: |-
          id(graph_advance)->execute(i);
          lv_obj_t *const CANVAS[4] = {id(ph_graph), id(ec_graph), id(orp_graph), id(temp_graph)};
          lv_obj_t *const cv = CANVAS[i];
          const auto &sum = id(g_sum)[i];
          const auto &cnt = id(g_cnt)[i];
          const float carry = id(g_carry)[i];
          std::array<float, 48> pts;
          // BEGIN unit graph_points
          float c = carry;  // empty half-hours repeat the previous point (mini-graph behaviour)
          for (int k = 0; k < 48; k++) {
            if (cnt[k] > 0)
              c = sum[k] / cnt[k];
            pts[k] = c;
          }
          // END unit graph_points
          // BEGIN unit graph_band
          constexpr uint32_t RED = 0xEF4444, AMBER = 0xF59E0B, GREEN = 0x10B981;
          static const float TH[4][5] = {  // mini-graph color_thresholds
              {5.0f, 5.5f, 5.8f, 6.21f, 6.51f},
              {1000, 1800, 2000, 2501, 2701},
              {150, 250, 300, 401, 451},
              {12, 16, 18, 21.01f, 24.01f}};
          static const uint32_t TH_COLOR[5] = {RED, AMBER, GREEN, AMBER, RED};
          auto band = [&](float val) -> uint32_t {  // colour of the highest threshold <= val
            int b = 0;
            for (int t = 1; t < 5; t++)
              if (val >= TH[i][t])
                b = t;
            return TH_COLOR[b];
          };
          // END unit graph_band
          constexpr int W = 207, H = 105, PAD = 3;
          const lv_color_t tile = lv_color_hex(0x232325);
          lv_canvas_fill_bg(cv, tile, LV_OPA_COVER);
          // BEGIN unit graph_range
          float lo = INFINITY, hi = -INFINITY;
          int first = -1;  // first non-blank point
          for (int k = 0; k < 48; k++) {
            if (std::isnan(pts[k]))
              continue;
            if (first < 0)
              first = k;
            lo = std::min(lo, pts[k]);
            hi = std::max(hi, pts[k]);
          }
          if (first >= 0 && hi - lo < 1e-6f) {  // flat line: centre it
            lo -= 0.5f;
            hi += 0.5f;
          }
          // END unit graph_range
          if (first < 0)
            return;  // no data yet: plain tile
          const float span = H - 1 - 2 * PAD;
          auto yv = [&](float val) { return PAD + (hi - val) / (hi - lo) * span; };  // value -> y
          auto vy = [&](float y) { return hi - (y - PAD) / span * (hi - lo); };      // y -> value
          auto xk = [](float k) { return k * (W - 1) / 47.0f; };                      // slot -> x

          // Fade fill: band colour at each height, 40% at the top fading to 6% at the bottom.
          lv_draw_buf_t *buf = lv_canvas_get_draw_buf(cv);
          const bool rgb565 = buf->header.cf == LV_COLOR_FORMAT_RGB565;
          for (int x = 0; x < W; x++) {
            const float f = x * 47.0f / (W - 1);
            const int k = std::min(46, (int) f);
            if (std::isnan(pts[k]) || std::isnan(pts[k + 1]))
              continue;
            const float val = pts[k] + (pts[k + 1] - pts[k]) * (f - k);
            for (int y = std::max(0, (int) lroundf(yv(val))); y < H; y++) {
              const auto a = (lv_opa_t) (255.0f * 0.4f * (1.0f - 0.85f * y / H));
              const lv_color_t px = lv_color_mix(lv_color_hex(band(vy(y))), tile, a);
              if (rgb565)
                ((uint16_t *) (buf->data + y * buf->header.stride))[x] = lv_color_to_u16(px);
              else
                lv_canvas_set_px(cv, x, y, px, LV_OPA_COVER);
            }
          }
          lv_obj_invalidate(cv);

          // Line: 3 px, split wherever it crosses a threshold so colours switch exactly there.
          lv_layer_t layer;
          lv_canvas_init_layer(cv, &layer);
          lv_draw_line_dsc_t dsc;
          lv_draw_line_dsc_init(&dsc);
          dsc.width = 3;
          dsc.round_start = 1;
          dsc.round_end = 1;
          auto seg = [&](float x0, float v0, float x1, float v1) {
            dsc.p1.x = lroundf(x0);
            dsc.p1.y = lroundf(yv(v0));
            dsc.p2.x = lroundf(x1);
            dsc.p2.y = lroundf(yv(v1));
            dsc.color = lv_color_hex(band((v0 + v1) / 2));
            lv_draw_line(&layer, &dsc);
          };
          if (first == 47)  // a single point so far: short dash
            seg(xk(47) - 1, pts[47], xk(47), pts[47]);
          for (int k = first; k < 47; k++) {
            const float a = pts[k], b = pts[k + 1];
            float cut[6] = {0.0f};
            int n = 1;
            for (int t = 1; t < 5; t++) {
              const float th = TH[i][t];
              if ((th - a) * (th - b) < 0)
                cut[n++] = (th - a) / (b - a);
            }
            cut[n++] = 1.0f;
            std::sort(cut, cut + n);
            for (int j = 0; j + 1 < n; j++)
              seg(xk(k + cut[j]), a + (b - a) * cut[j], xk(k + cut[j + 1]), a + (b - a) * cut[j + 1]);
          }
          lv_canvas_finish_layer(cv, &layer);
```

- [ ] **Step 5: Append the `interval:` block (graphs scroll at :00 / :30 even without new readings)**

```yaml
interval:
  - interval: 60s
    then:
      - lambda: |-
          for (int i = 0; i < 4; i++) {
            const int32_t before = id(g_head)[i];
            id(graph_advance)->execute(i);
            if (id(g_head)[i] != before)
              id(graph_redraw)->execute(i);
          }
```

- [ ] **Step 6: Run the tests**

Run: `.\tools\test.ps1`
Expected: `extracted units: graph_band, graph_points, graph_range, store_advance, value_color`, then `N checks, 0 failures` (N ≈ 110), exit 0.

- [ ] **Step 7: Validate and build**

Run: `.\tools\esphome.ps1 config` → `Configuration is valid!`
Run: `.\tools\esphome.ps1 compile` → `Successfully compiled program.` If the compiler rejects a type (e.g. comparing the `cf` bit-field with the enum, or `int32_t` vs `long` in `lroundf` assignments), add the explicit cast it asks for (`(lv_color_format_t) buf->header.cf`, `(int32_t) lroundf(...)`) and rebuild.

- [ ] **Step 8: Commit**

```powershell
git add growell-display.yaml test/logic_test.cpp
git commit -m "Record half-hour history and draw threshold-coloured mini-graphs"
```

---

### Task 5: Backfill from Home Assistant + connection status

**Files:**
- Modify: `growell-display.yaml` (append `http_request:`; add globals `g_bf_ok`, `g_ha_up`, `g_bf_next`; add script `backfill_all`; add a 2 s interval)
- Modify: `test/logic_test.cpp` (add wrappers/tests; replace `main`)

**Interfaces:**
- Consumes: `g_sum`, `g_cnt`, `g_head`, `g_carry`, `graph_redraw(i)` (Task 4); `ha_note` (Task 2); `api_server`, `ha_time`, substitutions `ha_url`, `ha_token`, `ent_*` (Task 1).
- Produces: units `history_url` (inputs `std::string base`, `const char *entity`; output `std::string url`), `backfill_accumulate` (inputs `int32_t h_now`, refs `sum, cnt, carry`; defines `add_sample(int64_t t, float v)`), `hist_parser` (defines `struct HistParser` with `on_sample`, `feed(const char*, size_t)`, `done`, `samples`, static `parse_iso(const char*, int64_t&)`).

- [ ] **Step 1: Add the failing tests to `test/logic_test.cpp`** — insert above `int main()`:

```cpp
// ---------------- history_url / backfill_accumulate / hist_parser ----------------
static std::string history_url(const std::string &base, const char *entity) {
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

static void test_history_url() {
  const char *tail = "/api/history/period?filter_entity_id=sensor.x&minimal_response&no_attributes";
  CHECK(history_url("http://ha:8123", "sensor.x") == std::string("http://ha:8123") + tail);
  CHECK(history_url("http://ha:8123/", "sensor.x") == std::string("http://ha:8123") + tail);  // trailing slash
  CHECK(history_url("http://ha:8123//", "sensor.x") == std::string("http://ha:8123") + tail);
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
```

and replace `main` with:

```cpp
int main() {
  test_value_color();
  test_store_advance();
  test_graph_points_and_range();
  test_graph_band();
  test_history_url();
  test_parse_iso();
  test_parser();
  test_backfill_accumulate();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `.\tools\test.ps1`
Expected: non-zero exit; g++ error `build/history_url.inc: No such file or directory`.

- [ ] **Step 3: Append the `http_request:` block**

```yaml
http_request:
  timeout: 10s  # HA history queries can take a few seconds
```

- [ ] **Step 4: Add the backfill globals** — append these entries to the end of the `globals:` list:

```yaml
  # Backfill bookkeeping: bit i set = graph i rebuilt from HA on this connection.
  - id: g_bf_ok
    type: uint8_t
    initial_value: "0"
  - id: g_ha_up
    type: bool
    initial_value: "false"
  # millis() at which the next backfill attempt may start.
  - id: g_bf_next
    type: uint32_t
    initial_value: "0"
```

- [ ] **Step 5: Add the `backfill_all` script** — append this entry to the end of the `script:` list:

```yaml
  # Rebuild each graph that isn't synced yet from HA's last 24 h of history.
  - id: backfill_all
    then:
      - repeat:
          count: 4
          then:
            - if:
                condition:
                  lambda: "return (id(g_bf_ok) & (1 << iteration)) == 0;"
                then:
                  - http_request.get:
                      url: !lambda |-
                        static const char *const ENTITY[4] = {"${ent_ph}", "${ent_ec}", "${ent_orp}", "${ent_temp}"};
                        const std::string base = "${ha_url}";
                        const char *entity = ENTITY[iteration];
                        // BEGIN unit history_url
                        std::string url = base;
                        while (!url.empty() && url.back() == '/')
                          url.pop_back();
                        url += "/api/history/period?filter_entity_id=";
                        url += entity;
                        url += "&minimal_response&no_attributes";  // default window: last 24 h
                        // END unit history_url
                        return url;
                      request_headers:
                        Authorization: "Bearer ${ha_token}"
                      capture_response: false  # we stream the body ourselves
                      on_response:
                        then:
                          - lambda: |-
                              const int i = iteration;
                              if (response->status_code != 200) {
                                ESP_LOGW("growell", "History %d: HTTP %d%s", i, response->status_code,
                                         response->status_code == 401 || response->status_code == 403
                                             ? " - check the ha_token secret" : "");
                                return;
                              }
                              const auto now = id(ha_time)->now();
                              if (!now.is_valid())
                                return;
                              const int32_t h_now = (int32_t) (now.timestamp / 1800);
                              std::array<float, 48> sum{};
                              std::array<uint16_t, 48> cnt{};
                              float carry = NAN;
                              // BEGIN unit backfill_accumulate
                              auto add_sample = [&](int64_t t, float v) {
                                int32_t k = (int32_t) (t / 1800) - (h_now - 47);
                                if (k < 0) {  // before the window: only sets the starting value
                                  carry = v;
                                  return;
                                }
                                if (k > 47)  // HA clock slightly ahead of ours
                                  k = 47;
                                sum[k] += v;
                                if (cnt[k] < 65535)
                                  cnt[k]++;
                              };
                              // END unit backfill_accumulate
                              // BEGIN unit hist_parser
                              // Streams HA's minimal history JSON:
                              //   [[{..."state":"6.1",...,"last_changed":"2026-09-24T10:00:00+00:00",...},
                              //     {"state":"6.2","last_changed":"..."}, ...]]
                              // and reports each numeric (timestamp, value) pair. `done` = the JSON closed.
                              struct HistParser {
                                std::function<void(int64_t, float)> on_sample;
                                bool done = false;
                                unsigned samples = 0;
                                int depth = 0;
                                bool in_str = false, esc = false;
                                int m_state = 0, m_changed = 0;  // progress through the two keys
                                int mode = 0;                     // 0 scan, 1 state value, 2 timestamp value
                                char val[40];
                                int len = 0;
                                bool have_state = false;
                                float state = 0;

                                static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
                                  y -= m <= 2;
                                  const int64_t era = (y >= 0 ? y : y - 399) / 400;
                                  const unsigned yoe = (unsigned) (y - era * 400);
                                  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
                                  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
                                  return era * 146097 + (int64_t) doe - 719468;
                                }
                                // "2026-09-24T22:15:00[.ffffff][+hh:mm|-hh:mm|Z]" -> unix seconds
                                static bool parse_iso(const char *s, int64_t &out) {
                                  int y, mo, d, hh, mi, ss, n = 0;
                                  if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &hh, &mi, &ss, &n) != 6 ||
                                      n != 19)
                                    return false;
                                  const char *p = s + n;
                                  if (*p == '.') {
                                    p++;
                                    while (*p >= '0' && *p <= '9')
                                      p++;
                                  }
                                  int off = 0;
                                  if (*p == '+' || *p == '-') {
                                    int oh, om, m2 = 0;
                                    if (sscanf(p + 1, "%2d:%2d%n", &oh, &om, &m2) != 2 || m2 != 5)
                                      return false;
                                    off = (oh * 60 + om) * 60 * (*p == '-' ? -1 : 1);
                                    p += 6;
                                  } else if (*p == 'Z') {
                                    p++;
                                  }
                                  if (*p != '\0' || mo < 1 || mo > 12 || d < 1 || d > 31)
                                    return false;
                                  out = days_from_civil(y, mo, d) * 86400 + hh * 3600 + mi * 60 + ss - off;
                                  return true;
                                }
                                void feed(const char *p, size_t n) {
                                  const char *const K_STATE = "\"state\":\"";
                                  const char *const K_CHANGED = "\"last_changed\":\"";
                                  for (size_t j = 0; j < n && !done; j++) {
                                    const char c = p[j];
                                    if (mode != 0) {  // inside a value we want
                                      if (c != '"') {
                                        if (len < (int) sizeof(val) - 1)
                                          val[len++] = c;
                                        continue;
                                      }
                                      val[len] = '\0';
                                      in_str = false;  // this quote closes the value string
                                      if (mode == 1) {
                                        char *end;
                                        state = strtof(val, &end);
                                        have_state = end != val && *end == '\0' && std::isfinite(state);
                                      } else {
                                        int64_t t;
                                        if (have_state && parse_iso(val, t)) {
                                          on_sample(t, state);
                                          samples++;
                                        }
                                        have_state = false;
                                      }
                                      mode = 0;
                                      continue;
                                    }
                                    // Track brackets outside strings to know when the document ends.
                                    if (in_str) {
                                      if (esc)
                                        esc = false;
                                      else if (c == '\\')
                                        esc = true;
                                      else if (c == '"')
                                        in_str = false;
                                    } else if (c == '"') {
                                      in_str = true;
                                    } else if (c == '[' || c == '{') {
                                      depth++;
                                    } else if (c == ']' || c == '}') {
                                      if (--depth == 0)
                                        done = true;
                                    }
                                    m_state = c == K_STATE[m_state] ? m_state + 1 : (c == K_STATE[0] ? 1 : 0);
                                    m_changed = c == K_CHANGED[m_changed] ? m_changed + 1 : (c == K_CHANGED[0] ? 1 : 0);
                                    if (K_STATE[m_state] == '\0') {
                                      mode = 1;
                                      len = 0;
                                      have_state = false;
                                      m_state = m_changed = 0;
                                    } else if (K_CHANGED[m_changed] == '\0') {
                                      mode = 2;
                                      len = 0;
                                      m_state = m_changed = 0;
                                    }
                                  }
                                }
                              };
                              // END unit hist_parser
                              HistParser parser;
                              parser.on_sample = add_sample;
                              uint8_t chunk[512];
                              uint32_t last_data = millis();
                              while (!parser.done) {
                                const int n = response->read(chunk, sizeof(chunk));
                                if (n > 0) {
                                  parser.feed((const char *) chunk, n);
                                  last_data = millis();
                                } else if (n < 0 || response->is_read_complete() || millis() - last_data > 10000) {
                                  break;
                                } else {
                                  delay(1);
                                }
                                App.feed_wdt();
                              }
                              if (!parser.done) {
                                ESP_LOGW("growell", "History %d: incomplete response (%u bytes), will retry", i,
                                         (unsigned) response->get_bytes_read());
                                return;
                              }
                              id(g_sum)[i] = sum;
                              id(g_cnt)[i] = cnt;
                              id(g_carry)[i] = carry;
                              id(g_head)[i] = h_now;
                              id(g_bf_ok) |= (uint8_t) (1 << i);
                              ESP_LOGI("growell", "History %d: %u samples", i, parser.samples);
                              id(graph_redraw)->execute(i);
                      on_error:
                        then:
                          - logger.log:
                              format: "History %u: request failed - is ha_url reachable from the display?"
                              args: ["(unsigned) iteration"]
      - lambda: "id(g_bf_next) = millis() + 300000;  // retry any failures in 5 min"
```

- [ ] **Step 6: Add the connection watcher** — append this entry to the end of the `interval:` list:

```yaml
  - interval: 2s
    then:
      - lambda: |-
          // Home Assistant = an API client subscribed to states.
          const bool up = id(api_server)->is_connected(true);
          if (up != id(g_ha_up)) {
            id(g_ha_up) = up;
            if (up) {
              lv_obj_add_flag(id(ha_note), LV_OBJ_FLAG_HIDDEN);
              id(g_bf_ok) = 0;                  // re-sync every graph after each (re)connect
              id(g_bf_next) = millis() + 3000;  // give HA a moment to send time and states
            } else {
              lv_obj_remove_flag(id(ha_note), LV_OBJ_FLAG_HIDDEN);
            }
          }
      - if:
          condition:
            lambda: |-
              return id(g_ha_up) && id(g_bf_ok) != 0x0F && id(ha_time)->now().is_valid() &&
                     (int32_t) (millis() - id(g_bf_next)) >= 0;
          then:
            - script.execute: backfill_all
```

- [ ] **Step 7: Run the tests**

Run: `.\tools\test.ps1`
Expected: `extracted units: backfill_accumulate, graph_band, graph_points, graph_range, hist_parser, history_url, store_advance, value_color`, then `N checks, 0 failures`, exit 0.

- [ ] **Step 8: Validate and build**

Run: `.\tools\esphome.ps1 config` → `Configuration is valid!`
Run: `.\tools\esphome.ps1 compile` → `Successfully compiled program.` If the compiler objects to a local-class detail inside the lambda (e.g. `std::function` member or static member function), move nothing out of the YAML: fix it in place (e.g. replace the `std::function` with a `void (*)(void *, int64_t, float)` plus a context pointer) and update `test_parser`'s `parse_all` accordingly, then re-run `.\tools\test.ps1`.

- [ ] **Step 9: Final checks on the finished file**

Run: `Select-String -Path growell-display.yaml -Pattern 'BEGIN unit' | Measure-Object | Select-Object -ExpandProperty Count`
Expected: `8`.
Run: `.\tools\esphome.ps1 compile` and read the size summary it prints (`RAM:` / `Flash:` lines).
Expected: flash use below the app partition size (no "section overflow" error).

- [ ] **Step 10: Commit**

```powershell
git add growell-display.yaml test/logic_test.cpp
git commit -m "Backfill graphs from Home Assistant history and show connection status"
```
