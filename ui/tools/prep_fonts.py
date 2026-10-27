#!/usr/bin/env python3
"""
Cut static, small font files out of the downloaded variable fonts.

  ui/assets/fonts/Nunito.ttf                 -> build/Nunito-Medium.ttf, Nunito-ExtraBold.ttf
  ui/assets/fonts/JetBrainsMono.ttf          -> build/JetBrainsMono-Regular.ttf
  ui/assets/fonts/MaterialSymbolsRounded.ttf -> build/Icons.ttf (only the icons in ICONS, filled)

Variable fonts render at their default instance in lv_font_conv, which for
Nunito is ExtraLight — far too thin for a 2.8" panel. The icon font is 14.5MB;
subsetting keeps the EEZ project (which embeds fonts as base64) small.

Usage:  python ui/tools/prep_fonts.py
"""
import json
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from fontTools import subset

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "fonts"
OUT = SRC / "build"

# Material Symbols names used by the UI. Add a name here, re-run, rebuild the project.
ICONS = [
    "home", "chat", "mic", "mic_off", "pets", "more_horiz", "settings", "favorite",
    "bolt", "battery_full", "battery_charging_full", "bluetooth", "bluetooth_connected",
    "wifi", "wifi_off", "check", "close", "send", "keyboard", "stop", "volume_up",
    "volume_off", "headset_mic", "graphic_eq", "schedule", "terminal", "play_arrow",
    "refresh", "delete", "upload", "sd_card", "palette", "brightness_6", "notifications",
    "info", "tune", "smart_toy", "arrow_back", "chevron_right", "star", "timer",
    "hourglass_top", "code", "bedtime", "celebration", "auto_awesome", "restart_alt",
    "power_settings_new", "dark_mode", "add", "lightbulb", "restaurant", "sync",
    "folder", "visibility", "lock", "keyboard_arrow_up", "keyboard_arrow_down", "person", "crown",
    # set by the firmware at runtime (status bar, approvals, keyboard, WiFi list)
    "battery_0_bar", "battery_1_bar", "battery_2_bar", "battery_3_bar", "battery_4_bar",
    "battery_5_bar", "battery_6_bar", "battery_alert", "circle", "link", "link_off",
    "signal_wifi_off", "network_wifi_1_bar", "network_wifi_2_bar", "network_wifi_3_bar",
    "signal_wifi_4_bar", "backspace", "keyboard_return", "delete_forever", "download",
    "bluetooth_disabled", "bluetooth_searching", "wifi_find", "edit_note", "question_mark",
    "edit", "help", "warning", "usb", "screen_rotation",
    # daily summary
    "insights", "history", "local_fire_department", "auto_stories",
]


def codepoints():
    table = {}
    for line in (SRC / "MaterialSymbolsRounded.codepoints").read_text().splitlines():
        name, cp = line.split()
        table[name] = int(cp, 16)
    return table


def instance(src, axes, dst):
    font = TTFont(src)
    instantiateVariableFont(font, axes, inplace=True)
    font.save(dst)
    return dst


def subset_to(path, unicodes, dst):
    opts = subset.Options()
    opts.layout_features = []      # no ligatures needed — we address icons by codepoint
    opts.notdef_outline = True
    font = TTFont(path)
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=unicodes)
    sub.subset(font)
    font.save(dst)


def main():
    OUT.mkdir(exist_ok=True)
    # ASCII + Latin-1 (de, fr, it, pt accents) + oe ligatures + typographic punctuation
    latin = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + [0x152, 0x153, 0x2022, 0x2026, 0x2014, 0x2019, 0x201C, 0x201D]
    for name, wght in [("Medium", 500), ("ExtraBold", 800)]:
        tmp = instance(SRC / "Nunito.ttf", {"wght": wght}, OUT / f"_Nunito-{name}.ttf")
        subset_to(tmp, latin, OUT / f"Nunito-{name}.ttf"); tmp.unlink()
    tmp = instance(SRC / "JetBrainsMono.ttf", {"wght": 400}, OUT / "_JBM.ttf")
    subset_to(tmp, latin, OUT / "JetBrainsMono-Regular.ttf"); tmp.unlink()

    cps = codepoints()
    missing = [n for n in ICONS if n not in cps]
    if missing:
        raise SystemExit(f"unknown icon names: {missing}")
    # Subset first (14.5MB → small), then pin the axes: filled, medium weight, 24pt optical size.
    tmp = OUT / "_icons.ttf"
    subset_to(SRC / "MaterialSymbolsRounded.ttf", [cps[n] for n in ICONS], tmp)
    instance(tmp, {"FILL": 1, "GRAD": 0, "opsz": 24, "wght": 500}, OUT / "Icons.ttf"); tmp.unlink()
    (OUT / "icons.json").write_text(json.dumps({n: cps[n] for n in ICONS}, indent=1))

    for f in sorted(OUT.glob("*.ttf")):
        print(f"{f.name:28s} {f.stat().st_size/1024:7.1f} KB")


if __name__ == "__main__":
    main()
