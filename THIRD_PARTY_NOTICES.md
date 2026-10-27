# Third-party notices

Vibe-a-Gochi builds on the free and open-source work below. Thank you to all of these projects.
Licenses are as published by each project when this was written; the project pages have the
full texts.

**Not affiliated:** "Claude" and "Anthropic" are trademarks of Anthropic, PBC. Vibe-a-Gochi is an
independent fan/maker project, not made, endorsed or supported by Anthropic. "Hosyond" is the
board seller's brand; this project is not affiliated with them either.

## Code this project contains or is derived from

| What | Where it is used | License | Source |
|---|---|---|---|
| **claude-desktop-buddy** (Anthropic's Hardware Buddy reference firmware): the wire protocol, the 18 ASCII pets (`firmware/src/buddies/`, with its LICENSE), the drawing API they use (`pet_ascii.h`), and the parts of the firmware ported from it: Bluetooth link, protocol handling, character transfer, pet stats, mood and settings (`ble_link.cpp`, `link.*`, `xfer.cpp`, `stats.*`, `buddy.*`, `pet.cpp`, `power.*`, `settings.*`) | firmware | MIT, Copyright 2026 Anthropic, PBC | github.com/anthropics/claude-desktop-buddy |
| **EEZ Flow runtime** (generated into `firmware/src/ui/eez-flow.*`) | firmware | MIT, Copyright Envox d.o.o. | github.com/eez-open/eez-framework |
| **EEZ project template** "LVGL with EEZ Flow 9.0" (`reference/eez/eez-project-templates/`) | UI generator | MIT, Copyright 2024 Envox d.o.o. | github.com/eez-open/eez-project-templates |
| **Adafruit GFX** classic 5x7 font (`firmware/src/glcdfont.h`, `ui/assets/fonts/glcdfont.c`) | ASCII pets | BSD-2-Clause, Copyright Adafruit Industries | github.com/adafruit/Adafruit-GFX-Library |
| **ES8311 codec start-up sequence** (register order and values in `firmware/src/sound.cpp`, following Espressif's driver) | firmware | Apache-2.0, Copyright Espressif Systems | github.com/espressif/esp-adf (esp_codec_dev) |

## Libraries the firmware is built with (downloaded by PlatformIO)

| Library | License | Source |
|---|---|---|
| LVGL 9.2 (graphics) and its Montserrat fonts | MIT; Montserrat: SIL OFL 1.1 | lvgl.io |
| LovyanGFX (display and touch driver) | FreeBSD (BSD-2-Clause) | github.com/lovyan03/LovyanGFX |
| ArduinoJson | MIT | arduinojson.org |
| NimBLE-Arduino (Bluetooth LE) | Apache-2.0 | github.com/h2zero/NimBLE-Arduino |
| AnimatedGIF | Apache-2.0 | github.com/bitbank2/AnimatedGIF |
| AsyncTCP (ESP32Async) | LGPL-3.0 | github.com/ESP32Async/AsyncTCP |
| ESPAsyncWebServer (ESP32Async) | LGPL-3.0 | github.com/ESP32Async/ESPAsyncWebServer |
| Arduino core for the ESP32 | LGPL-2.1 | github.com/espressif/arduino-esp32 |
| ESP-IDF | Apache-2.0 | github.com/espressif/esp-idf |
| pioarduino platform-espressif32 | Apache-2.0 | github.com/pioarduino/platform-espressif32 |

The ready-made firmware files contain these libraries. The LGPL libraries' terms are met by
publishing this complete source with the build files, so anyone can rebuild the firmware with
changed versions of them.

## Fonts and icons in the firmware

| Font | License | Source |
|---|---|---|
| Nunito | SIL Open Font License 1.1 (`ui/assets/fonts/Nunito-OFL.txt`) | github.com/googlefonts/nunito |
| JetBrains Mono | SIL Open Font License 1.1 (`ui/assets/fonts/JetBrainsMono-OFL.txt`) | github.com/JetBrains/JetBrainsMono |
| Material Symbols Rounded (icons) | Apache-2.0 (`ui/assets/fonts/MaterialSymbols-LICENSE.txt`) | github.com/google/material-design-icons |

## PC companion: Python packages (installed by the setup, not included here)

| Package | License |
|---|---|
| websockets | BSD-3-Clause |
| zeroconf | LGPL-2.1-or-later |
| numpy | BSD-3-Clause |
| faster-whisper (speech to text) and CTranslate2 | MIT |
| Whisper "small" model (downloaded on first use, via Systran/faster-whisper-small) | MIT, OpenAI |
| piper-tts (text to speech; OHF-Voice/piper1-gpl) | **GPL-3.0-or-later** |
| onnxruntime (used by Piper) | MIT |
| pyperclip | BSD-3-Clause |
| pywinauto | BSD-3-Clause |
| mcp (Model Context Protocol SDK) | MIT |
| Pillow, fontTools (only for the UI and character tools) | MIT-CMU (HPND), MIT |

Piper is GPL-licensed: the companion only uses it when you install it yourself (with the "voice"
setup). Vibe-a-Gochi does not include or distribute Piper.

## Speech voices (downloaded on first use from huggingface.co/rhasspy/piper-voices)

| Language | Voice | Training data license |
|---|---|---|
| English | en_GB-alba-medium | CC BY 4.0: "Alba" speech data, University of Edinburgh (datashare.ed.ac.uk/handle/10283/3270) |
| German | de_DE-thorsten-medium | CC0: Thorsten-Voice by Thorsten Müller |
| French | fr_FR-siwis-medium | CC BY 4.0: SIWIS French Speech Synthesis Database, University of Edinburgh (datashare.is.ed.ac.uk/handle/10283/2353) |
| Italian | it_IT-paola-medium | CC0: Voice-Dataset-Italian by paolapersico1 |
| Portuguese (Portugal) | pt_PT-tugão-medium | CC0: OHF-Voice voice datasets |

## Tools used to make it (not included)

PlatformIO (Apache-2.0), EEZ Studio (GPL-3.0; only its generated code, MIT, is included),
lv_font_conv (MIT), Python (PSF License), Node.js (MIT).
