local M = {
  cli_pipe  = 0,   -- pipe instance: /tmp/pipe/omci_<N>_cmd|ack
}

-- shell-quote whitelist
local function is_plain(s)
  return type(s) == "string"
    and not not s:match("^[A-Za-z0-9_%-%.%_/ ]+$")
end

local function is_int(v, lo, hi)
  return type(v) == "number" and v == math.floor(v) and v >= lo and v <= hi
end

local function is_hex_bytes(s, minb, maxb)
  return type(s) == "string" and #s % 2 == 0
    and #s >= minb * 2 and #s <= maxb * 2
    and not s:find("[^0-9a-fA-F]")
end

local TYPES = {
  u8     = function(v) return is_int(v, 0, 255)   end,
  u16    = function(v) return is_int(v, 0, 65535) end,
  u32    = function(v) return is_int(v, 0, 4294967295) end,
  bool   = function(v) return is_int(v, 0, 1) or type(v) == "boolean" end,
  dbg    = function(v) return is_int(v, 0, 4)    end,
  dbgmod = function(v) return is_int(v, 1, 11)   end,
  tci    = function(v) return is_int(v, 0, 2)    end,
  frame  = function(v) return is_hex_bytes(v, 10, 1976) end,
  attr   = function(v) return is_int(v, 0, 65535) end,
  text   = function(v) return is_plain(v) end,
}

local function argval(v)
  if type(v) == "boolean" then return v and 1 or 0 end
  return tostring(v)
end

local function pipe_cmd(cmd, timeout_ds)
  if not is_plain(cmd) then
    return nil, "cmd outside plain grammar"
  end
  local secs = math.floor(((timeout_ds or 50) + 5) / 10)
  if secs < 1 then secs = 1 end
  local p = io.popen("timeout " .. secs .. " omci_pipe.sh " ..
    tostring(M.cli_pipe) .. " '" .. cmd .. "' 2>/dev/null")
  if not p then
    return nil, "omci_pipe.sh unavailable"
  end
  local out = p:read("*a")
  p:close()
  if not out or out == "" then
    return nil, "no ack " .. secs .. "s)"
  end
  return (out:gsub("%s+$", ""))
end

-- errorcode=N present and nonzero = refused; absent = answered
local function ack_ok(out)
  local code = out:match("errorcode=(-?%d+)")
  if code and code ~= "0" then
    return false, "errorcode=" .. code
  end
  return true, nil
end

local function kv_parse(out)
  local t = {}
  for k, v in out:gmatch('([%w_]+)=("[^"]*")') do
    t[k] = v:sub(2, -2)
  end
  for k, v in out:gmatch("([%w_]+)=([%-]?[%w_:./-]+)") do
    if not t[k] then t[k] = tonumber(v) or v end
  end
  return t
end

function M.cli(cmd, timeout_ds)
  return pipe_cmd(cmd, timeout_ds)
end

function M.run(cmd, timeout_ds)
  local out, err = pipe_cmd(cmd, timeout_ds)
  if not out then
    return nil, err
  end
  local ok, why = ack_ok(out)
  return ok, why, out
end

