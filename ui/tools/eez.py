"""
Tiny builder for EEZ Studio LVGL projects (.eez-project JSON, LVGL 9, EEZ Flow on).

The generator scripts describe screens with these helpers; EEZ Studio stays
the visual editor/simulator and the exporter of LVGL C code. Object IDs are
derived from stable names (uuid5) so regenerating a project does not churn
every ID and EEZ Studio keeps its editor state.
"""
import base64, json, re, subprocess, tempfile, uuid
from pathlib import Path

_NS = uuid.UUID("6f1c2d4e-9a7b-4c3d-8e2f-1a2b3c4d5e6f")
_counter = {}


def oid(key):
    """Stable object ID: same key → same ID across runs (suffix counts duplicates)."""
    n = _counter.get(key, 0)
    _counter[key] = n + 1
    return str(uuid.uuid5(_NS, f"{key}#{n}"))


def reset_ids():
    _counter.clear()


# ---------------------------------------------------------------- widgets

FLAGS = {
    "screen": "CLICKABLE|PRESS_LOCK|CLICK_FOCUSABLE|GESTURE_BUBBLE|SNAPPABLE|SCROLL_ELASTIC|SCROLL_MOMENTUM|SCROLL_CHAIN_HOR|SCROLL_CHAIN_VER",
    "panel": "CLICKABLE|PRESS_LOCK|CLICK_FOCUSABLE|GESTURE_BUBBLE|SNAPPABLE|SCROLL_ELASTIC|SCROLL_MOMENTUM|SCROLL_CHAIN_HOR|SCROLL_CHAIN_VER",
    "static": "PRESS_LOCK|CLICK_FOCUSABLE|GESTURE_BUBBLE|SNAPPABLE|SCROLL_ELASTIC|SCROLL_MOMENTUM|SCROLL_CHAIN_HOR|SCROLL_CHAIN_VER",
    "label": "CLICK_FOCUSABLE|GESTURE_BUBBLE|PRESS_LOCK|SCROLLABLE|SCROLL_CHAIN_HOR|SCROLL_CHAIN_VER|SCROLL_ELASTIC|SCROLL_MOMENTUM|SCROLL_WITH_ARROW|SNAPPABLE",
    "button": "CLICKABLE|PRESS_LOCK|CLICK_FOCUSABLE|GESTURE_BUBBLE|SNAPPABLE|SCROLL_ELASTIC|SCROLL_ON_FOCUS|SCROLL_MOMENTUM|SCROLL_CHAIN_HOR|SCROLL_CHAIN_VER",   # LVGL 9 names: a drag on a button scrolls its list
    "image": "ADV_HITTEST|CLICK_FOCUSABLE|GESTURE_BUBBLE|PRESS_LOCK|SCROLLABLE|SCROLL_CHAIN_HOR|SCROLL_CHAIN_VER|SCROLL_ELASTIC|SCROLL_MOMENTUM|SCROLL_WITH_ARROW|SNAPPABLE",
    "input": "CLICKABLE|CLICK_FOCUSABLE|GESTURE_BUBBLE|PRESS_LOCK|SCROLL_ELASTIC|SCROLL_MOMENTUM|SCROLL_WITH_ARROW|SNAPPABLE",
}


def _base(kind, key, x, y, w, h, flags, wu="px", hu="px", ident=None, style=None,
          children=None, events=None, clickable=True, hidden=False):
    o = {
        "objID": oid(key),
        "type": kind,
        "left": x, "top": y, "width": w, "height": h,
        "customInputs": [], "customOutputs": [],
        "style": {"objID": oid(key + ".style"), "useStyle": "default",
                  "conditionalStyles": [], "childStyles": []},
        "timeline": [],
        "eventHandlers": [],
        "leftUnit": "px", "topUnit": "px", "widthUnit": wu, "heightUnit": hu,
        "children": children or [],
        "widgetFlags": flags + ("|HIDDEN" if hidden else ""),
        "hiddenFlagType": "literal",
        "clickableFlag": clickable,
        "clickableFlagType": "literal",
        "checkedStateType": "literal",
        "disabledStateType": "literal",
        "states": "",
        "localStyles": {"objID": oid(key + ".ls")},
        "groupIndex": 0,
    }
    if ident:
        o["identifier"] = ident
    if style:
        o["localStyles"]["definition"] = style
    for ev in events or []:
        o["eventHandlers"].append(ev)
    return o


