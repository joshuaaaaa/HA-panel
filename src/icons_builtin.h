#pragma once
// Built-in 8x8 icons. Each character is one pixel, see palette in icons.cpp.
// Icon names of weather icons match Home Assistant weather conditions, so an
// icon template like {{ states('weather.home') }} picks the right icon.

struct BuiltinIcon {
  const char* name;
  uint16_t delay;   // ms per frame
  uint8_t frames;
  const char* art;  // 64 chars per frame
};

// clang-format off
static const BuiltinIcon BUILTIN_ICONS[] = {
{"sunny", 0, 1,
  "Y..O..Y."
  ".Y.O.Y.."
  "..YYY..."
  "OOYYYOO."
  "..YYY..."
  ".Y.O.Y.."
  "Y..O..Y."
  "........"},
{"clear-night", 0, 1,
  "..yyy..."
  ".yy...W."
  "yy......"
  "yy......"
  "yy......"
  "yy....y."
  ".yy..yy."
  "..yyyy.."},
{"cloudy", 0, 1,
  "........"
  "...WW..."
  "..WWWW.."
  ".WWWWWW."
  "WWWWWWWW"
  "WWWWWWWW"
  ".WWWWWW."
  "........"},
{"partlycloudy", 0, 1,
  ".Y..Y..."
  "..YYY..."
  "YYYYWW.."
  "..YWWWW."
  ".WWWWWWW"
  "WWWWWWWW"
  ".WWWWWW."
  "........"},
{"rainy", 400, 2,
  "..WWW..."
  ".WWWWWW."
  "WWWWWWWW"
  ".WWWWWW."
  "........"
  ".B..B..B"
  "B..B..B."
  "........"
  // next frame
  "..WWW..."
  ".WWWWWW."
  "WWWWWWWW"
  ".WWWWWW."
  "........"
  "B..B..B."
  "..B..B.."
  ".B..B..B"},
{"pouring", 250, 2,
  "..www..."
  ".wwwwww."
  "wwwwwwww"
  ".wwwwww."
  "B.B.B.B."
  ".B.B.B.B"
  "B.B.B.B."
  ".B.B.B.B"
  // next frame
  "..www..."
  ".wwwwww."
  "wwwwwwww"
  ".wwwwww."
  ".B.B.B.B"
  "B.B.B.B."
  ".B.B.B.B"
  "B.B.B.B."},
{"snowy", 500, 2,
  "..WWW..."
  ".WWWWWW."
  "WWWWWWWW"
  ".WWWWWW."
  "........"
  ".l..l..l"
  "..l..l.."
  "l..l..l."
  // next frame
  "..WWW..."
  ".WWWWWW."
  "WWWWWWWW"
  ".WWWWWW."
  "........"
  "l..l..l."
  ".l..l..l"
  "..l..l.."},
{"lightning", 300, 2,
  "..www..."
  ".wwwwww."
  "wwwwwwww"
  ".wwYwww."
  "...YY..."
  "..YY...."
  "...Y...."
  "..Y....."
  // next frame
  "..www..."
  ".wwwwww."
  "wwwwwwww"
  ".wwwwww."
  "........"
  "........"
  "........"
  "........"},
{"fog", 0, 1,
  "........"
  "wwwwww.."
  "........"
  ".wwwwwww"
  "........"
  "wwwwww.."
  "........"
  ".wwwwwww"},
{"windy", 0, 1,
  "......W."
  "WWWWW..W"
  "......W."
  "........"
  "WWWWWWW."
  ".......W"
  "WWWWWW.."
  "........"},
{"thermometer", 0, 1,
  "...W...."
  "..W.W..."
  "..WRW..."
  "..WRW..."
  "..WRW..."
  ".WRRRW.."
  ".WRRRW.."
  "..WWW..."},
{"humidity", 0, 1,
  "...L...."
  "...L...."
  "..LLL..."
  "..LLL..."
  ".LLLLL.."
  ".LLLWL.."
  ".LLLLL.."
  "..LLL..."},
{"bulb", 0, 1,
  "..YYY..."
  ".YYYYY.."
  ".YYYYY.."
  ".YYYYY.."
  "..YYY..."
  "..www..."
  "..www..."
  "...w...."},
{"bulb_off", 0, 1,
  "..www..."
  ".w...w.."
  ".w...w.."
  ".w...w.."
  "..w.w..."
  "..ddd..."
  "..ddd..."
  "...d...."},
{"power", 0, 1,
  "...G...."
  ".G.G.G.."
  "G..G..G."
  "G.....G."
  "G.....G."
  ".G...G.."
  "..GGG..."
  "........"},
{"bolt", 0, 1,
  "....YY.."
  "...YY..."
  "..YY...."
  ".YYYYY.."
  "...YY..."
  "..YY...."
  ".YY....."
  ".Y......"},
{"solar", 0, 1,
  ".Y..Y..."
  "..YYY..."
  ".YYYYY.."
  "........"
  "BBwBBwBB"
  "BBwBBwBB"
  "wwwwwwww"
  "BBwBBwBB"},
{"battery", 0, 1,
  "........"
  "wwwwwww."
  "wGGGGGw."
  "wGGGGGww"
  "wGGGGGww"
  "wGGGGGw."
  "wwwwwww."
  "........"},
{"battery_low", 700, 2,
  "........"
  "wwwwwww."
  "wR....w."
  "wR....ww"
  "wR....ww"
  "wR....w."
  "wwwwwww."
  "........"
  // next frame
  "........"
  "wwwwwww."
  "w.....w."
  "w.....ww"
  "w.....ww"
  "w.....w."
  "wwwwwww."
  "........"},
{"home", 0, 1,
  "...R...."
  "..RRR..."
  ".RRRRR.."
  "RRRRRRR."
  ".WWWWW.."
  ".WWNNW.."
  ".WWNNW.."
  ".WWNNW.."},
{"ha", 0, 1,
  "...L...."
  "..LLL..."
  ".LLLLL.."
  "LLLWLLL."
  "LLWLWLL."
  "LLLWLLL."
  "LLLWLLL."
  "LLLLLLL."},
{"door", 0, 1,
  ".NNNNNN."
  ".NNNNNN."
  ".NNNNNN."
  ".NNNNYN."
  ".NNNNNN."
  ".NNNNNN."
  ".NNNNNN."
  ".NNNNNN."},
{"window", 0, 1,
  "wwwwwww."
  "wllwllw."
  "wllwllw."
  "wwwwwww."
  "wllwllw."
  "wllwllw."
  "wwwwwww."
  "........"},
{"lock", 0, 1,
  "..www..."
  ".w...w.."
  ".w...w.."
  "YYYYYYY."
  "YYYdYYY."
  "YYYdYYY."
  "YYYYYYY."
  "........"},
{"unlock", 0, 1,
  "..www..."
  ".w...w.."
  ".w......"
  "GGGGGGG."
  "GGGdGGG."
  "GGGdGGG."
  "GGGGGGG."
  "........"},
{"bell", 300, 2,
  "...Y...."
  "..YYY..."
  ".YYYYY.."
  ".YYYYY.."
  ".YYYYY.."
  "YYYYYYY."
  "........"
  "...Y...."
  // next frame
  "....Y..."
  "...YYY.."
  "..YYYYY."
  "..YYYYY."
  ".YYYYY.."
  "YYYYYY.."
  "........"
  "..Y....."},
{"mail", 0, 1,
  "........"
  "WWWWWWWW"
  "WW....WW"
  "W.W..W.W"
  "W..WW..W"
  "W......W"
  "WWWWWWWW"
  "........"},
{"wifi", 0, 1,
  "........"
  "..CCCC.."
  ".C....C."
  "C..CC..C"
  "..C..C.."
  "........"
  "...CC..."
  "...CC..."},
{"music", 0, 1,
  "..MMMMMM"
  "..M....M"
  "..M....M"
  "..M....M"
  "MMM..MMM"
  "MMM..MMM"
  "........"
  "........"},
{"heart", 600, 2,
  "........"
  ".RR.RR.."
  "RRRRRRR."
  "RRRRRRR."
  ".RRRRR.."
  "..RRR..."
  "...R...."
  "........"
  // next frame
  "........"
  "........"
  "..R.R..."
  ".RRRRR.."
  "..RRR..."
  "...R...."
  "........"
  "........"},
{"star", 0, 1,
  "...Y...."
  "...Y...."
  "YYYYYYY."
  ".YYYYY.."
  "..YYY..."
  ".YY.YY.."
  ".Y...Y.."
  "........"},
{"check", 0, 1,
  "........"
  ".......G"
  "......G."
  ".....G.."
  "G...G..."
  ".G.G...."
  "..G....."
  "........"},
{"error", 0, 1,
  "R......R"
  ".R....R."
  "..R..R.."
  "...RR..."
  "...RR..."
  "..R..R.."
  ".R....R."
  "R......R"},
{"warning", 0, 1,
  "...YY..."
  "..YddY.."
  "..YddY.."
  ".YYddYY."
  ".YYddYY."
  "YYYYYYYY"
  "YYYddYYY"
  "YYYYYYYY"},
{"info", 0, 1,
  "..BBBB.."
  ".BBWWBB."
  "BBBBBBBB"
  "BBBWWBBB"
  "BBBWWBBB"
  "BBBWWBBB"
  ".BBWWBB."
  "..BBBB.."},
{"fire", 200, 2,
  "...R...."
  "...RR..."
  "..RRR.R."
  ".RRORRR."
  ".RROORR."
  "RROYYORR"
  "RROYYOR."
  ".ROOOOR."
  // next frame
  "....R..."
  "...RR..."
  ".R.RRR.."
  ".RRROR.."
  ".RRROOR."
  "RROOYORR"
  ".ROYYORR"
  ".ROOOOR."},
{"gas", 0, 1,
  "...B...."
  "..BB...."
  "..BBB..."
  ".BBLBB.."
  ".BLLLB.."
  ".BLLLB.."
  "..BBB..."
  "........"},
{"leaf", 0, 1,
  "......gG"
  "....GGGG"
  "...GGGG."
  "..GGGgG."
  ".GGgGG.."
  "..gGG..."
  ".g......"
  "g......."},
{"tree", 0, 1,
  "...G...."
  "..GGG..."
  ".GGGGG.."
  "..GGG..."
  ".GGGGG.."
  "GGGGGGG."
  "...N...."
  "...N...."},
{"car", 0, 1,
  "........"
  "..RRRR.."
  ".RlRRlR."
  "RRRRRRRR"
  "RRRRRRRR"
  ".dd..dd."
  ".dd..dd."
  "........"},
{"washer", 250, 2,
  "wwwwwwww"
  "wdwwwwRw"
  "wwwwwwww"
  "ww.LL.ww"
  "w.LLLL.w"
  "w.LLWL.w"
  "ww.LL.ww"
  "wwwwwwww"
  // next frame
  "wwwwwwww"
  "wdwwwwRw"
  "wwwwwwww"
  "ww.LL.ww"
  "w.WLLL.w"
  "w.LLLL.w"
  "ww.LL.ww"
  "wwwwwwww"},
{"trash", 0, 1,
  "..www..."
  "wwwwwww."
  ".w.w.w.."
  ".w.w.w.."
  ".w.w.w.."
  ".w.w.w.."
  ".wwwww.."
  "........"},
{"fan", 200, 2,
  "..CC...."
  "..CCC..."
  "C..C..C."
  "CCCWCCC."
  "C..C..C."
  "...CCC.."
  "....CC.."
  "........"
  // next frame
  "....CC.."
  "C..CC..."
  "CC.C...."
  ".CCWCC.."
  "...C.CC."
  "..CC..C."
  ".CC....."
  "........"},
{"clock", 0, 1,
  "..WWWW.."
  ".W....W."
  "W..W...W"
  "W..W...W"
  "W..WWW.W"
  "W......W"
  ".W....W."
  "..WWWW.."},
{"calendar", 0, 1,
  "RRRRRRRR"
  "RWRRRRWR"
  "WWWWWWWW"
  "WdWdWdWW"
  "WWWWWWWW"
  "WdWdWdWW"
  "WWWWWWWW"
  "........"},
{"person", 0, 1,
  "...SS..."
  "..SSSS.."
  "..SSSS.."
  "...SS..."
  ".BBBBBB."
  ".BBBBBB."
  ".BBBBBB."
  ".BB..BB."},
{"shield", 0, 1,
  ".GGGGGG."
  ".GGGGGG."
  ".GGWGGG."
  ".GWWWGG."
  "..GWGG.."
  "..GGGG.."
  "...GG..."
  "........"},
{"tv", 0, 1,
  ".w...w.."
  "..w.w..."
  "wwwwwww."
  "wLLLLLw."
  "wLLLLLw."
  "wLLLLLw."
  "wwwwwww."
  ".w...w.."},
{"phone", 0, 1,
  ".wwwww.."
  ".wdddw.."
  ".wdddw.."
  ".wdddw.."
  ".wdddw.."
  ".wdddw.."
  ".wwWww.."
  ".wwwww.."},
{"coffee", 500, 2,
  "..w.w..."
  ".w.w...."
  "........"
  "WWWWWW.."
  "WNNNNWWW"
  "WNNNNW.W"
  "WNNNNWWW"
  ".WWWW..."
  // next frame
  ".w.w...."
  "..w.w..."
  "........"
  "WWWWWW.."
  "WNNNNWWW"
  "WNNNNW.W"
  "WNNNNWWW"
  ".WWWW..."},
{"garage", 0, 1,
  "...w...."
  "..www..."
  ".wwwww.."
  "wwwwwww."
  "wdddddw."
  "wwwwwww."
  "wdddddw."
  "wwwwwww."},
{"plug", 0, 1,
  "..w.w..."
  "..w.w..."
  ".wwwww.."
  ".wwwww.."
  ".wwwww.."
  "..www..."
  "...w...."
  "...w...."},
{"water", 0, 1,
  "...B...."
  "...B...."
  "..BBB..."
  "..BBB..."
  ".BBBBB.."
  ".BBBLB.."
  ".BBBBB.."
  "..BBB..."},
{"arrow_up", 0, 1,
  "...G...."
  "..GGG..."
  ".G.G.G.."
  "G..G..G."
  "...G...."
  "...G...."
  "...G...."
  "........"},
{"arrow_down", 0, 1,
  "...R...."
  "...R...."
  "...R...."
  "R..R..R."
  ".R.R.R.."
  "..RRR..."
  "...R...."
  "........"},
{"hourglass", 0, 1,
  "wwwwwww."
  ".YYYYY.."
  "..YYY..."
  "...Y...."
  "...Y...."
  "..Y.Y..."
  ".YYYYY.."
  "wwwwwww."},
{"co2", 0, 1,
  "........"
  ".ww..w.."
  "w...w.w."
  "w...w.w."
  "w...w.w."
  ".ww..w.."
  "......GG"
  ".....GG."},
{"sun_rise", 0, 1,
  "...Y...."
  ".Y...Y.."
  "...O...."
  "..OOO..."
  ".OOOOO.."
  "wwwwwwww"
  "........"
  "..lllll."},
{"moon", 0, 1,
  "..yyy..."
  ".yy....."
  "yy......"
  "yy......"
  "yy......"
  "yy......"
  ".yy....."
  "..yyy..."},
{"alarm", 400, 2,
  "........"
  "..RRR..."
  ".RRRRR.."
  ".RRRRR.."
  ".RRRRR.."
  "wwwwwww."
  "wwwwwww."
  "........"
  // next frame
  "R.....R."
  ".R.d.R.."
  "..ddd..."
  ".ddddd.."
  ".ddddd.."
  "wwwwwww."
  "wwwwwww."
  "........"},
{"pv", 0, 1,
  "Y......."
  ".Y.Y...."
  "..YY...."
  ".YYY.Y.."
  "..BBBBBB"
  ".BwBwBwB"
  ".BBBBBBB"
  "BwBwBwB."},
{"grid", 0, 1,
  "...w...."
  "..www..."
  "wwwwwww."
  "..w.w..."
  ".wwwww.."
  ".w...w.."
  "w.....w."
  "w.....w."},
{"money", 0, 1,
  "...G...."
  "..GGGG.."
  ".GG....."
  "..GGG..."
  "....GG.."
  ".GGGG..."
  "...G...."
  "........"},
{"speaker", 0, 1,
  "...w...."
  "..ww..w."
  "wwww.w.."
  "wwww.w.w"
  "wwww.w.w"
  "wwww.w.."
  "..ww..w."
  "...w...."},
};
// clang-format on

static const int BUILTIN_ICON_COUNT = sizeof(BUILTIN_ICONS) / sizeof(BUILTIN_ICONS[0]);

// aliases (HA weather conditions & friendly names)
struct IconAlias { const char* alias; const char* name; };
static const IconAlias ICON_ALIASES[] = {
  {"sun", "sunny"}, {"clear", "sunny"}, {"night", "clear-night"}, {"cloud", "cloudy"},
  {"rain", "rainy"}, {"snow", "snowy"}, {"snowy-rainy", "snowy"}, {"hail", "snowy"},
  {"lightning-rainy", "lightning"}, {"storm", "lightning"}, {"exceptional", "warning"},
  {"temp", "thermometer"}, {"temperature", "thermometer"}, {"hum", "humidity"}, {"light", "bulb"},
  {"energy", "bolt"}, {"electricity", "bolt"}, {"x", "error"}, {"ok", "check"}, {"flame", "fire"},
  {"lamp", "bulb"}, {"notification", "bell"}, {"up", "arrow_up"}, {"down", "arrow_down"},
  {"timer", "hourglass"}, {"sunrise", "sun_rise"}, {"sunset", "sun_rise"},
};
