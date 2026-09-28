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
const CARD_VERSION = "1.2.0";

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
  pages: ["sensor", "Stránky"],
};

const TYPES = { clock: "Hodiny", date: "Datum", entity: "Entita", template: "Šablona", text: "Text", effect: "Efekt" };
const PAGE_DEF = {
  name: "", type: "clock", enabled: true, icon: "", entity: "", decimals: -1, unit: "", text: "",
  color_tpl: "", icon_tpl: "", visible_tpl: "", progress_tpl: "", color: "", progress_color: "",
  duration: 8, effect: "", rainbow: false, style: 0, action: "", action_entity: "", action_service: "",
};
const PRESETS = [
  { t: "Hodiny", d: "velké číslice", p: { type: "clock", name: "Hodiny", style: 1, duration: 10 } },
  { t: "Hodiny s ikonou", d: "malé číslice", p: { type: "clock", name: "Hodiny", style: 0, icon: "clock", duration: 10 } },
  { t: "Datum", d: "s kalendářem", p: { type: "date", name: "Datum", duration: 5 } },
  { t: "Entita HA", d: "libovolný senzor", p: { type: "entity", name: "Senzor", decimals: 1, icon: "thermometer" } },
  { t: "Šablona HA", d: "Jinja šablona", p: { type: "template", name: "Šablona", text: "{{ states('sun.sun') }}" } },
  { t: "Text", d: "pevný text", p: { type: "text", name: "Text", text: "Ahoj!", icon: "heart" } },
  { t: "Efekt", d: "animace", p: { type: "effect", name: "Efekt", effect: "fire", duration: 10 } },
  { t: "Počasí", d: "teplota + ikona", p: { type: "template", name: "Počasí", text: "{{ state_attr('weather.forecast_home','temperature') | round(0) | int }}°", icon_tpl: "{{ states('weather.forecast_home') }}", icon: "partlycloudy" } },
  { t: "Teplota", d: "barva dle teploty", p: { type: "entity", name: "Venku", entity: "", decimals: 1, icon: "thermometer", color_tpl: "" } },
  { t: "Spotřeba / FVE", d: "W + pruh", p: { type: "entity", name: "FVE", entity: "", decimals: 0, icon: "solar", progress_color: "#ffc000" } },
  { t: "Baterie", d: "% + pruh", p: { type: "entity", name: "Baterie", entity: "", decimals: 0, icon: "battery" } },
  { t: "Pračka", d: "jen když pere", p: { type: "template", name: "Pračka", text: "", icon: "washer", visible_tpl: "" } },
  { t: "Světlo", d: "tlačítkem přepíná", p: { type: "template", name: "Světlo", text: "", icon: "bulb", action: "toggle" } },
];

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
      show_pages: true,
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
        .pages{display:flex;flex-direction:column;gap:6px;margin:6px 0}
        .pg{display:flex;align-items:center;gap:8px;background:var(--secondary-background-color);border:1px solid var(--divider-color);border-radius:10px;padding:6px 8px}
        .pg.off{opacity:.5}.pg.cur{border-color:var(--primary-color);box-shadow:0 0 0 1px var(--primary-color)}
        .pg canvas{width:28px;height:28px;image-rendering:pixelated;background:#000;border-radius:4px;flex:none}
        .pg .inf{flex:1;min-width:0}.pg .inf div{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
        .pg .nm{font-weight:600}.pg .ds{font-family:monospace;font-size:.8em;color:var(--secondary-text-color)}
        .pg .val{font-family:monospace;font-size:.85em;color:var(--primary-color)}
        .pg button.b{padding:4px 6px}.pg ha-icon{--mdc-icon-size:18px}
        .badge{font-size:.72em;padding:1px 6px;border-radius:99px;background:var(--divider-color);margin-left:4px;font-weight:400}
        .presets{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:6px;margin:6px 0}
        .presets button{text-align:left;flex-direction:column;align-items:flex-start}
        .presets small{color:var(--secondary-text-color);font-size:.75em}
        .pged{border:1px solid var(--primary-color);border-radius:10px;padding:10px;margin-top:8px}
        .pged label{display:block;font-size:.8em;color:var(--secondary-text-color);margin:6px 0 2px}
        .pged input[type=text],.pged select,.pged textarea{width:100%;box-sizing:border-box}
        .pged textarea{background:var(--secondary-background-color);color:var(--primary-text-color);border:1px solid var(--divider-color);border-radius:8px;padding:6px 8px;font-family:monospace;font-size:.85em;min-height:44px}
        .pged .two{display:grid;grid-template-columns:1fr 1fr;gap:8px}
        .tres{font-family:monospace;font-size:.8em;background:var(--secondary-background-color);border-radius:6px;padding:4px 6px;margin-top:3px;white-space:pre-wrap}
        .tres.err{color:var(--error-color,#db4437)}
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
        ${c.show_pages ? `
        <details id="pgsec" ${c.pages_open === false ? "" : "open"}><summary>Stránky <span class="muted" id="pgcount"></span></summary>
          <div class="pages" id="pglist"></div>
          <div class="row"><span class="muted" id="pgmsg"></span><span class="grow"></span><button class="b p" id="pgadd"><ha-icon icon="mdi:plus"></ha-icon>Přidat stránku</button></div>
          <div id="presets" class="presets" hidden></div>
          <div id="pged" class="pged" hidden></div>
        </details>` : ""}
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
    on("pgadd", "click", () => this._showPresets());
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
    if (this._config.show_pages) this._updatePages(attrs);
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
    // the proxy URL stays the same between updates -> use the entity state (last update time) as key
    const pic = img.attributes.entity_picture;
    const key = pic + "|" + img.state + "|" + img.last_updated;
    if (!pic || key === this._lastPic) return;
    this._lastPic = key;
    const im = new Image();
    im.onload = () => this._drawLeds(im);
    im.onerror = () => { this._lastPic = ""; };
    im.src = pic + (pic.includes("?") ? "&" : "?") + "_=" + Date.now();
  }


  // ------------------------------------------------------------------ pages
  _pagesAttr() {
    const s = this._st("pages");
    return (s && s.attributes) || {};
  }

  _topic() {
    const a = this._st("app");
    return a && a.attributes.topic;
  }

  _savePages(pages) {
    const topic = this._topic();
    const msg = this._$("pgmsg");
    if (!topic) {
      if (msg) msg.textContent = "Chybí MQTT prefix panelu (sensor Aplikace).";
      return;
    }
    this._localPages = pages;
    this._localPagesAt = Date.now();
    this._hass.callService("mqtt", "publish", { topic: topic + "/pages/set", payload: JSON.stringify(pages) });
    if (msg) msg.textContent = "Uloženo";
    this._renderPages();
  }

  _currentPages() {
    const remote = this._pagesAttr().pages || [];
    // keep the optimistic local copy until the panel confirms (max 10 s)
    if (this._localPages && Date.now() - this._localPagesAt < 10000) {
      if (JSON.stringify(remote) === JSON.stringify(this._localPages)) this._localPages = null;
      else return this._localPages;
    }
    this._localPages = null;
    return remote;
  }

  _updatePages() {
    const pages = this._currentPages();
    const sig = JSON.stringify(pages) + "|" + JSON.stringify(this._pagesAttr().icon_data || {});
    if (sig !== this._pagesSig) {
      this._pagesSig = sig;
      this._subscribeTemplates(pages);
      this._renderPages();
    } else {
      this._updatePageValues();
    }
  }

  _pageDesc(p) {
    switch (p.type) {
      case "entity": return p.entity;
      case "template": case "text": return p.text;
      case "effect": return p.effect + (p.text ? " · " + p.text : "");
      case "clock": return p.style == 1 ? "velké číslice" : "malé číslice";
      default: return "";
    }
  }

  _drawIconHex(cv, hex) {
    const g = cv.getContext("2d");
    const im = g.createImageData(8, 8);
    for (let i = 0; i < 64; i++) {
      im.data[i * 4] = parseInt(hex.substr(i * 6, 2), 16) || 0;
      im.data[i * 4 + 1] = parseInt(hex.substr(i * 6 + 2, 2), 16) || 0;
      im.data[i * 4 + 2] = parseInt(hex.substr(i * 6 + 4, 2), 16) || 0;
      im.data[i * 4 + 3] = 255;
    }
    g.putImageData(im, 0, 0);
  }

  _renderPages() {
    const list = this._$("pglist");
    if (!list) return;
    const pages = this._currentPages();
    const icons = this._pagesAttr().icon_data || {};
    const cur = this._st("app") ? this._st("app").state : "";
    this._$("pgcount").textContent = pages.length ? `(${pages.length})` : "";
    list.innerHTML = "";
    if (!this._st("pages")) {
      list.innerHTML = `<span class="muted">Entita „Stránky“ nenalezena – aktualizuj firmware panelu (1.2+).</span>`;
      return;
    }
    if (!pages.length) list.innerHTML = `<span class="muted">Žádné stránky – panel ukazuje výchozí hodiny.</span>`;
    pages.forEach((p, i) => {
      const row = document.createElement("div");
      row.className = "pg" + (p.enabled ? "" : " off") + (p.name === cur ? " cur" : "");
      const cv = document.createElement("canvas");
      cv.width = 8;
      cv.height = 8;
      if (icons[p.icon]) this._drawIconHex(cv, icons[p.icon]);
      row.appendChild(cv);
      const inf = document.createElement("div");
      inf.className = "inf";
      inf.innerHTML = `<div><span class="nm">${esc(p.name)}</span><span class="badge">${TYPES[p.type] || p.type}</span>${
        p.visible_tpl ? '<span class="badge">podmíněná</span>' : ""}${p.action ? '<span class="badge">akce</span>' : ""}</div>
        <div class="ds">${esc(this._pageDesc(p))}</div><div class="val" data-i="${i}"></div>`;
      row.appendChild(inf);
      const btn = (icon, title, fn, cls = "") => {
        const b = document.createElement("button");
        b.className = "b " + cls;
        b.title = title;
        b.innerHTML = `<ha-icon icon="${icon}"></ha-icon>`;
        b.onclick = fn;
        row.appendChild(b);
      };
      const upd = (fn) => {
        const copy = JSON.parse(JSON.stringify(pages));
        fn(copy);
        this._savePages(copy);
      };
      btn("mdi:arrow-up", "Nahoru", () => i > 0 && upd((a) => ([a[i - 1], a[i]] = [a[i], a[i - 1]])));
      btn("mdi:arrow-down", "Dolů", () => i < pages.length - 1 && upd((a) => ([a[i + 1], a[i]] = [a[i], a[i + 1]])));
      btn(p.enabled ? "mdi:pause" : "mdi:play", p.enabled ? "Vypnout" : "Zapnout", () => upd((a) => (a[i].enabled = !a[i].enabled)));
      btn("mdi:eye", "Zobrazit teď", () => this._call("select", "select_option", "page", { option: p.name }));
      btn("mdi:content-copy", "Duplikovat", () => upd((a) => a.splice(i + 1, 0, { ...a[i], name: a[i].name + " 2" })));
      btn("mdi:pencil", "Upravit", () => this._editPage(i), "p");
      list.appendChild(row);
    });
    this._updatePageValues();
  }

  // live values: entity state from hass, templates rendered by HA (render_template subscription)
  _entityValue(p) {
    const s = this._hass.states[p.entity];
    if (!s) return p.entity ? "entita nenalezena" : "";
    if (["unavailable", "unknown"].includes(s.state)) return "--";
    let v = s.state;
    if (!isNaN(parseFloat(v)) && isFinite(v) && p.decimals >= 0) v = (+v).toFixed(p.decimals);
    const unit = p.unit === "-" ? "" : p.unit || s.attributes.unit_of_measurement || "";
    return isNaN(parseFloat(s.state)) ? v : v + unit;
  }

  _updatePageValues() {
    const pages = this._currentPages();
    this._card.querySelectorAll(".pg .val").forEach((el) => {
      const p = pages[+el.dataset.i];
      if (!p) return;
      let v = "";
      if (p.type === "entity") v = this._entityValue(p);
      else if (p.type === "template") v = (this._tplValues || {})["t" + el.dataset.i] ?? "…";
      const vis = (this._tplValues || {})["v" + el.dataset.i];
      if (p.visible_tpl && vis !== undefined) v += (v ? " · " : "") + (/^(true|on|1|yes)$/i.test(String(vis).trim()) ? "zobrazená" : "skrytá");
      if (el.textContent !== v) el.textContent = v;
    });
  }

  async _subscribeTemplates(pages) {
    (this._unsubs || []).forEach((u) => { try { u(); } catch (e) {} });
    this._unsubs = [];
    this._tplValues = {};
    if (!this._hass.connection) return;
    const sub = async (key, template) => {
      try {
        const u = await this._hass.connection.subscribeMessage(
          (msg) => {
            this._tplValues[key] = msg.error ? "chyba šablony" : String(msg.result ?? "");
            this._updatePageValues();
          },
          { type: "render_template", template, report_errors: true }
        );
        this._unsubs.push(u);
      } catch (e) {
        this._tplValues[key] = "chyba: " + (e.message || e.code || e);
      }
    };
    pages.forEach((p, i) => {
      if (p.type === "template" && p.text) sub("t" + i, p.text);
      if (p.visible_tpl) sub("v" + i, p.visible_tpl);
    });
  }

  disconnectedCallback() {
    (this._unsubs || []).forEach((u) => { try { u(); } catch (e) {} });
    this._unsubs = [];
    this._pagesSig = null;
  }

  connectedCallback() {
    if (this._hass && this._config) this._update();
  }

  _showPresets() {
    const box = this._$("presets");
    this._$("pged").hidden = true;
    if (!box.hidden) { box.hidden = true; return; }
    box.innerHTML = "";
    PRESETS.forEach((pr) => {
      const b = document.createElement("button");
      b.className = "b";
      b.innerHTML = `${esc(pr.t)}<small>${esc(pr.d)}</small>`;
      b.onclick = () => { box.hidden = true; this._editPage(-1, { ...PAGE_DEF, ...pr.p }); };
      box.appendChild(b);
    });
    box.hidden = false;
  }

  async _renderOnce(template) {
    return new Promise(async (resolve) => {
      let unsub;
      const done = (v) => { try { unsub && unsub(); } catch (e) {} resolve(v); };
      setTimeout(() => done("timeout"), 5000);
      try {
        unsub = await this._hass.connection.subscribeMessage(
          (msg) => done(msg.error ? "chyba: " + msg.error : String(msg.result)),
          { type: "render_template", template, report_errors: true }
        );
      } catch (e) {
        done("chyba: " + (e.message || e.code));
      }
    });
  }

  _editPage(idx, preset) {
    const pages = this._currentPages();
    const p = { ...PAGE_DEF, ...(preset || pages[idx]) };
    const ed = this._$("pged");
    this._$("presets").hidden = true;
    const info = (this._st("app") || {}).attributes || {};
    const icons = info.icons || [];
    const effects = info.effects || ["rainbow", "plasma", "fire", "matrix", "snow"];
    const ents = Object.keys(this._hass.states).sort();
    const opt = (list, cur, empty) =>
      (empty !== undefined ? `<option value="">${empty}</option>` : "") +
      (cur && !list.includes(cur) ? [cur, ...list] : list).map((o) => `<option value="${esc(o)}" ${o === cur ? "selected" : ""}>${esc(o)}</option>`).join("");
    ed.innerHTML = `
      <b>${idx < 0 ? "Nová stránka" : "Upravit: " + esc(p.name)}</b>
      <div class="two">
        <div><label>Název</label><input type="text" data-k="name" value="${esc(p.name)}"></div>
        <div><label>Typ</label><select data-k="type">${Object.entries(TYPES).map(([k, v]) => `<option value="${k}" ${k === p.type ? "selected" : ""}>${v}</option>`).join("")}</select></div>
      </div>
      <div class="two">
        <div><label>Doba zobrazení (s)</label><input type="number" data-k="duration" value="${p.duration}" min="1" style="width:100%"></div>
        <div><label>Ikona</label><div class="row" style="margin:0"><canvas id="edic" width="8" height="8" style="width:28px;height:28px;image-rendering:pixelated;background:#000;border-radius:4px"></canvas>
          <select data-k="icon" class="grow">${opt(icons, p.icon, "bez ikony")}</select></div></div>
      </div>
      <div data-for="clock"><label>Styl hodin</label><select data-k="style"><option value="1" ${p.style == 1 ? "selected" : ""}>Velké číslice</option><option value="0" ${p.style == 1 ? "" : "selected"}>Malé číslice</option></select></div>
      <div data-for="entity">
        <label>Entita</label><input type="text" data-k="entity" list="hp-ents" value="${esc(p.entity)}" placeholder="sensor.…">
        <datalist id="hp-ents">${ents.map((e) => `<option value="${e}">${esc(this._hass.states[e].attributes.friendly_name || "")}</option>`).join("")}</datalist>
        <div class="two">
          <div><label>Desetinná místa (−1 = beze změny)</label><input type="number" data-k="decimals" value="${p.decimals}" min="-1" max="4" style="width:100%"></div>
          <div><label>Jednotka (prázdné = z HA, "-" = žádná)</label><input type="text" data-k="unit" value="${esc(p.unit)}"></div>
        </div>
      </div>
      <div data-for="template"><label>Šablona textu (Jinja)</label><textarea data-k="text" data-tpl>${esc(p.text)}</textarea><div class="tres" hidden></div></div>
      <div data-for="text"><label>Text</label><input type="text" data-k="text" value="${esc(p.text)}"></div>
      <div data-for="effect"><label>Text přes efekt</label><input type="text" data-k="text" value="${esc(p.text)}"></div>
      <div class="two">
        <div><label>${p.type === "effect" ? "Efekt" : "Efekt v pozadí"}</label><select data-k="effect">${opt(effects, p.effect, p.type === "effect" ? undefined : "žádný")}</select></div>
        <div><label>Barva textu</label><div class="row" style="margin:0"><label class="sw" style="margin:0;color:inherit"><input type="checkbox" data-k="color_on" ${p.color ? "checked" : ""}>vlastní</label><input type="color" data-k="color" value="${p.color || "#ffffff"}"></div></div>
      </div>
      <label class="sw" style="color:inherit"><input type="checkbox" data-k="enabled" ${p.enabled ? "checked" : ""}>Povoleno</label>
      <label class="sw" style="color:inherit"><input type="checkbox" data-k="rainbow" ${p.rainbow ? "checked" : ""}>Duhový text</label>
      <details><summary>Šablony (barva, ikona, viditelnost, průběh)</summary>
        <label>Barva (vrací #RRGGBB / název)</label><textarea data-k="color_tpl" data-tpl>${esc(p.color_tpl)}</textarea><div class="tres" hidden></div>
        <label>Ikona (vrací název ikony)</label><textarea data-k="icon_tpl" data-tpl>${esc(p.icon_tpl)}</textarea><div class="tres" hidden></div>
        <label>Zobrazit jen když (true / false)</label><textarea data-k="visible_tpl" data-tpl>${esc(p.visible_tpl)}</textarea><div class="tres" hidden></div>
        <label>Průběh 0–100 (pruh dole)</label><textarea data-k="progress_tpl" data-tpl>${esc(p.progress_tpl)}</textarea><div class="tres" hidden></div>
        <label>Barva průběhu</label><input type="color" data-k="progress_color" value="${p.progress_color || "#00a0ff"}">
      </details>
      <details><summary>Akce prostředního tlačítka</summary>
        <label>Akce</label><select data-k="action"><option value="">Další stránka</option><option value="toggle" ${p.action === "toggle" ? "selected" : ""}>Přepnout entitu</option><option value="service" ${p.action === "service" ? "selected" : ""}>Zavolat službu</option></select>
        <label>Entita akce (prázdné = entita stránky)</label><input type="text" data-k="action_entity" list="hp-ents" value="${esc(p.action_entity)}">
        <label>Služba (domain.service)</label><input type="text" data-k="action_service" value="${esc(p.action_service)}" placeholder="script.turn_on">
      </details>
      <div class="row">
        ${idx >= 0 ? '<button class="b" id="eddel" style="color:var(--error-color,#db4437)">Smazat</button>' : ""}
        <button class="b" id="edtest">Otestovat šablony</button>
        <span class="grow"></span>
        <button class="b" id="edcancel">Zrušit</button>
        <button class="b p" id="edsave">Uložit</button>
      </div>`;
    ed.hidden = false;
    const q = (sel) => ed.querySelector(sel);
    const all = (sel) => [...ed.querySelectorAll(sel)];
    const iconData = this._pagesAttr().icon_data || {};
    const drawIc = () => {
      const cv = q("#edic");
      const g = cv.getContext("2d");
      g.fillStyle = "#000";
      g.fillRect(0, 0, 8, 8);
      const n = q('[data-k="icon"]').value;
      if (iconData[n]) this._drawIconHex(cv, iconData[n]);
    };
    drawIc();
    q('[data-k="icon"]').onchange = drawIc;
    const updType = () => {
      const t = q('[data-k="type"]').value;
      all("[data-for]").forEach((d) => {
        const on = d.dataset.for === t;
        d.hidden = !on;
        d.querySelectorAll("input,textarea,select").forEach((x) => (x.disabled = !on));
      });
    };
    q('[data-k="type"]').onchange = updType;
    updType();
    q("#edcancel").onclick = () => (ed.hidden = true);
    if (q("#eddel")) q("#eddel").onclick = () => {
      if (!confirm("Smazat stránku " + p.name + "?")) return;
      const copy = JSON.parse(JSON.stringify(pages));
      copy.splice(idx, 1);
      ed.hidden = true;
      this._savePages(copy);
    };
    q("#edtest").onclick = async () => {
      const tests = all("textarea[data-tpl]").filter((t) => !t.disabled && t.value.trim());
      const t = q('[data-k="type"]').value;
      if (t === "entity") {
        const e = q('[data-k="entity"]').value.trim();
        this._$("pgmsg").textContent = "Entita → " + this._entityValue({ ...p, entity: e, decimals: +q('[data-k="decimals"]').value, unit: q('[data-k="unit"]').value });
      }
      for (const ta of tests) {
        const out = ta.nextElementSibling;
        out.hidden = false;
        out.textContent = "…";
        const r = await this._renderOnce(ta.value);
        out.textContent = "→ " + r;
        out.classList.toggle("err", r.startsWith("chyba"));
      }
    };
    q("#edsave").onclick = () => {
      const t = q('[data-k="type"]').value;
      const val = (k) => {
        const el = all(`[data-k="${k}"]`).find((x) => !x.disabled) || q(`[data-k="${k}"]`);
        return el ? (el.type === "checkbox" ? el.checked : el.value) : "";
      };
      const np = {
        ...PAGE_DEF,
        name: String(val("name")).trim() || TYPES[t],
        type: t,
        enabled: val("enabled"),
        duration: +val("duration") || 8,
        icon: val("icon"),
        style: +val("style"),
        entity: String(val("entity")).trim(),
        decimals: +val("decimals"),
        unit: val("unit"),
        text: ["template", "text", "effect"].includes(t) ? val("text") : "",
        effect: val("effect"),
        color: val("color_on") ? val("color") : "",
        rainbow: val("rainbow"),
        color_tpl: String(val("color_tpl")).trim(),
        icon_tpl: String(val("icon_tpl")).trim(),
        visible_tpl: String(val("visible_tpl")).trim(),
        progress_tpl: String(val("progress_tpl")).trim(),
        action: val("action"),
        action_entity: String(val("action_entity")).trim(),
        action_service: String(val("action_service")).trim(),
      };
      np.progress_color = np.progress_tpl ? val("progress_color") : "";
      const copy = JSON.parse(JSON.stringify(pages));
      if (idx < 0) copy.push(np);
      else copy[idx] = np;
      ed.hidden = true;
      this._savePages(copy);
    };
    ed.scrollIntoView({ behavior: "smooth", block: "nearest" });
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
          { name: "show_pages", selector: { boolean: {} } },
          { name: "pages_open", selector: { boolean: {} } },
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
      show_pages: "Stránky (správa)",
      pages_open: "Stránky rozbalené",
    };
    const data = {
      show_preview: true,
      show_controls: true,
      show_notify: true,
      notify_open: false,
      show_mood: true,
      show_indicators: true,
      show_settings: true,
      show_pages: true,
      pages_open: true,
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
  description: "Ovládání a nastavení LED panelu HA-Panel / AWTRIX: náhled, správa stránek, notifikace, nálada, indikátory.",
  preview: true,
  documentationURL: "https://github.com/joshuaaaaa/HA-panel",
});
console.info(`%c HAPANEL-CARD %c ${CARD_VERSION} `, "background:#03a9f4;color:#fff", "background:#333;color:#fff");
