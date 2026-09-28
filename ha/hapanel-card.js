/*
 * HA-Panel card for Home Assistant (Lovelace)
 * Control and configure a HA-Panel / AWTRIX-compatible LED matrix from a dashboard.
 *
 * Installation:
 *   1. copy this file to /config/www/hapanel-card.js
 *      (or use http://<panel-ip>/hapanel-card.js when HA runs over plain http)
 *   2. Settings -> Dashboards -> ⋮ -> Resources -> Add: /local/hapanel-card.js (JavaScript module)
 *   3. add card: type: custom:hapanel-card, entity: light.<panel>_displej
 */
const CARD_VERSION = "1.1.0";

// entity names published by the firmware (MQTT discovery); matched against friendly_name
const NAMES = {
  display: ["light", "Displej"],
  mood: ["light", "Nálada"],
  ind1: ["light", "Indikátor 1"],
  ind2: ["light", "Indikátor 2"],
  ind3: ["light", "Indikátor 3"],
  page: ["select", "Stránka"],
  transition: ["select", "Přechod"],
  autorotate: ["switch", "Automatické přepínání"],
  autobright: ["switch", "Automatický jas"],
  night: ["switch", "Noční režim"],
  uppercase: ["switch", "Velká písmena"],
  weekday: ["switch", "Pruh dní v týdnu"],
  screen: ["switch", "Obraz do Home Assistantu"],
  brightness: ["number", "Jas"],
  scroll: ["number", "Rychlost posunu textu"],
  apptime: ["number", "Doba zobrazení aplikace"],
  transms: ["number", "Délka přechodu"],
  next: ["button", "Další stránka"],
  prev: ["button", "Předchozí stránka"],
  select: ["button", "Akce stránky"],
  dismiss: ["button", "Zavřít notifikaci"],
  test: ["button", "Testovací obrazec"],
  restart: ["button", "Restart"],
  notify: ["notify", "Notifikace"],
  message: ["text", "Zpráva"],
  app: ["sensor", "Aplikace"],
  image: ["image", "Obrazovka"],
};

const slug = (s) =>
  s.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_|_$/g, "");

const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

const hexToRgb = (h) => [1, 3, 5].map((i) => parseInt(h.substr(i, 2), 16));
const rgbToHex = (c) => "#" + (c || [255, 255, 255]).map((v) => v.toString(16).padStart(2, "0")).join("");

class HAPanelCard extends HTMLElement {
  static getConfigElement() {
    return document.createElement("hapanel-card-editor");
  }

  static getStubConfig(hass) {
    const light = Object.keys(hass.states).find(
      (e) => e.startsWith("light.") && /displej$/i.test(hass.states[e].attributes.friendly_name || "")
    );
    return { entity: light || "", title: "LED panel" };
  }

  setConfig(config) {
    if (!config || !config.entity) throw new Error('Zvol entitu displeje (light.…_displej)');
    this._config = {
      title: "",
      show_preview: true,
      show_controls: true,
      show_notify: true,
      show_mood: true,
      show_indicators: true,
      show_settings: true,
      ...config,
    };
    this._built = false;
    if (this._hass) this._update();
  }

  set hass(hass) {
    this._hass = hass;
    this._update();
  }

  getCardSize() {
    return 8;
  }

  // ------------------------------------------------------------------ entity lookup
  _resolve() {
    const hass = this._hass;
    const main = this._config.entity;
    const reg = hass.entities || {};
    const deviceId = reg[main] && reg[main].device_id;
    let candidates;
    if (deviceId) candidates = Object.values(reg).filter((e) => e.device_id === deviceId).map((e) => e.entity_id);
    else candidates = Object.keys(hass.states);
    const mainName = (hass.states[main] && hass.states[main].attributes.friendly_name) || "";
    const devName = mainName.replace(/\s*Displej$/i, "");
    const base = main.replace(/^light\./, "").replace(/_displej$/, "");
    const ents = {};
    for (const [key, [domain, name]] of Object.entries(NAMES)) {
      const lname = name.toLowerCase();
      let found = candidates.find((id) => {
        if (!id.startsWith(domain + ".")) return false;
        const fn = ((hass.states[id] && hass.states[id].attributes.friendly_name) || "").toLowerCase();
        return fn === (devName + " " + name).toLowerCase() || (deviceId && fn.endsWith(lname));
      });
      if (!found) {
        const guess = `${domain}.${base}_${slug(name)}`;
        if (hass.states[guess]) found = guess;
      }
      if (found) ents[key] = found;
    }
    ents.display = main;
    this._ents = ents;
  }

