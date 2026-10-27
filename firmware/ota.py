# PlatformIO target "ota": build, then flash over WiFi (companion/flash_wifi.py).
#   python -m platformio run -d firmware -t ota
Import("env")  # noqa: F821

env.AddCustomTarget(  # noqa: F821
    name="ota",
    dependencies="$BUILD_DIR/${PROGNAME}.bin",
    actions='"$PYTHONEXE" "$PROJECT_DIR/../companion/flash_wifi.py" "$BUILD_DIR/${PROGNAME}.bin"',
    title="Upload over WiFi",
    description="Flash the board over WiFi (no boot mode or RST)")
