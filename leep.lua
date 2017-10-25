
print("Loading LEEP...")

local leep = Proto("leep", "LBNL Embedded Ethernet Protocol")

local header = ProtoField.uint64("leep.header", "Header", base.HEX)
local cmd = ProtoField.uint8("leep.cmd", "Command", base.HEX, {[0]="Write",[1]="Read"}, 0x10)
local addr = ProtoField.uint24("leep.addr", "Address", base.HEX)
local data = ProtoField.uint32("leep.data", "Data", base.HEX)
local junk = ProtoField.bytes("leep.junk", "Junk")

leep.fields = {header, cmd, addr, data, junk}

function leep.dissector (buf, pkt, root)
    pkt.cols.protocol = leep.name
    pkt.cols.info:clear()
  pkt.cols.info:append(pkt.src_port.."->"..pkt.dst_port.." ")

    local tree = root:add(leep, buf)

    if buf:len()<32
    then
        pkt.cols.info:append("Invalid (Truncated?)")
        return
    end

    pkt.cols.info:append(string.format("Header %08x %08x %06x",
                        buf(0,4):uint(), buf(4,4):uint(), buf(9,3):uint()))

    tree:add(header, buf(0,8))

    buf = buf(8):tvb()

    while buf:len()>=8
    do
        tree:add(cmd, buf(0,1))
        tree:add(addr, buf(1,3))
        tree:add(data, buf(4,4))

        buf = buf(8):tvb()
    end
    
    if buf:len()>0
    then
        tree:add(junk, buf(0))
    end
end

local utbl = DissectorTable.get("udp.port")
utbl:add(50006, leep)