  _st(key) {
    const id = this._ents && this._ents[key];
    return id ? this._hass.states[id] : undefined;
  }

  _call(domain, service, key, data = {}) {
    const id = this._ents[key];
    if (!id) return;
    this._hass.callService(domain, service, { entity_id: id, ...data });
  }

  // ------------------------------------------------------------------ render
  _build() {
    const c = this._config;
    this.innerHTML = "";
    const card = document.createElement("ha-card");
    card.innerHTML = `
      <style>
        .hp{padding:12px 16px 16px}
        .hd{display:flex;align-items:center;gap:8px;margin-bottom:10px}
        .hd .t{font-size:1.2em;font-weight:500;flex:1}
        .dot{width:10px;height:10px;border-radius:50%;background:var(--error-color,#db4437)}
        .dot.on{background:var(--success-color,#43a047)}
        .app{color:var(--secondary-text-color);font-size:.9em}
        .pv{background:#050608;border-radius:10px;padding:6px;margin-bottom:10px;position:relative}
        .pv canvas{width:100%;display:block;image-rendering:pixelated}
        .pv .ov{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;gap:8px;color:#aaa;font-size:.9em;text-align:center;padding:8px}
        .row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:8px 0}
        .row>.grow{flex:1;min-width:120px}
        button.b{background:var(--secondary-background-color);color:var(--primary-text-color);border:1px solid var(--divider-color);border-radius:8px;padding:6px 10px;cursor:pointer;display:inline-flex;align-items:center;gap:4px;font:inherit}
        button.b:hover{border-color:var(--primary-color)}
        button.b.p{background:var(--primary-color);color:var(--text-primary-color,#fff);border-color:var(--primary-color)}
        button.b.on{border-color:var(--primary-color);color:var(--primary-color)}
        input[type=range]{width:100%;accent-color:var(--primary-color)}
        input[type=text],input[type=number],select{background:var(--secondary-background-color);color:var(--primary-text-color);border:1px solid var(--divider-color);border-radius:8px;padding:6px 8px;font:inherit;min-width:0}
        input[type=color]{width:40px;height:34px;border:1px solid var(--divider-color);border-radius:8px;padding:2px;background:none}
        details{border-top:1px solid var(--divider-color);padding-top:6px;margin-top:8px}
        summary{cursor:pointer;font-weight:500;padding:4px 0}
        .lbl{font-size:.85em;color:var(--secondary-text-color);min-width:120px}
        .ind{display:flex;gap:10px}
        .ind .i{display:flex;flex-direction:column;align-items:center;gap:4px;font-size:.8em}
        .sw{display:flex;align-items:center;gap:6px;cursor:pointer;font-size:.9em}
        .muted{color:var(--secondary-text-color);font-size:.85em}
        a{color:var(--primary-color)}
      </style>
      <div class="hp">
        <div class="hd"><span class="dot" id="online"></span><span class="t" id="title"></span><span class="app" id="app"></span>
          <button class="b" id="power" title="Zapnout / vypnout"><ha-icon icon="mdi:power"></ha-icon></button></div>
        ${c.show_preview ? `<div class="pv"><canvas id="cv" width="320" height="80"></canvas><div class="ov" id="ov" hidden></div></div>` : ""}
        ${c.show_controls ? `
        <div class="row">
          <button class="b" id="prev" title="Předchozí"><ha-icon icon="mdi:chevron-left"></ha-icon></button>
          <button class="b" id="sel" title="Akce stránky / zavřít notifikaci"><ha-icon icon="mdi:circle-medium"></ha-icon></button>
          <button class="b" id="next" title="Další"><ha-icon icon="mdi:chevron-right"></ha-icon></button>
          <select id="page" class="grow"></select>
        </div>
        <div class="row"><ha-icon icon="mdi:brightness-6"></ha-icon><input type="range" id="bri" min="1" max="255" class="grow"><span id="briv" class="muted"></span></div>
        <div class="row">
          <label class="sw"><input type="checkbox" id="autorotate">Přepínat</label>
          <label class="sw"><input type="checkbox" id="autobright">Auto jas</label>
          <label class="sw"><input type="checkbox" id="night">Noc</label>
          <span class="grow"></span>
          <label class="sw" title="Výchozí barva textu">Text <input type="color" id="tcol"></label>
        </div>` : ""}
        ${c.show_notify ? `
        <details ${c.notify_open ? "open" : ""}><summary>Notifikace</summary>
          <div class="row"><input type="text" id="ntext" class="grow" placeholder="Text zprávy…"></div>
          <div class="row">
            <select id="nicon" class="grow"><option value="">bez ikony</option></select>
            <input type="color" id="ncol" value="#ffffff">
            <input type="number" id="ndur" value="5" min="1" style="width:64px" title="Doba (s)">
          </div>
          <div class="row">
            <label class="sw"><input type="checkbox" id="nrain">Duha</label>
            <label class="sw"><input type="checkbox" id="nhold">Držet</label>
            <label class="sw"><input type="checkbox" id="nwake">Probudit</label>
            <span class="grow"></span>
            <button class="b" id="ndis">Zavřít</button>
            <button class="b p" id="nsend"><ha-icon icon="mdi:send"></ha-icon>Odeslat</button>
          </div>
        </details>` : ""}
        ${c.show_mood ? `
        <details><summary>Nálada (celý panel jednou barvou)</summary>
          <div class="row"><button class="b" id="mood">Vypnuto</button><input type="color" id="mcol" value="#ffb464">
            <input type="range" id="mbri" min="1" max="255" class="grow"></div>
        </details>` : ""}
        ${c.show_indicators ? `
        <details><summary>Indikátory</summary>
          <div class="row ind">${[1, 2, 3].map((i) => `
            <div class="i"><input type="color" id="ic${i}" value="#ff0000"><button class="b" id="ib${i}">${i}</button>
            <select id="ie${i}"><option value="none">stálý</option><option value="blink">bliká</option><option value="fade">pulzuje</option></select></div>`).join("")}
          </div>
        </details>` : ""}
        ${c.show_settings ? `
        <details><summary>Nastavení</summary>
          <div class="row"><span class="lbl">Rychlost posunu</span><input type="range" id="scroll" min="5" max="120" class="grow"><span class="muted" id="scrollv"></span></div>
          <div class="row"><span class="lbl">Doba aplikace</span><input type="range" id="apptime" min="1" max="60" class="grow"><span class="muted" id="apptimev"></span></div>
          <div class="row"><span class="lbl">Přechod</span><select id="transition" class="grow"></select></div>
          <div class="row"><span class="lbl">Délka přechodu</span><input type="range" id="transms" min="50" max="2000" step="50" class="grow"><span class="muted" id="transmsv"></span></div>
          <div class="row">
            <label class="sw"><input type="checkbox" id="uppercase">Velká písmena</label>
            <label class="sw"><input type="checkbox" id="weekday">Dny v týdnu</label>
            <label class="sw"><input type="checkbox" id="screen">Obraz do HA</label>
          </div>
          <div class="row">
            <button class="b" id="test"><ha-icon icon="mdi:grid"></ha-icon>Test</button>
            <button class="b" id="restart"><ha-icon icon="mdi:restart"></ha-icon>Restart</button>
            <span class="grow"></span><a id="web" target="_blank" rel="noopener">Web panelu ↗</a>
          </div>
          <div class="muted" id="ver"></div>
        </details>` : ""}
      </div>`;
    this.appendChild(card);
    this._card = card;
    const $ = (id) => card.querySelector("#" + id);
    this._$ = $;
    const on = (id, ev, fn) => {
      const el = $(id);
      if (el) el.addEventListener(ev, fn);
    };

    on("power", "click", () => this._call("light", "toggle", "display"));
    on("prev", "click", () => this._call("button", "press", "prev"));
    on("next", "click", () => this._call("button", "press", "next"));
    on("sel", "click", () => this._call("button", "press", "select"));
    on("page", "change", (e) => this._call("select", "select_option", "page", { option: e.target.value }));
    on("bri", "input", (e) => ($("briv").textContent = e.target.value));
    on("bri", "change", (e) => this._call("light", "turn_on", "display", { brightness: +e.target.value }));
    on("tcol", "change", (e) => this._call("light", "turn_on", "display", { rgb_color: hexToRgb(e.target.value) }));
    for (const k of ["autorotate", "autobright", "night", "uppercase", "weekday", "screen"])
      on(k, "change", (e) => this._call("switch", e.target.checked ? "turn_on" : "turn_off", k));

    on("nsend", "click", () => this._sendNotify());
    on("ntext", "keydown", (e) => e.key === "Enter" && this._sendNotify());
    on("ndis", "click", () => this._call("button", "press", "dismiss"));

    on("mood", "click", () => {
      const s = this._st("mood");
      if (s && s.state === "on") this._call("light", "turn_off", "mood");
      else this._call("light", "turn_on", "mood", { rgb_color: hexToRgb($("mcol").value), brightness: +$("mbri").value || 150 });
    });
    const moodUpd = () =>
      this._call("light", "turn_on", "mood", { rgb_color: hexToRgb($("mcol").value), brightness: +$("mbri").value || 150 });
    on("mcol", "change", moodUpd);
    on("mbri", "change", moodUpd);

    for (const i of [1, 2, 3]) {
      const key = "ind" + i;
      on("ib" + i, "click", () => {
        const s = this._st(key);
        if (s && s.state === "on") this._call("light", "turn_off", key);
        else this._call("light", "turn_on", key, { rgb_color: hexToRgb($("ic" + i).value), effect: $("ie" + i).value });
      });
      const upd = () =>
        this._call("light", "turn_on", key, { rgb_color: hexToRgb($("ic" + i).value), effect: $("ie" + i).value });
      on("ic" + i, "change", upd);
      on("ie" + i, "change", upd);
    }

    for (const k of ["scroll", "apptime", "transms"]) {
      on(k, "input", (e) => ($(k + "v").textContent = e.target.value));
      on(k, "change", (e) => this._call("number", "set_value", k, { value: +e.target.value }));
    }
    on("transition", "change", (e) => this._call("select", "select_option", "transition", { option: e.target.value }));
    on("test", "click", () => this._call("button", "press", "test"));
    on("restart", "click", () => confirm("Restartovat panel?") && this._call("button", "press", "restart"));
    this._built = true;
  }

