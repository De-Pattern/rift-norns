-- RIFT-NORNS
-- RIFT's machines, off the
-- Digitone: FASCIA, ORGAN,
-- SILT, LIGATURE, RESONATOR.
--
-- E1  page
-- E2  select control
-- E3  change it
-- K2 hold + E1  machine
-- K2 hold + E3  patches
--     (let go of K2 to load)
-- K3  play a note (held)
--
-- MIDI notes and note-offs from
-- any device on the chosen
-- channel; CCs follow RIFT's map.

engine.name = "RiftNorns"

local MACHINES = include("lib/machines")
local FASCIA_PATCHES = include("lib/patches")

local VERSION = "0.2"
local EXT = "/home/we/.local/share/SuperCollider/Extensions/RiftNorns"
local HERE = norns.state.path   -- this script's folder, whatever the repo is called

local by_id = {}
for i, m in ipairs(MACHINES) do by_id[m.id] = i end
local WIRING = MACHINES.wiring
local SOURCES = MACHINES.sources

-- -----------------------------------------------------------------------------------------
-- state

local mi = 1                        -- the machine (index into MACHINES)
local page, sel = 1, 1
local k2_held, k2_used = false, false
local pending_machine, pending_patch = nil, nil
local flash_l, flash_r, flash_until = nil, nil, 0
local note_flash = 0
local last_note = 60
local held_k3 = nil
local dirty = true
local needs_restart = false
local install_msg = nil
local midi_dev
local ignore_param = false

local function M() return MACHINES[mi] end
local function pid(m, c) return "rn_" .. m.id .. "_" .. c end

