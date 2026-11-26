"""Prepare or execute a bounded HTTPS bring-up campaign; preparation is offline.

Execution requires --execute and an installation record whose evidence files
match their hashes. No installer, configuration write, module reset, or retry is
provided. A completed run still needs independent peer-log audit.
"""
import argparse
import hashlib
import ipaddress
import json
import os
import platform
from pathlib import Path
import re
import sys
import time
from urllib.parse import urlsplit

BODY = bytes(range(256))*8
BODY_HASH = hashlib.sha256(BODY).hexdigest()
REQUIRED_GATES = ('installation_readback', 'controller_identity', 'fpga_identity',
                  'configuration_backup', 'recovery_review', 'journal_ready',
                  'clock_ready', 'trusted_endpoint', 'basic_and_command_interface',
                  'c64_reset_after_run_authorized')
DEFINES = {'WAIT_WRAPS': '$80', 'EXTERNAL_ABORT': 1}
HELD = ['physical module restart and incarnation fencing',
        'certificate date rejection with isolated trusted fixtures',
        'native stack and global/scoped memory measurements',
        'journal persistence/endurance and power-loss recovery']


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def clock_domain():
    return dict(host=platform.node(),system=platform.system(),
                clock=time.get_clock_info('monotonic').implementation)


def validate_peers(record, plan):
    require(set(record['peer_logs'])==set(plan['endpoints']), 'Peer logs must cover every endpoint')
    for name,path in record['peer_logs'].items():
        events=[json.loads(line) for line in Path(path).read_text().splitlines()]
        require(events and events[0]['event']=='ready','Peer has no ready record')
        ready=events[0]
        require(ready.get('clock_domain')==clock_domain(),
                'Peer/runner clock domains differ; Windows and WSL logs cannot be mixed')
        require(0<=time.monotonic()-ready['monotonic']<900,'Stale or incompatible peer clock')
        require(not any(e['event']=='stopped' for e in events),'Peer already stopped')
        require(not any(e['event']=='accepted' for e in events),'Use fresh peer logs for this campaign')
        require(ready['mode']==('http' if name=='http' else 'handshake-stall' if name=='stall' else 'tls'), 'Peer mode')
        require(ready['address'][1]==urlsplit(plan['endpoints'][name]).port,'Peer port mismatch')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def cases():
    rows=[]
    def add(endpoint, route='ok', method='GET', expected='success'):
        rows.append(dict(id='h%03d' % (len(rows)+1), endpoint=endpoint,
                         route=route, method=method, expected=expected))
    for method in ('GET','HEAD'): add('http',method=method)
    for method in ('GET','HEAD'):
        for _ in range(5): add('trusted',method=method)
    for _ in range(2): add('trusted','chunked')
    for endpoint in ('wrong_name','untrusted'):
        add(endpoint,expected='authentication');add('trusted')
    for endpoint,route in (('stall','ok'),('trusted','delay-headers'),('trusted','read-stall')):
        add(endpoint,route,expected='timeout');add('trusted')
    for endpoint,route in (('stall','ok'),('trusted','read-stall')):
        add(endpoint,route,expected='cancel');add('trusted')
    for i in range(10):
        add('trusted')
        if i%2==0: add('wrong_name',expected='authentication')
    return rows


def validate_plan(plan):
    require(plan['schema']==1 and plan['cases']==cases(), 'Fixed case matrix changed')
    require(plan['held']==HELD, 'Unqualified cases must remain explicit')
    require(plan['request_guard_seconds']==35 and plan['idle_seconds']==180, 'Bounds changed')
    require(set(plan['endpoints'])=={'http','trusted','wrong_name','untrusted','stall'}, 'Endpoint set')
    for key,value in plan['endpoints'].items():
        parsed=urlsplit(value)
        require(parsed.scheme==('http' if key=='http' else 'https') and parsed.hostname and
                not parsed.username and not parsed.password and not parsed.query and not parsed.fragment and
                parsed.path in ('','/') and parsed.port, 'Explicit endpoint origin required: '+key)
    for item in plan['cases']:
        url=plan['endpoints'][item['endpoint']].rstrip('/')+'/case/'+item['id']+'/'+item['route']
        require(len(url.encode('ascii'))<800, 'URL too long')


