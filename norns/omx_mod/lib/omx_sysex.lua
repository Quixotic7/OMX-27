-- omx_sysex.lua
-- Shared SysEx codec + command constants for the OMX-27 <-> norns link.
-- Wire envelope:  F0 7D 00 00 <cmd> <payload...> F7   (0x7D = OMX manufacturer id)
-- Payload bytes are 7-bit packed (see encode/decode7bit) so they survive MIDI.

local M = {}

-- OMX -> norns
M.CMD_FRAME     = 0x50 -- chunk: [chunk(0-15)] [fid7] [cksum7] [7bit-encoded 32B]
M.CMD_INPUT     = 0x51 -- NT input event (phase 2)
M.CMD_STATUS    = 0x52 -- status: [0x01] = user input activity
M.CMD_FRAME_END = 0x53 -- pass complete: [fid7] [mask x3: bits0-6, bits7-13, bits14-15]
-- norns -> OMX
M.CMD_MIRROR_EN = 0x58 -- [0|1] enable screen-mirror streaming
M.CMD_LED       = 0x59 -- NT LED set (phase 2)
M.CMD_LED_BATCH = 0x5A -- NT LED batch (phase 2)
M.CMD_LED_SHOW  = 0x5B -- NT LED latch (phase 2)
M.CMD_DRAW      = 0x5C -- NT screen draw batch (phase 2)
M.CMD_DRAW_UPD  = 0x5D -- NT screen flush (phase 2)
M.CMD_REQ       = 0x5E -- [mask x3] resend chunks; all-zero = resend FRAME_END
M.CMD_PACE      = 0x5F -- [ms 1-127] gap between chunk messages

-- OMX screen geometry + transport
M.OMX_W = 128       -- SSD1306 width (and page stride)
M.OMX_H = 32        -- height
M.FB_BYTES = 512    -- full framebuffer (128*32/8)
M.CHUNK_BYTES = 32  -- transport chunk size (46-byte SysEx fits the OMX 128B USB FIFO with room)
M.CHUNK_COUNT = 16  -- FB_BYTES / CHUNK_BYTES

-- 16-bit chunk mask <-> 3 seven-bit bytes
function M.mask_to_bytes(m)
  return { m & 0x7f, (m >> 7) & 0x7f, (m >> 14) & 0x03 }
end
function M.mask_from_bytes(b0, b1, b2)
  return ((b0 or 0) & 0x7f) | (((b1 or 0) & 0x7f) << 7) | (((b2 or 0) & 0x03) << 14)
end

-- Is `data` (array incl. F0..F7) one of our messages? Returns cmd or nil.
function M.match(data)
  if type(data) ~= "table" then return nil end
  if data[1] == 0xf0 and data[2] == 0x7d and data[3] == 0x00 and data[4] == 0x00 then
    return data[5]
  end
  return nil
end

-- Decode 7-bit-packed payload back to raw 8-bit bytes.
-- Reads src[from..to] (inclusive). Each group = 1 hi-bits byte + up to 7 lows.
function M.decode7bit(src, from, to)
  local out = {}
  local i = from
  while i <= to do
    local hi = src[i]
    i = i + 1
    local n = math.min(7, to - i + 1)
    for j = 0, n - 1 do
      local b = src[i + j] & 0x7f
      if (hi >> j) & 1 == 1 then b = b | 0x80 end
      out[#out + 1] = b
    end
    i = i + n
  end
  return out
end

-- Encode raw bytes (array) -> 7-bit-packed (array). Mirror of firmware encode7bit.
function M.encode7bit(src)
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

-- Build a full sysex message (array incl. F0..F7) from cmd + raw payload array.
-- Payload is masked to 7 bits; pre-encode with encode7bit if it holds 8-bit data.
function M.build(cmd, payload)
  local msg = { 0xf0, 0x7d, 0x00, 0x00, cmd }
  if payload then
    for _, b in ipairs(payload) do msg[#msg + 1] = b & 0x7f end
  end
  msg[#msg + 1] = 0xf7
  return msg
end

return M
