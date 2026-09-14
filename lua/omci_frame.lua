-- ITU-T G.988
local byte, char, sub, rep = string.byte, string.char, string.sub, string.rep
local floor = math.floor
local fmt = string.format

local M = {}
M.BASE, M.EXT = 0x0a, 0x0b

function M.u16(f, i)
	local x, y = byte(f, i, i + 1)
	return x * 256 + y
end
function M.put16(v)
	return char(floor(v / 256) % 256, v % 256)
end

function M.tci(f)   return M.u16(f, 1) end
function M.prio(f)  return byte(f, 1) >= 128 end
function M.type(f)  return byte(f, 3) end

function M.db(f)    return floor(byte(f, 3) / 128) % 2 end
function M.ar(f)    return floor(byte(f, 3) /  64) % 2 end
function M.ak(f)    return floor(byte(f, 3) /  32) % 2 end
function M.mt(f)    return byte(f, 3) % 32 end

function M.fmt(f)   return byte(f, 4) end
function M.class(f) return M.u16(f, 5) end
function M.inst(f)  return M.u16(f, 7) end
function M.elen(f)  return M.u16(f, 9) end

local function body_base(f)
	return M.fmt(f) == M.EXT and 11 or 9
end

-- body(frame) -> binary string, length
function M.body(f)
	local base = body_base(f)
	if M.fmt(f) == M.EXT then
		local l = M.elen(f)
		if base - 1 + l > #f then l = #f - (base - 1) end
		if l < 0 then l = 0 end
		return sub(f, base, base - 1 + l), l
	end
	local l = #f - (base - 1)
	if l > 32 then l = 32 end
	return sub(f, base, base - 1 + l), l
end

function M.result(f) return byte(f, 11) end  -- ack/response frames

-- message-type mnemonics
M.MT = {
	CREATE = 4, DELETE = 6, SET = 8, GET = 9,
	GET_ALL_ALARMS = 11, GET_ALL_ALARMS_NEXT = 12,
	MIB_UPLOAD = 13, MIB_UPLOAD_NEXT = 14, MIB_RESET = 15,
	ALARM = 16, AVC = 17, TEST = 18,
	START_SW_DL = 19, DL_SECTION = 20, END_SW_DL = 21,
	ACTIVATE_SW = 22, COMMIT_SW = 23, SYNC_TIME = 24, REBOOT = 25,
	GET_NEXT = 26, TEST_RESULT = 27, GET_CURRENT_DATA = 28, SET_TABLE = 29,
}
M.MR = { OK = 0, ERROR = 1, NOT_SUPPORTED = 2, PARAM = 3,
	 UNKNOWN_ME = 4, UNKNOWN_ME_INST = 5, BUSY = 6,
	 EXISTS = 7, ATTR_FAILED = 9 }

-- reverse of M.MT (number -> name) for summary() below
local MT_NAME = {}
for name, num in pairs(M.MT) do MT_NAME[num] = name end

-- one-line frame summary for logging
function M.summary(f)
	local mt = M.mt(f)
	return fmt("%s tci=%04X %s(%d) ar=%d ak=%d class=%04X inst=%04X body=%dB",
		M.fmt(f) == M.EXT and "EXT" or "BASE",
		M.tci(f), MT_NAME[mt] or "?", mt,
		M.ar(f), M.ak(f),
		M.class(f), M.inst(f),
		select(2, M.body(f)))
end

