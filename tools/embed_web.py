#!/usr/bin/env python3
"""Compresses web/index.html into src/web_index.h (gzip byte array).

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

src = os.path.join(ROOT, "web", "index.html")
dst = os.path.join(ROOT, "src", "web_index.h")

html = open(src, "rb").read()
gz = gzip.compress(html, compresslevel=9, mtime=0)

lines = ["// Generated from web/index.html by tools/embed_web.py - do not edit",
         "#pragma once", "#include <Arduino.h>", "",
         "static const size_t WEB_INDEX_GZ_LEN = %d;" % len(gz),
         "static const uint8_t WEB_INDEX_GZ[] PROGMEM = {"]
for i in range(0, len(gz), 20):
    lines.append("  " + ",".join("0x%02x" % b for b in gz[i:i + 20]) + ",")
lines.append("};")
content = "\n".join(lines) + "\n"

old = open(dst).read() if os.path.exists(dst) else ""
if old != content:
    open(dst, "w").write(content)
    print("embed_web: %s -> %s (%d -> %d bytes)" % (src, dst, len(html), len(gz)))
