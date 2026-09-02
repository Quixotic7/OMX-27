-- omx27.lua
-- Standalone library for driving an OMX-27 in REMOTE mode from a norns script —
-- the OMX is to norns as a monome grid is to norns: the script owns all 27
-- RGB LEDs and the 128x32 screen, and receives every key/encoder/pot event.
-- (Self-contained: no omx_mod required; talks the same SysEx protocol.)
--
--   local omx = include('omx_remote/lib/omx27')
--
--   function init()
--     omx.connect()                 -- finds the OMX vport, enters REMOTE mode
--     omx.key = function(n, ev) end -- n 0-26 (0 = AUX); ev "down"|"up"|"hold"|"quick"
--     omx.enc = function(d) end     -- signed encoder delta
--     omx.enc_btn = function(z) end -- encoder button 1/0 (do your own hold timing)
--     omx.pot = function(n, v, hires) end -- n 0-4, v 0-127, hires 0-16383
--   end
--
--   -- LEDs (grid-style: stage then show; show() sends only what changed)
--   omx.led(n, r, g, b)   -- n 0-26, channels 0-127
--   omx.led_all(r, g, b)
--   omx.led_show()
--
--   -- screen: draw a 128x32 region with the normal norns screen API, then
--   omx.screen_send(x, y) -- peek the region at (x,y) (default 0,0) and ship it.
--   -- Flow-controlled: one frame in flight (the OMX acks each frame); a call
--   -- while unacked is dropped (returns false) so latency can't accumulate.
--
-- Exit REMOTE mode on the device with AUX + encoder HOLD.

local M = {}

-- script callbacks
M.key = nil      -- (n, ev)
M.enc = nil      -- (d)
M.enc_btn = nil  -- (z)
M.pot = nil      -- (n, v, hires)
M.status = nil   -- ("active"|"left")

-- peek brightness threshold: norns pixels are 0-15, >= this counts as lit
M.screen_threshold = 8

-- ---------------------------------------------------------------------------
-- protocol constants (envelope: F0 7D 00 00 <cmd> <payload...> F7)
-- ---------------------------------------------------------------------------
local CMD_INPUT     = 0x51 -- OMX -> norns input events (also norns -> OMX injection)
local CMD_STATUS    = 0x52
local CMD_LED       = 0x59
local CMD_LED_BATCH = 0x5A
local CMD_LED_SHOW  = 0x5B
local CMD_DRAW      = 0x5C
local CMD_DRAW_UPD  = 0x5D

local STATUS_FRAME_ACK = 0x04
local MODE_REMOTE = 9
local KEY_EVENTS = { [0] = "up", [1] = "down", [2] = "hold", [3] = "quick" }

local OMX_W, OMX_H = 128, 32
local FB_BYTES = 512
local CHUNK_BYTES = 32
local CHUNK_COUNT = 16

local dev = nil
local sysex_acc = nil
local frame_pending = false
local frame_sent_at = 0

-- LED staging: 27 x {r,g,b}, plus what the device last received (for diffs)
local led_stage = {}
local led_sent = {}
for i = 0, 26 do led_stage[i] = { 0, 0, 0 } end

-- last frame chunks sent (16 x 32-byte arrays), for chunk-level diffing
local last_chunks = nil

