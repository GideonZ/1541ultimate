#!/usr/bin/env python3
"""Execute the assembled 6502 repetition/control logic in py65, without a device.

Requires py65==1.2.0 and the repository assembler. Only the transaction routine
is replaced by a returning fixture; CPU instructions, history writes, counters,
stop handling and bounds are real. This is not an emulation of the UCI hardware.
"""
import subprocess
import tempfile
from pathlib import Path

from py65.devices.mpu6502 import MPU

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory(prefix='uci-repeat-') as temporary:
        root = Path(temporary)
        subprocess.run([str(ROOT/'tools/64tass/64tass'), '--cbm-prg',
                        '--labels='+str(root/'labels'), '-o', str(root/'agent.prg'),
                        str(ROOT/'tests/e2e/io/command_interface/uci_agent.asm')],
                       check=True, capture_output=True)
        labels = {}
        for line in (root/'labels').read_text().splitlines():
            if '=' in line:
                name, value = line.split('=', 1)
                if value.strip().startswith('$'):
                    labels[name.strip()] = int(value.strip()[1:], 16)
        binary = (root/'agent.prg').read_bytes()
        cases = [(0, 0, None, 0, 1), (1, 0, None, 1, 1), (3, 0, None, 3, 3),
                 (255, 0, None, 32, 32), (20, 1, None, 1, 1), (20, 0, 1, 2, 2)]
        for repeat, error, stop_after, wanted_history, wanted_sequence in cases:
            memory = [0]*65536
            load = int.from_bytes(binary[:2], 'little')
            memory[load:load+len(binary)-2] = binary[2:]
            memory[labels['transact']] = 0x60  # RTS: substitute the UCI transaction only.
            memory[0xC5FF] = memory[0xCA00] = 0xBD
            cpu = MPU(memory=memory, pc=labels['start'])
            while cpu.pc != labels['main_wait']:
                cpu.step()
            memory[0xC400:0xC407] = [0x20, 1, 0, 0, 23, 0, error]
            status = b'503 SERVICE UNAVAILABLE'
            memory[0xC500:0xC500+len(status)] = status
            memory[0xC00B], memory[0xC000] = repeat, 1
            for _ in range(50000):
                if stop_after and memory[0xC00C] == stop_after and cpu.pc == labels['main_wait']:
                    memory[0xC00B] = 1
                if memory[0xC000] == 0 and cpu.pc == labels['main_wait']:
                    break
                cpu.step()
            else:
                raise AssertionError('Native loop failed to terminate')
            assert memory[0xC001] == wanted_sequence
            assert memory[0xC00C] == wanted_history and memory[0xC00D] == error
            assert memory[0xC00B] == 0
            assert memory[0xC5FF] == memory[0xCA00] == 0xBD
            for index in range(wanted_history):
                entry = bytes(memory[0xC600+32*index:0xC620+32*index])
                assert entry[:5] == bytes([error, 0, 0, 0, 23])
                assert entry[5:28] == status
        print('Native repetition: 6 CPU-executed cases passed (one-shot, repeat, cap, error, stop, history bounds).')


if __name__ == '__main__':
    main()