  _sendNotify() {
    const $ = this._$;
    const text = $("ntext").value.trim();
    const icon = $("nicon").value;
    if (!text && !icon) return;
    const app = this._st("app");
    const topic = app && app.attributes.topic;
    const msg = {
      text,
      icon,
      color: $("ncol").value,
      duration: +$("ndur").value || 5,
      rainbow: $("nrain").checked,
      hold: $("nhold").checked,
      wakeup: $("nwake").checked,
    };
    if (topic) {
      this._hass.callService("mqtt", "publish", { topic: topic + "/notify", payload: JSON.stringify(msg) });
    } else if (this._ents.notify) {
      this._hass.callService("notify", "send_message", { entity_id: this._ents.notify, message: text });
    }
    $("ntext").value = "";
  }

  _setVal(id, value) {
    const el = this._$(id);
    if (!el || this._card.querySelector(":focus") === el || value === undefined) return;
    if (el.type === "checkbox") el.checked = !!value;
    else if (String(el.value) !== String(value)) el.value = value;
  }

  _fillSelect(id, options, current) {
    const el = this._$(id);
    if (!el) return;
    const key = options.join("\u0001");
    if (el._opts !== key) {
      el.innerHTML = options.map((o) => `<option value="${esc(o)}">${esc(o)}</option>`).join("");
      el._opts = key;
    }
    if (current !== undefined && this._card.querySelector(":focus") !== el) el.value = current;
  }

