-- Example of specializing dissector
-- Works with pscsim example database and pscsim.py
--
-- See http://wiki.wireshark.org/LuaAPI

print("Loading pscsimproto.lua")

-- Special handling of messages where body is
-- an ASCII string
pscstring = Proto("pscstring", "PSC Message")

local fstr = ProtoField.string("pscstring.msg", "Message")

pscstring.fields = {fstr}

-- https://bugs.wireshark.org/bugzilla/show_bug.cgi?id=3513
-- prevents us from using Field
-- so we restort to the PSCFieldInfo global
--local blkid = Field.new("psc.id")

function pscstring.dissector (buf, pkt, root)
  -- When #3514 is fixed the the following will work
  --local msgid = blkid()
  -- until then...
  local msgid = PSCFieldInfo["psc.id"]

  root:add(fstr, buf(0))

  -- Should only append info (as there may be other messages
  -- already process in this frame.  For this reason what we
  -- add should end with ", "
  pkt.cols.info:append("Msg:"..msgid.." '"..buf(0):string().."', ")
end

pscsingle = Proto("pscsingle", "PSC Single Register write")

local faddr = ProtoField.uint32("pscsingle.addr", "Address")
local fval  = ProtoField.uint32("pscsingle.val" , "Value")

pscsingle.fields = {faddr, fval}

function pscsingle.dissector (buf, pkt, root)
  local msgid = PSCFieldInfo["psc.id"]
  if buf:len()<8
  then
    root:add_expert_info(PI_MALFORMED, PI_ERROR,
         "Message body too short to be single write")
    return
  end
  root:add(faddr, buf(0,4))
  root:add(fval, buf(4,4))
  pkt.cols.info:append("Msg:"..msgid.." '"..buf(4,4):uint()
  .." -> "..buf(0,4):uint().."', ")
end

local ptbl = DissectorTable.get("psc.id")

ptbl:add(42, pscstring)
ptbl:add(1015, pscstring)

ptbl:add(100, pscsingle)

print("Loaded pscsimproto.lua")
