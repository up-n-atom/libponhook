-- local OMCI = require("omci_frame")

-- one-shot
function on_load()
  omci_hook_log("on_load [" .. os.time() .. "]")
end

-- one-shot
function on_unload()
  omci_hook_log("on_unload [" .. os.time() .. "]")
end

-- MIB reset - MIB lock in-place
function on_reset()
  omci_hook_log("on_reset [" .. os.time() .. "]")
end

function on_reboot()
  omci_hook_log("on_reboot [" .. os.time() .. "]")
end

-- MIB mutable
function on_ready()
  omci_hook_log("on_ready [" .. os.time() .. "]")
end

-- return nil = drop frame
-- return f   = forward frame
function on_rx(f)
  -- omci_hook_log("on_rx " .. OMCI.summary(f))
  return f
end

-- return nil = drop frame
-- return f   = forward frame
function on_tx(f)
  -- omci_hook_log("on_tx " .. OMCI.summary(f))
  return f
end
