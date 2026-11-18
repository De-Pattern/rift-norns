-- RIFT-NORNS
-- RIFT's machines, off the
-- Digitone. v0.1: FASCIA.
--
-- E1  page (4)
-- E2  select control
-- E3  change it
-- K2 hold + E3  browse patches
--     (let go to load)
-- K3  play a note
--
-- MIDI notes from any device
-- on the chosen channel; CCs
-- follow RIFT's FASCIA map.

engine.name = "RiftNorns"

local PATCHES = include("lib/patches")

local VERSION = "0.1"
local EXT = "/home/we/.local/share/SuperCollider/Extensions/RiftNorns"
local HERE = norns.state.path   -- this script's folder, whatever the repo is called

-- the 32 controls in RIFT's page order, with RIFT's MIDI CCs (FASCIA on a RIFT track sends
-- these when its knobs turn); the ten without a RIFT CC take 102..111
local NAMES = {
  "TYPA", "TYPB", "HARD", "DUST", "TZFM", "BEND", "MIX", "DEC",
  "BODA", "BODB", "TUNE", "GEOM", "MATL", "POS", "COUP", "DAMP",
  "MORPH", "FORM", "SHIFT", "RING", "SIDE", "FOLD", "LPG", "CHAR",
  "ATK", "CLIK", "CTYP", "XMOD", "FDBK", "BUZZ", "WATR", "VARY",
}
local CC = {
  TYPA = 41, TYPB = 45, HARD = 43, DUST = 102, TZFM = 103, BEND = 104, MIX = 105, DEC = 56,
  BODA = 42, BODB = 46, TUNE = 47, GEOM = 106, MATL = 107, POS = 108, COUP = 109, DAMP = 57,
  MORPH = 55, FORM = 58, SHIFT = 61, RING = 110, SIDE = 72, FOLD = 71, LPG = 59, CHAR = 63,
  ATK = 75, CLIK = 60, CTYP = 52, XMOD = 48, FDBK = 76, BUZZ = 111, WATR = 70, VARY = 77,
}
local PAGES = {"EXCITERS", "RESONATORS", "SPECTRAL", "ONSET/INTERACT"}
local LONG = {
  TYPA = "EXCITER A", TYPB = "EXCITER B", HARD = "HARDNESS", DUST = "DUST", TZFM = "TZFM",
  BEND = "BEND", MIX = "EXCITER MIX", DEC = "DECAY", BODA = "BODY A", BODB = "BODY B",
  TUNE = "TUNE", GEOM = "GEOMETRY", MATL = "MATERIAL", POS = "POSITION", COUP = "COUPLING",
  DAMP = "DAMPING", MORPH = "MORPH", FORM = "FORMANT", SHIFT = "SHIFT", RING = "RING",
  SIDE = "SIDEBAND", FOLD = "FOLD", LPG = "LPG", CHAR = "CHARACTER", ATK = "ATTACK",
  CLIK = "CLICK", CTYP = "CLICK TYPE", XMOD = "INTERACTION", FDBK = "FEEDBACK", BUZZ = "BUZZ",
  WATR = "WATER", VARY = "VARIATION",
}
local TYPES = {"STRIKE", "BLOW", "BOW", "DUST", "PULSE", "TZFM"}
local BODIES = {"MEMB", "PLATE", "BAR", "VIBE", "CHIME", "BELL", "GLASS", "TUBE", "STRING"}
local BODIES_B = {"MEMB", "PLATE", "BAR", "VIBE", "CHIME", "BELL", "GLASS", "TUBE", "STRING", "OFF"}
local CHOICES = {
  TYPA = TYPES, TYPB = TYPES, BODA = BODIES, BODB = BODIES_B,
  CTYP = {"SNAP", "TICK", "NOISE", "THUMP"}, XMOD = {"EXCITE", "BEND", "CHOKE", "TRADE"},
}
-- four letters in the box (RIFT's)
local SHORT = {
  STRIKE = "STRK", PULSE = "PULS", PLATE = "PLAT", CHIME = "CHIM", GLASS = "GLAS",
  STRING = "STRG", EXCITE = "EXCT", CHOKE = "CHOK", TRADE = "TRAD", NOISE = "NOIS", THUMP = "THMP",
}
local BIPOLAR = {TUNE = true, GEOM = true, SHIFT = true}

local page, sel = 1, 1
local k2_held, browsing, browse = false, false, 1
local flash_name, flash_value, flash_until = nil, nil, 0
local note_flash = 0
local last_note = 60
local dirty = true
local needs_restart = false
local install_msg = nil
local midi_dev
local ignore_param = false

-- a choice's engine value: the middle of its span of 0..127 (as RIFT's choice() reads it)
local function span(k, n) return ((2 * (k - 1) + 1) * 64) // n end

