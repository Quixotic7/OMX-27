-- omx_molly
-- Molly the Poly, played from an OMX-27 in REMOTE mode.
--
-- OMX layout:
--   AUX (key 0)     quick-tap: switch LIVE <-> SEQ   (AUX + enc HOLD exits)
--   keys 1 / 2      octave - / +
--   keys 8 / 9 / 10 randomize patch: lead / pad / percussion
--   keys 11-26      LIVE: play 16 scale notes    SEQ: toggle steps
--   encoder         SEQ: hold a step + turn = change that step's note
--   enc button      quick-press: seq start/stop
--   pots 1-5        wave shape / LP cutoff / resonance / release / noise
--
-- SEQ is a simple awake-style loop: 16 eighth-note steps, each with its own
-- note; toggle steps with the keys, hold a step and turn the encoder to
-- change its pitch. The playhead runs white across the bottom row.
--
-- norns: top half of the screen mirrors the OMX; K2 reconnects, K3 = play/stop.

engine.name = "MollyThePoly"

local MusicUtil = require "musicutil"
local MollyThePoly = require "molly_the_poly/lib/molly_the_poly_engine"
local omx = include("omx_remote/lib/omx27")

-- state -----------------------------------------------------------------
local mode = "live"          -- "live" | "seq"
local octave = 0             -- -3..3, offsets the scale root
local root = 48              -- C3
local scale_notes = {}       -- 16 notes for the bottom row
local held = {}              -- held bottom-row keys, [step 1-16] = true
local live_notes = {}        -- ringing live notes, [step] = midi note
local playing = false
local steps = {}             -- [1-16] = { on = bool, note = 1-16 (scale index) }
local playhead = 0
local seq_clock = nil
local pots = { 0, 0, 0, 0, 0 }
local last_event = "-"
local dirty = true
local connected = false

local POT_PARAMS = {
  { id = "osc_wave_shape",      label = "WAVE" },
  { id = "lp_filter_cutoff",    label = "CUT" },
  { id = "lp_filter_resonance", label = "RES" },
  { id = "env_2_release",       label = "REL" },
  { id = "noise_level",         label = "NOIS" },
}

local function build_scale()
  scale_notes = MusicUtil.generate_scale(root + octave * 12, "minor pentatonic", 4)
end

-- LEDs ------------------------------------------------------------------
local function update_leds()
  -- AUX shows the mode
  if mode == "live" then omx.led(0, 0, 20, 60) else omx.led(0, 0, 60, 10) end
  -- octave keys
  omx.led(1, 40, 15, 0)
  omx.led(2, 0, 15, 50)
  -- randomize keys
  omx.led(8, 30, 0, 0)
  omx.led(9, 15, 0, 30)
  omx.led(10, 30, 12, 0)
  for k = 3, 7 do omx.led(k, 0, 0, 0) end
  -- bottom row
  for i = 1, 16 do
    local key = 10 + i
    if held[i] then
      omx.led(key, 127, 127, 127)
    elseif mode == "live" then
      local deg = (scale_notes[i] - root) % 12
      if deg == 0 then omx.led(key, 0, 40, 60) else omx.led(key, 0, 6, 12) end
    else -- seq
      if playing and i == playhead then
        omx.led(key, 127, 127, 127)
      elseif steps[i].on then
        omx.led(key, 0, 45, 8)
      else
        omx.led(key, 2, 3, 3)
      end
    end
  end
  omx.led_show()
end

-- sound -----------------------------------------------------------------
local function note_on(id, note, vel)
  engine.noteOn(id, MusicUtil.note_num_to_freq(note), vel or 0.8)
end

local function seq_loop()
  while true do
    clock.sync(1 / 2) -- eighth notes
    playhead = (playhead % 16) + 1
    local st = steps[playhead]
    if st.on then
      local note = scale_notes[st.note]
      local id = 1000 + playhead
      note_on(id, note, 0.75)
      clock.run(function()
        clock.sleep(clock.get_beat_sec() * 0.4)
        engine.noteOff(id)
      end)
    end
    dirty = true
    update_leds()
  end
end

local function seq_start()
  if not playing then
    playing = true
    playhead = 0
    seq_clock = clock.run(seq_loop)
  end
end

local function seq_stop()
  if playing then
    playing = false
    if seq_clock then clock.cancel(seq_clock) end
    engine.noteOffAll()
  end
end

