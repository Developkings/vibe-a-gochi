#!/usr/bin/env python3
"""
Generate ui/vibe-buddy.eez-project — the Vibe-a-Gochi touch UI for the
Hosyond ES3C28P (ESP32-S3, 320x240 landscape, capacitive touch).

Open the result in EEZ Studio to view/edit it and to click through it in the
simulator (Run). EEZ exports the LVGL code to firmware/src/ui; this script also
writes the font C files to firmware/src/ui_fonts (see eez.font_entry) and the
translation table firmware/src/i18n_table.h.

What you see in the designer is sample data. On the device the firmware fills
the labels that carry an identifier (objects.<name> in C), the global variables,
and rebuilds the list panels (chat, projects, chats, characters, WiFi) at runtime.

Design: "warm night" — near-black warm ground, clay accent (Claude's color),
rounded cards, Nunito text, Material Symbols Rounded icons.

Portrait: every screen is laid out twice, at 320x240 (the EEZ project) and at 240x320. The
widget trees match; where a widget sits differently, firmware/src/ui_layout.h gets both places
and the firmware moves the widgets when the screen is turned (Settings · Screen).

Usage:  python ui/tools/gen_ui.py [--build]
"""
import json, struct, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from eez import *  # noqa: F401,F403
from i18n import LANGS, TR, FW

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "ui"
FONTS = UI / "assets/fonts/build"
IMAGES = UI / "assets/images"
FONT_C_OUT = ROOT / "firmware/src/ui_fonts"
TEMPLATE = ROOT / "reference/eez/eez-project-templates/templates/v0.23.0/LVGL with EEZ Flow-9.0.eez-project"
OUT = UI / "vibe-buddy.eez-project"
I18N_OUT = ROOT / "firmware/src/i18n_table.h"
THEME_OUT = ROOT / "firmware/src/theme.h"
LAYOUT_OUT = ROOT / "firmware/src/ui_layout.h"

W, H = 320, 240
SB = 24            # status bar height
DOCK_Y = 192       # bottom navigation dock top (H - 48)


def set_size(w, h):
    global W, H, DOCK_Y
    W, H, DOCK_Y = w, h, h - 48


def lp(land, port):
    """The landscape value, or the portrait one while the portrait layout is generated."""
    return port if H > W else land

# ------------------------------------------------------------------ theme
C = dict(
    bg="#15110F", surface="#221C19", raised="#2D2522", line="#3B312C",
    text="#F5EFEA", dim="#A3968D", faint="#6E625A",
    accent="#DA7756", accent_hi="#E8906F", accent_soft="#3B2620",
    ok="#5CC38A", ok_soft="#1D3227", bad="#EF6A5E", bad_soft="#3A1F1C",
    warn="#F0B54D", warn_soft="#3A2E18", info="#74A9F2", code_bg="#0D0A09",
    on_accent="#1A1210",
)

ICON = json.loads((FONTS / "icons.json").read_text())
I = {k: chr(v) for k, v in ICON.items()}


def S(default=None, pressed=None, checked=None, part="MAIN", **more_parts):
    """Local style: S({...}, pressed={...}); extra parts via INDICATOR={...}, KNOB={...}."""
    d = {part: {"DEFAULT": default or {}}}
    if pressed:
        d[part]["PRESSED"] = pressed
    if checked:
        d[part]["CHECKED"] = checked
    for p, v in more_parts.items():
        d[p] = v if "DEFAULT" in v else {"DEFAULT": v}
    return d


def box(bg=None, radius=0, border=None, opa=255, pad=0, shadow=False, **kw):
    """Plain container look: kills the default LVGL theme padding/border/shadow."""
    st = {"bg_color": bg or C["bg"], "bg_opa": opa if bg else 0, "radius": radius,
          "border_width": 0, "pad_top": pad, "pad_bottom": pad, "pad_left": pad, "pad_right": pad,
          "shadow_width": 0}
    if border:
        st.update(border_width=1, border_color=border, border_opa=255)
    if shadow:
        st.update(shadow_width=18, shadow_color="#000000", shadow_opa=120, shadow_offset_y=6)
    st.update(kw)
    return st


def txt(font="t14", color="text", **kw):
    st = {"text_font": font, "text_color": C.get(color, color)}
    st.update(kw)
    return st


TRANSLATED = []   # (C identifier, English text) of every translated label


def T(key, x, y, text, font="t14", color="text", w=None, h=None, align=None, center=False, ident=None, **kw):
    """Text label. Texts found in TR get a C name so the firmware can swap the language;
    ident= names a label the firmware fills itself (then it is not translated from TR)."""
    st = txt(font, color, **kw)
    if align:
        st["align"] = align
    if center:
        st["text_align"] = "CENTER"
    if not ident and text in TR:
        ident = f"tr_{len(TRANSLATED)}"
        TRANSLATED.append((ident, text))
    return label(key, x, y, text, style=S(st), w=w, h=h, ident=ident)


def dot(lbl):
    """Cut a fixed-width label with an ellipsis instead of wrapping (long translations)."""
    lbl["longMode"] = "DOT"
    return lbl


def named(widget, ident):
    widget["identifier"] = ident
    return widget


def longest(text):
    return max(len(t) for t in (text,) + TR.get(text, ()))


def icon(key, x, y, name, size=20, color="text", align=None, ident=None, bind=None):
    st = txt(f"i{size}", color)
    if align:
        st["align"] = align
    return label(key, x, y, I[name], style=S(st), ident=ident, bind=bind)