local function engine_value(name)
  local v = params:get("fsc_" .. name)
  local ch = CHOICES[name]
  if ch then return span(v, #ch) end
  return v
end

local function show_value(name)
  local v = params:get("fsc_" .. name)
  local ch = CHOICES[name]
  if ch then return ch[v] end
  if BIPOLAR[name] then
    local s = v - 64
    return (s > 0 and "+" or "") .. s
  end
  return tostring(v)
end

local function flash(name, value)
  flash_name, flash_value, flash_until = name, value, util.time() + 1.0
  dirty = true
end

-- ---------------------------------------------------------------------------------------
-- install: the UGen lives in SuperCollider's Extensions folder (sclang must compile its
-- class, scsynth must load its plugin), so it is copied there once and norns restarted

local function capture(cmd)
  local f = io.popen(cmd)
  local s = f:read("*a")
  f:close()
  return (s:gsub("%s+$", ""))
end

local function installed_version()
  local f = io.open(EXT .. "/VERSION", "r")
  if not f then return nil end
  local v = f:read("*l")
  f:close()
  return v
end

local function install()
  local bits = capture("getconf LONG_BIT")
  local src = HERE .. "ignore/RiftNorns/"
  os.execute("mkdir -p " .. EXT)
  if bits == "32" then
    os.execute("cp " .. src .. "RiftNorns.so " .. EXT .. "/RiftNorns.so")
  else
    -- not the stock 32-bit norns image: build the plugin here
    install_msg = "building the UGen..."
    redraw()
    local ok = os.execute("sh " .. HERE .. "ignore/src/build_on_norns.sh " .. EXT .. " > /tmp/rift_norns_build.log 2>&1")
    if not ok then
      install_msg = "build failed: /tmp/rift_norns_build.log"
      return false
    end
  end
  os.execute("cp " .. src .. "RiftNorns.sc " .. EXT .. "/RiftNorns.sc")
  local f = io.open(EXT .. "/VERSION", "w")
  f:write(VERSION .. "\n")
  f:close()
  return true
end

-- ---------------------------------------------------------------------------------------
-- params

