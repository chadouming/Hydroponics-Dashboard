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
