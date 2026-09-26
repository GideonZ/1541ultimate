#!/usr/bin/env python3
"""Run the complete production DMA ISR with simulated registers and queues.

The test substitutes only hardware/RTOS operations. It executes the exact
DmaUartInterrupt function extracted from dma_uart.cc, including its loop and
callback ordering. No firmware or device needed.
"""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
source = (ROOT.parent/'dma_uart.cc').read_text()
start = source.index('uint8_t DmaUART::DmaUartInterrupt(void *context)')
end = source.index('\nvoid DmaUART :: SetReceiveCallback', start)
defines = source[source.index('#define DMAUART_RxInterrupt'):source.index('/*', source.index('#define DMAUART_RxInterrupt'))]
# Definitions end before the driver's explanatory block.
prefix = (ROOT/'rx_rearm_fixture.cc').read_text()
marker = '// INSERT_PRODUCTION_ISR'
assert prefix.count(marker) == 1
with tempfile.TemporaryDirectory(prefix='dma-rx-rearm-') as directory:
    path = Path(directory)
    unit = path/'test.cc'
    unit.write_text(prefix.replace(marker, defines+'\n'+source[start:end]))
    subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-g',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all',
                    str(unit),'-o',str(path/'test')], check=True)
    subprocess.run([str(path/'test')],check=True,timeout=10)
