import struct

with open("result/ultimate.bin", "rb") as f:
    vd1 = 0
    vd2 = 0
    address = 0x10000
    while True:
        b = f.read(4)
        if len(b) < 4:
            break;
        (v, ) = struct.unpack(">L", b)
        if ((v & 0xFFFF0000) == 0xa2400000) and ((vd1 & 0xFFFF0000) == 0xb9f40000):
            print ("ERROR: {0:8x}:    {1:08x}".format(address-4, vd1))
            print ("ERROR: {0:8x}:    {1:08x}".format(address, v))
            
        if (((v >> 21) & 0x1F) == 18) and ((vd1 & 0xFFFF0000) == 0xb9f40000):
            print ("WARN:  {0:8x}:    {1:08x}".format(address-4, vd1))
            print ("WARN:  {0:8x}:    {1:08x}".format(address, v))

        # SRA   100100 Rd Ra 0* 0000001
        # SRC   100100 Rd Ra 0* 0100001
        # SRL   100100 Rd Ra 0* 1000001
        # BNEID 101111 10001  Ra   Imm  1011 1110 001R RRRR
        # BRLID 101110   Rd  10100 Imm  1011 10RR RRRD AL00  => B9F4 => b9E4 to disable branch delay slot
        
        #if ((v & 0xFC00000F) == 0x90000001):
        #    if((vd1 & 0xFFE0FFFF) == 0xBE20FFFC):
        #        r_iter = (vd1 & 0x001F0000) >> 16
        #        r_shft = (v   & 0x001F0000) >> 16
        #        print ("SHIFTLOOP: {0:8x}:    {1:08x} r{2:2d}".format(address-4, vd1, r_iter))
        #        print ("SHIFTLOOP: {0:8x}:    {1:08x}    r{2:d}".format(address, v, r_shft))

        vd2 = vd1
        vd1 = v
        address += 4




"""
   326dc:    b9f4a758     brlid    r15, -22696    // 9ce34 <memset>
   326e0:    10c00000     addk    r6, r0, r0
   326e4:    e896028c     lwi    r4, r22, 652
   326e8:    30a00014     addik    r5, r0, 20
   326ec:    a240001f     ori    r18, r0, 31
   326f0:    10640000     addk    r3, r4, r0
   326f4:    3252ffff     addik    r18, r18, -1
   326f8:    be32fffc     bneid    r18, -4        // 326f4
   326fc:    90630001     sra    r3, r3
   32700:    a46300ff     andi    r3, r3, 255
   32704:    10632000     addk    r3, r3, r4
   32708:    b000fffe     imm    -2
   3270c:    b9f4b4bc     brlid    r15, -19268    // 1dbc8 <_Znwj>
   32710:    a2400008     ori    r18, r0, 8         <-------- WRONG AFTER BRLID!! R18 is not saved
   32714:    12630000     addk    r19, r3, r0
   32718:    3252ffff     addik    r18, r18, -1
   3271c:    be32fffc     bneid    r18, -4        // 32718
   32720:    92730001     sra    r19, r19
   32724:    11130000     addk    r8, r19, r0
   32728:    e8d60298     lwi    r6, r22, 664
   3272c:    10a30000     addk    r5, r3, r0
   32730:    30e00100     addik    r7, r0, 256
"""
