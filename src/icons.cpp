#include "icons.h"
#include <LittleFS.h>
#include "config.h"
#include "icons_builtin.h"
#include <vector>

namespace Icons {

static const char* DIR = "/icons";

static uint32_t paletteColor(char ch) {
  switch (ch) {
    case 'W': return 0xFFFFFF; case 'w': return 0x909090; case 'd': return 0x303030;
    case 'R': return 0xFF0000; case 'r': return 0x800000;
    case 'O': return 0xFF7000; case 'o': return 0x803800;
    case 'Y': return 0xFFD000; case 'y': return 0xFFE890;
    case 'G': return 0x00E000; case 'g': return 0x007000;
    case 'C': return 0x00E0E0; case 'c': return 0x007070;
    case 'B': return 0x0040FF; case 'b': return 0x000080;
    case 'L': return 0x40A0FF; case 'l': return 0xA0D8FF;
    case 'P': return 0xFF40A0; case 'M': return 0xFF00FF; case 'V': return 0x8000FF;
    case 'N': return 0x8B4513; case 'S': return 0xFFC090;
    default: return 0;
  }
}

// ---------------------------------------------------------------- custom icon cache
struct CustomIcon {
  String name;
  uint8_t frames = 0;
  uint16_t delay = 0;
  RGB* px = nullptr;  // frames * 64
  uint32_t lastUse = 0;
  bool missing = false;
};
static const int CACHE = 8;
static CustomIcon cache[CACHE];
static std::vector<String> customNames;  // names of icons stored in LittleFS

static bool isCustom(const String& n) {
  for (auto& c : customNames) if (c == n) return true;
  return false;
}

String sanitizeName(const String& n) {
  String o;
  for (char ch : n) {
    if (isalnum((unsigned char)ch) || ch == '_' || ch == '-') o += (char)tolower((unsigned char)ch);
    if (o.length() >= 24) break;
  }
  return o;
}

static String pathFor(const String& n) { return String(DIR) + "/" + n + ".ico"; }

static const BuiltinIcon* findBuiltin(const String& name) {
  for (auto& a : ICON_ALIASES)
    if (name.equalsIgnoreCase(a.alias)) return findBuiltin(a.name);
  for (int i = 0; i < BUILTIN_ICON_COUNT; i++)
    if (name.equalsIgnoreCase(BUILTIN_ICONS[i].name)) return &BUILTIN_ICONS[i];
  return nullptr;
}

static CustomIcon* loadCustom(const String& name) {
  if (!isCustom(name)) return nullptr;
  uint32_t now = millis();
  for (auto& e : cache)
    if (e.name == name) { e.lastUse = now; return e.missing ? nullptr : &e; }
  // evict least recently used
  CustomIcon* slot = &cache[0];
  for (auto& e : cache) if (e.lastUse < slot->lastUse) slot = &e;
  free(slot->px);
  *slot = CustomIcon();
  slot->name = name;
  slot->lastUse = now;
  File f = LittleFS.open(pathFor(name), "r");
  if (!f) { slot->missing = true; return nullptr; }
  uint8_t hdr[4];
  if (f.read(hdr, 4) != 4 || hdr[0] != 'I' || hdr[1] == 0 || hdr[1] > ICON_MAX_FRAMES) {
    f.close();
    slot->missing = true;
    return nullptr;
  }
  slot->frames = hdr[1];
  slot->delay = hdr[2] | (hdr[3] << 8);
  size_t n = slot->frames * ICON_PIXELS;
  slot->px = (RGB*)malloc(n * sizeof(RGB));
  if (!slot->px || f.read((uint8_t*)slot->px, n * 3) != n * 3) {
    free(slot->px);
    slot->px = nullptr;
    slot->missing = true;
    f.close();
    return nullptr;
  }
  f.close();
  return slot;
}

static void invalidate(const String& name) {
  for (auto& e : cache)
    if (e.name == name) { free(e.px); e = CustomIcon(); }
}

void begin() {
  LittleFS.mkdir(DIR);
  customNames.clear();
  File dir = LittleFS.open(DIR);
  if (!dir) return;
  File f;
  while ((f = dir.openNextFile())) {
    String n = f.name();
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    if (n.endsWith(".ico")) customNames.push_back(n.substring(0, n.length() - 4));
    f.close();
  }
}

bool draw(Canvas& c, int x, int y, const String& nameIn, uint32_t ms) {
  if (nameIn.isEmpty()) return false;
  String name = nameIn;
  name.trim();
  if (name.startsWith("mdi:")) name = name.substring(4);  // allow HA style names
  String cname = sanitizeName(name);
  // custom icons override builtin ones with the same name
  CustomIcon* ci = loadCustom(cname);
  if (ci) {
    uint8_t fr = (ci->frames > 1 && ci->delay) ? (ms / ci->delay) % ci->frames : 0;
    const RGB* p = ci->px + fr * ICON_PIXELS;
    for (int i = 0; i < ICON_PIXELS; i++) c.set(x + (i & 7), y + (i >> 3), p[i]);
    return true;
  }
  const BuiltinIcon* b = findBuiltin(name);
  if (!b) return false;
  uint8_t fr = (b->frames > 1 && b->delay) ? (ms / b->delay) % b->frames : 0;
  const char* art = b->art + fr * ICON_PIXELS;
  for (int i = 0; i < ICON_PIXELS; i++) {
    uint32_t col = paletteColor(art[i]);
    if (col) c.set(x + (i & 7), y + (i >> 3), RGB(col));
  }
  return true;
}

bool exists(const String& name) {
  return findBuiltin(name) || isCustom(sanitizeName(name));
}

void list(JsonArray a) {
  for (int i = 0; i < BUILTIN_ICON_COUNT; i++) {
    JsonObject o = a.add<JsonObject>();
    o["name"] = BUILTIN_ICONS[i].name;
    o["builtin"] = true;
  }
  for (auto& n : customNames) {
    JsonObject o = a.add<JsonObject>();
    o["name"] = n;
    o["builtin"] = false;
  }
}

static void hexFrame(const RGB* px, String& out) {
  static const char* H = "0123456789ABCDEF";
  out.reserve(ICON_PIXELS * 6);
  for (int i = 0; i < ICON_PIXELS; i++) {
    uint8_t v[3] = {px[i].r, px[i].g, px[i].b};
    for (uint8_t b : v) { out += H[b >> 4]; out += H[b & 15]; }
  }
}

bool toJson(const String& nameIn, JsonObject o) {
  String name = sanitizeName(nameIn);
  CustomIcon* ci = loadCustom(name);
  if (ci) {
    o["name"] = name;
    o["builtin"] = false;
    o["delay"] = ci->delay;
    JsonArray fr = o["frames"].to<JsonArray>();
    for (int f = 0; f < ci->frames; f++) {
      String s;
      hexFrame(ci->px + f * ICON_PIXELS, s);
      fr.add(s);
    }
    return true;
  }
  const BuiltinIcon* b = findBuiltin(nameIn);
  if (!b) return false;
  o["name"] = b->name;
  o["builtin"] = true;
  o["delay"] = b->delay;
  JsonArray fr = o["frames"].to<JsonArray>();
  RGB px[ICON_PIXELS];
  for (int f = 0; f < b->frames; f++) {
    for (int i = 0; i < ICON_PIXELS; i++) px[i] = RGB(paletteColor(b->art[f * ICON_PIXELS + i]));
    String s;
    hexFrame(px, s);
    fr.add(s);
  }
  return true;
}

bool save(const String& nameIn, uint16_t delayMs, JsonArrayConst frames, String& err) {
  String name = sanitizeName(nameIn);
  if (name.isEmpty()) { err = "invalid name"; return false; }
  size_t n = frames.size();
  if (n == 0 || n > ICON_MAX_FRAMES) { err = "1-8 frames required"; return false; }
  File f = LittleFS.open(pathFor(name), "w");
  if (!f) { err = "fs error"; return false; }
  uint8_t hdr[4] = {'I', (uint8_t)n, (uint8_t)(delayMs & 0xFF), (uint8_t)(delayMs >> 8)};
  f.write(hdr, 4);
  for (JsonVariantConst fr : frames) {
    const char* s = fr.as<const char*>();
    uint8_t buf[ICON_PIXELS * 3];
    memset(buf, 0, sizeof(buf));
    if (s) {
      size_t len = strlen(s);
      for (size_t i = 0; i < sizeof(buf) && i * 2 + 1 < len; i++) {
        char h[3] = {s[i * 2], s[i * 2 + 1], 0};
        buf[i] = (uint8_t)strtoul(h, nullptr, 16);
      }
    }
    f.write(buf, sizeof(buf));
  }
  f.close();
  Lock l;
  invalidate(name);
  if (!isCustom(name)) customNames.push_back(name);
  return true;
}

bool remove(const String& nameIn) {
  String name = sanitizeName(nameIn);
  Lock l;
  invalidate(name);
  for (size_t i = 0; i < customNames.size(); i++)
    if (customNames[i] == name) { customNames.erase(customNames.begin() + i); break; }
  return LittleFS.remove(pathFor(name));
}

}  // namespace Icons