def on(event, key, action=None, user_data=0):
    """Event handler. action=None → flow output (wire with Page.connect), else native user action.
    user_data reaches a native action as lv_event_get_user_data(e)."""
    h = {"objID": oid(key + ".ev." + event), "eventName": event, "userData": user_data}
    if action:
        h.update(handlerType="action", action=action)
    else:
        h.update(handlerType="flow")
    return h


def screen(key, w, h, style, children):
    return _base("LVGLScreenWidget", key, 0, 0, w, h, FLAGS["screen"], style=style, children=children)


def panel(key, x, y, w, h, style=None, children=None, ident=None, events=None,
          clickable=False, scroll=False, wu="px", hu="px", hidden=False):
    flags = FLAGS["panel"] if clickable else FLAGS["static"]
    if scroll:
        flags += "|SCROLLABLE|SCROLL_WITH_ARROW"
    return _base("LVGLPanelWidget", key, x, y, w, h, flags, wu=wu, hu=hu, ident=ident,
                 style=style, children=children, events=events, clickable=clickable, hidden=hidden)


def label(key, x, y, text, style=None, w=None, h=None, ident=None, bind=None, long_mode="WRAP"):
    """Text label. w/h None → sized to content. bind=<variable name> shows a global variable."""
    o = _base("LVGLLabelWidget", key, x, y, w or 0, h or 0, FLAGS["label"],
              wu="px" if w else "content", hu="px" if h else "content",
              ident=ident, style=style, clickable=False)
    o.update(text=bind or text, textType="expression" if bind else "literal",
             longMode=long_mode, recolor=False)
    return o


def button(key, x, y, w, h, style=None, children=None, ident=None, events=None, checkable=False):
    flags = FLAGS["button"] + ("|CHECKABLE" if checkable else "")
    return _base("LVGLButtonWidget", key, x, y, w, h, flags, ident=ident, style=style,
                 children=children, events=events)


def image(key, x, y, img, ident=None, style=None, zoom=256, events=None):
    o = _base("LVGLImageWidget", key, x, y, 0, 0, FLAGS["image"], wu="content", hu="content",
              ident=ident, style=style, events=events, clickable=bool(events))
    o.update(image=img, setPivot=False, pivotX=0, pivotY=0, zoom=zoom, angle=0, innerAlign="CENTER")
    return o


def slider(key, x, y, w, h, value, style=None, ident=None, lo=0, hi=100):
    o = _base("LVGLSliderWidget", key, x, y, w, h, FLAGS["input"], ident=ident, style=style)
    o.update(min=lo, minType="literal", max=hi, maxType="literal", mode="NORMAL",
             value=value, valueType="literal", valueLeft=0, valueLeftType="literal")
    return o


def switch(key, x, y, w, h, checked, style=None, ident=None):
    o = _base("LVGLSwitchWidget", key, x, y, w, h, FLAGS["input"], ident=ident, style=style)
    if checked:
        o["states"] = "CHECKED"
        o["checkedState"] = True
    return o


def bar(key, x, y, w, h, value, style=None, ident=None, lo=0, hi=100):
    o = _base("LVGLBarWidget", key, x, y, w, h, FLAGS["static"], ident=ident, style=style, clickable=False)
    o.update(min=lo, minType="literal", max=hi, maxType="literal", mode="NORMAL",
             value=value, valueType="literal", valueStart=0, valueStartType="literal",
             enableAnimation=False)
    return o


def arc(key, x, y, w, h, value, style=None, ident=None, bg_start=135, bg_end=45):
    o = _base("LVGLArcWidget", key, x, y, w, h, FLAGS["static"], ident=ident, style=style, clickable=False)
    o.update(rangeMin=0, rangeMinType="literal", rangeMax=100, rangeMaxType="literal",
             value=value, valueType="literal", bgStartAngle=bg_start, bgEndAngle=bg_end,
             mode="NORMAL", rotation=0)
    return o


def user_widget(key, x, y, w, h, page_name, ident=None):
    o = _base("LVGLUserWidgetWidget", key, x, y, w, h, FLAGS["panel"], ident=ident)
    o.update(userWidgetPageName=page_name,
             userPropertyValues={"objID": oid(key + ".upv"), "values": {}})
    return o


def keyframes(key, frames):
    """Timeline: frames = [(start_s, end_s, {"top": v, "scale": v, "opacity": v, ...}), ...]."""
    props = ["left", "top", "width", "height", "scale", "scaleX", "scaleY", "rotate", "opacity"]
    out = []
    for i, (s, e, vals) in enumerate(frames):
        kf = {"objID": oid(f"{key}.kf{i}"), "start": s, "end": e}
        for p in props:
            kf[p] = {"enabled": p in vals, "easingFunction": vals.get("ease", "easeInOutSine")}
            if p in vals:
                kf[p]["value"] = vals[p]
        out.append(kf)
    return out