-- a machine's pages as the screen shows them: RIFT's, and LIGATURE's WIRING after its first
local function pages(m)
  local p = {}
  for i, pg in ipairs(m.pages) do
    p[#p + 1] = pg
    if m.id == "ligature" and i == 1 then p[#p + 1] = {title = "WIRING", wiring = true} end
  end
  return p
end

-- a choice's engine value: the middle of its span of 0..127 (as RIFT reads a choice)
local function span(k, n) return ((2 * (k - 1) + 1) * 64) // n end
local function opt_of(v, n) return math.min((v * n) >> 7, n - 1) + 1 end

local function engine_value(m, c)
  local v = params:get(pid(m, c))
  local d = m.ctl[c]
  if d.choices then return span(v, #d.choices) end
  return v
end

local function show_value(m, c)
  local v = params:get(pid(m, c))
  local d = m.ctl[c]
  if d.choices then return d.choices[v] end
  if d.bipolar then
    local s = v - 64
    return (s > 0 and "+" or "") .. s
  end
  return tostring(v)
end

local function flash(l, r)
  flash_l, flash_r, flash_until = l, r, util.time() + 1.0
  dirty = true
end

local function send_machine()
  local m = M()
  engine.machine(m.id)
  for _, c in ipairs(m.order) do engine.param(c, engine_value(m, c)) end
end

-- -----------------------------------------------------------------------------------------
-- install: the UGens live in SuperCollider's Extensions folder (sclang compiles their classes,
-- scsynth loads the plugin), so they are copied there once and norns restarted

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
    install_msg = "building the UGens..."
    redraw()
    local ok = os.execute("sh " .. HERE .. "ignore/src/build_on_norns.sh " .. EXT .. " > /tmp/rift_norns_build.log 2>&1")
    if not ok then
      install_msg = "build failed: see log"
      return false
    end
  end
  os.execute("cp " .. src .. "RiftNorns.sc " .. EXT .. "/RiftNorns.sc")
  local f = io.open(EXT .. "/VERSION", "w")
  f:write(VERSION .. "\n")
  f:close()
  return true
end

-- -----------------------------------------------------------------------------------------
-- params

local function patch_names(m)
  if m.id == "fascia" then
    local t = {}
    for i, p in ipairs(FASCIA_PATCHES) do t[i] = p.name end
    return t
  end
  return {"init"}
end

local function set_knobs(m, knobs)
  ignore_param = true
  for k, c in ipairs(m.order) do
    local d = m.ctl[c]
    local v = knobs[k]
    if d.choices then v = opt_of(v, #d.choices) end
    params:set(pid(m, c), v)
  end
  ignore_param = false
  if m == M() then
    for _, c in ipairs(m.order) do engine.param(c, engine_value(m, c)) end
  end
end

local function load_patch(i)
  local m = M()
  if m.id == "fascia" then
    local p = FASCIA_PATCHES[i]
    if not p then return end
    set_knobs(m, p.v)
    params:set("rn_trem_depth", p.trem > 0 and 0.35 or 0)
    if p.trem > 0 then params:set("rn_trem_rate", p.trem) end
    flash("PATCH", p.name)
  else
    local d = {}
    for k, c in ipairs(m.order) do d[k] = m.ctl[c].default end
    set_knobs(m, d)
    flash("PATCH", "init")
  end
end

local function select_machine(i)
  mi = util.clamp(i, 1, #MACHINES)
  page, sel = 1, 1
  if params:get("rn_machine") ~= mi then params:set("rn_machine", mi) end
  send_machine()
  flash("MACHINE", M().name)
end

local function add_params()
  params:add_separator("RIFT-NORNS")
  local names = {}
  for i, m in ipairs(MACHINES) do names[i] = m.name end
  params:add_option("rn_machine", "machine", names, 1)
  params:set_action("rn_machine", function(v)
    if v ~= mi then
      mi = v
      page, sel = 1, 1
      send_machine()
      dirty = true
    end
  end)
  for _, m in ipairs(MACHINES) do
    params:add_group("rn_" .. m.id, m.name, #m.order)
    for _, c in ipairs(m.order) do
      local d = m.ctl[c]
      local id = pid(m, c)
      if d.choices then
        params:add_option(id, d.long, d.choices, opt_of(d.default, #d.choices))
      elseif d.bipolar then
        params:add_number(id, d.long, 0, 127, d.default, function(p)
          local s = p:get() - 64
          return (s > 0 and "+" or "") .. s
        end)
      else
        params:add_number(id, d.long, 0, 127, d.default)
      end
      params:set_action(id, function()
        if not ignore_param and m == M() then engine.param(c, engine_value(m, c)) end
        dirty = true
      end)
    end
  end
  params:add_group("rn_voice", "VOICE / MIDI", 9)
  params:add_number("rn_voices", "voices", 1, 16, 6)
  params:set_action("rn_voices", function(v) engine.voices(v) end)
  params:add_control("rn_amp", "level", controlspec.new(0, 1, "lin", 0, 0.7, ""))
  params:set_action("rn_amp", function(v) engine.amp(v) end)
  params:add_control("rn_pan", "pan", controlspec.PAN)
  params:set_action("rn_pan", function(v) engine.pan(v) end)
  params:add_control("rn_release", "organ release", controlspec.new(0.01, 10, "exp", 0, 0.5, "s"))
  params:set_action("rn_release", function(v) engine.release(v) end)
  params:add_control("rn_trem_rate", "tremolo rate", controlspec.new(0.1, 20, "exp", 0, 5.5, "Hz"))
  params:set_action("rn_trem_rate", function(v) engine.trem_rate(v) end)
  params:add_control("rn_trem_depth", "tremolo depth", controlspec.new(0, 1, "lin", 0, 0, ""))
  params:set_action("rn_trem_depth", function(v) engine.trem_depth(v) end)
  params:add_number("rn_midi_dev", "midi device", 1, 16, 1)
  params:set_action("rn_midi_dev", function(v)
    if midi_dev then midi_dev.event = nil end
    midi_dev = midi.connect(v)
    midi_dev.event = midi_event
  end)
  params:add_number("rn_midi_ch", "midi channel", 0, 16, 0, function(p)
    return p:get() == 0 and "omni" or tostring(p:get())
  end)
  params:add_option("rn_cc", "cc map", {"off", "RIFT"}, 2)
end

-- -----------------------------------------------------------------------------------------
-- playing

local function note_on(note, vel)
  engine.note_on(note, vel)
  last_note = note
  note_flash = util.time() + 0.08
  dirty = true
end

local function note_off(note) engine.note_off(note) end

function midi_event(data)
  local msg = midi.to_msg(data)
  local ch = params:get("rn_midi_ch")
  if ch ~= 0 and msg.ch ~= ch then return end
  if msg.type == "note_on" and msg.vel > 0 then
    note_on(msg.note, msg.vel / 127)
  elseif msg.type == "note_off" or (msg.type == "note_on" and msg.vel == 0) then
    note_off(msg.note)
  elseif msg.type == "cc" and params:get("rn_cc") == 2 then
    local m = M()
    for _, c in ipairs(m.order) do
      local d = m.ctl[c]
      if d.cc == msg.cc then
        local v = msg.val
        if d.choices then v = opt_of(v, #d.choices) end
        params:set(pid(m, c), v)
        flash(d.long, show_value(m, c))
        break
      end
    end
  end
end

-- -----------------------------------------------------------------------------------------
-- controls

local function page_count() return #pages(M()) end

local function wiring_patch()
  local m = M()
  local v = params:get(pid(m, "PATCH"))
  return WIRING[v], v
end

local function block_desc(i)
  local w = wiring_patch()
  local b = w and w.blocks[i]
  if not b then return i .. " -", "" end
  -- an input: the block number it comes from (its cable is lit), or the source's tag
  local function src(s)
    if s == 0 then return "-" end
    if s <= 12 then return tostring(s) end
    return SOURCES[s] or "?"
  end
  return i .. " " .. b.type, "A:" .. src(b.a) .. "  B:" .. src(b.b)
end

function enc(n, d)
  if needs_restart then return end
  local m = M()
  if k2_held then
    k2_used = true
    if n == 1 then
      pending_machine = util.clamp((pending_machine or mi) + d, 1, #MACHINES)
      flash("MACHINE", MACHINES[pending_machine].name)
    elseif n == 3 then
      local names = patch_names(m)
      pending_patch = util.clamp((pending_patch or params:get("rn_patch_" .. m.id) or 1) + d, 1, #names)
      flash("PATCH", names[pending_patch])
    end
    return
  end
  local pg = pages(m)
  if n == 1 then
    page = util.clamp(page + d, 1, #pg)
    sel = math.min(sel, pg[page].wiring and 12 or 8)
    dirty = true
  elseif n == 2 then
    if pg[page].wiring then
      sel = util.clamp(sel + d, 1, 12)
      flash(block_desc(sel))
    else
      sel = util.clamp(sel + d, 1, 8)
      dirty = true
    end
  elseif n == 3 then
    local c = pg[page].wiring and "PATCH" or pg[page].controls[sel]
    local dd = m.ctl[c]
    -- a choice steps one option a click (FUNC + turn on the DN2); a knob one step
    params:delta(pid(m, c), dd.choices and (d > 0 and 1 or -1) or d)
    if pg[page].wiring then
      flash("PATCH", show_value(m, c))
    elseif m.id == "ligature" and c:match("^S%d") then
      local bi, ab = c:match("^S(%d+)([AB])$")
      local l = block_desc(tonumber(bi))
      flash(l .. " " .. ab, show_value(m, c))
    else
      flash(dd.long, show_value(m, c))
    end
  end
end

function key(n, z)
  if needs_restart then return end
  if n == 2 then
    if z == 1 then
      k2_held, k2_used = true, false
      pending_machine, pending_patch = nil, nil
    else
      k2_held = false
      if pending_machine and pending_machine ~= mi then select_machine(pending_machine) end
      if pending_patch then
        params:set("rn_patch_" .. M().id, pending_patch)
        load_patch(pending_patch)
      end
      pending_machine, pending_patch = nil, nil
    end
  elseif n == 3 then
    if z == 1 then
      held_k3 = last_note
      note_on(held_k3, 0.85)
    elseif held_k3 then
      note_off(held_k3)
      held_k3 = nil
    end
  end
end

-- -----------------------------------------------------------------------------------------
-- screen: thin lines, values only while a control moves. Knob pages lay their eight cells
-- out as the DN2's knobs (A-D over E-H); LIGATURE's WIRING page lays the twelve blocks out in
-- three rows of four, in run order, with their cables.

local COLX = {16, 48, 80, 112}
local ROWY = {21, 45}
local A0, A1 = math.pi * 0.75, math.pi * 2.25

local function knob(x, y, v, bip, hi, dim)
  local r = 6
  screen.line_width(1)
  screen.level(dim and 1 or 2)
  screen.move(x + r * math.cos(A0), y + r * math.sin(A0))
  screen.arc(x, y, r, A0, A1)
  screen.stroke()
  local a = A0 + (v / 127) * (A1 - A0)
  local from = bip and math.pi * 1.5 or A0
  local lo, hi_a = math.min(from, a), math.max(from, a)
  screen.level(hi and 15 or (dim and 3 or 7))
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
  screen.level(hi and 15 or 4)
  screen.rect(x - 13.5, y - 5.5, 27, 11)
  screen.stroke()
  screen.level(hi and 15 or 8)
  screen.move(x, y + 3)
  screen.text_center(word)
end

local function draw_cells(m, pg)
  local wired = m.id == "ligature" and wiring_patch()
  for c = 1, 8 do
    local name = pg.controls[c]
    local d = m.ctl[name]
    local x, y = COLX[(c - 1) % 4 + 1], ROWY[c <= 4 and 1 or 2]
    local hi = c == sel
    -- a LIGATURE setting whose block this patch leaves empty: drawn dim
    local dim = false
    if wired and name:match("^S%d") then
      local bi = tonumber(name:match("^S(%d+)"))
      dim = not wired.blocks[bi]
    end
    if d.choices then
      choice_box(x, y, d.box[params:get(pid(m, name))], hi)
    else
      knob(x, y, params:get(pid(m, name)), d.bipolar, hi, dim)
    end
    screen.level(hi and 15 or (dim and 2 or 5))
    screen.move(x, y + 13)
    screen.text_center(d.label)
    if hi then
      screen.move(x - 8, y + 15.5)
      screen.line(x + 8, y + 15.5)
      screen.stroke()
    end
  end
end

-- WIRING: blocks 1-4 on the top row, 5-8 and 9-12 below (the BLOCKS pages' order). A block's
-- inputs are on its top edge (A left, B right), its output on its bottom edge; a cable runs
-- from an output to an input. A source that is not a block (PIT GAT TRG VEL NOI LF1 LF2) is
-- a tag over the input it feeds. The voice's output block has a notch on its right edge.
local BW, BH = 24, 9
local BOXNAME = {PHASE = "PHAS", SLOPE = "SLOP", CYCLE = "CYCL", IMPLS = "IMPL", CREAS = "CRES"}
local function bpos(i)
  local col, row = (i - 1) % 4, (i - 1) // 4
  local x = 4 + col * 32
  local y = 18 + row * 17
  return x, y
end

local function cable(x1, y1, x2, y2)
  -- out (bottom edge) down, round, into an input (top edge): a hanging patch cable
  local sag = math.max(math.abs(x2 - x1) * 0.25, 5)
  screen.move(x1, y1)
  screen.curve(x1, y1 + sag, x2, y2 - sag, x2, y2)
  screen.stroke()
end

local function draw_wiring()
  local w = wiring_patch()
  if not w then return end
  -- cables first (dim), then the selected block's on top, then the boxes over them
  for pass = 1, 2 do
    for i = 1, 12 do
      local b = w.blocks[i]
      if b then
        for k, s in ipairs({b.a, b.b}) do
          if s >= 1 and s <= 12 and w.blocks[s] then
            local on_sel = (i == sel or s == sel)
            if (pass == 1 and not on_sel) or (pass == 2 and on_sel) then
              local sx, sy = bpos(s)
              local tx, ty = bpos(i)
              local jx = k == 1 and tx + 5 or tx + BW - 5
              screen.level(on_sel and 15 or 3)
              cable(sx + BW / 2, sy + BH, jx, ty)
            end
          end
        end
      end
    end
  end
  for i = 1, 12 do
    local b = w.blocks[i]
    local x, y = bpos(i)
    local hi = i == sel
    if b then
      screen.level(0)
      screen.rect(x, y, BW, BH)
      screen.fill()
      screen.level(hi and 15 or 6)
      screen.rect(x + 0.5, y + 0.5, BW - 1, BH - 1)
      screen.stroke()
      screen.level(hi and 15 or 9)
      screen.move(x + BW / 2, y + 7)
      screen.text_center(BOXNAME[b.type] or b.type)
      -- tags for the sources that are not blocks, over the jack they feed
      for k, s in ipairs({b.a, b.b}) do
        if s >= 13 then
          local jx = k == 1 and x + 5 or x + BW - 5
          screen.level(0)
          screen.rect(jx - 7, y - 7, 14, 6)
          screen.fill()
          screen.level(hi and 15 or 5)
          screen.move(jx, y - 1)
          screen.text_center(SOURCES[s])
        end
      end
      if i == w.out then
        screen.level(hi and 15 or 10)
        screen.move(x + BW, y + 2)
        screen.line(x + BW + 3, y + BH / 2)
        screen.line(x + BW, y + BH - 2)
        screen.close()
        screen.fill()
      end
    else
      screen.level(hi and 6 or 1)
      screen.rect(x + 0.5, y + 0.5, BW - 1, BH - 1)
      screen.stroke()
    end
  end
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
  local m = M()
  local pg = pages(m)
  local now = util.time()
  -- header
  screen.level(note_flash > now and 15 or 3)
  screen.rect(1, 2, 3, 3)
  screen.fill()
  if flash_l and now < flash_until then
    local r = string.upper(flash_r or "")
    local l = flash_l
    -- the two halves never meet: drop the left label when the value needs the room
    if screen.text_extents(l) + screen.text_extents(r) + 8 > 119 then l = "" end
    screen.level(15)
    screen.move(8, 7)
    screen.text(l)
    screen.move(127, 7)
    screen.text_right(r)
  else
    local title = pg[page].title
    if title == m.name then title = "" end
    -- the WIRING page is the patch: its name is the title
    if pg[page].wiring then title = WIRING[params:get(pid(m, "PATCH"))].name end
    local cnt = page .. "/" .. #pg
    screen.level(10)
    screen.move(8, 7)
    screen.text(m.name)
    local tx = 8 + screen.text_extents(m.name) + 6
    if tx + screen.text_extents(title) + 6 > 127 - screen.text_extents(cnt) then title = "" end
    screen.level(5)
    screen.move(tx, 7)
    screen.text(title)
    screen.move(127, 7)
    screen.text_right(cnt)
  end
  screen.level(2)
  screen.move(0, 10.5)
  screen.line(128, 10.5)
  screen.stroke()
  if pg[page].wiring then draw_wiring() else draw_cells(m, pg[page]) end
  screen.update()
end

-- -----------------------------------------------------------------------------------------

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
  -- the patch each machine last loaded (not a sound control: no engine action)
  for _, m in ipairs(MACHINES) do
    params:add_number("rn_patch_" .. m.id, m.name .. " patch", 1, #patch_names(m), 1)
    params:hide("rn_patch_" .. m.id)
  end
  params:default()
  params:bang()
  mi = params:get("rn_machine")
  send_machine()
  engine.bpm(clock.get_tempo())
  clock.run(function()
    local t = clock.get_tempo()
    while true do
      clock.sleep(1)
      local n = clock.get_tempo()
      if n ~= t then t = n; engine.bpm(n) end
    end
  end)
  local ui = metro.init(function()
    local now = util.time()
    if flash_l and now >= flash_until then flash_l = nil; dirty = true end
    if note_flash > 0 and now >= note_flash then note_flash = 0; dirty = true end
    if dirty then dirty = false; redraw() end
  end, 1 / 20)
  ui:start()
end

function cleanup()
  engine.panic()
end