-- OMX input -------------------------------------------------------------
local function on_key(n, ev)
  if n == 0 then -- AUX: quick tap switches mode
    if ev == "quick" then
      mode = (mode == "live") and "seq" or "live"
      last_event = "mode: " .. mode
    end
  elseif n == 1 or n == 2 then
    if ev == "down" then
      octave = util.clamp(octave + (n == 1 and -1 or 1), -3, 3)
      build_scale()
      last_event = "octave " .. octave
    end
  elseif n >= 8 and n <= 10 then
    if ev == "down" then
      local kind = ({ [8] = "lead", [9] = "pad", [10] = "percussion" })[n]
      MollyThePoly.randomize_params(kind)
      last_event = "random " .. kind
    end
  elseif n >= 11 then
    local i = n - 10
    if ev == "down" then
      held[i] = true
      if mode == "live" then
        live_notes[i] = scale_notes[i]
        note_on(i, live_notes[i])
        last_event = "note " .. MusicUtil.note_num_to_name(live_notes[i], true)
      else
        steps[i].on = not steps[i].on
        last_event = string.format("step %d %s", i, steps[i].on and "on" or "off")
      end
    elseif ev == "up" then
      held[i] = nil
      if live_notes[i] then
        engine.noteOff(i)
        live_notes[i] = nil
      end
    end
  end
  dirty = true
  update_leds()
end

local function on_enc(d)
  if mode == "seq" then
    for i = 1, 16 do
      if held[i] then -- hold step + turn = set step note
        steps[i].note = util.clamp(steps[i].note + d, 1, 16)
        steps[i].on = true
        last_event = string.format("step %d: %s", i,
          MusicUtil.note_num_to_name(scale_notes[steps[i].note], true))
        dirty = true
        return
      end
    end
  end
end

local function on_enc_btn(z)
  if z == 1 then
    if playing then seq_stop() else seq_start() end
    last_event = playing and "play" or "stop"
    dirty = true
    update_leds()
  end
end

local function on_pot(n, v, hires)
  pots[n + 1] = v
  local p = POT_PARAMS[n + 1]
  params:set_raw(p.id, v / 127)
  last_event = string.format("%s %d%%", p.label, math.floor(v / 127 * 100 + 0.5))
  dirty = true
end

-- norns -----------------------------------------------------------------
function init()
  MollyThePoly.add_params()
  params:set("osc_wave_shape", 3)
  build_scale()

  -- default awake-ish pattern
  for i = 1, 16 do steps[i] = { on = false, note = 1 } end
  local seed = { { 1, 1 }, { 4, 8 }, { 7, 5 }, { 9, 1 }, { 12, 10 }, { 15, 6 } }
  for _, s in ipairs(seed) do
    steps[s[1]].on = true
    steps[s[1]].note = s[2]
  end

  connected = omx.connect()
  omx.key = on_key
  omx.enc = on_enc
  omx.enc_btn = on_enc_btn
  omx.pot = on_pot
  omx.status = function(s)
    if s == "active" then
      dirty = true
      update_leds()
    end
  end

  update_leds()

  metro.init(function()
    if dirty then
      dirty = false
      redraw()
    end
  end, 1 / 15):start()
end

function key(n, z)
  if z == 1 then
    if n == 2 then
      connected = omx.connect()
      dirty = true
    elseif n == 3 then
      on_enc_btn(1)
    end
  end
end

function redraw()
  screen.clear()

  -- top 128x32 mirrors to the OMX
  screen.level(15)
  screen.move(2, 8)
  screen.text(mode == "live" and "MOLLY  live" or
    (playing and "MOLLY  seq >" or "MOLLY  seq #"))
  screen.move(126, 8)
  screen.text_right("oct " .. octave)
  screen.move(2, 18)
  screen.text(last_event)
  -- step row / pot bars
  if mode == "seq" then
    for i = 1, 16 do
      local x = 2 + (i - 1) * 8
      if steps[i].on then
        screen.rect(x, 26 - steps[i].note / 3, 6, 2 + steps[i].note / 3)
        screen.fill()
      else
        screen.rect(x + 0.5, 28.5, 5, 2)
        screen.stroke()
      end
      if playing and i == playhead then
        screen.rect(x, 22, 6, 1)
        screen.fill()
      end
    end
  else
    for i = 1, 5 do
      local x = 2 + (i - 1) * 25
      screen.rect(x + 0.5, 28.5, 21, 3)
      screen.stroke()
      if pots[i] > 0 then
        screen.rect(x, 28, math.max(1, 21 * pots[i] / 127), 4)
        screen.fill()
      end
    end
  end

  -- bottom half: norns-only status
  screen.level(4)
  screen.move(0, 38)
  screen.line(128, 38)
  screen.stroke()
  screen.move(2, 48)
  screen.text(connected and "OMX connected" or "OMX not found - K2 retry")
  screen.move(2, 58)
  screen.text("K3 play/stop  AUX tap: mode")

  screen.update()
  omx.screen_send(0, 0)
end

function cleanup()
  seq_stop()
  omx.disconnect()
end
