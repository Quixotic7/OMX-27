-- omx_mod/lib/mod.lua
-- OMX-27 <-> monome norns bridge, as a norns mod.
--
-- PHASE 1: SCREEN MIRROR. The OMX-27 streams its 128x32 OLED over USB-MIDI SysEx;
-- the mod renders it 1:1, pixel-exact, centered on the norns 128x64 screen.
--
-- Transport (lossy-tolerant, atomic frames):
--   * the OMX sends changed 64-byte chunks, each tagged with a frame id + checksum,
--     then a FRAME_END listing the chunks in that pass.
--   * we STAGE chunks and only present a pass once every chunk it lists has
--     arrived (so a frame never appears band-by-band); anything missing or
--     corrupt is re-requested (CMD_REQ) and presented when it lands.
--
-- Mirror mode: off / auto (show only while touching the OMX) / forced (always).
-- Safety: never draws over the norns screen unless a live frame is present; the
-- metro is pcall-guarded so it can't wedge matron.
-- Mod-menu controls (E2/E3/K3): E2 select row (row 1 = tab), E3 change, K2 back, K3 cycle mode.

local mod = require 'core/mods'
local util = require 'util'
local tab = require 'tabutil'

local NAME = mod.this_name or "omx_mod"
local sx = require(NAME .. '/lib/omx_sysex')

local unpack = table.unpack or unpack

-- ---------------------------------------------------------------------------
-- config

local ROOT = _path.data .. NAME .. "/"
local STATE_FILE = ROOT .. "state.lua"
local FPS = 15                  -- norns redraws ~15Hz; keeps our CPU load low
local Y_OFF = (64 - sx.OMX_H) // 2
local STALE_TICKS = FPS * 3     -- drop the mirror if no frames for ~3s (OMX unplugged)
local ACTIVITY_TICKS = FPS * 3  -- "auto": keep showing ~3s after last OMX input
local REQ_TICKS = 5             -- re-request missing chunks after ~330ms
local REQ_MAX = 3               -- then give up on the pass (next pass supersedes)
local PRESENT_FALLBACK = 9      -- ~600ms: present what we have rather than nothing

local MODES = { "off", "auto", "forced" }

-- ---------------------------------------------------------------------------
-- state

local state = { port = 1, mode = 2, pace = 15 } -- default: auto, 15ms between chunks

local dev, our_event, script_event, sysex_acc

local fb = {}                 -- presented framebuffer (512 bytes, SSD1306 page layout)
local has_frame = false
local poke_dirty = true
local need_draw = true
local poke_str = nil
local rx_count = 0
local last_rx_hb = 0
local last_activity_hb = -1e9

-- staging: latest chunk data per slot + the frame id it belongs to
local stage = {}              -- stage[c] = array of 64 bytes
local stage_fid = {}          -- stage_fid[c] = fid
local pend = nil              -- pending FRAME_END { fid=, mask=, since=, reqs= }
local last_req_hb = -1e9

-- debug counters (dust/data/omx_mod/debug.log)
local ev_count, feed_done, onsx, rej, badck, ends, presented, reqs = 0, 0, 0, 0, 0, 0, 0, 0
local rejlen = {} -- histogram of rejected message lengths

local draw_metro
local heartbeat = 0
local streaming = false

local saved_redraw, saved_script_redraw
local takeover_active = false

local params_present = false
local menu_active = false
local sync_params

local TABS = { "info", "screen" }
local menu_tab = 1
local menu_sel = 1
local menu = {}

local function noop_redraw() end
local function mode_name() return MODES[state.mode] or "off" end

local function rows_for_tab()
  if TABS[menu_tab] == "info" then return { "tab", "port", "mirror", "pace" } end
  return { "tab" }
end

-- ---------------------------------------------------------------------------
-- fs / persistence / debug

local function ensure_dir(p)
  if not util.file_exists(p) then pcall(function() util.make_dir(p) end) end
end

local function save_state()
  ensure_dir(ROOT)
  pcall(function() tab.save({ port = state.port, mode = state.mode, pace = state.pace }, STATE_FILE) end)