  _update() {
    if (!this._config || !this._hass) return;
    this._resolve();
    if (!this._built) this._build();
    const $ = this._$;
    const disp = this._st("display");
    const app = this._st("app");
    const attrs = (app && app.attributes) || {};

    $("title").textContent = this._config.title || attrs.hostname || "LED panel";
    const online = disp && disp.state !== "unavailable";
    $("online").classList.toggle("on", !!online);
    $("app").textContent = app ? app.state : "";
    $("power").classList.toggle("on", disp && disp.state === "on");

    if (this._config.show_controls && disp) {
      this._setVal("bri", disp.attributes.brightness || 0);
      if ($("briv")) $("briv").textContent = disp.attributes.brightness || "";
      if (disp.attributes.rgb_color) this._setVal("tcol", rgbToHex(disp.attributes.rgb_color));
      const page = this._st("page");
      if (page) this._fillSelect("page", page.attributes.options || [], page.state);
      for (const k of ["autorotate", "autobright", "night"]) {
        const s = this._st(k);
        if (s) this._setVal(k, s.state === "on");
      }
    }
    if (this._config.show_notify && attrs.icons) {
      const el = $("nicon");
      const key = attrs.icons.join(",");
      if (el && el._opts !== key) {
        const cur = el.value;
        el.innerHTML = '<option value="">bez ikony</option>' + attrs.icons.map((i) => `<option>${esc(i)}</option>`).join("");
        el.value = cur;
        el._opts = key;
      }
    }
    if (this._config.show_mood) {
      const m = this._st("mood");
      if (m && $("mood")) {
        $("mood").textContent = m.state === "on" ? "Zapnuto" : "Vypnuto";
        $("mood").classList.toggle("on", m.state === "on");
        if (m.attributes.rgb_color) this._setVal("mcol", rgbToHex(m.attributes.rgb_color));
        if (m.attributes.brightness) this._setVal("mbri", m.attributes.brightness);
      }
    }
    if (this._config.show_indicators) {
      for (const i of [1, 2, 3]) {
        const s = this._st("ind" + i);
        const b = $("ib" + i);
        if (!s || !b) continue;
        b.classList.toggle("on", s.state === "on");
        b.style.background = s.state === "on" && s.attributes.rgb_color ? rgbToHex(s.attributes.rgb_color) : "";
        if (s.attributes.rgb_color && s.state === "on") this._setVal("ic" + i, rgbToHex(s.attributes.rgb_color));
        if (s.attributes.effect) this._setVal("ie" + i, s.attributes.effect);
      }
    }
    if (this._config.show_settings) {
      for (const k of ["scroll", "apptime", "transms"]) {
        const s = this._st(k);
        if (s && !isNaN(+s.state)) {
          this._setVal(k, +s.state);
          if ($(k + "v")) $(k + "v").textContent = s.state;
        }
      }
      const tr = this._st("transition");
      if (tr) this._fillSelect("transition", tr.attributes.options || [], tr.state);
      for (const k of ["uppercase", "weekday", "screen"]) {
        const s = this._st(k);
        if (s) this._setVal(k, s.state === "on");
      }
      if ($("web") && attrs.ip) $("web").href = "http://" + attrs.ip;
      if ($("ver")) $("ver").textContent = attrs.version ? `Firmware ${attrs.version} · karta ${CARD_VERSION} · MQTT ${attrs.topic || ""}` : "";
    }
    if (this._config.show_preview) this._updatePreview(attrs);
  }