local function load_patch(i)
  local p = PATCHES[i]
  if not p then return end
  ignore_param = true
  for k, name in ipairs(NAMES) do
    local ch = CHOICES[name]
    local v = p.v[k]
    if ch then v = math.min((v * #ch) >> 7, #ch - 1) + 1 end
    params:set("fsc_" .. name, v)
  end
  ignore_param = false
  for _, name in ipairs(NAMES) do engine.param(name, engine_value(name)) end
  params:set("fsc_trem_depth", p.trem > 0 and 0.35 or 0)
  params:set("fsc_trem_rate", p.trem > 0 and p.trem or params:get("fsc_trem_rate"))
  params:set("fsc_patch", i)
  flash("PATCH", p.name)
end

local function add_params()
  params:add_separator("FASCIA")
  local pnames = {}
  for i, p in ipairs(PATCHES) do pnames[i] = p.name end
  params:add_option("fsc_patch", "patch", pnames, 1)
  params:add_trigger("fsc_load", "load patch")
  params:set_action("fsc_load", function() load_patch(params:get("fsc_patch")) end)

  for pg = 1, 4 do
    params:add_group("fsc_page" .. pg, "SYN" .. pg .. " " .. PAGES[pg], 8)
    for c = 1, 8 do
      local name = NAMES[(pg - 1) * 8 + c]
      local id = "fsc_" .. name
      local ch = CHOICES[name]
      local default = PATCHES[1].v[(pg - 1) * 8 + c]
      if ch then
        params:add_option(id, LONG[name], ch, math.min((default * #ch) >> 7, #ch - 1) + 1)
      elseif BIPOLAR[name] then
        params:add_number(id, LONG[name], 0, 127, default, function(p)
          local s = p:get() - 64
          return (s > 0 and "+" or "") .. s
        end)
      else
        params:add_number(id, LONG[name], 0, 127, default)
      end
      params:set_action(id, function()
        if not ignore_param then engine.param(name, engine_value(name)) end
        dirty = true
      end)
    end
  end

  params:add_group("fsc_voice", "VOICE / MIDI", 8)
  params:add_number("fsc_voices", "voices", 1, 16, 6)
  params:set_action("fsc_voices", function(v) engine.voices(v) end)
  params:add_control("fsc_amp", "level", controlspec.new(0, 1, "lin", 0, 0.7, ""))
  params:set_action("fsc_amp", function(v) engine.amp(v) end)
  params:add_control("fsc_pan", "pan", controlspec.PAN)
  params:set_action("fsc_pan", function(v) engine.pan(v) end)
  params:add_control("fsc_trem_rate", "tremolo rate", controlspec.new(0.1, 20, "exp", 0, 5.5, "Hz"))
  params:set_action("fsc_trem_rate", function(v) engine.trem_rate(v) end)
  params:add_control("fsc_trem_depth", "tremolo depth", controlspec.new(0, 1, "lin", 0, 0, ""))
  params:set_action("fsc_trem_depth", function(v) engine.trem_depth(v) end)
  params:add_number("fsc_midi_dev", "midi device", 1, 16, 1)
  params:set_action("fsc_midi_dev", function(v)
    if midi_dev then midi_dev.event = nil end
    midi_dev = midi.connect(v)
    midi_dev.event = midi_event
  end)
  params:add_number("fsc_midi_ch", "midi channel", 0, 16, 0, function(p)
    return p:get() == 0 and "omni" or tostring(p:get())
  end)
  params:add_option("fsc_cc", "cc map", {"off", "RIFT"}, 2)
end

-- ---------------------------------------------------------------------------------------
-- playing

local CC_TO_NAME = {}
for name, cc in pairs(CC) do CC_TO_NAME[cc] = name end

local function play(note, vel)
  engine.note_on(note, vel)
  last_note = note
  note_flash = util.time() + 0.08
  dirty = true
end

function midi_event(data)
  local msg = midi.to_msg(data)
  local ch = params:get("fsc_midi_ch")
  if ch ~= 0 and msg.ch ~= ch then return end
  if msg.type == "note_on" and msg.vel > 0 then
    play(msg.note, msg.vel / 127)
  elseif msg.type == "cc" and params:get("fsc_cc") == 2 then
    local name = CC_TO_NAME[msg.cc]
    if name then
      local c = CHOICES[name]
      local v = msg.val
      if c then v = math.min((v * #c) >> 7, #c - 1) + 1 end
      params:set("fsc_" .. name, v)
      flash(name, show_value(name))
    end
  end
end

-- ---------------------------------------------------------------------------------------
-- controls

function enc(n, d)
  if needs_restart then return end
  if n == 1 then
    page = util.clamp(page + d, 1, 4)
    dirty = true
  elseif n == 2 then
    sel = util.clamp(sel + d, 1, 8)
    dirty = true
  elseif n == 3 then
    if k2_held then
      browsing = true
      browse = util.clamp(browse + d, 1, #PATCHES)
      flash("PATCH", PATCHES[browse].name)
      return
    end
    local name = NAMES[(page - 1) * 8 + sel]
    -- a choice steps one option a click (FUNC + turn on the DN2); a knob one step
    params:delta("fsc_" .. name, CHOICES[name] and (d > 0 and 1 or -1) or d)
    flash(name, show_value(name))
  end
end

function key(n, z)
  if needs_restart then return end
  if n == 2 then
    k2_held = z == 1
    if z == 1 then
      browsing = false
      browse = params:get("fsc_patch")
    elseif browsing then
      load_patch(browse)
      browsing = false
    end
  elseif n == 3 and z == 1 then
    play(last_note, 0.85)
  end
end

-- ---------------------------------------------------------------------------------------
-- screen: thin lines, the cells laid out as the DN2's knobs (A-D over E-H); values show only
-- while a control moves

local COLX = {16, 48, 80, 112}
local ROWY = {21, 45}
local A0, A1 = math.pi * 0.75, math.pi * 2.25

local function knob(x, y, v, bip, hi)
  local r = 6
  screen.line_width(1)
  screen.level(2)
  screen.move(x + r * math.cos(A0), y + r * math.sin(A0))
  screen.arc(x, y, r, A0, A1)
  screen.stroke()
  local a = A0 + (v / 127) * (A1 - A0)
  local from = bip and math.pi * 1.5 or A0
  local lo, hi_a = math.min(from, a), math.max(from, a)
  screen.level(hi and 15 or 7)
  if hi_a - lo > 0.01 then
    screen.move(x + r * math.cos(lo), y + r * math.sin(lo))
    screen.arc(x, y, r, lo, hi_a)
    screen.stroke()
  end
  screen.move(x, y)
  screen.line(x + (r - 1) * math.cos(a), y + (r - 1) * math.sin(a))
  screen.stroke()
end

local function choice_box(x, y, word, hi)
  local w = SHORT[word] or word
  screen.level(hi and 15 or 4)
  screen.rect(x - 13.5, y - 5.5, 27, 11)
  screen.stroke()
  screen.level(hi and 15 or 8)
  screen.move(x, y + 3)
  screen.text_center(w)
end

function redraw()
  screen.clear()
  screen.font_face(1)
  screen.font_size(8)
  screen.line_width(1)
  if install_msg or needs_restart then
    screen.level(15)
    screen.move(64, 24)
    screen.text_center(install_msg or "RIFT-NORNS installed")
    screen.level(6)
    screen.move(64, 40)
    screen.text_center(needs_restart and "SYSTEM > RESTART, then" or "")
    screen.move(64, 50)
    screen.text_center(needs_restart and "load RIFT-NORNS again" or "")
    screen.update()
    return
  end
  local now = util.time()
  -- header
  screen.level(note_flash > now and 15 or 3)
  screen.rect(1, 2, 3, 3)
  screen.fill()
  if flash_name and now < flash_until then
    screen.level(15)
    screen.move(8, 7)
    screen.text(flash_name == "PATCH" and "PATCH" or (LONG[flash_name] or flash_name))
    screen.move(127, 7)
    screen.text_right(string.upper(flash_value or ""))
  else
    screen.level(10)
    screen.move(8, 7)
    screen.text("FASCIA")
    screen.level(5)
    screen.move(40, 7)
    screen.text(PAGES[page])
    screen.move(127, 7)
    screen.text_right(page .. "/4")
  end
  screen.level(2)
  screen.move(0, 10.5)
  screen.line(128, 10.5)
  screen.stroke()
  -- the eight cells
  for c = 1, 8 do
    local name = NAMES[(page - 1) * 8 + c]
    local x, y = COLX[(c - 1) % 4 + 1], ROWY[c <= 4 and 1 or 2]
    local hi = c == sel
    local ch = CHOICES[name]
    if ch then
      choice_box(x, y, ch[params:get("fsc_" .. name)], hi)
    else
      knob(x, y, params:get("fsc_" .. name), BIPOLAR[name], hi)
    end
    screen.level(hi and 15 or 5)
    screen.move(x, y + 13)
    screen.text_center(name)
    if hi then
      screen.move(x - 8, y + 15.5)
      screen.line(x + 8, y + 15.5)
      screen.stroke()
    end
  end
  screen.update()
end

-- ---------------------------------------------------------------------------------------

function init()
  if installed_version() ~= VERSION then
    if install() then
      needs_restart = true
      install_msg = nil
    end
    redraw()
    return
  end
  add_params()
  params:default()
  params:bang()
  local ui = metro.init(function()
    local now = util.time()
    if flash_name and now >= flash_until then flash_name = nil; dirty = true end
    if note_flash > 0 and now >= note_flash then note_flash = 0; dirty = true end
    if dirty then dirty = false; redraw() end
  end, 1 / 20)
  ui:start()
end

function cleanup()
  if engine.panic then engine.panic() end
end