-- generic frame: M.new{tci=,mt=,ak=,ar=,class=,inst=,body=,fmt=}
function M.new(o)
	local f = o.fmt or M.EXT
	local h = M.put16(o.tci or 0)
		.. char((o.mt or 0) + (o.ak or 0) * 32 + (o.ar or 0) * 64)
		.. char(f)
		.. M.put16(o.class or 0) .. M.put16(o.inst or 0)
	local body = o.body or ""
	if f == M.EXT then
		return h .. M.put16(#body) .. body
	end
	return h .. body .. rep("\0", 32 - #body) .. "\0\0\0\40"
end

-- reply to req: same tci/class/inst/mt, ak=req.ar, ar=0
function M.reply(req, result, body)
	return M.new{
		tci   = M.tci(req),
		mt    = M.mt(req),
		ak    = M.ar(req),
		ar    = 0,
		fmt   = M.fmt(req),
		class = M.class(req),
		inst  = M.inst(req),
		body  = char(result) .. (body or ""),
	}
end

-- rewrite a header field into a new frame
function M.with(f, field, v)
	if field == "tci"  then return M.put16(v) .. sub(f, 3) end
	if field == "type" then return sub(f, 1, 2) .. char(v) .. sub(f, 4) end
	if field == "fmt"  then return sub(f, 1, 3) .. char(v) .. sub(f, 5) end
	if field == "class" then return sub(f, 1, 4) .. M.put16(v) .. sub(f, 7) end
	if field == "inst"  then return sub(f, 1, 6) .. M.put16(v) .. sub(f, 9) end
	if field == "mt" then
		return M.with(f, "type", byte(f, 3) - byte(f, 3) % 32 + v)
	end
	error("omci_frame.with: unknown field " .. tostring(field))
end

local function attr_segs(f, sizes, mask, voff)
	local out, p = {}, voff
	for a = 1, #sizes do
		if math.floor(mask / 2^(16 - a)) % 2 == 1 then
			out[#out + 1] = { attr = a, pos = p, size = sizes[a] }
			p = p + sizes[a]
		end
	end
	return out
end

-- ET/GET mask parser: lead=0 for SET, lead=1 for GET
local function mask_parse(f, sizes, lead)
	local base = body_base(f)
	local moff = base + lead
	local mask = M.u16(f, moff)
	return mask, attr_segs(f, sizes, mask, moff + 2), M.fmt(f) == M.EXT, moff, base
end

local function set_parse(f, sizes) return mask_parse(f, sizes, 0) end
local function get_parse(f, sizes) return mask_parse(f, sizes, 1) end

-- clear attribute's mask bit + drop its bytes
local function strip_attr(f, sizes, attr, parse)
	local mask, segs, ext, moff, base = parse(f, sizes)
	local vals, gone = "", false
	for i = 1, #segs do
		if segs[i].attr == attr then
			gone = true
		else
			local s = segs[i]
			vals = vals .. sub(f, s.pos, s.pos + s.size - 1)
		end
	end
	if not gone then
		return f
	end
	local newmask = mask - 2^(16 - attr)
	local lead_bytes = sub(f, base, moff - 1)
	if ext then
		return sub(f, 1, 8) .. M.put16(#lead_bytes + 2 + #vals)
			.. lead_bytes .. M.put16(newmask) .. vals
	end
	-- baseline: fixed 32-byte content = lead_bytes + mask(2) + values + CPCS tail
	local budget = 30 - #lead_bytes
	if #vals > budget then
		return f
	end
	return sub(f, 1, 8) .. lead_bytes .. M.put16(newmask) .. vals
		.. rep("\0", budget - #vals) .. sub(f, 41)
end

function M.strip_set_attr(f, sizes, attr) return strip_attr(f, sizes, attr, set_parse) end
function M.strip_get_attr(f, sizes, attr) return strip_attr(f, sizes, attr, get_parse) end

-- overwrite attribute's value in place
local function replace_attr(f, sizes, attr, value, parse)
	local _, segs = parse(f, sizes)
	value = tostring(value or "")
	if #value > sizes[attr] then
		value = sub(value, 1, sizes[attr])
	end
	value = value .. rep("\0", sizes[attr] - #value)
	for i = 1, #segs do
		local s = segs[i]
		if s.attr == attr then
			return sub(f, 1, s.pos - 1) .. value
				.. sub(f, s.pos + s.size)
		end
	end
	return f
end

function M.replace_set_attr(f, sizes, attr, value) return replace_attr(f, sizes, attr, value, set_parse) end
function M.replace_get_attr(f, sizes, attr, value) return replace_attr(f, sizes, attr, value, get_parse) end

-- overwrite attribute's value in place
function M.replace_create_attr(f, sizes, sbc, attr, value)
	value = tostring(value or "")
	for _, s in ipairs(attr_segs(f, sizes, sbc, body_base(f))) do
		if s.attr == attr then
			if #value > s.size then value = sub(value, 1, s.size) end
			value = value .. rep("\0", s.size - #value)
			return sub(f, 1, s.pos - 1) .. value
				.. sub(f, s.pos + s.size)
		end
	end
	return f
end

local ok_nixio, nixio = pcall(require, "nixio")
if ok_nixio and nixio.bin and nixio.bin.hexlify then
	function M.hex(f)   return nixio.bin.hexlify(f) end
	function M.unhex(h) return nixio.bin.unhexlify(h) end
else
	function M.hex(f)
		return (f:gsub(".", function(c) return fmt("%02X", byte(c)) end))
	end
	M.unhex = function(h)
		return (h:gsub("%x%x", function(cc)
			return char(tonumber(cc, 16)) end))
	end
end

return M
