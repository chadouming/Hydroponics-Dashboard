# Extract the "// BEGIN unit" blocks from growell-display.yaml and run test/logic_test.cpp
# using the g++ and python3 that ship in the ESPHome image.
$root = Split-Path -Parent $PSScriptRoot
docker run --rm -v "${root}:/config" --entrypoint bash ghcr.io/esphome/esphome:2026.9.0 -c `
    'set -e; cd /config; python3 test/config_policy.py; python3 tools/extract_units.py; g++ -std=gnu++17 -Wall -Wextra -O1 -o test/build/logic_test test/logic_test.cpp; test/build/logic_test'
exit $LASTEXITCODE