  _updatePreview(attrs) {
    const img = this._st("image");
    const ov = this._$("ov");
    const scr = this._st("screen");
    const W = attrs.width || 32, H = attrs.height || 8;
    if (!img || img.state === "unavailable" || (scr && scr.state !== "on")) {
      ov.hidden = false;
      if (!ov._btn) {
        ov.innerHTML = `<span>Živý náhled je vypnutý.</span>`;
        const b = document.createElement("button");
        b.className = "b";
        b.textContent = "Zapnout obraz do HA";
        b.onclick = () => this._call("switch", "turn_on", "screen");
        ov.appendChild(b);
        ov._btn = true;
      }
      this._drawEmpty(W, H);
      return;
    }
    ov.hidden = true;
    const pic = img.attributes.entity_picture;
    if (!pic || pic === this._lastPic) return;
    this._lastPic = pic;
    const im = new Image();
    im.onload = () => this._drawLeds(im);
    im.src = pic;
  }

  _drawEmpty(W, H) {
    if (this._emptyDrawn) return;
    this._emptyDrawn = true;
    const data = new Uint8ClampedArray(W * H * 4);
    this._drawPixels(data, W, H);
  }

  _drawLeds(im) {
    this._emptyDrawn = false;
    const W = im.naturalWidth, H = im.naturalHeight;
    const t = document.createElement("canvas");
    t.width = W;
    t.height = H;
    const x = t.getContext("2d");
    x.drawImage(im, 0, 0);
    this._drawPixels(x.getImageData(0, 0, W, H).data, W, H);
  }