-- long form = { short, args={types}, result }: nil/"ok"->ok,err | "raw"->ok,err,raw
-- "num"->value,err | "kvn:K"/"kvs:K"->value,err (numeric/quoted field K)
-- "table"->kv,err | "text"->text,err | "supported"->true/false/nil,err
local CMDS = {
  attr_avc_send =                { "aas",   { "u16", "u16", "attr" } },
  attr_change =                  { "ac",    { "u16", "u16", "attr" } },
  alarm_seq_num_reset =          { "asnr" },
  action_timeout_get =           { "atg",   nil, "kvn:action_timeout" },
  action_timeout_set =           { "ats",   { "u32" } },
  class_dump_all =               { "cda",   nil, "text" },
  class_dump_xml =               { "cdx",   nil, "text" },
  class_get =                    { "cg",    { "u16" }, "text" },
  class_prop_get =               { "cpg",   { "u16" }, "raw" },
  dbg_level_get =                { "dlg",   nil, "text" },
  dbg_level_set =                { "dls",   { "dbg" } },
  dbg_module_level_get =         { "dmlg",  { "dbgmod" }, "kvn:level" },
  dbg_module_level_set =         { "dmls",  { "dbgmod", "dbg" } },
  failsafe_enable =              { "fe",    { "bool" } },
  interval_end =                 { "ie",    { "u8" } },
  iop_mask_get =                 { "img",   nil, "kvn:iop_mask" },
  iop_mask_set =                 { "ims",   { "u32", "u32" } },
  msg_counters_get =             { "mcg",   nil, "table" },
  mib_dump =                     { "md",    nil, "text" },
  mib_dump_all =                 { "mda",   nil, "text" },
  msg_dump_console_enable =      { "mdce",  { "bool" } },
  msg_dump_disable =             { "mdd" },
  msg_dump_file_enable =         { "mdfe",  { "text" } },
  mib_dump_xml =                 { "mdx",   nil, "text" },
  managed_entity_attr_data_get = { "meadg", { "u16", "u16", "attr" }, "raw" },
  managed_entity_attr_data_set = { "meads", { "u16", "u16", "attr", "text" } },
  managed_entity_alarm_get =     { "meag",  { "u16", "u16", "attr" }, "raw" },
  managed_entity_attr_offset_get = { "meaog", { "u16", "attr" }, "kvn:offset" },
  managed_entity_attr_prop_get = { "meapg", { "u16", "attr" }, "raw" },
  managed_entity_alarm_set =     { "meas",  { "u16", "u16", "attr", "u8" } },
  managed_entity_attr_size_get = { "measg", { "u16", "attr" }, "kvn:size" },
  managed_entity_attr_type_get = { "meatg", { "u16", "attr" }, "kvn:type" },
  managed_entity_attr_version_get = { "meavg", { "u16", "attr" }, "kvn:version" },
  managed_entity_create =        { "mec",   { "u16", "u16" } },
  managed_entity_count_get =     { "mecg",  { "u16" }, "kvn:count" },
  managed_entity_delete =        { "med",   { "u16", "u16" } },
  managed_entity_get =           { "meg",   { "u16", "u16", "u8" }, "raw" },
  managed_entity_inst_count_get = { "meicg", { "u16" }, "kvn:count" },
  managed_entity_is_supported =  { "meis",  { "u16" }, "supported" },
  msg_num_get =                  { "mng",   nil, "table" },
  msg_pool_size =                { "mps",   nil, "kvn:num" },
  mib_reset =                    { "mr",    { "bool" } },
  mib_store =                    { "ms" },
  pa_dbg_module_level_get =      { "pdmlg", { "dbgmod" }, "kvn:dbg_level" },
  pa_dbg_module_level_set =      { "pdmls", { "dbgmod", "dbg" } },
  processing_enable =            { "pe",    { "bool" } },
  raw_message_recv =             { "rmr",   { "frame" } },
  raw_message_send =             { "rms",   { "frame" } },
  status =                       { "s",     nil, "table" },
  sw_image_upgrade =             { "swiu",  { "u16", "bool", "bool", "text" } },
  tci_check_ignore =             { "tcici", { "tci" } },
  version_information_get =      { "vig",   nil, "kvs:omci_version" },
}

local function getter(out, result)
  local kind, key = result:match("^(%a+):?([%w_]*)$")
  if kind == "kvn" then
    local v = out:match(key .. "=(%-?%d+)")
    if not v then
      return nil, "no " .. key .. " in ack: " .. out:sub(1, 60)
    end
    return tonumber(v)
  end
  if kind == "kvs" then
    local v = out:match(key .. '="([^"]*)"') or out:match(key .. "=(%S+)")
    if not v then
      return nil, "no " .. key .. " in ack: " .. out:sub(1, 60)
    end
    return v
  end
end

for long, spec in pairs(CMDS) do
  local short, argtypes, result = spec[1], spec[2], spec[3]
  local nargs = argtypes and #argtypes or 0
  M[long] = function(...)
    local n = select("#", ...)
    if n < nargs or n > nargs + 1 then
      return nil, long .. ": expected " .. nargs ..
        " args (optional trailing timeout_ds)"
    end
    local parts = { short }
    for i = 1, nargs do
      local v = select(i, ...)
      if not TYPES[argtypes[i]](v) then
        return nil, long .. ": arg " .. i .. " invalid (" ..
          argtypes[i] .. ")"
      end
      parts[#parts + 1] = tostring(argval(v))
    end
    local timeout_ds = select(nargs + 1, ...)
    local out, err = pipe_cmd(table.concat(parts, " "), timeout_ds)
    if not out then
      return nil, err
    end
    if not result or result == "ok" or result == "raw" then
      local ok, why = ack_ok(out)
      return ok, why, out
    elseif result == "text" then
      local ok, why = ack_ok(out)
      if not ok then
        return nil, why, out
      end
      return out
    elseif result == "table" then
      local t = kv_parse(out)
      if not t.errorcode then
        return nil, "unparsable ack: " .. out:sub(1, 60)
      end
      return t
    elseif result == "supported" then
      local code = out:match("errorcode=(-?%d+)")
      if not code then
        return nil, "unparsable ack: " .. out:sub(1, 60)
      end
      return code == "0", nil, out
    else
      return getter(out, result)
    end
  end
end
return M
