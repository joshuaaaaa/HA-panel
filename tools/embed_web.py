#!/usr/bin/env python3
"""Compresses web/index.html and ha/hapanel-card.js into src/web_index.h / src/web_card.h.

Runs automatically as a PlatformIO pre-build script, or manually:
    python3 tools/embed_web.py
"""
import gzip
import os

try:
    Import("env")  # noqa: F821  (PlatformIO / SCons)
    ROOT = env["PROJECT_DIR"]  # noqa: F821
except Exception:
    ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

def embed(src, dst, name):
    data = open(src, "rb").read()
    gz = gzip.compress(data, compresslevel=9, mtime=0)
    lines = ["// Generated from %s by tools/embed_web.py - do not edit" % os.path.relpath(src, ROOT),
             "#pragma once", "#include <Arduino.h>", "",
             "static const size_t %s_LEN = %d;" % (name, len(gz)),
             "static const uint8_t %s[] PROGMEM = {" % name]
    for i in range(0, len(gz), 20):
        lines.append("  " + ",".join("0x%02x" % b for b in gz[i:i + 20]) + ",")
    lines.append("};")
    content = "\n".join(lines) + "\n"
    old = open(dst).read() if os.path.exists(dst) else ""
    if old != content:
        open(dst, "w").write(content)
        print("embed_web: %s -> %s (%d -> %d bytes)" % (src, dst, len(data), len(gz)))


embed(os.path.join(ROOT, "web", "index.html"), os.path.join(ROOT, "src", "web_index.h"), "WEB_INDEX_GZ")
embed(os.path.join(ROOT, "ha", "hapanel-card.js"), os.path.join(ROOT, "src", "web_card.h"), "WEB_CARD_GZ")
