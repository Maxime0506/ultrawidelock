--[[
  Aliro protocol analyzer for Wireshark (Tier A: clear-text BLE plane).

  Makes docs/protocol-research.md executable. This file decodes the parts of an
  Aliro transaction that a passive BLE sniffer sees in the clear, with no keys
  and no firmware changes:

    * aliro_adv      -- the 0xFFF2 advertising service data (research doc, sec 3).
                        Auto-fires on every ADV that carries 16-bit service
                        UUID 0xFFF2. Byte-exact and confirmed against Wireshark's
                        own EIR/AD parser.

    * aliro_timesync -- the Procedure-0 time-sync message (research doc, sec 5),
                        the one time sync sent before any session key exists and
                        therefore readable in the clear. It rides GATT/L2CAP on a
                        dynamic handle a passive tool cannot identify on its own,
                        so it is NOT auto-hooked: select the message's payload
                        bytes and use "Decode As" -> "aliro_timesync".

  Everything after "Access Protocol Completed" (in-session time sync, ranging
  setup M1-M4, suspend/resume) is encrypted under BleSK and is NOT decodable from
  a passive capture. That is Tier B and needs a key or the firmware's own
  decrypted trace; it is deliberately not attempted here.

  Install: copy to your Wireshark "Personal Lua Plugins" directory
  (Help > About Wireshark > Folders), or run:
      tshark -X lua_script:tools/aliro.lua ...
  See docs/wireshark.md for the capture recipe.
]]--

----------------------------------------------------------------------
-- aliro_adv : 0xFFF2 advertising service data (research doc, section 3)
----------------------------------------------------------------------
-- Layout of the 24-byte service-data value Wireshark hands us (its "byte N"
-- absolute-in-AdvData numbering minus 7):
--   0        flags byte     bit7 UWB flow, bit6 BLE-only flow,
--                           bits4:3 notification/error, bits2:0 adv version
--   1        TX power (int8, dBm)
--   2-11     reader group identifier (+ sub-identifier), truncated
--   12-15    dynamic tag expiry (Unix time; 0xFFFFFFFF = reader has no clock)
--   16       reserved
--   17-23    dynamic tag (first 7 octets of AES-128(GRK, pad || AdvA || expiry))

local adv = Proto("aliro_adv", "Aliro Advertisement (0xFFF2)")

