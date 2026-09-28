#include "png.h"

static uint32_t crcTable[256];
static bool crcReady = false;

static void initCrc() {
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crcTable[n] = c;
  }
  crcReady = true;
}

static uint32_t crc(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
  for (size_t i = 0; i < n; i++) c = crcTable[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c;
}

static void put32(std::vector<uint8_t>& o, uint32_t v) {
  o.push_back(v >> 24);
  o.push_back(v >> 16);
  o.push_back(v >> 8);
  o.push_back(v);
}

static void chunk(std::vector<uint8_t>& o, const char* type, const std::vector<uint8_t>& data) {
  put32(o, data.size());
  size_t start = o.size();
  o.insert(o.end(), type, type + 4);
  o.insert(o.end(), data.begin(), data.end());
  put32(o, crc(&o[start], o.size() - start) ^ 0xFFFFFFFFu);
}

void encodePng(const uint8_t* rgb, int w, int h, std::vector<uint8_t>& out) {
  if (!crcReady) initCrc();
  out.clear();
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  out.insert(out.end(), sig, sig + 8);

  std::vector<uint8_t> ihdr;
  put32(ihdr, w);
  put32(ihdr, h);
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(2);  // color type RGB
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  chunk(out, "IHDR", ihdr);

  // raw scanlines with filter byte 0
  std::vector<uint8_t> raw;
  raw.reserve(h * (1 + w * 3));
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), rgb + y * w * 3, rgb + (y + 1) * w * 3);
  }
  // zlib stream of stored deflate blocks
  std::vector<uint8_t> z;
  z.push_back(0x78);
  z.push_back(0x01);
  size_t pos = 0;
  do {
    size_t n = min<size_t>(raw.size() - pos, 65535);
    bool last = pos + n >= raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(n & 0xFF);
    z.push_back(n >> 8);
    z.push_back(~n & 0xFF);
    z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  } while (pos < raw.size());
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  put32(z, (b << 16) | a);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", std::vector<uint8_t>());
}