def validate_installation(record, plan, base):
    require(record['plan_sha256']==hashlib.sha256(json.dumps(plan,sort_keys=True).encode()).hexdigest(), 'Plan identity mismatch')
    require(record['candidate_sha256']==plan['candidate_sha256'], 'Candidate identity mismatch')
    require(re.fullmatch('[0-9a-f]{64}',record['candidate_sha256']) is not None,'Candidate hash')
    address=ipaddress.ip_address(record['device'])
    require(address.version==4 and address.is_private and not address.is_loopback,'LAN IPv4 required')
    for name in REQUIRED_GATES:
        gate=record['gates'][name]
        require(gate['confirmed'] is True, 'Unconfirmed gate: '+name)
        path=(base/gate['evidence']).resolve()
        require(path.is_relative_to(base.resolve()) and path.is_file(),'Missing local evidence: '+name)
        require(digest(path)==gate['sha256'],'Changed evidence: '+name)
    require(record['expires_unix']>time.time() and record['expires_unix']<=time.time()+86400,
            'Installation/operator preflight record must expire within 24 hours')


def decode(block, data, cancelled=False):
    require(len(block)==512,'Truncated native result')
    count=block[1]; length=int.from_bytes(block[2:4],'little'); flags=block[6]
    require(length==len(data) and length<=4096,'Body length mismatch')
    require(block[5]&0x3f==0,'Interface did not return to Idle')
    require(flags==(8 if cancelled else 0),'Native agent timeout/truncation/cancellation flags')
    require(count<=8 and (cancelled or count>0),'Invalid block count')
    lengths=[int.from_bytes(block[0x30+2*i:0x32+2*i],'little') for i in range(count)]
    require(all(a<=b for a,b in zip([0]+lengths,lengths)) and (not lengths or lengths[-1]==length),
            'Native block boundaries inconsistent')
    return block[0x100:0x100+block[4]].rstrip(b'\0')


def check_exchange(case, block, data, elapsed):
    expected=case['expected'];status=decode(block,data,expected=='cancel')
    require(0<=elapsed<=35,'Host request guard exceeded')
    if expected=='success':
        require(status.startswith(b'HTTP/1.1 200 OK'),'Expected successful HTTP response')
        require(data==(b'' if case['method']=='HEAD' else BODY),'Response bytes differ')
    elif expected!='cancel':
        require(status==b'503 SERVICE UNAVAILABLE' and not data,'Failure leaked response data or wrong status')
    else:
        require(not data,'Cancellation leaked response data')
    return status.decode('latin1')