end

local function load_state()
  local d
  if util.file_exists(STATE_FILE) then pcall(function() d = tab.load(STATE_FILE) end) end
  d = d or {}
  state.port = d.port or 1
  state.mode = d.mode or 2
  state.pace = d.pace or 15
  if state.mode < 1 or state.mode > #MODES then state.mode = 2 end
  state.pace = util.clamp(state.pace, 1, 127)
end

local function init_fb()
  for i = 1, sx.FB_BYTES do fb[i] = 0 end
end

local function menu_redraw()
  if menu_active then pcall(function() mod.menu.redraw() end) end
end

local DBG_FILE = ROOT .. "debug.log"
local function dbg(s)
  pcall(function()
    local f = io.open(DBG_FILE, "a")
    if f then f:write(tostring(s) .. "\n"); f:close() end
  end)
end

-- ---------------------------------------------------------------------------
-- midi out

local function send(cmd, payload)
  if dev then pcall(function() dev:send(sx.build(cmd, payload)) end) end
end

local function send_pace() send(sx.CMD_PACE, { state.pace }) end
local function send_enable(on)
  if on then send_pace() end -- always (re)assert pacing alongside enable
  send(sx.CMD_MIRROR_EN, { on and 1 or 0 })
end

-- 16-bit chunk masks (bit c = chunk c)
local function mask_has(mask, c) return (mask & (1 << c)) ~= 0 end
local function mask_add(mask, c) return mask | (1 << c) end

-- ---------------------------------------------------------------------------
-- frame presentation

local function present_pass(mask7)
  for c = 0, sx.CHUNK_COUNT - 1 do
    if mask_has(mask7, c) and stage[c] then
      local base = c * sx.CHUNK_BYTES
      local src = stage[c]
      for x = 1, sx.CHUNK_BYTES do fb[base + x] = src[x] end
    end
  end
  has_frame = true
  poke_dirty = true
  need_draw = true
  presented = presented + 1
  last_rx_hb = heartbeat
end

-- which chunks listed in `mask7` for `fid` are still missing from staging?
local function missing_mask(fid, mask7)
  local m = 0
  for c = 0, sx.CHUNK_COUNT - 1 do
    if mask_has(mask7, c) and stage_fid[c] ~= fid then m = mask_add(m, c) end
  end
  return m
end

local function try_complete()
  if not pend then return end
  local m = missing_mask(pend.fid, pend.mask)
  if m == 0 then
    present_pass(pend.mask)
    pend = nil
  end
end

local function request_missing()
  if not pend then return end
  -- waited long enough: show the chunks we do have instead of nothing
  if (heartbeat - pend.since) >= PRESENT_FALLBACK then
    local have = 0
    for c = 0, sx.CHUNK_COUNT - 1 do
      if mask_has(pend.mask, c) and stage_fid[c] == pend.fid then have = mask_add(have, c) end
    end
    if have ~= 0 then present_pass(have) end
    pend = nil
    return
  end
  if (heartbeat - last_req_hb) < REQ_TICKS then return end
  if pend.reqs >= REQ_MAX then return end
  local m = missing_mask(pend.fid, pend.mask)
  if m ~= 0 then
    send(sx.CMD_REQ, sx.mask_to_bytes(m))
    reqs = reqs + 1
    pend.reqs = pend.reqs + 1
    last_req_hb = heartbeat
  end
end

-- ---------------------------------------------------------------------------
-- incoming sysex

