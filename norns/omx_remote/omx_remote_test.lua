-- omx_remote_test
-- OMX-27 REMOTE mode test — the OMX as a norns controller, grid-style.
--
-- OMX:   keys light white while held, encoder moves a yellow cursor along
--        the bottom row over a dim rainbow, encoder button inverts the
--        screen; the OMX screen shows the last event + live pot bars.
--        Exit REMOTE mode on the device with AUX + encoder HOLD.
-- norns: the top half of the norns screen is exactly what the OMX shows
--        (that region is mirrored to the OMX); the bottom half shows
--        connection status. K2 reconnects. E1-E3/K3 are unused.

local omx = include('omx_remote/lib/omx27')

local held = {}          -- keys currently down, [n] = true
local cursor = 0         -- encoder cursor 0-15 (bottom row keys 11-26)
local invert = false     -- encoder button held -> inverted OMX screen
local pots = { 0, 0, 0, 0, 0 }
local last_event = "waiting for input..."
local dirty = true
local phase = 0.0
local connected = false

-- hue 0-1 -> r,g,b 0-127 (s=v=1)
local function hsv(h)
  local i = math.floor(h * 6) % 6
  local f = h * 6 - math.floor(h * 6)
  local q, t = 1 - f, f
  local tbl = {
    [0] = { 1, t, 0 }, { q, 1, 0 }, { 0, 1, t },
    { 0, q, 1 }, { t, 0, 1 }, { 1, 0, q },
  }
  local c = tbl[i]
  return math.floor(c[1] * 127), math.floor(c[2] * 127), math.floor(c[3] * 127)
end

local function update_leds()
  for n = 0, 26 do
    if held[n] then
      omx.led(n, 127, 127, 127)
    else
      local r, g, b = hsv((phase + n / 27) % 1)
      omx.led(n, r // 6, g // 6, b // 6) -- dim rainbow
    end
  end
  local cur = 11 + cursor
  if not held[cur] then omx.led(cur, 127, 127, 0) end
  omx.led_show() -- diffs only; cheap when nothing changed
end

function init()
  connected = omx.connect()

  omx.key = function(n, ev)
    if ev == "down" then held[n] = true
    elseif ev == "up" then held[n] = nil end
    last_event = string.format("key(%d, %s)", n, ev)
    dirty = true
    update_leds()
  end

  omx.enc = function(d)
    cursor = (cursor + d) % 16
    last_event = string.format("enc(%+d)", d)
    dirty = true
    update_leds()
  end

  omx.enc_btn = function(z)
    invert = (z == 1)
    last_event = string.format("enc_btn(%d)", z)
    dirty = true
  end

  omx.pot = function(n, v, hires)
    pots[n + 1] = v
    last_event = string.format("pot(%d, %d)", n, v)
    dirty = true
  end

  omx.status = function(s)
    print("omx status: " .. s)
    if s == "active" then
      dirty = true
      update_leds()
    end
  end

  -- rainbow at 10fps
  local rainbow = metro.init(function()
    phase = phase + 0.015
    update_leds()
  end, 1 / 10)
  rainbow:start()

  -- screen at up to 15fps (frame-in-flight gating drops extras)
  local draw = metro.init(function()
    redraw()
  end, 1 / 15)
  draw:start()
end

function key(n, z)
  if n == 2 and z == 1 then
    connected = omx.connect()
    dirty = true
  end
end

function redraw()
  screen.clear()

  -- top 128x32: the OMX screen (this exact region is mirrored to the OMX)
  if invert then
    screen.level(15)
    screen.rect(0, 0, 128, 32)
    screen.fill()
    screen.level(0)
  else
    screen.level(15)
  end
  screen.move(2, 8)
  screen.text("OMX REMOTE test")
  screen.move(2, 18)
  screen.text(last_event)
  for i = 1, 5 do
    local x = 2 + (i - 1) * 25
    screen.rect(x + 0.5, 28.5, 21, 3)
    screen.stroke()
    if pots[i] > 0 then
      screen.rect(x, 28, math.max(1, 21 * pots[i] / 127), 4)
      screen.fill()
    end
  end

  -- bottom half: status, norns-only
  screen.level(4)
  screen.move(0, 38)
  screen.line(128, 38)
  screen.stroke()
  screen.move(2, 48)
  screen.text(connected and "OMX connected" or "OMX not found - K2 retry")
  screen.move(2, 58)
  screen.text("exit on OMX: AUX + enc hold")

  screen.update()
  omx.screen_send(0, 0)
end

function cleanup()
  omx.disconnect()
end
