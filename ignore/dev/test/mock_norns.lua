-- Run rift-norns.lua against a mock of the norns Lua API: every page drawn, every control turned,
-- patches browsed and loaded, MIDI notes and CCs. lua5.3 ignore/dev/test/mock_norns.lua .
local dir = arg[1] or "."
local calls = {}
local function log(...) calls[#calls + 1] = table.concat({...}, " ") end

-- engine
engine = setmetatable({}, {__index = function(_, k)
  return function(...) local a = {...}; for i = 1, #a do a[i] = tostring(a[i]) end; log("engine." .. k, table.unpack(a)) end
end})

-- params (just enough of norns' ParamSet)
local P = {list = {}, by = {}}
local function addp(id, name, kind, spec)
  local p = {id = id, name = name, kind = kind, value = spec.default, spec = spec}
  function p:get() return self.value end
  P.list[#P.list + 1] = p
  P.by[id] = p
  return p
end
params = {}
function params:add_separator() end
function params:add_group() end
function params:add_option(id, name, opts, d) addp(id, name, "option", {default = d or 1, min = 1, max = #opts, opts = opts}) end
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
function params:delta(id, d) local p = P.by[id]; params:set(id, p.value + d) end
function params:default() end
function params:bang() for _, p in ipairs(P.list) do if p.action and p.kind ~= "trigger" then p.action(p.value) end end end

controlspec = {new = function(mn, mx, w, s, d) return {minval = mn, maxval = mx, default = d} end}
controlspec.PAN = {minval = -1, maxval = 1, default = 0}
local t = 0
util = {time = function() return t end, clamp = function(x, a, b) return math.max(a, math.min(b, x)) end}
local texts = {}
local cx, cy = 0, 0
screen = setmetatable({}, {__index = function(_, k)
  if k == "move" then return function(x, y) cx, cy = x, y end end
  if k == "text" or k == "text_center" or k == "text_right" then
    return function(s)
      assert(type(s) == "string", "screen." .. k .. " given " .. type(s))
      texts[#texts + 1] = {k, s, cx, cy}
    end
  end
  return function() end
end})
metro = {init = function(f) return {start = function() end, f = f} end}
local mdev = {}
midi = {connect = function() return mdev end,
  to_msg = function(d) return d end}
_path = {code = "/home/we/dust/code/"}
norns = {state = {path = "/home/we/dust/code/rift-norns/"}}
include = function(p) return dofile(dir .. "/" .. p .. ".lua") end
-- the UGen is installed (its VERSION matches)
local real_open = io.open
io.open = function(path, mode)
  if path:match("Extensions/RiftNorns/VERSION") and (mode or "r") == "r" then
    return {read = function() return "0.1" end, close = function() end}
  end
  return real_open(path, mode)
end

dofile(dir .. "/rift-norns.lua")
assert(engine and init, "script loaded")
init()

-- what a CCed text would measure in the 04B_03 font: ~4 px a char + spacing; widest cells
local function width(s) return #s * 4 end

for page = 1, 4 do
  for _ = 1, 3 do enc(1, -1) end
  for _ = 1, page - 1 do enc(1, 1) end
  for sel = 1, 8 do
    for _ = 1, 8 do enc(2, -1) end
    for _ = 1, sel - 1 do enc(2, 1) end
    enc(3, 5); enc(3, -3); enc(3, 200); enc(3, -400)
    texts = {}
    redraw()
    for _, tx in ipairs(texts) do
      local kind, s, x = tx[1], tx[2], tx[3]
      local w = width(s)
      local left = kind == "text_center" and x - w / 2 or (kind == "text_right" and x - w or x)
      assert(left >= 0 and left + w <= 128, "text off screen: " .. s)
    end
  end
end
-- patches: hold K2, browse, let go
key(2, 1); for _ = 1, 7 do enc(3, 1) end; key(2, 0)
assert(params:get("fsc_patch") == 8, "patch 8 loaded")
key(2, 1); for _ = 1, 40 do enc(3, 1) end; key(2, 0)
assert(params:get("fsc_patch") == 31, "last patch loaded")
key(3, 1); key(3, 0)
-- MIDI
mdev.event({type = "note_on", note = 64, vel = 100, ch = 1})
mdev.event({type = "cc", cc = 42, val = 80, ch = 1})       -- BODA
assert(params:get("fsc_BODA") == math.min((80 * 9) >> 7, 8) + 1)
mdev.event({type = "cc", cc = 47, val = 10, ch = 1})       -- TUNE
assert(params:get("fsc_TUNE") == 10)
t = 5; texts = {}; redraw()
cleanup()

local n_note, n_param = 0, 0
for _, c in ipairs(calls) do
  if c:match("^engine.note_on") then n_note = n_note + 1 end
  if c:match("^engine.param") then n_param = n_param + 1 end
end
print(("ok: %d engine calls (%d note_on, %d param)"):format(#calls, n_note, n_param))
for i = #calls - 6, #calls do print("  " .. calls[i]) end
