"""Reconcile complete native transactions with complete local endpoint logs.

This audit qualifies only the fixed bring-up matrix. It cannot qualify physical
restart identity, entropy, resource trends, or journal recovery.
"""
import argparse
import hashlib
import json
from pathlib import Path
from https_hardware_runner import cases, check_exchange, decode, digest, require, validate_plan


def audit(directory, peer_paths):
    directory=Path(directory)
    plan=json.loads((directory/'plan.json').read_text());validate_plan(plan)
    run=json.loads((directory/'run.json').read_text())
    require(run['status']=='awaiting-independent-peer-audit','Run incomplete/failed/stopped')
    require(run['plan_sha256']==hashlib.sha256(json.dumps(plan,sort_keys=True).encode()).hexdigest(),'Plan changed')
    require(run['transactions_sha256']==digest(directory/'transactions.jsonl'),'Raw evidence changed')
    require(run['idle_seconds']>=180 and run['c64_reset_to_basic_requested'] is True,'Final cleanup missing')
    records=[json.loads(line) for line in (directory/'transactions.jsonl').read_text().splitlines()]
    require(len(records)==len(cases())*3+2,'Missing/extra native transactions')
    require(len(run['cases'])==len(cases()),'Missing/extra case results')
    connections=[];hashes={};domain=None
    for path in sorted(set(map(str,peer_paths))):
        events=[json.loads(line) for line in Path(path).read_text().splitlines()]
        require(events and events[0]['event']=='ready' and events[-1]['event']=='stopped','Incomplete peer log')
        require(events[0].get('clock_domain') is not None,'Missing peer clock domain')
        if domain is None:domain=events[0]['clock_domain']
        require(events[0]['clock_domain']==domain,'Mixed peer clock domains')
        require(domain==run['clock_domain'],'Runner and peer clock domains differ')
        require(events[-1]['active']==0,'Peer cleanup incomplete')
        require(all(a['monotonic']<=b['monotonic'] for a,b in zip(events,events[1:])),'Peer clock order')
        require(not any(e['event'] in ('rejected','handler_error','request_rejected','unexpected_input_during_stall') for e in events),
                'Unexpected peer activity')
        accepted=[e for e in events if e['event']=='accepted']
        require(len(accepted)==events[-1]['accepted_attempts'],'Peer attempt count')
        for accept in accepted:
            related=[e for e in events if e.get('connection')==accept['connection']]
            require(sum(e['event']=='connection_closed' for e in related)==1,'Missing/duplicate connection closure')
            connections.append((events[0]['mode'],related))
        hashes[path]=digest(path)
    used=set()
    for index,(case,result) in enumerate(zip(cases(),run['cases'])):
        require(all(result[k]==v for k,v in case.items()),'Case identity/order changed')
        reset,header,exchange=records[index*3:index*3+3]
        require(reset['command']=='0610' and decode(bytes.fromhex(reset['block']),bytes.fromhex(reset['data']))==b'00,OK','Reset missing')
        header_data=bytes.fromhex(header['data'])
        url=plan['endpoints'][case['endpoint']].rstrip('/')+'/case/'+case['id']+'/'+case['route']
        require(header['command']==(bytes([6,0x11,6 if case['method']=='HEAD' else 1])+url.encode()+b'\0').hex(),'URL/method changed')
        require(decode(bytes.fromhex(header['block']),header_data)==b'00,OK' and len(header_data)==1,'Header result')
        require(exchange['command']==bytes([6,0x32,header_data[0],0xff]).hex(),'Exchange handle')
        require(result['start']==exchange['start'] and result['end']==exchange['end'],'Case timing changed')
        data=bytes.fromhex(exchange['data'])
        require(hashlib.sha256(data).hexdigest()==result['data_sha256'],'Body hash')
        require(check_exchange(case,bytes.fromhex(exchange['block']),data,exchange['end']-exchange['start'])==result['status'],'Result status')
        matches=[(i,mode,events) for i,(mode,events) in enumerate(connections)
                 if exchange['start']<=events[0]['monotonic']<=exchange['end']]
        require(len(matches)==1,'Expected exactly one peer connection per exchange')
        number,mode,events=matches[0];require(number not in used,'Reused peer connection');used.add(number)
        require(mode==('http' if case['endpoint']=='http' else 'handshake-stall' if case['endpoint']=='stall' else 'tls'),'Wrong peer protocol')
        requests=[e for e in events if e['event']=='request']
        if case['expected']=='authentication':
            require(not requests and not any(e['event']=='handshake_complete' for e in events),'Authentication failure reached HTTP')
            require(any(e['event']=='connection_error' for e in events),'No failed TLS handshake observed')
        elif case['endpoint']!='stall':
            require(len(requests)==1 and requests[0]['case']==case['id'] and requests[0]['method']==case['method']
                    and requests[0]['route']==case['route'],'Peer request mismatch')
        if case['expected']=='success':
            sent=[e for e in events if e['event']=='response_sent']
            require(len(sent)==1 and sent[0]['body_bytes']==len(data),'Incomplete peer response')
            if data:require(sent[0]['body_sha256']==result['data_sha256'],'Peer body hash')
        if case['expected'] in ('timeout','cancel'):
            require(any(e['event']=='stall_started' for e in events),'No actual stalled peer')
            require(any(e['event']=='peer_closed_during_stall' for e in events),'Client closure not observed during stall')
        closed=next(e for e in events if e['event']=='connection_closed')
        require(closed['monotonic']<=exchange['end']+1.5,'Peer cleanup exceeded external guard')
        if case['expected']=='cancel':
            require(result['cancel_at'] is not None and 0<=exchange['end']-result['cancel_at']<=1.5,'Cancellation guard')
    require(len(used)==len(connections),'Unexpected connection or plaintext fallback')
    for row in records[-2:]:
        require(row['command']=='0610' and decode(bytes.fromhex(row['block']),bytes.fromhex(row['data']))==b'00,OK','Final release')
    return dict(bringup_matrix_pass=True,full_hardware_qualification=False,cases=len(cases()),
                peer_hashes=hashes,transactions_sha256=digest(directory/'transactions.jsonl'),held=run['held'])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
    p.add_argument('--peer',action='append',required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();result=audit(a.directory,a.peer)
    with a.output.open('x') as f:json.dump(result,f,indent=2)
    print(json.dumps(result,indent=2))
