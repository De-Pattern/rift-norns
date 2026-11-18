-- Run rift-norns.lua against a mock of the norns Lua API, and record every screen for
-- render_screens.py (which replays them with cairo and norns' font, and checks them).
--   lua5.3 ignore/dev/test/mock_rift.lua <script dir> <dump file>
-- It plays through every machine, every page, every control, LIGATURE's wiring for every
-- patch, patch browsing, machine select, MIDI notes, note-offs and CCs.
local dir = arg[1] or "."
local dump = arg[2] and io.open(arg[2], "w")
local here = debug.getinfo(1, "S").source:sub(2):match("(.*/)") or "./"
local FONT = dofile(here .. "font_norns.lua")

local calls = {}
local function log(...) calls[#calls + 1] = table.concat({...}, " ") end

engine = setmetatable({}, {__index = function(_, k)
  return function(...) local a = {...}; for i = 1, #a do a[i] = tostring(a[i]) end; log("engine." .. k, table.unpack(a)) end
end})

-- params: just enough of norns' ParamSet
local P = {list = {}, by = {}}
local function addp(id, name, kind, spec)
  assert(not P.by[id], "duplicate param " .. id)
  local p = {id = id, name = name, kind = kind, value = spec.default, spec = spec}
  P.list[#P.list + 1] = p
  P.by[id] = p
  return p
end
params = {}
function params:add_separator() end
function params:add_group() end
function params:hide() end
function params:add_option(id, name, opts, d) addp(id, name, "option", {default = d or 1, min = 1, max = #opts}) end
function params:add_number(id, name, mn, mx, d, fmt) local p = addp(id, name, "number", {default = d, min = mn, max = mx}); p.fmt = fmt end
function params:add_control(id, name, cs) addp(id, name, "control", {default = cs.default or 0, min = cs.minval, max = cs.maxval}) end
function params:add_trigger(id, name) addp(id, name, "trigger", {default = 0}) end
function params:set_action(id, f) P.by[id].action = f end
function params:get(id) return assert(P.by[id], id).value end
function params:set(id, v)
  local p = assert(P.by[id], id)
  if p.spec.min then v = math.max(p.spec.min, math.min(p.spec.max, v)) end
  if p.value ~= v then p.value = v; if p.action then p.action(v) end end
end
function params:delta(id, d) params:set(id, P.by[id].value + d) end
function params:default() end
function params:bang() for _, p in ipairs(P.list) do if p.action and p.kind ~= "trigger" then p.action(p.value) end end end

controlspec = {new = function(mn, mx, w, s, d) return {minval = mn, maxval = mx, default = d} end}
controlspec.PAN = {minval = -1, maxval = 1, default = 0}
local t = 0
util = {time = function() return t end, clamp = function(x, a, b) return math.max(a, math.min(b, x)) end}
clock = {run = function() end, sleep = function() end, get_tempo = function() return 120 end}
metro = {init = function(f) return {start = function() end, f = f} end}
local mdev = {}
midi = {connect = function() return mdev end, to_msg = function(d) return d end}
_path = {code = "/home/we/dust/code/"}
norns = {state = {path = "/home/we/dust/code/rift-norns/"}}
include = function(p) return dofile(dir .. "/" .. p .. ".lua") end

local function extents(s)
  local w, n = 0, #s
  for i = 1, n do
    local m = FONT[s:byte(i)] or {4, 5, 0}
    if i < n then w = w + m[2] else w = w + m[3] + m[1] end
  end
  return w, 6
end
local ops = {}
local function rec(...) local a = {...}; for i = 1, #a do a[i] = tostring(a[i]) end; ops[#ops + 1] = table.concat(a, "\t") end
screen = {
  clear = function() ops = {} end,
  update = function() end,
  text_extents = function(s) return extents(s) end,
}
for _, k in ipairs({"level", "move", "line", "rect", "arc", "curve", "stroke", "fill", "text", "text_center",
                    "text_right", "close", "line_width", "font_face", "font_size", "circle"}) do
  screen[k] = function(...) rec(k, ...) end
end
local function snap(name)
  redraw()
  if dump then
    dump:write("frame\t" .. name .. "\n")
    for _, o in ipairs(ops) do dump:write(o .. "\n") end
  end
end

-- installed: the UGens' VERSION matches
local real_open = io.open
io.open = function(path, mode)
  if path:match("Extensions/RiftNorns/VERSION") and (mode or "r") == "r" then
    return {read = function() return "0.2" end, close = function() end}
  end
  return real_open(path, mode)
end

dofile(dir .. "/rift-norns.lua")
init()
local MACHINES = dofile(dir .. "/lib/machines.lua")

local function set_machine(i)
  key(2, 1)
  for _ = 1, 8 do enc(1, -1) end
  for _ = 1, i - 1 do enc(1, 1) end
  key(2, 0)
end

for mi_, m in ipairs(MACHINES) do
  set_machine(mi_)
  t = t + 5
  snap(m.id .. "_p1")
  local npages = #m.pages + (m.id == "ligature" and 1 or 0)
  for pg = 1, npages do
    for _ = 1, 8 do enc(1, -1) end
    for _ = 1, pg - 1 do enc(1, 1) end
    t = t + 5
    snap(m.id .. "_page" .. pg)
    local ncells = (m.id == "ligature" and pg == 2) and 12 or 8
    for s = 1, ncells do
      for _ = 1, 12 do enc(2, -1) end
      for _ = 1, s - 1 do enc(2, 1) end
      snap(m.id .. "_page" .. pg .. "_sel" .. s)
      if not (m.id == "ligature" and pg == 2) then
        enc(3, 7); snap(m.id .. "_page" .. pg .. "_turn" .. s)
        enc(3, 200); enc(3, -400); snap(m.id .. "_page" .. pg .. "_min" .. s)
        enc(3, 64)
        t = t + 5
      end
    end
  end
  -- MIDI on this machine: a note, its off, a CC on every control
  mdev.event({type = "note_on", note = 64, vel = 100, ch = 1})
  mdev.event({type = "note_off", note = 64, vel = 0, ch = 1})
  for _, c in ipairs(m.order) do mdev.event({type = "cc", cc = m.ctl[c].cc, val = 99, ch = 1}) end
  snap(m.id .. "_after_cc")
end

-- LIGATURE: every patch's wiring, each block selected
set_machine(4)
for _ = 1, 8 do enc(1, -1) end
enc(1, 1)  -- WIRING
for p = 1, #MACHINES.wiring do
  for _ = 1, 20 do enc(3, -1) end
  for _ = 1, p - 1 do enc(3, 1) end
  t = t + 5
  for s = 1, 12 do
    for _ = 1, 12 do enc(2, -1) end
    for _ = 1, s - 1 do enc(2, 1) end
    snap("wiring_" .. p .. "_sel" .. s)
  end
  t = t + 5
  snap("wiring_" .. p)
end

-- FASCIA's patches: hold K2, browse, let go
set_machine(1)
key(2, 1); for _ = 1, 7 do enc(3, 1) end; snap("fascia_browse"); key(2, 0)
assert(params:get("rn_patch_fascia") == 8, "FASCIA patch 8 loaded")
key(3, 1); key(3, 0)
if dump then dump:close() end
local n = {}
for _, c in ipairs(calls) do local k = c:match("^(engine%.[%w_]+)"); n[k] = (n[k] or 0) + 1 end
local s = {}
for k, v in pairs(n) do s[#s + 1] = k .. " " .. v end
table.sort(s)
print("ok: " .. table.concat(s, ", "))