def pill(key, x, y, w, h, bg, children, radius=None, border=None, ident=None):
    return panel(key, x, y, w, h, style=S(box(bg, radius if radius is not None else h // 2, border)),
                 children=children, ident=ident)


def btn(key, x, y, w, h, bg, children, radius=12, pressed_bg=None, border=None, ident=None):
    st = box(bg, radius, border)
    return button(key, x, y, w, h, style=S(st, pressed={"bg_color": pressed_bg or C["raised"],
                                                        "transform_width": -2, "transform_height": -2}),
                  children=children, ident=ident)


def act(widget, action, event="CLICKED", user_data=0):
    """Wire a native action (action_<name>(lv_event_t*) in the firmware)."""
    widget["eventHandlers"].append(on(event, f"{widget['objID']}.{action}.{event}", action, user_data=user_data))
    return widget


def header(key, title, page, action_icon=None):
    """Sub-screen header: back button, title, optional action button (returned last)."""
    back = btn(f"{key}.back", 6, SB + 2, 36, 32, C["surface"], [icon(f"{key}.backi", 0, 0, "arrow_back", 20, align="CENTER")], radius=10)
    page.back(back)
    kids = [back, T(f"{key}.title", 50, SB + 7, title, "h18")]
    if action_icon:
        kids.append(btn(f"{key}.act", W - 42, SB + 2, 36, 32, C["accent_soft"],
                        [icon(f"{key}.acti", 0, 0, action_icon, 20, "accent", align="CENTER")], radius=10))
    return kids


def png_size(name):
    return struct.unpack(">II", (IMAGES / f"{name}.png").read_bytes()[16:24])


def floating(key, x, y, w, h, child):
    """EEZ timelines set absolute positions: an animated widget moves inside this box (from 0,0),
    and turning the screen moves the box."""
    return panel(key + ".box", x, y, w, h, style=S(box()), children=[child])


def pet_slot(key, x, y, preview, ident, bob=4, period=2.4):
    """Where the firmware draws the live pet (see pet.cpp); preview PNG for the designer."""
    w, h = png_size(preview)
    img = image(key, 0, bob, preview, ident=ident)
    img["timeline"] = keyframes(key + ".tl", [(0, period / 2, {"top": 0}), (period / 2, period, {"top": bob})])
    return floating(key, x, y - bob, w, h + bob, img)


# ------------------------------------------------------------------ shared pieces

def status_bar_widget():
    """Icons and texts are global variables the firmware sets (link, bt, wifi, battery, clock)."""
    root = panel("sb.root", 0, 0, W, SB, style=S(box()), children=[
        label("sb.dot", 8, 3, "", style=S(txt("i16", "accent")), bind="sb_link_icon"),
        label("sb.link", 26, 4, "", style=S(txt("t12", "dim")), bind="sb_link_text"),
        # status icons in the top-right corner, the clock to their left
        label("sb.clock", W - 116, 4, "", style=S(txt("t12", "text")), bind="clock_text"),
        label("sb.bt", W - 60, 3, "", style=S(txt("i16", "dim")), bind="sb_bt_icon"),
        label("sb.wifi", W - 42, 3, "", style=S(txt("i16", "dim")), bind="sb_wifi_icon"),
        label("sb.bat", W - 22, 3, "", style=S(txt("i16", "dim")), bind="sb_bat_icon"),
        # last child (the firmware colours children 3..6 by index), drawn left of bluetooth
        label("sb.usb", W - 78, 3, "", style=S(txt("i16", "dim")), bind="sb_usb_icon"),
    ])
    return Page("status_bar", W, SB, root, user_widget=True)


DOCK = [("home", "home", "Home"), ("chat", "chat", "Chat"), ("voice", "mic", "Talk"),
        ("pet", "pets", "Buddy"), ("more", "more_horiz", "More")]


def dock(page, active):
    """Bottom navigation. Center item (voice) is a raised round mic button."""
    kids = [panel(f"{page.name}.dock.bg", 0, DOCK_Y, W, H - DOCK_Y,
                  style=S(box(C["surface"], 0, border_side="TOP", border_width=1, border_color=C["line"])))]
    idx = [d[0] for d in DOCK].index(active) if active in [d[0] for d in DOCK] else -1
    items = []
    slot = W // len(DOCK)        # 64 landscape, 48 portrait
    bw = min(56, slot - 4)
    for i, (target, ic, name) in enumerate(DOCK):
        x = i * slot
        k = f"{page.name}.dock.{target}"
        on_ = target == active
        if target == "voice":
            b = button(k, x + (slot - 52) // 2, DOCK_Y - 14, 52, 52,
                       style=S(box(C["accent"], 26, shadow=True, border=C["bg"], border_width=3),
                               pressed={"bg_color": C["accent_hi"]}),
                       children=[icon(k + ".i", 0, 0, "mic", 24, C["on_accent"], align="CENTER")])
        else:
            col = "accent" if on_ else "faint"
            b = button(k, x + (slot - bw) // 2, DOCK_Y + 2, bw, 44, style=S(box(), pressed={"bg_color": C["raised"], "bg_opa": 255}),
                       children=[icon(k + ".i", 0, -7, ic, 20, col, align="CENTER"),
                                 T(k + ".l", 0, 12, name, "t11", col, align="CENTER")])
            if on_:
                b["children"].append(panel(k + ".bar", 0, -20, 18, 3, style=S(box(C["accent"], 2, align="CENTER"))))
        if not on_:
            anim = "MOVE_LEFT" if i > idx else "MOVE_RIGHT"
            if target == "voice":
                anim = "OVER_TOP"
            page.nav(b, "CLICKED", target, anim, 220)
        items.append(b)
    return kids + items


def status_bar():
    return user_widget("sbw", 0, 0, W, SB, "status_bar")


def stage_glow(key, cx, cy, d):
    """Soft warm halo behind a pet: two concentric translucent circles."""
    return [panel(f"{key}.g1", cx - d // 2, cy - d // 2, d, d, style=S(box(C["accent"], d // 2, opa=18))),
            panel(f"{key}.g2", cx - d // 3, cy - d // 3, d * 2 // 3, d * 2 // 3, style=S(box(C["accent"], d // 3, opa=22)))]


# ------------------------------------------------------------------ screens

def scr(name, children, bg="bg"):
    return Page(name, W, H, screen(f"{name}.scr", W, H, S(box(C[bg], 0, opa=255)), children))


def home():
    p = scr("home", [])
    cx, dy = W // 2, lp(0, 50)   # portrait: the stage a little lower, the message bubble near the dock
    pet = pet_slot("home.pet", cx - 102, 30 + dy, "pv_home", "home_pet")
    sh = panel("home.shadow", 0, 0, 80, 10, style=S(box("#000000", 5, opa=110)))
    sh["timeline"] = keyframes("home.sh.tl", [(0, 1.2, {"width": 66, "left": 7}), (1.2, 2.4, {"width": 80, "left": 0})])
    shadow = floating("home.shadow", cx - 40, 136 + dy, 80, 10, sh)
    sp = icon("home.spark", 0, 12, "auto_awesome", 16, "warn")
    sp["timeline"] = keyframes("home.spark.tl", [(0, 1.2, {"top": 0, "opacity": 90}), (1.2, 2.4, {"top": 12, "opacity": 255})])
    spark = floating("home.spark", cx + 78, 58 + dy, 22, 34, sp)

    waiting = btn("home.wait", W - 112, SB + 6, 104, 24, C["warn_soft"], [
        icon("home.wait.i", 8, 0, "hourglass_top", 16, "warn", align="LEFT_MID"),
        T("home.wait.t", 28, 0, "1 waiting", "t12", "warn", align="LEFT_MID", ident="home_wait_text")],
        radius=12, ident="home_wait")
    p.nav(waiting, "CLICKED", "approval", "OVER_TOP")
    lw = lp(120, 112)
    level = pill("home.lvl", 8, SB + 6, lw, 24, C["accent_soft"], [
        icon("home.lvl.i", 8, 0, "star", 16, "accent", align="LEFT_MID"),
        dot(T("home.lvl.t", 28, 0, "Lv 5  Clawd", "t12", "accent", w=lw - 32, h=16, align="LEFT_MID", ident="home_level"))])

    bubble = btn("home.bubble", 10, DOCK_Y - 50, W - 20, 44, C["surface"], [
        panel("home.bub.dot", 10, 0, 8, 8, style=S(box(C["accent"], 4, align="LEFT_MID"))),
        T("home.bub.who", 26, 4, "Claude  ·  2 running", "t11", "faint", ident="home_who"),
        label("home.bub.msg", 26, 19, "", style=S(txt("t14", "text")), w=W - 60, h=20, bind="last_message",
              long_mode="DOT"),
        icon("home.bub.go", -8, 0, "chevron_right", 20, "faint", align="RIGHT_MID")], radius=14,
        border=C["line"], ident="home_bubble")
    p.nav(bubble, "CLICKED", "chat", "MOVE_LEFT")

    p.root["children"] = (stage_glow("home", cx, 92 + dy, 150) + [shadow, pet, spark, status_bar(), level, waiting, bubble]
                          + dock(p, "home"))
    p.loop_animation(2.4)
    return p


def approval():
    p = scr("approval", [])
    warn_i = icon("ap.warn", 14, 12, "notifications", 20, "warn")
    warn_i["timeline"] = keyframes("ap.warn.tl", [(0, 0.4, {"rotate": 150}), (0.4, 0.8, {"rotate": -150}), (0.8, 1.2, {"rotate": 0})])
    # Portrait: the pet under the title, a shorter card, Approve on its own row above Deny / Always.
    pet = pet_slot("ap.pet", lp(191, 14), lp(-26, 39), "pv_approval", "approval_pet", bob=3, period=0.6)
    card = panel("ap.card", 10, lp(46, 118), W - 20, lp(132, 110), style=S(box(C["surface"], 16, C["line"])), children=[
        panel("ap.tool.bg", 12, 12, 40, 40, style=S(box(C["accent_soft"], 12)), children=[
            icon("ap.tool.i", 0, 0, "terminal", 24, "accent", align="CENTER", ident="approval_icon")]),
        dot(T("ap.tool", 62, 10, "Bash", "h24", w=lp(W - 166, W - 96), h=28, ident="approval_tool")),
        T("ap.sub", 62, 38, "wants to run a command", "t12", "dim", ident="approval_sub"),
        panel("ap.code", 12, 62, W - 44, lp(56, 36), style=S(box(C["code_bg"], 10, pad=8)), children=[
            label("ap.hint", 0, 0, "", style=S(txt("m12", "#E6D5C8")), w=W - 60, bind="prompt_hint")]),
    ])
    # Landscape: in the card's top-right corner (the title needs the top row); portrait: beside the pet.
    timer = pill("ap.timer", lp(W - 86, W - 74), lp(58, 52), 64, 22, C["warn_soft"], [
        icon("ap.timer.i", 6, 0, "timer", 16, "warn", align="LEFT_MID"),
        T("ap.timer.t", 26, 0, "0:08", "t12", "warn", align="LEFT_MID", ident="approval_timer")])
    # Three buttons; "Always" (allow + don't ask again) only for requests from the Claude Code hooks:
    # without it the firmware hides it and widens the others (ui_live.cpp approval(), both layouts).
    row2 = (W - 26) // 2
    deny = btn("ap.deny", 10, lp(186, 278), lp(96, row2), lp(46, 38), C["bad_soft"], [
        T("ap.deny.t", 0, 0, "Deny", "h18", "bad", align="CENTER")], radius=14, pressed_bg="#4A2622",
        ident="approval_deny")
    always = btn("ap.always", lp(112, 16 + row2), lp(186, 278), lp(96, row2), lp(46, 38), C["accent_soft"], [
        T("ap.always.t", 0, 0, "Always", "h18", "accent", align="CENTER")], radius=14, pressed_bg=C["raised"],
        ident="approval_always")
    allow = btn("ap.allow", lp(214, 10), lp(186, 234), lp(96, W - 20), lp(46, 38), C["ok"], [
        T("ap.allow.t", 0, 0, "Approve", "h18", "#0F1E15", align="CENTER")], radius=14, pressed_bg="#7AD3A2",
        ident="approval_allow")
    # The firmware sends the decision and leaves the screen (it knows where you came from).
    act(deny, "permission_deny")
    act(always, "permission_always")
    act(allow, "permission_approve")
    p.root["children"] = [warn_i, T("ap.title", 42, 13, "Needs your OK", "h18", "warn"), pet, card, timer, deny,
                          always, allow]
    p.loop_animation(1.2)
    return p


def bubble(key, y, text, mine=False, h=None):
    w = 200 if mine else 238
    x = W - w - 10 if mine else 10
    bg, col = (C["accent"], C["on_accent"]) if mine else (C["surface"], "text")
    lines = max(1, (len(text) * 7) // (w - 20) + 1)
    hh = h or 12 + lines * 18
    b = panel(key, x, y, w, hh, style=S(box(bg, 14, pad=0)), children=[
        T(key + ".t", 10, 6, text, "t14", col, w=w - 20)])
    return b, y + hh + 8


def chat():
    """Sample conversation; the firmware rebuilds chat_list from the companion (see chat_view.cpp)."""
    p = scr("chat", [])
    y = 6
    items = []
    b, y = bubble("ch.b1", y, "I found 3 failing tests in auth.spec.ts. Want me to fix them?")
    items.append(b)
    items.append(pill("ch.tool1", 10, y, 160, 22, C["raised"], [
        icon("ch.tool1.i", 8, 0, "check", 16, "ok", align="LEFT_MID"),
        T("ch.tool1.t", 28, 0, "Read auth.spec.ts", "m12", "dim", align="LEFT_MID")]))
    y += 30
    b, y = bubble("ch.b2", y, "Yes, fix them", mine=True)
    items.append(b)
    b, y = bubble("ch.b3", y, "On it. Updating the token check and running the tests again.")
    items.append(b)
    where = btn("ch.where", 6, SB + 2, W - 12, 26, C["surface"], [
        icon("ch.where.i", 8, 0, "folder", 16, "accent", align="LEFT_MID"),
        dot(label("ch.where.p", 30, 0, "", style=S(txt("t12", "accent", align="LEFT_MID")), w=lp(88, 70), h=16,
                  bind="current_project")),
        label("ch.where.c", lp(124, 106), 0, "", style=S(txt("t12", "dim", align="LEFT_MID")), w=lp(150, 90), h=16,
              bind="current_chat", long_mode="DOT"),
        icon("ch.where.go", -6, 0, "keyboard_arrow_down", 20, "faint", align="RIGHT_MID")], radius=10)
    p.nav(where, "CLICKED", "projects", "OVER_BOTTOM")
    # Quick replies above the dock: one row in landscape, two in portrait (Explain + keyboard below).
    row_y = [DOCK_Y - 36] if W > H else [DOCK_Y - 70, DOCK_Y - 36]
    lst = panel("ch.list", 0, SB + 30, W, row_y[0] - SB - 34, style=S(box()), children=items, scroll=True,
                ident="chat_list")
    # Portrait: a 3x2 grid, Continue · Yes · Stop over No · Explain · keyboard.
    cell = (W - 20 - 12) // 3
    grid = {0: (0, 0), 1: (1, 0), 2: (0, 1), 3: (1, 1)}
    chips = []
    x, y = 10, row_y[0]
    for i, word in enumerate(["Continue", "Yes", "No", "Explain"]):
        cw = 20 + longest(word) * 7   # wide enough for every language
        if len(row_y) > 1:
            col, row = grid[i]
            x, y, cw = 10 + col * (cell + 6), row_y[row], cell
        c = btn(f"ch.q{i}", x, y, cw, 28, C["accent_soft"], [T(f"ch.q{i}.t", 0, 0, word, "t12", "accent", align="CENTER")],
                radius=14, border="#5A3528")
        act(c, "send_quick_reply", user_data=i)
        chips.append(c)
        x += cw + 6
    kx, kw = (W - 42, 32) if len(row_y) == 1 else (10 + 2 * (cell + 6), cell)
    kb = btn("ch.kb", kx, row_y[-1], kw, 28, C["surface"], [icon("ch.kb.i", 0, 0, "keyboard", 20, "dim", align="CENTER")],
             radius=14)
    act(kb, "type_message")
    # Stop: landscape, over the end of the chat while Claude works; portrait, the grid's top right
    # (dimmed while there is nothing to stop). The firmware shows / dims it.
    sx, sy, sw, sh = ((W - 100) // 2, row_y[0] - 38, 100, 30) if len(row_y) == 1 else (kx, row_y[0], cell, 28)
    stop = btn("ch.stop", sx, sy, sw, sh, C["bad_soft"], [
        icon("ch.stop.i", lp(10, 4), 0, "stop", 20, "bad", align="LEFT_MID"),
        T("ch.stop.t", lp(34, 26), 0, "Stop", "t12", "bad", align="LEFT_MID")], radius=sh // 2, border=C["bad"],
        ident="chat_stop")
    stop["widgetFlags"] += "|HIDDEN"
    act(stop, "stop_claude")
    p.root["children"] = [lst, status_bar(), where] + chips + [kb, stop] + dock(p, "chat")
    return p


def list_row(key, y, ic, ic_color, title, sub, right=None):
    kids = [panel(key + ".ibg", 8, 0, 32, 32, style=S(box(C["accent_soft"] if ic_color == "accent" else C["raised"], 10,
                                                            align="LEFT_MID")), children=[
                icon(key + ".i", 0, 0, ic, 20, ic_color, align="CENTER")]),
            T(key + ".t", 50, 6, title, "t14", "text"),
            T(key + ".s", 50, 25, sub, "t11", "faint")]
    if right:
        kids.append(T(key + ".r", -30, 0, right, "t11", "faint", align="RIGHT_MID"))
    kids.append(icon(key + ".c", -6, 0, "chevron_right", 20, "faint", align="RIGHT_MID"))
    return btn(key, 0, y, W - 20, 46, C["surface"], kids, radius=12)


def projects():
    """Claude Code projects (folders on the PC). Sample rows; the firmware fills projects_list."""
    p = scr("projects", [])
    data = [("vibe-a-gochi", "~/code/vibe-a-gochi", "12 chats"),
            ("website", "~/code/website", "8 chats"),
            ("home-automation", "~/code/home-automation", "5 chats")]
    rows = [list_row(f"prj.{i}", i * 52, "folder", "accent" if i == 0 else "dim", name, path, n)
            for i, (name, path, n) in enumerate(data)]
    lst = panel("prj.list", 10, SB + 40, W - 20, H - SB - 40, style=S(box()), children=rows, scroll=True,
                ident="projects_list")
    p.root["children"] = [status_bar()] + header("prj", "Projects", p) + [lst]
    return p


def chats():
    """Chat history of the selected project, plus starting a new chat there."""
    p = scr("chats", [])
    new = btn("cht.new", 10, SB + 40, W - 20, 40, C["accent"], [
        icon("cht.new.i", 12, 0, "add", 20, C["on_accent"], align="LEFT_MID"),
        dot(T("cht.new.t", 40, 0, "New chat in this project", "h18", C["on_accent"], w=W - 76, h=24,
              align="LEFT_MID"))],
        radius=12, pressed_bg=C["accent_hi"])
    act(new, "new_chat")
    data = [("Fix failing auth tests", "now · running", "ok"), ("Add touch UI generator", "2 h ago", "dim"),
            ("Battery ADC calibration", "yesterday", "dim")]
    rows = [list_row(f"cht.{i}", i * 52, "chat", "accent" if i == 0 else col, title, when)
            for i, (title, when, col) in enumerate(data)]
    lst = panel("cht.list", 10, SB + 86, W - 20, H - SB - 86, style=S(box()), children=rows, scroll=True,
                ident="chats_list")
    hdr = header("cht", "", p)
    hdr[1] = dot(label("cht.title", 50, SB + 7, "", style=S(txt("h18", "text")), w=W - 60, h=24, bind="current_project"))
    p.root["children"] = [status_bar()] + hdr + [new, lst]
    return p


def voice():
    """Voice conversation. Opening the screen starts listening (voice_open), leaving stops it."""
    p = scr("voice", [])
    cx, cy = W // 2, lp(102, 130)
    rings = []
    for i, d in enumerate([120, 150, 180]):
        r = panel(f"vo.ring{i}", cx - d // 2, cy - d // 2, d, d,
                  style=S(box(None, d // 2, C["accent"], border_width=2, border_opa=110 - i * 30)))
        t0 = i * 0.4
        r["timeline"] = keyframes(f"vo.ring{i}.tl", [(t0, t0 + 0.8, {"opacity": 40}), (t0 + 0.8, t0 + 1.6, {"opacity": 255})])
        rings.append(r)
    level = arc("vo.level", cx - 52, cy - 52, 104, 104, 0, style=S(
        {"arc_width": 6, "arc_color": C["line"], "bg_opa": 0, "border_width": 0, "pad_top": 0, "pad_bottom": 0,
         "pad_left": 0, "pad_right": 0},
        INDICATOR={"arc_width": 6, "arc_color": C["accent"], "arc_rounded": True},
        KNOB={"bg_opa": 0, "pad_top": 0, "pad_bottom": 0, "pad_left": 0, "pad_right": 0}), bg_start=0, bg_end=360,
        ident="voice_level")
    pet = pet_slot("vo.pet", cx - 102, cy - 75, "pv_home", "voice_pet", bob=3, period=0.8)
    back = btn("vo.back", 8, 8, 36, 32, C["surface"], [icon("vo.backi", 0, 0, "arrow_back", 20, align="CENTER")], radius=10)
    p.back(back)
    hs = pill("vo.hs", W - 130, 10, 120, 26, C["ok_soft"], [
        icon("vo.hs.i", 8, 0, "headset_mic", 16, "ok", align="LEFT_MID"),
        T("vo.hs.t", 28, 0, "Headset on", "t12", "ok", align="LEFT_MID")], ident="voice_headset")
    status = T("vo.status", 0, 168, "Listening…", "h18", "text", w=W, center=True, ident="voice_status")
    said = label("vo.said", 60, 192, "", style=S(txt("t12", "dim", text_align="CENTER")), w=W - 120, h=16,
                 bind="voice_transcript", long_mode="DOT")
    mute = btn("vo.mute", 10, H - 44, 40, 40, C["surface"], [icon("vo.mute.i", 0, 0, "mic_off", 20, "dim", align="CENTER",
                                                                 ident="voice_mute_icon")], radius=20)
    act(mute, "voice_mute")
    stop = btn("vo.stop", W - 50, H - 44, 40, 40, C["bad_soft"], [icon("vo.stop.i", 0, 0, "stop", 20, "bad", align="CENTER")], radius=20)
    act(stop, "voice_stop")
    said["top"] = H - 26
    status["top"] = lp(186, 238)
    act(p.root, "voice_open", "SCREEN_LOADED")
    act(p.root, "voice_close", "SCREEN_UNLOAD_START")
    p.root["children"] = stage_glow("vo", cx, cy, 200) + rings + [level, pet, back, hs, status, said, mute, stop]
    p.loop_animation(1.6)
    return p


def meter(key, y, ic, name, value, color, ident):
    x = lp(120, 10)   # portrait: under the pet, full width
    return [icon(key + ".i", x, y, ic, 16, color),
            T(key + ".n", x + 22, y, name, "t12", "dim"),
            bar(key + ".b", x + 76, y + 5, W - x - 88, 8, value, ident=ident,
                style=S(box(C["raised"], 4), INDICATOR=box(C[color], 4)))]


def pet():
    p = scr("pet", [])
    tiles = []
    for i, (val, name) in enumerate([("42", "approved"), ("3", "denied"), ("184K", "tokens"), ("2h 10m", "napped")]):
        if W > H:
            x, y, tw, th = 10 + i * 76, 132, 72, 50
        else:   # 2 x 2
            tw = (W - 26) // 2
            x, y, th = 10 + (i % 2) * (tw + 6), 166 + (i // 2) * 52, 46
        tiles.append(panel(f"pet.tile{i}", x, y, tw, th, style=S(box(C["surface"], 12)), children=[
            T(f"pet.tile{i}.v", 0, 8, val, "h18", "text", align="TOP_MID", ident=f"pet_stat_{i}"),
            # the tokens tile's name changes (total / today / this chat / last hour): the firmware sets it
            T(f"pet.tile{i}.n", 0, -6, name, "t11", "faint", align="BOTTOM_MID",
              ident=f"pet_stat_{i}_name" if i in (2, 3) else None)]))
    img = pet_slot("pet.img", -17, 30, "pv_pet", "pet_pet", bob=4, period=3)
    hi = icon("pet.heart", 0, 10, "favorite", 16, "#F07A9A")
    hi["timeline"] = keyframes("pet.heart.tl", [(0, 1.5, {"top": 0, "opacity": 60}), (1.5, 3, {"top": 10, "opacity": 255})])
    heart = floating("pet.heart", 92, 40, 22, 32, hi)
    my = lp(84, 118)   # meters: right of the pet, or under it
    # DevelopKing's crown, after its "DK" title (the firmware places and shows it)
    crown = icon("pet.crown", 160, SB + 10, "crown", 20, "warn", ident="pet_crown")
    crown["widgetFlags"] += "|HIDDEN"
    p.root["children"] = ([status_bar(),
                           dot(T("pet.name", 120, SB + 6, "Clawd", "h24", w=lp(130, W - 126), h=28, ident="pet_name")),
                           dot(T("pet.owner", 120, SB + 36, "Felix's buddy  ·  cat", "t12", "dim", w=W - 130, h=16,
                                 ident="pet_owner")),
                           pill("pet.lv", lp(W - 66, 120), lp(SB + 8, SB + 58), 56, 24, C["accent_soft"], [
                               T("pet.lv.t", 0, 0, "Lv 5", "t12", "accent", align="CENTER", ident="pet_level")]),
                           crown, img, heart]
                          + meter("pet.mood", my, "favorite", "Mood", 75, "accent", "pet_mood")
                          + meter("pet.fed", my + 16, "restaurant", "Fed", 40, "warn", "pet_fed")
                          + meter("pet.en", my + 32, "bolt", "Energy", 60, "ok", "pet_energy")
                          + tiles + dock(p, "pet"))
    p.loop_animation(3)
    return p


def more():
    p = scr("more", [])
    tiles = [("today", "insights", "Today"), ("characters", "palette", "Characters"), ("commands", "terminal", "Commands"),
             ("headset", "headset_mic", "Headset"), ("settings", "settings", "Settings"),
             ("info", "info", "Device"), ("clock", "schedule", "Clock")]
    kids = [status_bar()]
    cols = lp(4, 3)
    tw = (W - 20 - (cols - 1) * 6) // cols   # 70 landscape, 69 portrait
    for i, (target, ic, name) in enumerate(tiles):
        x, y = 10 + (i % cols) * (tw + 6), SB + 8 + (i // cols) * 80
        t = btn(f"more.{target}", x, y, tw, 72, C["surface"], [
            panel(f"more.{target}.bg", 0, 10, 36, 36, style=S(box(C["accent_soft"], 12, align="TOP_MID")), children=[
                icon(f"more.{target}.i", 0, 0, ic, 20, "accent", align="CENTER")]),
            dot(T(f"more.{target}.t", 0, -8, name, "t12", "text", w=tw - 6, h=16, center=True,
                  align="BOTTOM_MID"))], radius=16)
        p.nav(t, "CLICKED", target, "MOVE_LEFT" if target != "clock" else "FADE_IN")
        kids.append(t)
    p.root["children"] = kids + dock(p, "more")
    return p


def today():
    """Daily summary (companion/daily.py). The firmware fills the identified labels and stacks the
    body's cards in a column (their heights follow the text), so only their widths matter here."""
    p = scr("today", [])
    hdr = header("tdy", "", p, "history")   # history: today ⇄ yesterday
    hdr[1] = dot(label("tdy.title", 50, SB + 7, "Today", style=S(txt("h18", "text")), w=W - 110, h=24,
                       ident="today_title"))
    act(hdr[-1], "summary_toggle")
    bw = W - 20
    tiles = []
    for i, (val, name) in enumerate([("6h 11m", "Active"), ("147", "Prompts"), ("552K", "Tokens"), ("53", "Files")]):
        if W > H:
            tw, th = (bw - 18) // 4, 50
            x, y = i * (tw + 6), 0
        else:   # 2 x 2
            tw, th = (bw - 6) // 2, 46
            x, y = (i % 2) * (tw + 6), (i // 2) * (th + 6)
        tiles.append(panel(f"tdy.t{i}", x, y, tw, th, style=S(box(C["surface"], 12)), children=[
            T(f"tdy.t{i}.v", 0, 7, val, "h18", "accent" if i == 0 else "text", align="TOP_MID", ident=f"today_val_{i}"),
            T(f"tdy.t{i}.n", 0, -5, name, "t11", "faint", align="BOTTOM_MID")]))
    tiles_box = panel("tdy.tiles", 0, 0, bw, lp(50, 98), children=tiles, style=S(box()))
    recap = panel("tdy.recap", 0, 0, bw, 0, hu="content", ident="today_recap_card",
                  style=S(box(C["accent_soft"], 12, pad=10)), children=[
                      label("tdy.recap.t", 0, 0, "You spent the day bringing the board's portrait mode to life.",
                            style=S(txt("t14", "text")), w=bw - 20, ident="today_recap")])
    details = panel("tdy.det", 0, 0, bw, 0, hu="content", ident="today_details",
                    style=S(box(C["surface"], 12, pad=10)), children=[
                        label(f"tdy.det.{k}", 0, 0, sample, style=S(txt("t12", col)), w=bw - 20, ident=f"today_{k}")
                        for k, sample, col in [("projects", "CYD Vibe-a-Gochi 6h 11m", "text"),
                                               ("tools", "Edit 312  ·  PowerShell 286  ·  Read 142", "dim"),
                                               ("board", "Board: 12 approved  ·  1 denied", "dim")]])
    col = (bw - 20) // 7
    week_kids = [T("tdy.wk.t", 10, 8, "This week", "t12", "dim"),
                 pill("tdy.streak", -8, 6, 104, 20, C["warn_soft"], [
                     icon("tdy.streak.i", 6, 0, "local_fire_department", 16, "warn", align="LEFT_MID"),
                     dot(T("tdy.streak.t", 24, 0, "4-day streak", "t11", "warn", w=76, h=14, align="LEFT_MID",
                           ident="today_streak"))])]
    week_kids[-1]["localStyles"]["definition"]["MAIN"]["DEFAULT"]["align"] = "TOP_RIGHT"
    for i, v in enumerate([0, 0, 0, 0, 60, 55, 92]):
        week_kids += [bar(f"tdy.wb{i}", 10 + i * col + (col - 12) // 2, 32, 12, 40, v, ident=f"week_bar_{i}",
                          style=S(box(C["raised"], 4), INDICATOR=box(C["accent"], 4))),
                      T(f"tdy.wd{i}", 10 + i * col, 76, "MTWTFSS"[i], "t11", "faint", w=col, center=True,
                        ident=f"week_day_{i}")]
    week = panel("tdy.week", 0, 0, bw, 96, style=S(box(C["surface"], 12)), children=week_kids)
    y = tiles_box["height"] + 8   # stacked for the designer; on the device a flex column does it
    for card, h in [(recap, 64), (details, 72), (week, 0)]:
        card["top"] = y
        y += h + 8
    body = panel("tdy.body", 10, SB + 40, bw, H - SB - 40, style=S(box()), scroll=True, ident="today_body",
                 children=[tiles_box, recap, details, week])
    act(p.root, "summary_open", "SCREEN_LOADED")   # asks the companion for the numbers
    p.root["children"] = [status_bar()] + hdr + [body]
    return p


def characters():
    """Sample cards; the firmware fills characters_grid with the 18 species and installed GIF packs."""
    p = scr("characters", [])
    cards = []
    items = [("thumb_developking", "DK", "GIF"), ("thumb_cat", "cat", "ASCII"), ("thumb_capybara", "capybara", "ASCII"),
             ("thumb_duck", "duck", "ASCII"), ("thumb_ghost", "ghost", "ASCII"), ("thumb_robot", "robot", "ASCII")]
    for i, (img, name, src) in enumerate(items):
        x, y = 4 + (i % 3) * 100, 4 + (i // 3) * 92
        sel = name == "cat"
        tag_bg, tag_col = {"GIF": (C["info"], "#0B1626"), "SD": (C["warn"], "#271D0A")}.get(src, (C["raised"], C["dim"]))
        kids = [image(f"chr.{name}.img", 0, -10, img, style=S({"align": "CENTER"})),
                T(f"chr.{name}.n", 8, -8, name, "t12", "text", align="BOTTOM_LEFT"),
                pill(f"chr.{name}.tag", -6, -8, 34, 14, tag_bg, [T(f"chr.{name}.tag.t", 0, 0, src, "t9", tag_col, align="CENTER")])]
        kids[-1]["localStyles"]["definition"]["MAIN"]["DEFAULT"]["align"] = "BOTTOM_RIGHT"
        if sel:
            kids.append(panel(f"chr.{name}.sel", -6, 6, 18, 18, style=S(box(C["accent"], 9, align="TOP_RIGHT")), children=[
                icon(f"chr.{name}.sel.i", 0, 0, "check", 16, C["on_accent"], align="CENTER")]))
        c = btn(f"chr.{name}", x, y, 94, 86, C["surface"], kids, radius=14, border=C["accent"] if sel else None)
        if sel:
            c["localStyles"]["definition"]["MAIN"]["DEFAULT"]["border_width"] = 2
        cards.append(c)
    grid = panel("chr.grid", 6, SB + 38, W - 12, H - SB - 38, style=S(box()), children=cards, scroll=True,
                 ident="characters_grid")
    hdr = header("chr", "Characters", p, "upload")
    p.nav(hdr[-1], "CLICKED", "install", "MOVE_LEFT")
    p.root["children"] = [status_bar()] + hdr + [grid]
    return p


def install():
    p = scr("install", [])
    sd = btn("ins.sd", 10, lp(150, 210), W - 20, 40, C["accent"], [
        icon("ins.sd.i", 12, 0, "sd_card", 20, C["on_accent"], align="LEFT_MID"),
        T("ins.sd.t", 40, 0, "Import from SD card", "h18", C["on_accent"], align="LEFT_MID")],
        radius=12, pressed_bg=C["accent_hi"])
    act(sd, "import_from_sd")
    p.root["children"] = [status_bar()] + header("ins", "Install a character", p) + [
        T("ins.how", 14, SB + 44, "Drop a character folder on the Hardware Buddy window in Claude, or put it in "
          "/characters on the SD card.", "t14", "dim", w=W - 28),
        sd,
        T("ins.status", 14, lp(200, 262), "", "t12", "faint", w=W - 28, ident="install_status")]
    return p


COMMANDS = [("play_arrow", "Continue", "accent"), ("code", "Run tests", "info"), ("check", "Commit", "ok"),
            ("refresh", "/compact", "dim"), ("lightbulb", "Explain step", "warn"), ("stop", "Stop", "bad"),
            ("keyboard", "Type…", "dim"), ("add", "New command", "faint")]


def commands():
    """user_data = index in COMMANDS (commands.cpp maps it to what is sent)."""
    p = scr("commands", [])
    kids = []
    bw, bh = (W - 28) // 2, lp(36, 48)   # 146 x 36 landscape, 106 x 48 portrait (more height to spare)
    for i, (ic, name, col) in enumerate(COMMANDS):
        x, y = 10 + (i % 2) * (bw + 8), SB + 42 + (i // 2) * (bh + 6)
        lbl = T(f"cmd.{i}.t", 38, 0, name, "t14", "text", w=bw - 44, h=lp(18, 36), align="LEFT_MID",
                ident="custom_command_text" if name == "New command" else None)
        b = btn(f"cmd.{i}", x, y, bw, bh, C["surface"], [icon(f"cmd.{i}.i", 10, 0, ic, 20, col, align="LEFT_MID"), dot(lbl)],
                radius=12)
        act(b, "send_command", user_data=i)
        if name == "New command":
            act(b, "edit_custom_command", "LONG_PRESSED")
        kids.append(b)
    p.root["children"] = [status_bar()] + header("cmd", "Quick commands", p) + kids
    return p


def row(key, y, ic, name, value=None, ctrl=None, color="text", value_bind=None):
    """Settings row: icon, label, value text (or a variable) or control, chevron."""
    kids = [icon(key + ".i", 10, 0, ic, 20, "dim" if color == "text" else color, align="LEFT_MID"),
            T(key + ".t", 40, 0, name, "t14", color, align="LEFT_MID")]
    if value_bind:
        kids += [dot(label(key + ".v", -30, 0, "", style=S(txt("t12", "faint", align="RIGHT_MID")), w=lp(110, 70), h=16,
                           bind=value_bind)),
                 icon(key + ".c", -6, 0, "chevron_right", 20, "faint", align="RIGHT_MID")]
        kids[-2]["localStyles"]["definition"]["MAIN"]["DEFAULT"]["text_align"] = "RIGHT"
    if value:
        kids += [T(key + ".v", -30, 0, value, "t12", "faint", align="RIGHT_MID"),
                 icon(key + ".c", -6, 0, "chevron_right", 20, "faint", align="RIGHT_MID")]
    if ctrl:
        kids.append(ctrl)
    return btn(key, 0, y, W - 20, 40, C["surface"], kids, radius=12)


def sw(key, on_=True, ident=None, action=None):
    """Switch; action → native action_<name> on VALUE_CHANGED, ident → objects.<ident> in C."""
    s = switch(key, -10, 0, 40, 22, on_, ident=ident, style=S(
        box(C["raised"], 11, align="RIGHT_MID"),
        INDICATOR={"DEFAULT": box(C["raised"], 11), "CHECKED": {"bg_color": C["accent"], "bg_opa": 255}},
        KNOB={"DEFAULT": box(C["text"], 11, pad=-3)}))
    if action:
        act(s, action, "VALUE_CHANGED")
    return s


def sl(key, value, ident=None, action=None, x=-12, y=0, w=None, align="RIGHT_MID"):
    w = w or lp(120, 90)
    st = box(C["raised"], 3)
    if align:
        st["align"] = align
    s = slider(key, x, y, w, 6, value, ident=ident, style=S(st, INDICATOR=box(C["accent"], 3),
                                                            KNOB=box(C["text"], 8, pad=5)))
    if action:
        act(s, action, "VALUE_CHANGED")
        act(s, action, "RELEASED")   # final value → saved
    return s


def settings():
    p = scr("settings", [])
    spec = [  # (key, icon, name, row kwargs, screen it opens)
        ("owner", "person", "Owner", dict(value_bind="owner_name_text"), "!edit_owner"),
        ("petname", "pets", "Pet name", dict(value_bind="pet_name_text"), "!edit_pet_name"),
        ("bright", "brightness_6", "Brightness", dict(ctrl=sl("set.bright.s", 70, "brightness_slider", "set_brightness")), None),
        ("screen", "screen_rotation", "Screen", dict(value_bind="screen_orientation_name"), "orientation"),
        ("vol", "volume_up", "Volume", dict(ctrl=sl("set.vol.s", 55, "volume_slider", "set_volume")), None),
        ("sound", "notifications", "Sound effects", dict(ctrl=sw("set.sound.s", True, "sound_switch", "set_sound_enabled")), None),
        ("read", "graphic_eq", "Read replies aloud", dict(ctrl=sw("set.read.s", True, "read_aloud_switch", "set_read_aloud")), None),
        ("recap", "auto_stories", "Day recap by Claude", dict(ctrl=sw("set.recap.s", True, "recap_switch", "set_recap")), None),
        ("led", "lightbulb", "Status LED", dict(ctrl=sw("set.led.s", True, "led_switch", "set_led")), None),
        ("nap", "bedtime", "Nap when idle", dict(ctrl=sw("set.nap.s", True, "nap_switch", "set_nap")), None),
        ("hud", "chat", "Show messages", dict(ctrl=sw("set.hud.s", True, "hud_switch", "set_hud")), None),
        ("lang", "chat", "Language", dict(value_bind="ui_language_name"), "language"),
        ("speech", "mic", "Speech language", dict(value_bind="speech_language_name"), "speech_language"),
        ("bt", "bluetooth", "Bluetooth", dict(value_bind="bt_status_text"), "pairing"),
        ("wifi", "wifi", "WiFi", dict(value_bind="wifi_status_text"), "wifi"),
        ("demo", "play_arrow", "Demo mode", dict(ctrl=sw("set.demo.s", False, "demo_switch", "set_demo")), None),
        ("reset", "restart_alt", "Reset…", dict(value=" ", color="bad"), "reset"),
    ]
    rows = []
    for i, (key, ic, name, kw, target) in enumerate(spec):
        r = row(f"set.{key}", i * 46, ic, name, **kw)
        if target and target.startswith("!"):   # a keyboard opens (native action)
            act(r, target[1:])
        elif target:
            p.nav(r, "CLICKED", target, "MOVE_LEFT")
        rows.append(r)
    lst = panel("set.list", 10, SB + 40, W - 20, H - SB - 40, style=S(box()), children=rows, scroll=True)
    p.root["children"] = [status_bar()] + header("set", "Settings", p) + [lst]
    return p


def picker(name, title, options, action, check_prefix):
    """Language list: tap selects (native action gets the index) and goes back; check on the current one."""
    p = scr(name, [])
    rows = []
    for i, opt in enumerate(options):
        r = btn(f"{name}.{i}", 0, i * 46, W - 20, 40, C["surface"], [
            T(f"{name}.{i}.t", 14, 0, opt, "t14", "text", align="LEFT_MID"),
            panel(f"{name}.{i}.ck", -10, 0, 22, 22, ident=f"{check_prefix}{i}", hidden=i != 0,
                  style=S(box(C["accent"], 11, align="RIGHT_MID")), children=[
                      icon(f"{name}.{i}.cki", 0, 0, "check", 16, C["on_accent"], align="CENTER")])], radius=12)
        act(r, action, user_data=i)
        p.back(r)
        rows.append(r)
    lst = panel(f"{name}.list", 10, SB + 40, W - 20, H - SB - 40, style=S(box()), children=rows, scroll=True)
    p.root["children"] = [status_bar()] + header(name, title, p) + [lst]
    return p


def language():
    return picker("language", "Language", [n for _, n in LANGS], "set_ui_language", "lang_check_")


def speech_language():
    """Index 0 follows the interface language, 1.. = LANGS."""
    return picker("speech_language", "Speech language", ["Same as interface"] + [n for _, n in LANGS],
                  "set_speech_language", "speech_check_")


ORIENTATIONS = ["Landscape", "Landscape, flipped", "Portrait", "Portrait, flipped"]


def orientation():
    """Index = Settings.rotation (ui_layout.cpp turns the display and moves the widgets)."""
    return picker("orientation", "Screen", ORIENTATIONS, "set_orientation", "orient_check_")


def wifi():
    """Network list (filled by wifi_ui.cpp on open), tap → password keyboard."""
    p = scr("wifi", [])
    hdr = header("wifi", "WiFi", p, "refresh")
    act(hdr[-1], "wifi_scan")
    sample = [list_row(f"wifi.{i}", i * 52, ic, "accent" if i == 0 else "dim", n, s)
              for i, (ic, n, s) in enumerate([("signal_wifi_4_bar", "HomeNet", "Connected"),
                                              ("network_wifi_3_bar", "Neighbours-5G", "")])]
    lst = panel("wifi.list", 10, SB + 62, W - 20, H - SB - 62, style=S(box()), children=sample, scroll=True,
                ident="wifi_list")
    act(p.root, "wifi_scan", "SCREEN_LOADED")
    p.root["children"] = [status_bar()] + hdr + [
        dot(T("wifi.status", 16, SB + 40, "HomeNet · 192.168.1.42", "t12", "dim", w=W - 32, h=16, ident="wifi_status")),
        lst]
    return p


def reset():
    """Tap twice to confirm; the firmware swaps the hint under each row."""
    p = scr("reset", [])
    rows = []
    for i, (ic, name) in enumerate([("delete", "Delete characters"), ("bluetooth_disabled", "Forget pairing"),
                                    ("delete_forever", "Factory reset")]):
        r = btn(f"rst.{i}", 10, SB + 42 + i * 56, W - 20, 50, C["bad_soft"], [
            icon(f"rst.{i}.i", 12, 0, ic, 20, "bad", align="LEFT_MID"),
            T(f"rst.{i}.t", 44, 7, name, "t14", "bad"),
            T(f"rst.{i}.h", 44, 27, "", "t11", "faint", ident=f"reset_hint_{i}")], radius=12, pressed_bg="#4A2622")
        act(r, "reset_action", user_data=i)
        rows.append(r)
    p.root["children"] = [status_bar()] + header("rst", "Reset", p) + rows
    return p


def headset():
    p = scr("headset", [])
    # Landscape: the headset on the left, details to its right. Portrait: headset on top, details under it.
    cx, cy = lp(62, W // 2), lp(118, SB + 76)
    tx, ty, tw = lp(124, 16), lp(SB, 110), lp(184, W - 32)   # details column (ty + 48 = first line)
    ripple = []
    for i, d in enumerate([70, 92]):
        r = panel(f"hs.r{i}", cx - d // 2, cy - d // 2, d, d, style=S(box(None, d // 2, C["ok"], border_width=2, border_opa=120 - i * 50)))
        r["timeline"] = keyframes(f"hs.r{i}.tl", [(i * 0.5, i * 0.5 + 1, {"opacity": 30}), (i * 0.5 + 1, i * 0.5 + 2, {"opacity": 255})])
        ripple.append(r)
    bw = (tw - 8) // 2
    mute = btn("hs.mute", tx, ty + 172, bw, 36, C["surface"], [
        icon("hs.mute.i", 10, 0, "mic_off", 20, "dim", align="LEFT_MID"),
        T("hs.mute.t", 36, 0, "Mute", "t14", "text", align="LEFT_MID")], radius=12, ident="headset_mute")
    act(mute, "voice_mute")
    ptt = btn("hs.ptt", tx + bw + 8, ty + 172, bw, 36, C["accent_soft"], [
        icon("hs.ptt.i", 10, 0, "mic", 20, "accent", align="LEFT_MID"),
        T("hs.ptt.t", 36, 0, "Hold", "t14", "accent", align="LEFT_MID")], radius=12)
    act(ptt, "push_to_talk_down", "PRESSED")
    act(ptt, "push_to_talk_up", "RELEASED")
    kids = ripple + [
        panel("hs.core", cx - 26, cy - 26, 52, 52, style=S(box(C["ok_soft"], 26)), children=[
            icon("hs.core.i", 0, 0, "headset_mic", 32, "ok", align="CENTER")]),
        dot(T("hs.state", tx, ty + 48, "Connected", "h18", "ok", w=tw + 6, h=24, ident="headset_state")),
        dot(T("hs.pc", tx, ty + 72, "DESKTOP-PC  ·  WiFi  ·  38 ms", "t12", "dim", w=tw + 6, h=16, ident="headset_pc")),
        T("hs.mic.t", tx, ty + 100, "Mic level", "t12", "dim"),
        bar("hs.mic.b", tx, ty + 118, tw, 8, 64, ident="headset_mic_level",
            style=S(box(C["raised"], 4), INDICATOR=box(C["ok"], 4))),
        T("hs.spk.t", tx, ty + 134, "Speaker", "t12", "dim"),
        sl("hs.spk.s", 70, "speaker_slider", "set_speaker_volume", x=tx, y=ty + 154, w=tw, align=None),
        mute, ptt,
    ]
    p.root["children"] = [status_bar()] + header("hs", "Headset", p) + kids
    p.loop_animation(2)
    return p


def info():
    p = scr("info", [])
    lines = [("Board", "Hosyond ES3C28P · ESP32-S3"), ("Memory", "8 MB PSRAM · 16 MB flash"),
             ("Bluetooth", "Claude-A1B2 · encrypted"), ("WiFi", "HomeNet · 192.168.1.42"),
             ("Battery", "87% · 4.02 V · charging"), ("Uptime", "2h 14m"), ("Firmware", "Vibe-a-Gochi 0.1")]
    kids = []
    for i, (k, v) in enumerate(lines):
        kids += [T(f"inf.{i}.k", 0, i * 24, k, "t12", "faint"),
                 dot(T(f"inf.{i}.v", 86, i * 24, v, "t12", "text", w=W - 130, h=16, ident=f"info_value_{i}"))]
    card = panel("inf.card", 10, SB + 42, W - 20, H - SB - 44, style=S(box(C["surface"], 14, pad=12)), children=kids, scroll=True)
    p.root["children"] = [status_bar()] + header("inf", "Device", p) + [card]
    return p


def clock():
    p = scr("clock", [])
    px, py = lp(159, (W - 135) // 2), lp(120, 196)   # the sleeping pet: bottom right, or under the date
    pet = pet_slot("clk.pet", px, py, "pv_clock", "clock_pet", bob=2, period=3)
    z = T("clk.zz", 0, 14, "z Z", "h18", "dim")
    z["timeline"] = keyframes("clk.zz.tl", [(0, 1.5, {"top": 0, "opacity": 60}), (1.5, 3, {"top": 14, "opacity": 255})])
    zz = floating("clk.zz", min(px + 131, W - 40), py + 16, 40, 40, z)
    back = btn("clk.tap", 0, 0, W, H, C["bg"], [], radius=0)
    back["localStyles"]["definition"]["MAIN"]["DEFAULT"]["bg_opa"] = 0
    p.back(back)
    p.root["children"] = [back, label("clk.time", 18, 34, "", style=S(txt("clock64", "text")), bind="clock_text"),
                          T("clk.date", 22, 112, "Sunday, October 25", "t16", "dim", ident="clock_date"),
                          T("clk.note", 22, 136, "Charging · nothing running", "t12", "faint", ident="clock_note"),
                          pet, zz]
    p.loop_animation(3)
    return p


def pairing():
    p = scr("pairing", [])
    code = pill("pair.code", 30, lp(92, 112), W - 60, 64, C["surface"], [
        T("pair.code.t", 0, 0, "482 913", "clock40", "text", align="CENTER", ident="pair_code")], radius=16, border=C["line"])
    forget = btn("pair.forget", 30, H - 44, W - 60, 34, C["bad_soft"], [
        T("pair.forget.t", 0, 0, "Forget pairing", "t14", "bad", align="CENTER")], radius=12, pressed_bg="#4A2622")
    act(forget, "reset_action", user_data=1)
    p.root["children"] = [status_bar()] + header("pair", "Bluetooth pairing", p) + [
        dot(T("pair.hint", 30, SB + 46, "Type this code on your computer", "t14", "dim", w=W - 60, h=20, ident="pair_hint")),
        code,
        T("pair.how", 30, lp(164, 190), "Claude app  ·  Developer  ·  Hardware Buddy  ·  Connect", "t11", "faint", w=W - 60),
        forget]
    return p


# ------------------------------------------------------------------ assemble

def fonts():
    out = []
    latin = "0x20-0x7E,0xA0-0xFF,0x152-0x153,0x2026"   # ASCII + Latin-1 + oe: en, de, fr, it, pt
    icons = "".join(I.values())
    digits = "0x20,0x2D,0x30-0x3A"
    for name, f, size, rng in [
        ("t9", "Nunito-ExtraBold.ttf", 9, latin), ("t11", "Nunito-Medium.ttf", 11, latin),
        ("t12", "Nunito-Medium.ttf", 12, latin), ("t14", "Nunito-Medium.ttf", 14, latin),
        ("t16", "Nunito-Medium.ttf", 16, latin), ("h18", "Nunito-ExtraBold.ttf", 18, latin),
        ("h24", "Nunito-ExtraBold.ttf", 24, latin), ("m12", "JetBrainsMono-Regular.ttf", 12, latin),
        ("clock40", "Nunito-ExtraBold.ttf", 40, digits),
        ("clock64", "Nunito-ExtraBold.ttf", 64, digits)]:
        out.append(font_entry(name, FONTS / f, size, FONT_C_OUT, ranges=rng))
    for size in (16, 20, 24, 32):
        out.append(font_entry(f"i{size}", FONTS / "Icons.ttf", size, FONT_C_OUT, ranges="", symbols=icons))
    return out


def c_str(s):
    return json.dumps(s, ensure_ascii=False)   # UTF-8 literal; GCC's execution charset is UTF-8


def write_i18n():
    """firmware/src/i18n_table.h: every translated label (by EEZ object) and the firmware texts."""
    n = len(LANGS)
    out = ["// Generated by ui/tools/gen_ui.py from ui/tools/i18n.py - edit the translations there.",
           "#pragma once", '#include "ui/screens.h"', "",
           f"constexpr int I18N_LANG_COUNT = {n};",
           "static const char *const I18N_CODES[] = {" + ", ".join(c_str(c) for c, _ in LANGS) + "};",
           "static const char *const I18N_NAMES[] = {" + ", ".join(c_str(nm) for _, nm in LANGS) + "};", "",
           f"struct TrLabel {{ lv_obj_t **obj; const char *text[{n}]; }};", "",
           "static const TrLabel TR_LABELS[] = {"]
    for ident, en in TRANSLATED:
        out.append(f"  {{&objects.{ident}, {{" + ", ".join(c_str(t) for t in (en,) + TR[en]) + "}},")
    out += ["};", "",
            f"static const char *const TR_STRINGS[][{n}] = {{"]
    for name, texts in FW.items():
        assert len(texts) == n, name
        out.append("  {" + ", ".join(c_str(t) for t in texts) + "},")
    out += ["};", "",
            "inline lv_obj_t *lang_check(int i) {",
            "  lv_obj_t *c[] = {" + ", ".join(f"objects.lang_check_{i}" for i in range(n)) + "};",
            "  return c[i];", "}", "",
            "inline lv_obj_t *speech_check(int i) {   // 0 = same as interface",
            "  lv_obj_t *c[] = {" + ", ".join(f"objects.speech_check_{i}" for i in range(n + 1)) + "};",
            "  return c[i];", "}", ""]
    I18N_OUT.write_text("\n".join(out), encoding="utf-8")
    # The enum lives in its own header so any file can name a string without pulling the tables in.
    (I18N_OUT.parent / "i18n_strings.h").write_text(
        "// Generated by ui/tools/gen_ui.py from ui/tools/i18n.py (FW).\n#pragma once\n\n"
        "enum class TrString {\n" + "".join(f"  {k},\n" for k in FW) + "  COUNT\n};\n", encoding="utf-8")
    used = {e for _, e in TRANSLATED}
    unused = [t for t in TR if t not in used]
    print(f"i18n: {len(TRANSLATED)} labels, {len(FW)} firmware texts, {n} languages" +
          (f"; unused: {unused}" if unused else ""))


def write_theme():
    """firmware/src/theme.h: the palette and icon codepoints for widgets built at runtime."""
    out = ["// Generated by ui/tools/gen_ui.py - the same palette and icons as the EEZ screens.", "#pragma once",
           "#include <lvgl.h>", ""]
    for k, v in C.items():
        out.append(f"#define THEME_{k.upper()} lv_color_hex(0x{v[1:]})")
    out.append("")
    for k, v in ICON.items():
        utf8 = "".join(f"\\x{b:02X}" for b in chr(v).encode("utf-8"))
        out.append(f'#define ICON_{k.upper()} "{utf8}"')
    THEME_OUT.write_text("\n".join(out) + "\n", encoding="utf-8")


def all_pages():
    return [home(), approval(), chat(), voice(), pet(), more(), today(), characters(), install(), commands(),
            settings(), headset(), info(), clock(), pairing(), projects(), chats(),
            language(), speech_language(), orientation(), wifi(), reset()], [status_bar_widget()]


# Lists the firmware empties and fills itself: their sample rows are not on the device.
RUNTIME_LISTS = {"chat_list", "projects_list", "chats_list", "wifi_list", "characters_grid"}
CONTENT = -32768   # size from the content (an unsized label or image)


def geom(o):
    return (o["left"], o["top"], o["width"] if o["widthUnit"] == "px" else CONTENT,
            o["height"] if o["heightUnit"] == "px" else CONTENT)


def layout_diff(land, port, uw_land, uw_port, out, page, path=()):
    """Walk both layouts of a page side by side; collect each widget whose place differs, with the
    path of child indexes the firmware follows from the screen (a user widget holds its page's root)."""
    assert land["type"] == port["type"] and len(land["children"]) == len(port["children"]), \
        f"{page}: the portrait tree differs at {path}"
    if geom(land) != geom(port):
        out.append((page, path, geom(land), geom(port)))
    if land.get("identifier") in RUNTIME_LISTS:
        return
    if land["type"] == "LVGLUserWidgetWidget":
        name = land["userWidgetPageName"]
        layout_diff(uw_land[name], uw_port[name], uw_land, uw_port, out, page, path + (0,))
    for i, (cl, cp) in enumerate(zip(land["children"], port["children"])):
        layout_diff(cl, cp, uw_land, uw_port, out, page, path + (i,))


def write_layout(land_pages, land_widgets, port_pages, port_widgets):
    uw_l = {w.name: w.root for w in land_widgets}
    uw_p = {w.name: w.root for w in port_widgets}
    out = []
    for pl, pp in zip(land_pages, port_pages):
        layout_diff(pl.root, pp.root, uw_l, uw_p, out, pl.name)
    g = lambda v: "{" + ", ".join(str(n) for n in v) + "}"
    lines = ["// Generated by ui/tools/gen_ui.py: where each widget sits in landscape and in portrait",
             "// (only the ones that move). path = child indexes + 1 from the screen.",
             "#pragma once", '#include "ui/screens.h"', "",
             f"constexpr int16_t LAYOUT_CONTENT = {CONTENT};   // LV_SIZE_CONTENT", "",
             "struct LayoutBox { lv_obj_t **screen; const char *path; int16_t land[4], port[4]; };", "",
             "static const LayoutBox LAYOUT[] = {"]
    for page, path, gl, gp in out:
        p = " ".join(f'"\\x{i + 1:02x}"' for i in path) or '""'
        lines.append(f"  {{&objects.{page}, {p}, {g(gl)}, {g(gp)}}},")
    lines += ["};", ""]
    LAYOUT_OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"layout: {len(out)} widgets move between landscape and portrait")


def main():
    # Portrait first, only for the places; then the landscape project (fresh IDs and translations).
    set_size(240, 320)
    reset_ids()
    port_pages, port_widgets = all_pages()
    set_size(320, 240)
    reset_ids()
    TRANSLATED.clear()
    pages, widgets = all_pages()
    write_layout(pages, widgets, port_pages, port_widgets)
    bitmaps = [bitmap_entry(f.stem, f) for f in sorted(IMAGES.glob("*.png"))]
    s = lambda name, val: global_var(name, "string", json.dumps(val, ensure_ascii=False))
    variables = [
        s("clock_text", "10:42"),
        s("last_message", "Running the test suite… 42 passed, 0 failed"),
        s("prompt_hint", "git push origin main --force-with-lease"),
        s("voice_transcript", "fix the login bug and run the tests again"),
        s("current_project", "vibe-a-gochi"),
        s("current_chat", "Fix failing auth tests"),
        s("ui_language_name", "English"),
        s("speech_language_name", "Same as interface"),
        s("sb_link_icon", I["circle"]), s("sb_link_text", "Claude"),
        s("sb_usb_icon", I["usb"]), s("sb_bt_icon", I["bluetooth_connected"]), s("sb_wifi_icon", I["wifi"]), s("sb_bat_icon", I["battery_5_bar"]),
        s("bt_status_text", "Claude-A1B2"), s("wifi_status_text", "HomeNet"),
        s("owner_name_text", ""), s("pet_name_text", "Buddy"),
        s("screen_orientation_name", "Landscape"),   # keep last: ui_live.cpp sizes its cache by it
    ]
    actions = [user_action(n) for n in [
        "permission_approve", "permission_deny", "permission_always", "send_quick_reply", "type_message",
        "voice_open", "voice_close", "voice_stop", "voice_mute", "push_to_talk_down", "push_to_talk_up",
        "send_command", "edit_custom_command", "new_chat",
        "set_brightness", "set_volume", "set_sound_enabled", "set_read_aloud", "set_led", "set_nap", "set_hud",
        "set_demo", "set_ui_language", "set_speech_language", "set_orientation", "set_speaker_volume",
        "set_recap", "summary_toggle", "summary_open", "stop_claude", "edit_owner", "edit_pet_name",
        "wifi_scan", "reset_action", "import_from_sd"]]
    proj = project(TEMPLATE, W, H, pages, widgets, fonts(), bitmaps, variables, actions,
                   description="Vibe-a-Gochi: Claude desk buddy for the Hosyond ES3C28P 2.8\" ESP32-S3 board",
                   dest="../firmware/src/ui")
    OUT.write_text(json.dumps(proj, indent=2, ensure_ascii=False), encoding="utf-8")
    write_i18n()
    write_theme()
    print(f"wrote {OUT} ({OUT.stat().st_size // 1024} KB, {len(pages)} screens)")
    if "--build" in sys.argv:
        ok, log = build(OUT)
        print("\n".join(l for l in log.splitlines() if l.strip() and not l.startswith("File ")
                        and "cache" not in l.lower()))
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
