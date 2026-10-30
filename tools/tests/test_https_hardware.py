"""Offline hardware-runner contract, evidence-mutation and loopback fixture tests."""
import copy
import hashlib
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import types

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import https_hardware_runner as runner
from audit_https_hardware import audit
from https_test_endpoint import Endpoint, BODY


def result(data=b'',status=b'00,OK',flags=0):
    block=bytearray(512);block[0]=0x20;block[1]=0 if flags==8 else 1
    block[2:4]=len(data).to_bytes(2,'little');block[4]=len(status);block[6]=flags
    block[0x30:0x32]=len(data).to_bytes(2,'little');block[0x100:0x100+len(status)]=status
    return block


def plan():
    return dict(schema=1,candidate_sha256='a'*64,cases=runner.cases(),held=runner.HELD,
                request_guard_seconds=35,idle_seconds=180,endpoints={
                    'http':'http://127.0.0.1:9080','trusted':'https://localhost:9443',
                    'wrong_name':'https://127.0.0.1:9443','untrusted':'https://localhost:9444',
                    'stall':'https://localhost:9445'})


def synthetic(directory):
    p=plan();(directory/'plan.json').write_text(json.dumps(p));records=[];results=[];peers={}
    for mode in ('http','tls','handshake-stall'):
        peers[mode]=[dict(event='ready',monotonic=0,mode=mode,clock_domain=runner.clock_domain())]
    clock=10
    def transaction(command,data,status=b'00,OK',flags=0):
        nonlocal clock
        row=dict(command=command.hex(),start=clock,end=clock+1,block=result(data,status,flags).hex(),data=data.hex())
        records.append(row);clock+=2;return row
    for case in runner.cases():
        transaction(bytes.fromhex('0610'),b'')
        url=p['endpoints'][case['endpoint']]+'/case/'+case['id']+'/'+case['route']
        transaction(bytes([6,0x11,6 if case['method']=='HEAD' else 1])+url.encode()+b'\0',b'\0')
        data=BODY if case['expected']=='success' and case['method']=='GET' else b''
        status=b'HTTP/1.1 200 OK' if case['expected']=='success' else b'503 SERVICE UNAVAILABLE'
        if case['expected']=='cancel': status=b''
        row=transaction(bytes.fromhex('063200ff'),data,status,8 if case['expected']=='cancel' else 0)
        results.append(dict(**case,start=row['start'],end=row['end'],status=status.decode(),
                            cancel_at=row['start']+.5 if case['expected']=='cancel' else None,
                            data_sha256=hashlib.sha256(data).hexdigest()))
        mode='http' if case['endpoint']=='http' else 'handshake-stall' if case['endpoint']=='stall' else 'tls'
        log=peers[mode];connection=sum(e['event']=='accepted' for e in log)+1
        def event(name,**kw):log.append(dict(event=name,connection=connection,monotonic=row['start']+.1+len(log)*0.00001,**kw))
        event('accepted')
        if case['expected']=='authentication':event('connection_error')
        else:
            if mode!='handshake-stall':event('request',case=case['id'],method=case['method'],route=case['route'])
            if case['expected']=='success':event('response_sent',body_bytes=len(data),body_sha256=hashlib.sha256(data).hexdigest())
            else:event('stall_started');event('peer_closed_during_stall')
        event('connection_closed')
    transaction(bytes.fromhex('0610'),b'');clock+=180;transaction(bytes.fromhex('0610'),b'')
    (directory/'transactions.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in records))
    run=dict(status='awaiting-independent-peer-audit',cases=results,idle_seconds=180,clock_domain=runner.clock_domain(),
             c64_reset_to_basic_requested=True,held=runner.HELD,
             transactions_sha256=runner.digest(directory/'transactions.jsonl'),
             plan_sha256=hashlib.sha256(json.dumps(p,sort_keys=True).encode()).hexdigest())
    (directory/'run.json').write_text(json.dumps(run));paths=[]
    for mode,events in peers.items():
        events.append(dict(event='stopped',monotonic=clock,active=0,accepted_attempts=sum(e['event']=='accepted' for e in events)))
        path=directory/(mode+'.jsonl');path.write_text(''.join(json.dumps(r)+'\n' for r in events));paths.append(path)
    return paths


class Contracts(unittest.TestCase):
    def test_peer_clock_gates(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);record={'peer_logs':{}}
            for name,url in plan()['endpoints'].items():
                path=d/(name+'.jsonl');record['peer_logs'][name]=str(path)
                value=dict(event='ready',clock_domain=runner.clock_domain(),monotonic=time.monotonic(),
                           mode='http' if name=='http' else 'handshake-stall' if name=='stall' else 'tls',
                           address=['127.0.0.1',runner.urlsplit(url).port])
                path.write_text(json.dumps(value)+'\n')
            runner.validate_peers(record,plan())
            path=Path(record['peer_logs']['trusted']);original=path.read_text()
            for change in ('clock','old','port','stopped','used'):
                value=json.loads(original)
                if change=='clock':value['clock_domain']['system']='different OS'
                if change=='old':value['monotonic']-=901
                if change=='port':value['address'][1]+=1
                lines=[value]
                if change=='stopped':lines.append({'event':'stopped'})
                if change=='used':lines.append({'event':'accepted'})
                path.write_text(''.join(json.dumps(x)+'\n' for x in lines))
                with self.assertRaises(ValueError):runner.validate_peers(record,plan())

    def test_plan(self):
        runner.validate_plan(plan())
        for mutate in (lambda p:p['cases'].pop(),lambda p:p.update(idle_seconds=0),
                       lambda p:p['endpoints'].update(trusted='http://localhost:9443'),
                       lambda p:p.update(held=[])):
            p=plan();mutate(p)
            with self.assertRaises(ValueError):runner.validate_plan(p)

    def test_execution_gates(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);(d/'evidence').write_text('operator record')
            record=dict(candidate_sha256='a'*64,plan_sha256=hashlib.sha256(json.dumps(plan(),sort_keys=True).encode()).hexdigest(),
                        device='192.168.1.107',expires_unix=time.time()+300,gates={
                            name:dict(confirmed=True,evidence='evidence',sha256=runner.digest(d/'evidence')) for name in runner.REQUIRED_GATES})
            runner.validate_installation(record,plan(),d)
            for name in runner.REQUIRED_GATES:
                bad=copy.deepcopy(record);bad['gates'][name]['confirmed']=False
                with self.assertRaises(ValueError):runner.validate_installation(bad,plan(),d)
            for key,value in [('expires_unix',0),('candidate_sha256','b'*64),('device','127.0.0.1')]:
                bad=copy.deepcopy(record);bad[key]=value
                with self.assertRaises(ValueError):runner.validate_installation(bad,plan(),d)
            (d/'evidence').write_text('changed')
            with self.assertRaises(ValueError):runner.validate_installation(record,plan(),d)

    def test_negative_results(self):
        case=next(c for c in runner.cases() if c['expected']=='authentication')
        for data,status,flags in [(b'secret',b'503 SERVICE UNAVAILABLE',0),(b'',b'HTTP/1.1 200 OK',0),
                                  (b'',b'503 SERVICE UNAVAILABLE',1)]:
            with self.assertRaises(ValueError):runner.check_exchange(case,result(data,status,flags),data,1)
        with self.assertRaises(ValueError):runner.check_exchange(case,result(b'',b'503 SERVICE UNAVAILABLE'),b'',36)

    def test_audit_and_evidence_mutations(self):
        for mutation in range(12):
            with tempfile.TemporaryDirectory() as d:
                d=Path(d);peers=synthetic(d)
                self.assertTrue(audit(d,peers)['bringup_matrix_pass'])
                if mutation==0:(d/'transactions.jsonl').write_text('')
                elif mutation==1:peers[1].write_text('\n'.join(peers[1].read_text().splitlines()[:-1]))
                elif mutation==2:
                    value=json.loads((d/'run.json').read_text());value['cases'].pop();(d/'run.json').write_text(json.dumps(value))
                elif mutation==3:
                    value=json.loads((d/'run.json').read_text());value['status']='failed-or-stopped';(d/'run.json').write_text(json.dumps(value))
                elif mutation==4:
                    value=json.loads((d/'run.json').read_text());value['idle_seconds']=0;(d/'run.json').write_text(json.dumps(value))
                elif mutation==5:peers[1].write_text(peers[1].read_text().replace('connection_closed','missing_close',1))
                elif mutation==6:peers[1].write_text(peers[1].read_text().replace('"GET"','"HEAD"',1))
                elif mutation==7:peers[1].write_text(peers[1].read_text().replace('peer_closed_during_stall','stall_finished',1))
                elif mutation==8:
                    value=json.loads((d/'run.json').read_text());value['cases'][0]['end']+=10;(d/'run.json').write_text(json.dumps(value))
                elif mutation==9:peers[0].write_text(peers[0].read_text().replace('"mode": "http"','"mode": "tls"'))
                elif mutation==10:peers[1].write_text(peers[1].read_text().replace('"active": 0','"active": 1'))
                elif mutation==11:
                    value=json.loads((d/'plan.json').read_text());value['cases'].reverse();(d/'plan.json').write_text(json.dumps(value))
                with self.assertRaises((ValueError,KeyError)):audit(d,peers)

    def test_loopback_fixture(self):
        with tempfile.TemporaryDirectory() as d:
            server=Endpoint(('127.0.0.1',0),Path(d)/'peer.jsonl',mode='http')
            thread=threading.Thread(target=server.handle_request);thread.start()
            with socket.create_connection(server.server_address,timeout=2) as peer:
                peer.sendall(b'GET /case/local/ok HTTP/1.1\r\nHost: localhost\r\n\r\n')
                data=b''
                while part:=peer.recv(4096):data+=part
            thread.join(3);server.close_fixture()
            self.assertEqual(data.split(b'\r\n\r\n',1)[1],BODY)
            events=[json.loads(line) for line in (Path(d)/'peer.jsonl').read_text().splitlines()]
            self.assertEqual(events[-1]['active'],0)

    def test_runner_control_flow_and_stop(self):
        for fail in (False,True):
            with tempfile.TemporaryDirectory() as d:
                d=Path(d);peer=d/'peer.jsonl';peer.write_text('')
                clock=[0.0];resets=[];current=[None];index=[0]
                class Machine:
                    def writemem(self,address,data):
                        self_address=address
                        assert self_address==0xc00a and data==b'\1'
                    def reset(self,force):resets.append(force)
                machine=Machine()
                class Api:
                    def __init__(self,*args,**kwargs):
                        self.machine=machine;self.runners=object()
                        self.configs=types.SimpleNamespace(current=lambda *a:'Enabled')
                class Native:
                    def __init__(self,*args,**kwargs):
                        assert kwargs['agent_defines']==runner.DEFINES
                        self.poll_hook=None
                    def start(self):pass
                    def _run(self,command,*args):
                        clock[0]+=.01
                        if command[1]==0x10:return result(),b''
                        if command[1]==0x11:
                            current[0]=runner.cases()[index[0]]
                            return result(b'\0'),b'\0'
                        case=current[0];index[0]+=1
                        if fail:raise RuntimeError('Injected transport failure')
                        if case['expected']=='cancel':
                            with peer.open('a') as stream:stream.write(json.dumps(dict(event='stall_started',monotonic=clock[0]))+'\n')
                        if self.poll_hook:self.poll_hook()
                        data=BODY if case['expected']=='success' and case['method']=='GET' else b''
                        status=b'HTTP/1.1 200 OK' if case['expected']=='success' else b'503 SERVICE UNAVAILABLE'
                        if case['expected']=='cancel':status=b''
                        clock[0]+=.01
                        return result(data,status,8 if case['expected']=='cancel' else 0),data
                modules={'bootstrap':types.ModuleType('bootstrap'),
                         'api':types.SimpleNamespace(UltimateApi=Api),
                         'rest':types.SimpleNamespace(TRANSPORT_RETRIES=3),
                         'uci_native':types.SimpleNamespace(NativeUci=Native)}
                installation=dict(device='192.168.1.107',peer_logs={key:str(peer) for key in plan()['endpoints']})
                def sleep(seconds):clock[0]+=seconds
                with patch.dict(sys.modules,modules),patch.object(runner.time,'monotonic',lambda:clock[0]),patch.object(runner.time,'sleep',sleep):
                    if fail:
                        with self.assertRaisesRegex(RuntimeError,'Injected'):runner.run(plan(),installation,d)
                    else:runner.run(plan(),installation,d)
                saved=json.loads((d/'run.json').read_text())
                if fail:
                    self.assertEqual(saved['status'],'failed-or-stopped');self.assertEqual(resets,[])
                    self.assertIn('Injected',(d/'transactions.jsonl').read_text())
                else:
                    self.assertEqual(saved['status'],'awaiting-independent-peer-audit')
                    self.assertEqual(len(saved['cases']),len(runner.cases()));self.assertEqual(resets,[True])


if __name__=='__main__':unittest.main(verbosity=2)