def run(plan, installation, out):
    # Imported only after all offline execution gates have passed.
    root=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(root/'tests/lib'))
    import bootstrap  # noqa: F401
    from api import UltimateApi
    import rest
    from uci_native import NativeUci
    rest.TRANSPORT_RETRIES=1
    api=UltimateApi(installation['device'],os.environ.get('U64_PASSWORD'),timeout=1)
    report=dict(schema=1,complete=False,hardware_tested=True,status='running',cases=[],held=HELD,clock_domain=clock_domain(),
                plan_sha256=hashlib.sha256(json.dumps(plan,sort_keys=True).encode()).hexdigest())
    def save(): (out/'run.json').write_text(json.dumps(report,indent=2)+'\n')
    save()
    events=(out/'transactions.jsonl').open('x',encoding='utf-8')
    native=NativeUci(api.machine,api.runners,busy_timeout=35,agent_defines=DEFINES)
    started=time.monotonic()
    def stop_check():
        require(not (out/'operator.stop').exists(),'Operator stop; no retry or next case')
        require(time.monotonic()-started<900,'Campaign deadline')
    def transact(command):
        stop_check();begin=time.monotonic()
        try:
            block,data=native._run(command,0,4096)
            row=dict(command=command.hex(),start=begin,end=time.monotonic(),block=block.hex(),data=data.hex())
            events.write(json.dumps(row)+'\n');events.flush()
            return block,data,row
        except BaseException as error:
            events.write(json.dumps(dict(command=command.hex(),start=begin,end=time.monotonic(),
                                         error=repr(error)))+'\n');events.flush();raise
    try:
        # No configuration changes. The operator must have enabled the interface.
        require(api.configs.current('C64 and Cartridge Settings','Command Interface')=='Enabled',
                'Command interface is not enabled')
        native.start()
        for case in plan['cases']:
            stop_check()
            block,data,_=transact(bytes.fromhex('0610'));require(decode(block,data)==b'00,OK','FREE_ALL failed')
            url=plan['endpoints'][case['endpoint']].rstrip('/')+'/case/'+case['id']+'/'+case['route']
            block,data,_=transact(bytes([6,0x11,6 if case['method']=='HEAD' else 1])+url.encode()+b'\0')
            require(decode(block,data)==b'00,OK' and len(data)==1 and data[0]<16,'Header creation failed')
            command=bytes([6,0x32,data[0],0xff]);cancel_at=None
            peerfile=Path(installation['peer_logs'][case['endpoint']])
            require(peerfile.is_file(),'Peer log missing')
            peer_offset=peerfile.stat().st_size
            exchange_start=time.monotonic()
            def poll():
                nonlocal cancel_at
                stop_check()
                if case['expected']=='cancel' and cancel_at is None:
                    with peerfile.open('rb') as peer:
                        peer.seek(peer_offset);new=peer.read().splitlines()
                    for line in new:
                        try: event=json.loads(line)
                        except json.JSONDecodeError: continue
                        if event.get('event')=='stall_started' and event['monotonic']>=exchange_start:
                            api.machine.writemem(0xc00a,b'\1');cancel_at=time.monotonic();break
            native.poll_hook=poll
            block,data,row=transact(command);native.poll_hook=None
            status=check_exchange(case,block,data,row['end']-row['start'])
            if case['expected']=='cancel':
                require(cancel_at is not None,'No cancellation delivered')
                require(row['end']-cancel_at<=1.5,'Cancellation did not settle within external guard')
            report['cases'].append(dict(**case,status=status,start=row['start'],end=row['end'],
                                        cancel_at=cancel_at,data_sha256=hashlib.sha256(data).hexdigest()))
            save()
        block,data,_=transact(bytes.fromhex('0610'));require(decode(block,data)==b'00,OK','Final cleanup failed')
        idle_start=time.monotonic()
        while time.monotonic()-idle_start<180:
            stop_check();time.sleep(0.2)
        report['idle_seconds']=time.monotonic()-idle_start
        block,data,_=transact(bytes.fromhex('0610'));require(decode(block,data)==b'00,OK','Post-idle cleanup failed')
        api.machine.reset(force=True)  # C64 CPU only; explicitly gated above.
        report['c64_reset_to_basic_requested']=True
        report['status']='awaiting-independent-peer-audit'
    except BaseException as error:
        report['status']='failed-or-stopped';report['error']=repr(error);save();raise
    finally:
        events.close();report['transactions_sha256']=digest(out/'transactions.jsonl');save()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--installation',type=Path)
    parser.add_argument('--execute',action='store_true')
    args=parser.parse_args();plan=json.loads(args.plan.read_text());validate_plan(plan)
    args.output.mkdir(parents=True,exist_ok=False)
    (args.output/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    if not args.execute:
        (args.output/'preparation.json').write_text(json.dumps(dict(offline=True,device_contact=False,
            case_count=len(cases()),plan_sha256=hashlib.sha256(json.dumps(plan,sort_keys=True).encode()).hexdigest(),
            held=HELD),indent=2)+'\n')
        print('Offline plan validated; no device contact.');return
    require(args.installation is not None,'Explicit installation record required')
    record=json.loads(args.installation.read_text())
    validate_installation(record,plan,args.installation.parent)
    validate_peers(record,plan)
    (args.output/'installation.json').write_text(json.dumps(record,indent=2)+'\n')
    run(plan,record,args.output)


if __name__=='__main__': main()