local NOTIF = { [0] = "none", [1] = "notification", [2] = "error", [3] = "reserved" }
-- The research doc does not pin the byte order of the expiry; rather than guess,
-- the dissector reads it both ways and picks the plausible date (see below), and
-- always shows the raw bytes so the choice stays checkable. See docs/wireshark.md.
local f = {
  flags    = ProtoField.uint8 ("aliro_adv.flags", "Flags", base.HEX),
  flow_uwb = ProtoField.bool  ("aliro_adv.flow_uwb", "BLE + UWB flow supported", 8, nil, 0x80),
  flow_ble = ProtoField.bool  ("aliro_adv.flow_ble", "BLE-only flow supported", 8, nil, 0x40),
  notif    = ProtoField.uint8 ("aliro_adv.notif", "Notification/error", base.DEC, NOTIF, 0x18),
  advver   = ProtoField.uint8 ("aliro_adv.adv_version", "Advertisement version", base.DEC, nil, 0x07),
  txpower  = ProtoField.int8  ("aliro_adv.tx_power", "TX power (dBm)", base.DEC),
  groupid  = ProtoField.bytes ("aliro_adv.group_id", "Reader group identifier (+sub-id)"),
  expiry   = ProtoField.bytes ("aliro_adv.tag_expiry", "Dynamic tag expiry (raw)"),
  expiry_s = ProtoField.string("aliro_adv.tag_expiry_utc", "Dynamic tag expiry"),
  resv     = ProtoField.uint8 ("aliro_adv.reserved", "Reserved", base.HEX),
  tag      = ProtoField.bytes ("aliro_adv.dynamic_tag", "Dynamic tag (7 octets)"),
}
for _, fld in pairs(f) do adv.fields[#adv.fields + 1] = fld end

local e_short   = ProtoExpert.new("aliro_adv.short", "Aliro service data shorter than 24 bytes (truncated advert)", expert.group.MALFORMED, expert.severity.WARN)
local e_no_uwb  = ProtoExpert.new("aliro_adv.no_uwb", "UWB flow flag clear: phone will not range with this reader (control-only build)", expert.group.PROTOCOL, expert.severity.NOTE)
local e_no_clock= ProtoExpert.new("aliro_adv.no_clock", "Dynamic tag expiry is 0xFFFFFFFF: reader has no clock", expert.group.PROTOCOL, expert.severity.NOTE)
adv.experts = { e_short, e_no_uwb, e_no_clock }

-- A valid recent Unix expiry sits roughly in 2020..2100. Only one byte order
-- puts a real Aliro expiry there, so we let plausibility pick the endianness
-- rather than guessing: 0x66.. as MSB reads sane big-endian, garbage little.
local EXP_LO, EXP_HI = 1577836800, 4102444800  -- 2020-01-01 .. 2100-01-01 UTC
local function plausible(t) return t >= EXP_LO and t <= EXP_HI end
local function utc(t) return os.date("!%Y-%m-%d %H:%M:%S UTC", t) end

function adv.dissector(tvb, pinfo, tree)
  local n = tvb:len()
  local t = tree:add(adv, tvb(), "Aliro Advertisement")
  pinfo.cols.protocol = "Aliro-ADV"

  if n < 1 then return 0 end

  -- Flags byte and its sub-fields
  local flags_v = tvb(0, 1):uint()
  local ft = t:add(f.flags, tvb(0, 1))
  ft:add(f.flow_uwb, tvb(0, 1))
  ft:add(f.flow_ble, tvb(0, 1))
  ft:add(f.notif,    tvb(0, 1))
  ft:add(f.advver,   tvb(0, 1))
  local uwb = (bit.band(flags_v, 0x80) ~= 0)
  local ver = bit.band(flags_v, 0x07)
  if not uwb then t:add_proto_expert_info(e_no_uwb) end

  local info = { "Aliro ADV", uwb and "UWB flow" or "control-only", "v" .. ver }

  if n >= 2 then t:add(f.txpower, tvb(1, 1)) end
  if n >= 12 then t:add(f.groupid, tvb(2, 10)) end

  if n >= 16 then
    local et = t:add(f.expiry, tvb(12, 4))
    local le, be = tvb(12, 4):le_uint(), tvb(12, 4):uint()
    if le == 0xFFFFFFFF then
      et:append_text(" (no clock)")
      t:add(f.expiry_s, tvb(12, 4), "n/a (no clock)"):set_generated()
      t:add_proto_expert_info(e_no_clock)
      info[#info + 1] = "no-clock"
    else
      -- Endianness is not pinned in the research doc; pick the byte order that
      -- yields a plausible date, and always show both so it stays checkable.
      local le_ok, be_ok = plausible(le), plausible(be)
      local decoded, order
      if be_ok and not le_ok then decoded, order = utc(be), "big-endian"
      elseif le_ok and not be_ok then decoded, order = utc(le), "little-endian"
      elseif le_ok and be_ok then decoded, order = utc(le), "little-endian?; both plausible"
      else decoded, order = "implausible either way", "unknown order" end
      local text = decoded .. " (" .. order .. "; LE=" .. le .. " BE=" .. be .. ")"
      et:append_text(" -> " .. decoded)
      t:add(f.expiry_s, tvb(12, 4), text):set_generated()
      info[#info + 1] = "expires " .. decoded
    end
  end

  if n >= 17 then t:add(f.resv, tvb(16, 1)) end
  if n >= 24 then t:add(f.tag, tvb(17, 7)) end

  if n < 24 then t:add_proto_expert_info(e_short) end

  pinfo.cols.info = table.concat(info, " | ")
  return n
end

-- Verified dispatch: Wireshark's EIR/AD parser hands 16-bit service-data payloads
-- to this table keyed by the lowercase short UUID.
DissectorTable.get("btcommon.eir_ad.entry.uuid"):add("fff2", adv)

----------------------------------------------------------------------
-- aliro_timesync : Procedure-0 time-sync message (research doc, section 5)
----------------------------------------------------------------------
-- Field set and sizes are taken verbatim from research doc section 5. The wire
-- ORDER follows the doc's listing order and the 1-byte width of the clock-skew
-- flag is inferred; neither is byte-confirmed against a live capture yet, so
-- treat this parser as LIKELY and verify before relying on offsets. This is why
-- it is offered for manual "Decode As", not auto-hooked. Total 23 bytes.
local ts = Proto("aliro_timesync", "Aliro Time Sync (Procedure 0)")

local TS_SUCCESS = { [0] = "0", [1] = "1", [2] = "2" }  -- meanings not given in doc
local tf = {
  devcount = ProtoField.uint64("aliro_timesync.device_event_count", "Device event count", base.DEC),
  uwbtime  = ProtoField.uint64("aliro_timesync.uwb_device_time", "UWB device time (us)", base.DEC),
  uncert   = ProtoField.uint8 ("aliro_timesync.uncertainty", "UWB device time uncertainty (log-encoded)", base.DEC),
  skewflag = ProtoField.bool  ("aliro_timesync.clock_skew_available", "Clock-skew available"),
  maxppm   = ProtoField.uint16("aliro_timesync.device_max_ppm", "Device max PPM", base.DEC),
  success  = ProtoField.uint8 ("aliro_timesync.success", "Success", base.DEC, TS_SUCCESS),
  retry    = ProtoField.uint16("aliro_timesync.retry_delay", "Retry delay", base.DEC),
}
for _, fld in pairs(tf) do ts.fields[#ts.fields + 1] = fld end

local ts_short = ProtoExpert.new("aliro_timesync.short", "Payload shorter than 23 bytes; not a full Procedure-0 time sync", expert.group.MALFORMED, expert.severity.WARN)
ts.experts = { ts_short }

function ts.dissector(tvb, pinfo, tree)
  local n = tvb:len()
  local t = tree:add(ts, tvb(), "Aliro Time Sync (Procedure 0)")
  pinfo.cols.protocol = "Aliro-TS"
  if n < 23 then
    t:add_proto_expert_info(ts_short)
    if n > 0 then t:add(tvb(0, n), "Raw (" .. n .. " bytes)") end
    return n
  end
  t:add_le(tf.devcount, tvb(0, 8))
  t:add_le(tf.uwbtime,  tvb(8, 8))
  t:add(tf.uncert,      tvb(16, 1))
  t:add(tf.skewflag,    tvb(17, 1))
  t:add_le(tf.maxppm,   tvb(18, 2))
  t:add(tf.success,     tvb(20, 1))
  t:add_le(tf.retry,    tvb(21, 2))
  pinfo.cols.info = "Aliro Time Sync (Procedure 0)"
  return 23
end

-- Manual only: register for "Decode As" against L2CAP/ATT payloads. No auto hook
-- because the transport handle is dynamic and unidentifiable from a passive capture.
DissectorTable.get("btl2cap.cid"):add_for_decode_as(ts)
