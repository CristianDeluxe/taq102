#!/usr/bin/env python3
"""Turn a QLC+ workspace into the validation manifest the tablet desk needs.

The desk validates its show map before it enables a single control, and the
live `/vc.json` cannot carry that check: it has no speed-group membership, no
fixture or channel bindings, no solo-frame ancestry and no way to tell whether
a fog cue ever stops. All of that is in the `.qxw`, so it is extracted here,
on the Mac, into one JSON document that is published beside the map and hashed
with it. See docs/2026-09-12-dmx-desk-design.md.

    tools/show-manifest.py "QLC+ Setups/DeluxeEventos.qxw" -o manifest.json
    tools/show-manifest.py show.qxw --reaches 364      # who can start fog

The workspace is opened read-only and never written back.
"""

import argparse
import hashlib
import json
import re
import sys
import urllib.parse
import xml.etree.ElementTree as ET

NS = "{http://www.qlcplus.org/Workspace}"

# A function that can run forever is not safe to reach from a held gesture.
FOREVER = "Loop"

# The Virtual Console widgets that hold other widgets.
CONTAINERS = ("Frame", "SoloFrame")


def multiplier(value):
    """A speed dial's per-function multiplier, as a number.

    The file stores an enum index, not a factor: 6 is x1, and each step
    doubles, so 7 is x2 and 10 is x16. "None" means the dial does not touch
    that time at all, which is not the same as x1.
    """
    if value is None or value == "None":
        return None
    try:
        return 2.0 ** (int(value) - 6)
    except ValueError:
        return None


def tag(element):
    return element.tag[len(NS):] if element.tag.startswith(NS) else element.tag


def text_of(parent, name, default=None):
    child = parent.find(NS + name)
    return child.text if child is not None and child.text is not None else default


def script_targets(function):
    """Function ids a Script starts. Commands are URL-encoded in the file."""
    started = []
    for command in function.findall(NS + "Command"):
        decoded = urllib.parse.unquote(command.text or "")
        match = re.match(r"\s*startfunction\s*:\s*(\d+)", decoded)
        if match:
            started.append(int(match.group(1)))
    return started


def function_record(function):
    """One function, with everything it can reach and whether it can stop."""
    kind = function.get("Type")
    record = {
        "id": int(function.get("ID")),
        "type": kind,
        "name": function.get("Name", ""),
        "runOrder": text_of(function, "RunOrder"),
        "targets": [],
        "fixtures": [],
    }

    speed = function.find(NS + "Speed")
    if speed is not None:
        record["speed"] = {
            "fadeIn": int(speed.get("FadeIn", 0)),
            "fadeOut": int(speed.get("FadeOut", 0)),
            "duration": int(speed.get("Duration", 0)),
        }

    if kind == "Chaser":
        steps = []
        for step in function.findall(NS + "Step"):
            steps.append({
                "number": int(step.get("Number", 0)),
                "hold": int(step.get("Hold", 0)),
                "fadeIn": int(step.get("FadeIn", 0)),
                "fadeOut": int(step.get("FadeOut", 0)),
                "function": int(step.text) if step.text else None,
            })
        record["steps"] = steps
        record["targets"] = [s["function"] for s in steps if s["function"] is not None]
    elif kind == "Collection":
        record["targets"] = [int(child.text) for child in function.findall(NS + "Function")
                             if child.text]
    elif kind == "Script":
        record["commands"] = [urllib.parse.unquote(c.text or "")
                              for c in function.findall(NS + "Command")]
        record["targets"] = script_targets(function)
    elif kind == "Sequence":
        bound = function.get("BoundScene")
        if bound is not None:
            record["targets"] = [int(bound)]
    elif kind == "Scene":
        record["fixtures"] = [int(v.get("ID")) for v in function.findall(NS + "FixtureVal")]
        record["channels"] = {
            v.get("ID"): [int(n) for n in (v.text or "").split(",") if n != ""]
            for v in function.findall(NS + "FixtureVal") if v.text
        }
    elif kind in ("EFX", "RGBMatrix"):
        record["fixtures"] = [int(f.findtext(NS + "ID", "-1")) for f in function.findall(NS + "Fixture")]

    return record


def terminates(fid, functions, seen=None):
    """Does starting this function end on its own? Cycles read as 'no'.

    A scene runs until something stops it. A chaser or a script set to Loop
    never ends. Anything that can reach a function that never ends, never ends.
    """
    seen = seen if seen is not None else set()
    if fid in seen:
        return False
    seen = seen | {fid}
    function = functions.get(fid)
    if function is None:
        return False
    kind = function["type"]
    if kind == "Scene":
        return False
    if function.get("runOrder") == FOREVER:
        return False
    if kind == "Chaser" and any(step["hold"] <= 0 for step in function.get("steps", [])):
        return False
    return all(terminates(t, functions, seen) for t in function["targets"])


def reaches(fid, functions):
    """Every function id reachable from this one, transitively."""
    out, stack = set(), [fid]
    while stack:
        current = stack.pop()
        for target in functions.get(current, {}).get("targets", []):
            if target not in out:
                out.add(target)
                stack.append(target)
    return sorted(out)