-- ---------------------------------------------------------------------------
-- codec
-- ---------------------------------------------------------------------------
local function build(cmd, payload)
  local msg = { 0xf0, 0x7d, 0x00, 0x00, cmd }
  if payload then
    for _, b in ipairs(payload) do msg[#msg + 1] = b & 0x7f end
  end
  msg[#msg + 1] = 0xf7
  return msg
end

-- raw 8-bit bytes -> 7-bit-safe stream (1 hi-bits byte + up to 7 lows per group)
local function encode7bit(src)
  local out = {}
  local n = #src
  local i = 1
  while i <= n do
    local m = math.min(7, n - i + 1)
    local hi = 0
    for j = 0, m - 1 do
      if (src[i + j] & 0x80) ~= 0 then hi = hi | (1 << j) end
    end
    out[#out + 1] = hi
    for j = 0, m - 1 do out[#out + 1] = src[i + j] & 0x7f end
    i = i + m
  end
  return out
end

-- ---------------------------------------------------------------------------
-- transport
-- ---------------------------------------------------------------------------
local function send(cmd, payload)
  if dev then pcall(function() dev:send(build(cmd, payload)) end) end
end

local function on_sysex(data)
  if not (data[2] == 0x7d and data[3] == 0x00 and data[4] == 0x00) then return end
  local cmd = data[5]
  if cmd == CMD_STATUS then
    local s = data[6]
    if s == STATUS_FRAME_ACK then
      frame_pending = false
    elseif s == 0x02 and M.status then M.status("active")
    elseif s == 0x03 and M.status then M.status("left")
    end
  elseif cmd == CMD_INPUT then
    local sub = data[6]
    if sub == 0x00 and M.key then
      M.key(data[7], KEY_EVENTS[data[8]] or data[8])
    elseif sub == 0x01 and M.enc then
      local amt = data[8] or 1
      M.enc(data[7] == 2 and amt or -amt)
    elseif sub == 0x02 and M.enc_btn then
      M.enc_btn(data[7])
    elseif sub == 0x03 and M.pot then
      M.pot(data[7], data[8], ((data[9] or 0) << 7) | (data[10] or 0))
    end
  end
end

-- reassemble whole F0..F7 messages; ignore MIDI real-time bytes (>= 0xF8)
local function feed(data)
  for _, b in ipairs(data) do
    if b == 0xf0 then
      sysex_acc = { 0xf0 }
    elseif b >= 0xf8 then -- realtime, skip
    elseif sysex_acc then
      sysex_acc[#sysex_acc + 1] = b
      if b == 0xf7 then
        on_sysex(sysex_acc)
        sysex_acc = nil
      end
    end
  end
end

local function find_omx_port()
  if not midi or not midi.vports then return nil end
  for i = 1, #midi.vports do
    local vp = midi.vports[i]
    local name = vp and vp.name
    if type(name) == "string" and name:lower():find("omx") then return i end
  end
  return nil
end

-- Connect and enter REMOTE mode. `port` optional (auto-detects the OMX vport).
-- Returns true on success.
function M.connect(port)
  port = port or find_omx_port()
  if not port then
    print("omx27: no OMX midi vport found")
    return false
  end
  dev = midi.connect(port)
  dev.event = feed
  frame_pending = false
  last_chunks = nil
  for i = 0, 26 do led_sent[i] = nil end
  -- input-injection MODE command switches the OMX into REMOTE mode
  send(CMD_INPUT, { 0x05, MODE_REMOTE })
  print("omx27: connected on midi port " .. port)
  return true
end

function M.disconnect()
  if dev then dev.event = nil end
  dev = nil
end

function M.connected()
  return dev ~= nil
end

-- ---------------------------------------------------------------------------
-- LEDs
-- ---------------------------------------------------------------------------
function M.led(n, r, g, b)
  if n >= 0 and n <= 26 then
    led_stage[n] = { r & 0x7f, g & 0x7f, b & 0x7f }
  end
end

function M.led_all(r, g, b)
  for i = 0, 26 do led_stage[i] = { r & 0x7f, g & 0x7f, b & 0x7f } end
end

local function led_eq(a, b)
  return a and b and a[1] == b[1] and a[2] == b[2] and a[3] == b[3]
end

-- Send staged LEDs that differ from what the device has, then latch.
function M.led_show()
  local changed = {}
  for i = 0, 26 do
    if not led_eq(led_stage[i], led_sent[i]) then changed[#changed + 1] = i end
  end
  if #changed == 0 then return end
  if #changed > 8 then
    local payload = { 0, 27 } -- start, count
    for i = 0, 26 do
      local c = led_stage[i]
      payload[#payload + 1] = c[1]
      payload[#payload + 1] = c[2]
      payload[#payload + 1] = c[3]
    end
    send(CMD_LED_BATCH, payload)
  else
    for _, i in ipairs(changed) do
      local c = led_stage[i]
      send(CMD_LED, { i, c[1], c[2], c[3] })
    end
  end
  send(CMD_LED_SHOW)
  for i = 0, 26 do led_sent[i] = { led_stage[i][1], led_stage[i][2], led_stage[i][3] } end
end

-- ---------------------------------------------------------------------------
-- screen
-- ---------------------------------------------------------------------------

-- True when a new frame may be sent (previous one acked, or ack timed out).
function M.screen_ready()
  if frame_pending and (util.time() - frame_sent_at) > 0.25 then
    frame_pending = false -- lost ack; don't wedge
  end
  return not frame_pending
end

-- Peek the 128x32 norns screen region at (x,y) and send it to the OMX.
-- Call after drawing (screen.update not required). Returns true if a frame
-- (or nothing-changed no-op) was handled, false if dropped (frame in flight).
function M.screen_send(x, y)
  if not dev then return false end
  if not M.screen_ready() then return false end
  x = x or 0
  y = y or 0

  local px = screen.peek(x, y, OMX_W, OMX_H)
  if not px then return false end

  -- pack to SSD1306 pages, rotated 180 (panel orientation), thresholded
  local fb = {}
  for i = 1, FB_BYTES do fb[i] = 0 end
  local thr = M.screen_threshold
  for yy = 0, OMX_H - 1 do
    local row = yy * OMX_W
    for xx = 0, OMX_W - 1 do
      if string.byte(px, row + xx + 1) >= thr then
        local rx = OMX_W - 1 - xx -- rotate 180
        local ry = OMX_H - 1 - yy
        local idx = rx + (ry >> 3) * OMX_W + 1
        fb[idx] = fb[idx] | (1 << (ry & 7))
      end
    end
  end

  -- chunk-level diff vs the last sent frame
  local sent = false
  local chunks = {}
  for c = 0, CHUNK_COUNT - 1 do
    local chunk = {}
    for i = 1, CHUNK_BYTES do chunk[i] = fb[c * CHUNK_BYTES + i] end
    chunks[c] = chunk
    local same = false
    if last_chunks and last_chunks[c] then
      same = true
      for i = 1, CHUNK_BYTES do
        if last_chunks[c][i] ~= chunk[i] then same = false break end
      end
    end
    if not same then
      local payload = { c }
      for _, b in ipairs(encode7bit(chunk)) do payload[#payload + 1] = b end
      send(CMD_DRAW, payload)
      sent = true
    end
  end
  last_chunks = chunks

  if sent then
    send(CMD_DRAW_UPD)
    frame_pending = true
    frame_sent_at = util.time()
  end
  return true
end

return M
