"""Execute the actual RAM package in an RV32I/MMIO model with a write allowlist.

This checks binary behavior, not physical FPGA/UART timing or display readiness.
No socket, hardware transport, system emulator, or firmware loader is invoked.
"""
import json
from pathlib import Path
import struct
import sys

MASK = 0xffffffff
BASE = 0x03000000
UART = 0x10060900
SCREEN = 0x10141000

def signed(n, bits=32):
    return (n & ((1 << (bits-1))-1)) - (n & (1 << (bits-1)))

class Machine:
    def __init__(self, package, scenario):
        address, size, start = struct.unpack('<III', package[:12])
        assert address == start == BASE and len(package) == size + 12
        assert 0 < size < 65536
        self.ram = bytearray(0x100000)
        self.ram[:size] = package[12:]
        self.r = [0] * 32
        self.pc = start
        self.scenario = scenario
        self.steps = self.sent = self.resets = 0
        self.regs = {0: 19, 1: 0, 3: 5}
        if scenario == 'wrong-baud': self.regs[0] = 86
        if scenario == 'module-off': self.regs[3] = 0x35
        self.video = {4: 48, 5: 25}
        self.screen = bytearray(4096)
        self.colors = bytearray(4096)
        self.pending = False
        self.rx_address = None
        self.packet = b''
        self.write_addresses = set()

    def read8(self, address):
        if BASE <= address < BASE + len(self.ram): return self.ram[address-BASE]
        if address == 0x1000000b: return 121 if self.scenario == 'wrong-fpga' else 122
        timer = (self.steps // 40 + (65000 if self.scenario == 'timer-wrap' else 0)) & 65535
        if address == 0x10000022: return timer >> 8
        if address == 0x10000023: return timer & 255
        if 0x10140000 <= address < 0x1014000e: return self.video.get(address - 0x10140000, 0)
        if UART <= address < UART + 16:
            off = address - UART
            if off == 2:
                return 0xc0 | (32 if self.pending else 0) | (8 if self.scenario == 'overflow' else 0)
            if off == 8: return len(self.packet) & 255
            if off == 9: return len(self.packet) >> 8
            return self.regs.get(off, 0)
        raise AssertionError(f'Forbidden read {address:08x}')

    def write8(self, address, value):
        value &= 255
        if BASE <= address < BASE + len(self.ram):
            self.ram[address-BASE] = value
            return
        self.write_addresses.add(address)
        if address == 0x10000000:
            assert value == 0
            return
        if SCREEN <= address < SCREEN + 4096:
            self.screen[address-SCREEN] = value
            return
        if 0x10142000 <= address < 0x10143000:
            self.colors[address-0x10142000] = value
            return
        if address in (0x1014000a, 0x1014000b, 0x1014000d):
            assert value == (0xc0 if address == 0x1014000d else 0)
            self.video[address-0x10140000] = value
            return
        if UART <= address < UART + 16:
            off = address - UART
            assert off in (2,3,4,5,6,7,8,9,10,11,12,13,14,15)
            if off == 3:
                # No enable/boot/route/baud mutation and no module restart.
                assert value == 0x85 and self.regs[3] == 5
                self.resets += 1
                self.pending = False
                self.rx_address = None
                return
            if off == 2:
                assert value == 15
                return
            if off == 11:
                assert value == 1
                self.pending = False
                return
            self.regs[off] = value
            if off == 15:
                self.rx_address = sum(self.regs.get(12+i,0) << (8*i) for i in range(4))
                assert BASE <= self.rx_address < BASE + len(self.ram) - 65536
            if off == 10:
                assert value == 1 and self.sent == 0
                self.sent += 1
                tx = sum(self.regs.get(4+i,0) << (8*i) for i in range(4))
                count = self.regs[8] | self.regs[9] << 8
                assert count == 4
                request = bytes(self.read8(tx+i) for i in range(count))
                assert request[:2] == b'\x02\xfe'  # only runtime IDENTIFY
                minor = 21 if self.scenario == 'equal' else 22 if self.scenario == 'newer' else 20
                name = f'ESP32 WiFi Bridge V1.{minor}'.encode() + b'\0'
                packet = request + struct.pack('<HH', 1, minor) + name + b'\xa5'*3
                if self.scenario == 'malformed': packet = b'\x03' + packet[1:]
                if self.scenario == 'truncated': packet = packet[:7]
                if self.scenario == 'stale': packet = packet[:2] + b'\0\0' + packet[4:]
                self.packet = packet
                if self.scenario != 'timeout':
                    assert self.rx_address is not None
                    for i, b in enumerate(packet): self.write8(self.rx_address+i, b)
                    self.pending = True
            return
        raise AssertionError(f'Forbidden write {address:08x}={value:02x}')

    def load(self, address, size):
        return sum(self.read8(address+i) << (8*i) for i in range(size))

    def step(self):
        ins = self.load(self.pc, 4)
        assert ins & 3 == 3  # no compressed instructions
        op, rd, f3, a, b, f7 = ins & 127, ins >> 7 & 31, ins >> 12 & 7, ins >> 15 & 31, ins >> 20 & 31, ins >> 25
        va, vb = self.r[a], self.r[b]
        imm = signed(ins >> 20, 12)
        pc, nxt, value = self.pc, (self.pc + 4) & MASK, None
        if op == 0x37: value = ins & 0xfffff000
        elif op == 0x17: value = pc + (ins & 0xfffff000)
        elif op == 0x6f:
            off = ((ins >> 31) << 20) | ((ins >> 12 & 255) << 12) | ((ins >> 20 & 1) << 11) | ((ins >> 21 & 1023) << 1)
            value, nxt = nxt, (pc + signed(off,21)) & MASK
        elif op == 0x67:
            assert f3 == 0
            value, nxt = nxt, (va + imm) & MASK & ~1
        elif op == 0x63:
            off = ((ins >> 31) << 12) | ((ins >> 7 & 1) << 11) | ((ins >> 25 & 63) << 5) | ((ins >> 8 & 15) << 1)
            take = {0: va == vb, 1: va != vb, 4: signed(va) < signed(vb), 5: signed(va) >= signed(vb), 6: va < vb, 7: va >= vb}[f3]
            if take: nxt = (pc + signed(off,13)) & MASK
        elif op == 3:
            width = {0:1,1:2,2:4,4:1,5:2}[f3]
            value = self.load((va + imm) & MASK, width)
            if f3 < 4: value = signed(value,width*8)
        elif op == 0x23:
            off = signed(((ins >> 25) << 5) | (ins >> 7 & 31),12)
            width = {0:1,1:2,2:4}[f3]
            for i in range(width): self.write8((va+off+i)&MASK, vb >> (8*i))
        elif op == 0x13:
            sh = (ins >> 20) & 31
            value = {0: lambda: va+imm, 2: lambda: int(signed(va)<imm), 3: lambda: int(va<(imm&MASK)), 4: lambda: va^(imm&MASK), 6: lambda: va|(imm&MASK), 7: lambda: va&(imm&MASK), 1: lambda: va<<sh, 5: lambda: (signed(va) if f7 == 32 else va)>>sh}[f3]()
        elif op == 0x33:
            assert f7 in (0,32)  # no M extension
            value = {0:lambda: va-vb if f7==32 else va+vb, 1:lambda:va<<(vb&31), 2:lambda:int(signed(va)<signed(vb)), 3:lambda:int(va<vb), 4:lambda:va^vb, 5:lambda:(signed(va) if f7==32 else va)>>(vb&31), 6:lambda:va|vb, 7:lambda:va&vb}[f3]()
        elif op == 0x0f: pass  # memory fence
        elif op == 0x73:
            assert ins == 0x30047073  # csrci mstatus,8, startup only
        else: raise AssertionError(f'Unsupported instruction {ins:08x} at {pc:08x}')
        if value is not None and rd: self.r[rd] = value & MASK
        self.pc = nxt
        self.steps += 1

    def run(self):
        stable = 0
        while self.steps < 2000000:
            self.step()
            if b'STOPPED.' in self.screen:
                stable += 1
                if stable > 2000: break
        else: raise AssertionError('Binary did not reach terminal display within instruction guard')
        good = self.scenario in ('valid','equal','newer','timer-wrap')
        assert (b'IDENTIFIED (VERSION' in self.screen) == good
        if not good: assert b'UNKNOWN - DO NOT INSTALL' in self.screen
        passive = self.scenario in ('module-off','wrong-baud','wrong-fpga')
        assert self.sent == (0 if passive or self.scenario == 'overflow' else 1)
        assert self.resets == (0 if passive else 2)
        return {'scenario':self.scenario,'instructions':self.steps,'identity_requests':self.sent,'local_dma_resets':self.resets,'write_allowlist_passed':True}

if __name__ == '__main__':
    package = Path(sys.argv[1]).read_bytes()
    results = []
    for scenario in ('valid','equal','newer','timer-wrap','module-off','wrong-baud','wrong-fpga','timeout','malformed','truncated','stale','overflow'):
        results.append(Machine(package,scenario).run())
    print(json.dumps({'binary_scenarios':results,'hardware_tested':False},indent=2))