# ---------------------------------------------------------------- flow

def change_screen(key, x, y, target, anim="FADE_IN", speed=220):
    return {
        "objID": oid(key), "type": "LVGLActionComponent",
        "left": x, "top": y, "width": 240, "height": 54,
        "customInputs": [], "customOutputs": [],
        "actions": [{
            "objID": oid(key + ".a"), "action": "changeScreen",
            "screen": target, "screenType": "literal",
            "fadeMode": anim, "fadeModeType": "literal",
            "speed": speed, "speedType": "literal",
            "delay": 0, "delayType": "literal",
            "useStack": True, "useStackType": "literal",
        }],
    }


def go_back(key, x, y, anim="MOVE_RIGHT", speed=220):
    return {
        "objID": oid(key), "type": "LVGLActionComponent",
        "left": x, "top": y, "width": 240, "height": 54,
        "customInputs": [], "customOutputs": [],
        "actions": [{
            "objID": oid(key + ".a"), "action": "changeScreen",
            "screen": "home", "screenType": "literal", "showPreviousScreen": True,
            "fadeMode": anim, "fadeModeType": "literal",
            "speed": speed, "speedType": "literal",
            "delay": 0, "delayType": "literal",
            "useStack": True, "useStackType": "literal",
        }],
    }


def start_block(key, x, y):
    return {"objID": oid(key), "type": "StartActionComponent", "left": x, "top": y,
            "width": 74, "height": 34, "customInputs": [], "customOutputs": []}


def animate_block(key, x, y, frm, to, speed):
    return {"objID": oid(key), "type": "AnimateActionComponent", "left": x, "top": y,
            "width": 188, "height": 54, "customInputs": [], "customOutputs": [],
            "from": str(frm), "to": str(to), "speed": str(speed)}


class Page:
    """A screen (or user widget) plus its flow blocks and wires."""

    def __init__(self, name, w, h, root, user_widget=False):
        self.name, self.w, self.h, self.root = name, w, h, root
        self.flow, self.lines, self.user_widget = [], [], user_widget
        self._fx = w + 60   # flow blocks are parked right of the screen in the editor

    def add(self, block):
        self.flow.append(block)
        return block

    def connect(self, src, output, dst, inp="@seqin"):
        self.lines.append({"objID": oid(f"{self.name}.line.{src['objID']}.{output}"),
                           "source": src["objID"], "output": output,
                           "target": dst["objID"], "input": inp})

    def nav(self, widget, event, target, anim="FADE_IN", speed=220):
        """Wire widget's event to a change-screen block (creates the event handler too)."""
        widget["eventHandlers"].append(on(event, f"{self.name}.{widget['objID']}"))
        y = 40 + len(self.flow) * 70
        blk = self.add(change_screen(f"{self.name}.go.{target}.{widget['objID']}", self._fx, y, target, anim, speed))
        self.connect(widget, event, blk)

    def back(self, widget, event="CLICKED"):
        widget["eventHandlers"].append(on(event, f"{self.name}.{widget['objID']}.back"))
        y = 40 + len(self.flow) * 70
        blk = self.add(go_back(f"{self.name}.back.{widget['objID']}", self._fx, y))
        self.connect(widget, event, blk)

    def loop_animation(self, seconds):
        """Play this page's timeline forever (Start → Animate 0..seconds, looping)."""
        s = self.add(start_block(f"{self.name}.start", self._fx, -120))
        a = self.add(animate_block(f"{self.name}.anim", self._fx + 120, -120, 0, seconds, 1))
        self.connect(s, "@seqout", a)
        self.connect(a, "@seqout", a)

    def to_json(self):
        d = {
            "objID": oid(f"page.{self.name}"),
            "components": [self.root] + self.flow,
            "connectionLines": self.lines,
            "localVariables": [], "userProperties": [],
            "name": self.name, "left": 0, "top": 0, "width": self.w, "height": self.h,
            "createAtStart": True, "deleteOnScreenUnload": False,
        }
        if self.user_widget:
            d["isUsedAsUserWidget"] = True
        return d


# ---------------------------------------------------------------- assets

LV_FONT_CONV = Path(r"C:\pio\tools\lv_font_conv\lv_font_conv.js")