  _drawPixels(d, W, H) {
    const cv = this._$("cv");
    if (!cv) return;
    const cell = 10;
    if (cv.width !== W * cell) {
      cv.width = W * cell;
      cv.height = H * cell;
    }
    const g = cv.getContext("2d");
    g.fillStyle = "#050608";
    g.fillRect(0, 0, cv.width, cv.height);
    for (let y = 0; y < H; y++)
      for (let xx = 0; xx < W; xx++) {
        const i = (y * W + xx) * 4;
        const r = d[i], gg = d[i + 1], b = d[i + 2];
        const lit = r | gg | b;
        g.shadowBlur = lit ? 6 : 0;
        g.shadowColor = `rgb(${r},${gg},${b})`;
        g.fillStyle = lit ? `rgb(${Math.min(255, r + 25)},${Math.min(255, gg + 25)},${Math.min(255, b + 25)})` : "#15171c";
        g.beginPath();
        g.arc(xx * cell + cell / 2, y * cell + cell / 2, cell * 0.42, 0, 7);
        g.fill();
      }
    g.shadowBlur = 0;
  }
}

// ------------------------------------------------------------------ visual editor
class HAPanelCardEditor extends HTMLElement {
  setConfig(config) {
    this._config = { ...config };
    this._render();
  }

  set hass(hass) {
    this._hass = hass;
    if (this._form) this._form.hass = hass;
    else this._render();
  }

  _render() {
    if (!this._hass || !this._config) return;
    const schema = [
      { name: "entity", required: true, selector: { entity: { domain: "light" } } },
      { name: "title", selector: { text: {} } },
      {
        type: "grid",
        name: "",
        schema: [
          { name: "show_preview", selector: { boolean: {} } },
          { name: "show_controls", selector: { boolean: {} } },
          { name: "show_notify", selector: { boolean: {} } },
          { name: "notify_open", selector: { boolean: {} } },
          { name: "show_mood", selector: { boolean: {} } },
          { name: "show_indicators", selector: { boolean: {} } },
          { name: "show_settings", selector: { boolean: {} } },
        ],
      },
    ];
    const labels = {
      entity: "Displej panelu (light.…_displej)",
      title: "Nadpis",
      show_preview: "Živý náhled",
      show_controls: "Ovládání",
      show_notify: "Notifikace",
      notify_open: "Notifikace rozbalené",
      show_mood: "Nálada",
      show_indicators: "Indikátory",
      show_settings: "Nastavení",
    };
    const data = {
      show_preview: true,
      show_controls: true,
      show_notify: true,
      notify_open: false,
      show_mood: true,
      show_indicators: true,
      show_settings: true,
      ...this._config,
    };
    if (!this._form) {
      this._form = document.createElement("ha-form");
      this._form.computeLabel = (s) => labels[s.name] || s.name;
      this._form.addEventListener("value-changed", (e) => {
        this._config = e.detail.value;
        this.dispatchEvent(new CustomEvent("config-changed", { detail: { config: this._config }, bubbles: true, composed: true }));
      });
      this.appendChild(this._form);
    }
    this._form.hass = this._hass;
    this._form.schema = schema;
    this._form.data = data;
  }
}

customElements.define("hapanel-card", HAPanelCard);
customElements.define("hapanel-card-editor", HAPanelCardEditor);
window.customCards = window.customCards || [];
window.customCards.push({
  type: "hapanel-card",
  name: "HA-Panel LED matice",
  description: "Ovládání a nastavení LED panelu HA-Panel / AWTRIX: náhled, stránky, notifikace, nálada, indikátory.",
  preview: true,
  documentationURL: "https://github.com/joshuaaaaa/HA-panel",
});
console.info(`%c HAPANEL-CARD %c ${CARD_VERSION} `, "background:#03a9f4;color:#fff", "background:#333;color:#fff");
