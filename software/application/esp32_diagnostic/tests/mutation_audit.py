"""Demonstrate rejection of unsafe local diagnostic and updater mutations."""
import json
from pathlib import Path
import shutil
import subprocess
import sys

root, out = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(exist_ok=False)
cases = [
    ('flash-register-write', 'software/application/esp32_diagnostic/main.cc', '    diagnostic_line(1,', '    *(volatile uint8_t *)0x10060200u = 6;\n    diagnostic_line(1,', 'binary'),
    ('controller-reset', 'software/io/wifi/controller_probe.h', 'flow | 128', 'flow | 128 | 0x30', 'binary'),
    ('wrong-command', 'software/io/wifi/controller_probe.h', 'request[0] = 2;', 'request[0] = 3;', 'binary'),
    ('accept-malformed', 'software/io/wifi/controller_probe.h', 'result = controller_identity_parse(data, length, 0xfe, sequence, id)', 'result = true', 'binary'),
    ('bypass-identity-gate', 'software/application/update_u2p/update_https_u64ii.cc', 'if (!controller_preflight())', 'if (false && !controller_preflight())', 'installer'),
    ('accept-equal-or-unknown', 'software/application/update_u2p/update_https_u64ii.cc', 'if (decision != ControllerInstallDecision::Older)', 'if (false)', 'installer'),
    ('ignore-controller-write-failure', 'software/application/update_u2p/update_https_u64ii.cc', 'if (!update_esp32_impl())', 'if ((update_esp32_impl(), false))', 'installer'),
]
report = []
for name, rel, old, new, kind in cases:
    work = out / name
    for directory in ('software/application/esp32_diagnostic', 'target/u64ii/riscv/esp32_diagnostic'):
        shutil.copytree(root/directory, work/directory, ignore=shutil.ignore_patterns('result','__pycache__'))
    for file in ('software/io/wifi/controller_identity.h','software/io/wifi/controller_probe.h','software/application/update_u2p/update_https_u64ii.cc'):
        dest = work/file; dest.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(root/file,dest)
    changed = work/rel
    content = changed.read_text(); assert old in content
    changed.write_text(content.replace(old,new,1))
    if kind == 'binary':
        p = subprocess.run(['make','-C',str(work/'target/u64ii/riscv/esp32_diagnostic'),'CROSS=/opt/daily64-sdk/riscv-xpack/bin/riscv-none-elf-'],capture_output=True,text=True)
        (work/'build.log').write_text(p.stdout+p.stderr)
        assert p.returncode == 0, name + ': mutation must build before rejection counts'
        command = ['python3', str(root/'software/application/esp32_diagnostic/tests/rv32_audit.py'),str(work/'target/u64ii/riscv/esp32_diagnostic/result/esp32-identity.ue2')]
    else:
        command = ['python3', str(root/'software/application/esp32_diagnostic/tests/installer_test.py'),str(work),str(work/'tests')]
    p = subprocess.run(command,capture_output=True,text=True)
    (work/'rejection.log').write_text(p.stdout+p.stderr)
    assert p.returncode != 0, name + ': unsafe mutation escaped'
    # An assertion must reject behavior; compiler/import failures do not count.
    assert ('AssertionError' in p.stderr or 'Assertion `' in p.stdout or 'Assertion `' in p.stderr), name + ': not a behavioral rejection'
    report.append({'name':name,'rejected':True,'exit_code':p.returncode})
(out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
