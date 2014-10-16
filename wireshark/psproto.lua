-- pscdrv protocol disector for Wireshark
-- Handles message re-assembly
--
-- See http://wiki.wireshark.org/LuaAPI

-- Message wrapper protocol
psc = Proto("psc", "PSC Protocol")

local flit = ProtoField.string("psc.frame", "Frame Marker")
local fmid = ProtoField.uint16("psc.id", "Message ID")
local fmsz = ProtoField.uint32("psc.size", "Message Size")
local fmbd = ProtoField.bytes ("psc.bytes", "Message Body")

psc.fields = {flit, fmid, fmsz, fmbd}

-- DissectorTable for customizing interpretion of body
-- of specific message IDs
local msgtbl = DissectorTable.new("psc.id", "Message ID",
    ftypes.UINT16, base.DEC)

-- As a workaround for
-- https://bugs.wireshark.org/bugzilla/show_bug.cgi?id=3513
-- Provide our field information to msgtbl dissectors
-- in a global.
-- Clearly no thread safe, but I don't know if it needs to be...
PSCFieldInfo = nil

function psc.dissector (buf, pkt, root)

  pkt.cols.protocol = psc.name
  pkt.cols.info:clear()
  
  local consumed = 0

  while buf:len()>0
  do
    if buf:len() < 8
    then
      -- not enough bytes to finish decode
      -- indicate how many bytes were consumed,
      -- and how many more are needed to continue
      pkt.desegment_offset = consumed
      pkt.desegment_len = 8 - buf:len()
      return
    end

    local pid = buf(0,2)
    local bid = buf(2,2)
    local bsz = buf(4,4)

    -- check framing constant
    if pid:string() ~= "PS"
    then
      local t = root:add(psc, buf(0,8))
      local f = t:add(flit, pid)
      f:add_expert_info(PI_MALFORMED, PI_ERROR, "Framing Error")
      t:add(fmid, bid)
      t:add(fmsz, bsz)
      pkt.cols.info:append('[Framing error]')
      return
    end

    local blen = bsz:uint()
    local mlen = 8 + blen

    if buf:len() < mlen
    then
      pkt.desegment_offset = consumed
      pkt.desegment_len = mlen - buf:len()
      return
    end

    local t = root:add(psc, buf(0,mlen))
    --t:add_expert_info(PI_MALFORMED, PI_ERROR, "Hello")
    t:add(flit, pid)
    t:add(fmid, bid)
    t:add(fmsz, bsz)

    local subd = msgtbl:get_dissector(bid:uint())
    if subd
    then
      -- custom interpretation
      PSCFieldInfo = {["psc.id"]=bid:uint()}
      subd:call(buf(8, blen):tvb(), pkt, t)
      PSCFieldInfo = nil
    else
      -- Interpret body as bytes...
      t:add(fmbd, buf(8, blen))
      pkt.cols.info:append("Msg:"..bid:uint().." Size:"..blen..", ")
    end

    consumed = consumed + mlen
    buf = buf(mlen):tvb()
  end
end

local ttbl = DissectorTable.get("tcp.port")
-- port 8765 is arbitrary, change as necessary
ttbl:add(8765, psc)

print("Loaded psproto.lua")