def collect_widgets(node, parent_id, out):
    """The Virtual Console tree, flattened, keeping ancestry."""
    kind = tag(node)
    if kind in ("VirtualConsole", "Appearance", "WindowState", "Font"):
        for child in node:
            collect_widgets(child, parent_id, out)
        return
    if node.get("ID") is None:
        # The Virtual Console's outermost Frame carries no id: it is the page
        # container, and /vc.json gives it one of its own. Walk through it.
        for child in node:
            collect_widgets(child, parent_id, out)
        return

    widget = {
        "kind": kind,
        "id": int(node.get("ID")),
        "caption": node.get("Caption", ""),
        "parent": parent_id,
    }
    if kind == "Button":
        function = node.find(NS + "Function")
        widget["function"] = (int(function.get("ID"))
                              if function is not None and function.get("ID") else None)
        widget["action"] = text_of(node, "Action")
    elif kind == "SpeedDial":
        members = []
        for function in node.findall(NS + "Function"):
            if not function.text:
                continue
            members.append({
                "id": int(function.text),
                "fadeIn": multiplier(function.get("FadeIn")),
                "fadeOut": multiplier(function.get("FadeOut")),
                "duration": multiplier(function.get("Duration")),
            })
        widget["functions"] = members
        absolute = node.find(NS + "AbsoluteValue")
        if absolute is not None:
            widget["absolute"] = {"min": int(absolute.get("Minimum", 0)),
                                  "max": int(absolute.get("Maximum", 0))}
        widget["time"] = int(text_of(node, "Time", 0))
    elif kind == "XYPad":
        fixtures = []
        for fixture in node.findall(NS + "Fixture"):
            axes = {}
            for axis in fixture.findall(NS + "Axis"):
                axes[axis.get("ID")] = {
                    "low": float(axis.get("LowLimit", 0)),
                    "high": float(axis.get("HighLimit", 1)),
                    "reverse": axis.get("Reverse") == "True",
                }
            fixtures.append({"id": int(fixture.get("ID")),
                             "head": int(fixture.get("Head", 0)), "axes": axes})
        widget["fixtures"] = fixtures
        pan = node.find(NS + "Pan")
        tilt = node.find(NS + "Tilt")
        if pan is not None:
            widget["pan"] = {"min": int(pan.get("Min", 0)), "max": int(pan.get("Max", 255))}
        if tilt is not None:
            widget["tilt"] = {"min": int(tilt.get("Min", 0)), "max": int(tilt.get("Max", 255))}
    elif kind == "Slider":
        widget["mode"] = text_of(node, "SliderMode")
        cng = node.find(NS + "SliderMode")
        if cng is not None:
            widget["clickAndGo"] = cng.get("ClickAndGoType")
            widget["monitor"] = cng.get("Monitor") == "True"

    out.append(widget)
    # Only frames hold other widgets. A widget's own children describe itself -
    # an XY pad's <Fixture> and its <Axis ID="X">, a dial's <Function> - and
    # are already recorded above; walking into them would read "X" as an id.
    if kind in CONTAINERS:
        for child in node:
            if tag(child) in ("Appearance", "WindowState"):
                continue
            collect_widgets(child, widget["id"], out)


def build(path):
    with open(path, "rb") as handle:
        raw = handle.read()
    # A workspace is a plain document with a bare <!DOCTYPE Workspace>. Entity
    # definitions have no business in one, and expanding them is how a parser
    # reads a file it was never pointed at, or exhausts memory on a nested
    # definition. Refuse rather than expand.
    if b"<!ENTITY" in raw:
        raise SystemExit(f"{path}: entity definitions in a workspace, refusing")
    root = ET.fromstring(raw)

    creator = root.find(NS + "Creator")
    engine = root.find(NS + "Engine")
    if engine is None:
        raise SystemExit(f"{path}: no Engine section, not a workspace")

    functions = {}
    for function in engine.findall(NS + "Function"):
        record = function_record(function)
        functions[record["id"]] = record
    for fid, record in functions.items():
        record["terminates"] = terminates(fid, functions)
        record["reaches"] = reaches(fid, functions)

    fixtures = []
    for fixture in engine.findall(NS + "Fixture"):
        fixtures.append({
            "id": int(text_of(fixture, "ID", -1)),
            "manufacturer": text_of(fixture, "Manufacturer", ""),
            "model": text_of(fixture, "Model", ""),
            "mode": text_of(fixture, "Mode", ""),
            "name": text_of(fixture, "Name", ""),
            "universe": int(text_of(fixture, "Universe", 0)),
            "address": int(text_of(fixture, "Address", 0)),
            "channels": int(text_of(fixture, "Channels", 0)),
        })

    widgets = []
    console = root.find(NS + "VirtualConsole")
    if console is not None:
        for child in console:
            collect_widgets(child, None, widgets)

    return {
        "schema": 1,
        "workspace": {
            "path": path,
            "sha256": hashlib.sha256(raw).hexdigest(),
            "creator": text_of(creator, "Version", "") if creator is not None else "",
            "author": text_of(creator, "Author", "") if creator is not None else "",
        },
        "fixtures": fixtures,
        "functions": [functions[k] for k in sorted(functions)],
        "widgets": widgets,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("workspace")
    parser.add_argument("-o", "--out", help="write the manifest here instead of stdout")
    parser.add_argument("--reaches", type=int, metavar="FID",
                        help="print which functions can start this one, and stop")
    args = parser.parse_args()

    manifest = build(args.workspace)

    if args.reaches is not None:
        target = args.reaches
        by_id = {f["id"]: f for f in manifest["functions"]}
        print(f"function {target}: {by_id.get(target, {}).get('name', '?')}")
        for function in manifest["functions"]:
            if target in function["reaches"]:
                ends = "terminates" if function["terminates"] else "RUNS UNTIL STOPPED"
                print(f"  reached from {function['id']:4d} {function['type']:<10} "
                      f"{function['name']!r}  [{ends}]")
        return 0

    text = json.dumps(manifest, indent=1, ensure_ascii=False)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write(text + "\n")
        print(f"{args.out}: {len(manifest['functions'])} functions, "
              f"{len(manifest['fixtures'])} fixtures, {len(manifest['widgets'])} widgets")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