local function on_sysex(data)
  onsx = onsx + 1
  local cmd = sx.match(data)
  if cmd == sx.CMD_STATUS then
    if data[6] == 1 then last_activity_hb = heartbeat end
    return
  elseif cmd == sx.CMD_FRAME_END then
    ends = ends + 1
    local fid = data[6]
    if not fid or not data[7] then return end
    local mask = sx.mask_from_bytes(data[7], data[8], data[9])
    pend = { fid = fid, mask = mask, since = heartbeat, reqs = 0 }
    try_complete()
    if pend then
      -- something's missing: ask right away (ignore the REQ spacing once)
      last_req_hb = -1e9
      request_missing()
    end
    return
  elseif cmd ~= sx.CMD_FRAME then
    return
  end

  local chunk, fid, cksum = data[6], data[7], data[8]
  if not chunk or chunk < 0 or chunk >= sx.CHUNK_COUNT or not fid or not cksum then return end
  local decoded = sx.decode7bit(data, 9, #data - 1)
  if #decoded ~= sx.CHUNK_BYTES then
    rej = rej + 1
    rejlen[#data] = (rejlen[#data] or 0) + 1
    return
  end
  local sum = 0
  for i = 1, sx.CHUNK_BYTES do sum = sum + decoded[i] end
  if (sum & 0x7f) ~= cksum then badck = badck + 1; return end

  stage[chunk] = decoded
  stage_fid[chunk] = fid
  rx_count = rx_count + 1
  last_rx_hb = heartbeat
  if pend and pend.fid == fid then try_complete() end
end

-- debug: loss-pattern instrumentation
local rt_mid = 0      -- realtime bytes seen while a sysex was open
local restart_mid = 0 -- F0 seen while a sysex was still open (lost F7 = truncated tail)
local evlen = {}      -- histogram of event sizes

-- reassemble whole F0..F7 messages; ignore MIDI real-time bytes (>= 0xF8)
local function feed(data)
  local n = #data
  evlen[n] = (evlen[n] or 0) + 1
  for _, b in ipairs(data) do
    if b == 0xf0 then
      if sysex_acc then restart_mid = restart_mid + 1 end
      sysex_acc = { 0xf0 }
    elseif b >= 0xf8 then
      if sysex_acc then rt_mid = rt_mid + 1 end
    elseif sysex_acc then
      sysex_acc[#sysex_acc + 1] = b
      if b == 0xf7 then
        feed_done = feed_done + 1
        on_sysex(sysex_acc)
        sysex_acc = nil
      end
    end
  end
end

local function hist(t)
  local keys = {}
  for k in pairs(t) do keys[#keys + 1] = k end
  table.sort(keys)
  local out = {}
  for _, k in ipairs(keys) do out[#out + 1] = k .. ":" .. t[k] end
  return table.concat(out, " ")
end

our_event = function(data)
  ev_count = ev_count + 1
  feed(data)
  if script_event then script_event(data) end
end

-- find the vport the OMX enumerated on (USB product name "omx-27-v3")
local function find_omx_port()
  if not midi or not midi.vports then return nil end
  for i = 1, #midi.vports do
    local vp = midi.vports[i]
    local name = vp and vp.name
    if type(name) == "string" and name:lower():find("omx") then return i end
  end
  return nil
end

local function connect()
  if not midi then return end
  local found = find_omx_port()
  if found then state.port = found end
  dev = midi.connect(state.port)
  local cur = dev.event
  if cur ~= our_event then script_event = cur end
  dev.event = our_event
end

local function update_stream()
  local want = (mode_name() ~= "off") or (menu_active and TABS[menu_tab] == "screen")
  if want ~= streaming then
    streaming = want
    send_enable(want)
  end
end

-- ---------------------------------------------------------------------------
-- render (OMX draws rotated 180 via setRotation(2); flip both axes to match)

local function build_poke()
  local rows = {}
  for r = 0, sx.OMX_H - 1 do
    local sy = (sx.OMX_H - 1) - r
    local base = (sy >> 3) * sx.OMX_W
    local bit = sy & 7
    local rowt = {}
    for x = 0, sx.OMX_W - 1 do
      local sxp = (sx.OMX_W - 1) - x
      rowt[x + 1] = (((fb[base + sxp + 1] or 0) >> bit) & 1) == 1 and 15 or 0
    end
    rows[#rows + 1] = string.char(unpack(rowt))
  end
  poke_str = table.concat(rows)
  poke_dirty = false
end

local function blit_mirror()
  if not has_frame then return end
  if poke_dirty then build_poke() end
  screen.poke(0, Y_OFF, sx.OMX_W, sx.OMX_H, poke_str)
end

-- ---------------------------------------------------------------------------
-- screen takeover (override both global redraw and norns.script.redraw)

local function set_takeover(active)
  if active and not takeover_active then
    if type(redraw) == "function" and redraw ~= noop_redraw then saved_redraw = redraw end
    if norns and norns.script and norns.script.redraw and norns.script.redraw ~= noop_redraw then
      saved_script_redraw = norns.script.redraw
    end
    redraw = noop_redraw
    if norns and norns.script then norns.script.redraw = noop_redraw end
    takeover_active = true
    need_draw = true
  elseif (not active) and takeover_active then
    takeover_active = false
    if norns and norns.script and saved_script_redraw then norns.script.redraw = saved_script_redraw end
    if saved_redraw and saved_redraw ~= noop_redraw then
      redraw = saved_redraw
    elseif saved_script_redraw and saved_script_redraw ~= noop_redraw then
      redraw = saved_script_redraw
    end
    saved_redraw = nil; saved_script_redraw = nil
    -- repaint the script only in play mode; never draw over a norns menu
    if _menu and _menu.mode == false then
      if type(redraw) == "function" then pcall(redraw) end
      pcall(function() screen.update() end)
    end
  end
end

local function want_play_mirror()
  if not has_frame then return false end
  local m = mode_name()
  if m == "off" then return false end
  if m == "forced" then return true end
  return (heartbeat - last_activity_hb) < ACTIVITY_TICKS
end

-- ---------------------------------------------------------------------------
-- metro (guarded; cheap when idle)

local function tick()
  local ok, err = pcall(function()
    heartbeat = heartbeat + 1
    if type(_menu) ~= "table" then return end

    if streaming and heartbeat % FPS == 0 then
      if not has_frame then connect() end
      send_enable(true)
    end
    request_missing() -- re-ask for anything still missing from the pending pass

    if has_frame and (heartbeat - last_rx_hb) > STALE_TICKS then
      has_frame = false
      poke_dirty = true
    end

    local play = (_menu.mode == false)
    local want = play and want_play_mirror()
    set_takeover(want)

    if want then
      if need_draw then
        screen.clear(); blit_mirror(); screen.update()
        need_draw = false
      end
    elseif (not play) and menu_active and TABS[menu_tab] == "screen" then
      if need_draw then menu.redraw(); need_draw = false end
    end

    if heartbeat % (FPS * 2) == 0 then
      dbg("tick ev=" .. ev_count .. " sx=" .. feed_done .. " rx=" .. rx_count .. " rej=" .. rej ..
        " badck=" .. badck .. " ends=" .. ends .. " pres=" .. presented .. " reqs=" .. reqs ..
        " pend=" .. tostring(pend ~= nil) .. " frame=" .. tostring(has_frame) ..
        " rt_mid=" .. rt_mid .. " restart_mid=" .. restart_mid ..
        " | rejlen{" .. hist(rejlen) .. "} evlen{" .. hist(evlen) .. "}")
    end
  end)
  if not ok then print("omx_mod tick error: " .. tostring(err)) end
end

local function start_metro()
  if not metro then return end
  if not draw_metro then draw_metro = metro.init(tick, 1 / FPS, -1) end
  if draw_metro then
    draw_metro.event = tick
    draw_metro.time = 1 / FPS
    draw_metro:start()
  end
end

-- ---------------------------------------------------------------------------
-- mode control

local function set_mode(m)
  state.mode = ((m - 1) % #MODES) + 1
  save_state()
  connect()
  update_stream()
  if mode_name() == "off" then set_takeover(false) end
  sync_params()
  menu_redraw()
end

-- ---------------------------------------------------------------------------
-- params

sync_params = function()
  if not params_present then return end
  pcall(function()
    params:set("omx_mode", state.mode, true)
    params:set("omx_port", state.port, true)
    params:set("omx_pace", state.pace, true)
  end)
end

local function add_params()
  if not pcall(function() params:add_group("omx_mod", "omx_mod", 3) end) then
    pcall(function() params:add_group("omx_mod", 3) end)
  end
  params:add_option("omx_mode", "OMX mirror", MODES, state.mode)
  params:set_action("omx_mode", function(v)
    if v ~= state.mode then set_mode(v) end
  end)
  params:add { type = "number", id = "omx_port", name = "OMX midi port", min = 1, max = 16, default = state.port }
  params:set_action("omx_port", function(v)
    if v ~= state.port then
      state.port = v; save_state(); connect(); update_stream()
    end
  end)
  params:add { type = "number", id = "omx_pace", name = "OMX chunk pace (ms)", min = 1, max = 127, default = state.pace }
  params:set_action("omx_pace", function(v)
    if v ~= state.pace then
      state.pace = v; save_state(); send_pace()
    end
  end)
  params_present = true
end

-- ---------------------------------------------------------------------------
-- mod hooks

mod.hook.register("system_post_startup", "omx-startup", function()
  ensure_dir(ROOT)
  load_state()
  init_fb()
  connect()
  update_stream()
  start_metro()
end)

mod.hook.register("script_pre_init", "omx-pre-init", function()
  add_params()
end)

mod.hook.register("script_post_init", "omx-post-init", function()
  draw_metro = nil -- script.clear() freed metros
  connect()
  update_stream()
  start_metro()
  sync_params()
end)

mod.hook.register("script_post_cleanup", "omx-post-cleanup", function()
  params_present = false
  takeover_active = false
  saved_redraw = nil
  saved_script_redraw = nil
end)

mod.hook.register("system_pre_shutdown", "omx-shutdown", function()
  send_enable(false)
end)

-- ---------------------------------------------------------------------------
-- mod menu

function menu.init()
  menu_active = true
  menu_sel = 1
  need_draw = true
  update_stream()
end

function menu.deinit()
  menu_active = false
  update_stream()
end

function menu.key(n, z)
  if z ~= 1 then return end
  if n == 2 then
    mod.menu.exit()
  elseif n == 3 then
    set_mode(state.mode + 1); menu_redraw()
  end
end

function menu.enc(n, d)
  local rows = rows_for_tab()
  if n == 2 then
    menu_sel = util.clamp(menu_sel + d, 1, #rows)
  elseif n == 3 then
    local row = rows[menu_sel]
    if row == "tab" then
      menu_tab = menu_tab + (d > 0 and 1 or -1)
      if menu_tab < 1 then menu_tab = #TABS elseif menu_tab > #TABS then menu_tab = 1 end
      menu_sel = 1
      need_draw = true
      update_stream()
    elseif row == "port" then
      state.port = util.clamp(state.port + d, 1, 16)
      save_state(); connect(); update_stream()
    elseif row == "mirror" then
      set_mode(state.mode + (d > 0 and 1 or -1))
    elseif row == "pace" then
      state.pace = util.clamp(state.pace + d, 1, 127)
      save_state(); send_pace(); sync_params()
    end
  end
  menu_redraw()
end

local function draw_row(idx, y, label, val)
  screen.level(menu_sel == idx and 15 or 4)
  screen.move(0, y); screen.text(label)
  screen.move(127, y); screen.text_right(tostring(val))
end

function menu.redraw()
  screen.clear()
  if TABS[menu_tab] == "info" then
    screen.level(15); screen.move(0, 10); screen.text("OMX-27 norns link")
    draw_row(1, 26, "tab", "info")
    draw_row(2, 36, "midi port", state.port)
    draw_row(3, 46, "mirror", mode_name())
    draw_row(4, 56, "pace ms", state.pace)
    screen.level(has_frame and 6 or 3); screen.move(0, 64)
    screen.text(has_frame and ("rx " .. rx_count .. " rej " .. rej) or "no frames yet")
  else
    draw_row(1, 8, "tab", "screen")
    if has_frame then
      blit_mirror()
    else
      screen.level(4); screen.move(64, 34); screen.text_center("waiting for OMX-27...")
    end
    screen.level(3); screen.move(127, 8); screen.text_right(has_frame and ("rx " .. rx_count) or "no rx")
  end
  screen.update()
end

mod.menu.register(mod.this_name, menu)
