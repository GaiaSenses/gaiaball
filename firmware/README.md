# Gaiaball v2 firmware

Stub. Build commands (run from the repository root):

    pio run -d firmware -e esp32dev
    pio test -d firmware -e native
    arduino-cli compile --fqbn esp32:esp32:esp32 firmware/Gaiaball

Tooling is installed user-level: `~/.local/bin/pio` and `~/.local/bin/arduino-cli`.