def _font_conv(ttf_path, size, bpp, ranges, symbols, fmt, lv_name, out):
    cmd = ["node", str(LV_FONT_CONV), "--font", str(ttf_path), "--size", str(size),
           "--bpp", str(bpp), "--format", fmt, "-o", str(out), "--no-compress"]
    # Symbols go in as numeric ranges: private-use icon codepoints don't survive
    # the Windows command line reliably.
    rng = ",".join(filter(None, [ranges] + [hex(ord(c)) for c in symbols]))
    cmd += ["-r", rng]
    if fmt == "lvgl":
        cmd += ["--lv-font-name", lv_name, "--lv-include", "lvgl.h"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(f"lv_font_conv failed for {ttf_path}:\n{r.stdout}\n{r.stderr}")


def font_entry(name, ttf_path, size, c_out_dir, bpp=4, ranges="0x20-0x7E", symbols=""):
    """
    Font for EEZ (embedded TTF; the editor renders it for the preview) plus the
    LVGL C file for the firmware. EEZ Studio's headless build declares fonts in
    fonts.h but renders them asynchronously and never writes them, so we write
    ui_font_<name>.c ourselves with the same converter EEZ uses.
    """
    data = Path(ttf_path).read_bytes()
    lv_name = f"ui_font_{name}"
    c_out = Path(c_out_dir) / f"{lv_name}.c"
    c_out.parent.mkdir(parents=True, exist_ok=True)
    _font_conv(ttf_path, size, bpp, ranges, symbols, "lvgl", lv_name, c_out)
    src = c_out.read_text(encoding="utf-8")
    line_h = int(re.search(r"\.line_height = (\d+)", src).group(1))
    base = int(re.search(r"\.base_line = (-?\d+)", src).group(1))
    parts = []
    for part in filter(None, ranges.split(",")):
        lo, _, hi = part.partition("-")
        lo, hi = int(lo, 0), int(hi or lo, 0)
        parts.append(f"{lo} - {hi}" if lo != hi else str(lo))
    parts += [str(ord(c)) for c in symbols]
    return {
        "objID": oid(f"font.{name}"),
        "name": name,
        "renderingEngine": "LVGL",
        "source": {"objID": oid(f"font.{name}.src"), "filePath": Path(ttf_path).name, "size": size},
        "embeddedFontFile": base64.b64encode(data).decode(),
        "bpp": bpp, "threshold": 128,
        "height": line_h, "ascent": line_h - base, "descent": base,
        "glyphs": [],
        "lvglRanges": ",".join(parts),
        "lvglSymbols": "",
    }


def bitmap_entry(name, png_path, bpp=32):
    data = base64.b64encode(Path(png_path).read_bytes()).decode()
    return {"objID": oid(f"bmp.{name}"), "name": name,
            "image": "data:image/png;base64," + data, "bpp": bpp, "alwaysBuild": False}


def global_var(name, typ, default, native=False):
    return {"objID": oid(f"var.{name}"), "name": name, "type": typ,
            "defaultValue": default, "persistent": False, "native": native}


def user_action(name, native=True):
    """Native → EEZ declares action_<name>(lv_event_t*) in actions.h; the firmware implements it."""
    return {"objID": oid(f"action.{name}"), "components": [], "connectionLines": [],
            "localVariables": [], "name": name,
            "implementationType": "native" if native else "flow"}


EEZ_STUDIO = Path.home() / "AppData/Local/Programs/eezstudio/EEZ Studio.exe"


def build(project_path, timeout=180):
    """Headless EEZ Studio build: exports LVGL C code. Returns (ok, log)."""
    r = subprocess.run([str(EEZ_STUDIO), "--build-project", str(Path(project_path).resolve())],
                       capture_output=True, text=True, timeout=timeout, encoding="utf-8", errors="replace")
    log = r.stdout + r.stderr
    errs = re.search(r"(\d+) errors? and", log)
    ok = "Build successfully finished" in log and (not errs or errs.group(1) == "0")
    return ok, log


def project(template_path, w, h, pages, widgets, fonts, bitmaps, variables, actions,
            description="", dest="src/ui"):
    p = json.loads(Path(template_path).read_text(encoding="utf-8"))
    g = p["settings"]["general"]
    g.update(displayWidth=w, displayHeight=h, flowSupport=True, lvglVersion="9.0",
             description=description, colorFormat="BGR")
    p["settings"]["build"]["destinationFolder"] = dest.replace("/", "\\")
    p["settings"]["build"]["lvglInclude"] = "lvgl.h"
    p["userPages"] = [pg.to_json() for pg in pages]
    p["userWidgets"] = [pg.to_json() for pg in widgets]
    p["fonts"], p["bitmaps"] = fonts, bitmaps
    p["variables"]["globalVariables"] = variables
    p["actions"] = actions
    return p
