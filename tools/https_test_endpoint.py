"""Bounded raw HTTPS fixtures. Loopback by default; no device or DNS operations."""
import argparse,hashlib,json,re,socket,socketserver,ssl,threading,time
from datetime import datetime,timezone
from pathlib import Path
from https_hardware_runner import clock_domain

BODY=bytes(range(256))*8
BODY_SHA256=hashlib.sha256(BODY).hexdigest()
ROUTE=re.compile(r'/case/([A-Za-z0-9_-]{1,40})/(ok|chunked|early-close|delay-headers|read-stall)\Z')

class Endpoint(socketserver.ThreadingTCPServer):
    allow_reuse_address=False
    daemon_threads=False
    block_on_close=True

    def __init__(self,address,log,*,context=None,mode='tls',allowed=('127.0.0.1',),fault_seconds=36,max_connections=64):
        if mode not in ('http','tls','handshake-stall') or (mode=='tls' and context is None):
            raise ValueError('TLS mode needs a certificate context')
        if not 0<fault_seconds<=60 or not 1<=max_connections<=1000:
            raise ValueError('Fixture bounds exceeded')
        self.context=context;self.mode=mode;self.allowed=set(allowed);self.fault_seconds=fault_seconds
        self.limit=max_connections;self.count=0;self.stop=threading.Event();self.lock=threading.RLock()
        self.slots=threading.BoundedSemaphore(8);self.active={};self.log=Path(log).open('x',encoding='utf-8')
        try:super().__init__(address,Handler)
        except BaseException:self.log.close();raise
        self.record('ready',address=self.server_address,mode=mode,body_sha256=BODY_SHA256,
                    fault_seconds=fault_seconds,max_connections=max_connections,clock_domain=clock_domain())

    def record(self,event,**values):
        with self.lock:
            self.log.write(json.dumps(dict(utc=datetime.now(timezone.utc).isoformat(),monotonic=time.monotonic(),event=event,**values))+'\n')
            self.log.flush()

    def process_request(self,request,address):
        with self.lock:
            self.count+=1;number=self.count
        if address[0] not in self.allowed or number>self.limit or not self.slots.acquire(blocking=False):
            self.record('rejected',peer=address,connection=number)
            self.shutdown_request(request);return
        with self.lock:self.active[request]=number
        self.record('accepted',peer=address,connection=number)
        try:super().process_request(request,address)
        except BaseException:
            with self.lock:self.active.pop(request,None)
            self.slots.release();self.shutdown_request(request);raise

    def process_request_thread(self,request,address):
        try:super().process_request_thread(request,address)
        finally:
            with self.lock:self.active.pop(request,None)
            self.slots.release()

    def handle_error(self,request,address):
        import traceback
        self.record('handler_error',peer=address,traceback=traceback.format_exc())

    def close_fixture(self):
        self.stop.set()
        self.server_close()  # All handlers have bounded I/O and observe stop.
        self.record('stopped',accepted_attempts=self.count,active=len(self.active))
        self.log.close()

class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        server=self.server
        with server.lock:number=server.active[self.request]
        peer=self.request
        def event(name,**values):server.record(name,connection=number,**values)
        try:
            if server.mode=='handshake-stall':
                peer.settimeout(2)
                hello=peer.recv(4096)
                event('client_hello_received',bytes=len(hello),sha256=hashlib.sha256(hello).hexdigest())
                self.hold(peer,event,allow_input=True)
                return
            peer.settimeout(3)
            if server.mode=='tls':
                peer=server.context.wrap_socket(peer,server_side=True,do_handshake_on_connect=False)
                event('handshake_started')
                peer.do_handshake()
                event('handshake_complete',version=peer.version(),cipher=peer.cipher()[0])
            peer.settimeout(0.2);data=b'';deadline=time.monotonic()+5
            while b'\r\n\r\n' not in data:
                if server.stop.is_set() or time.monotonic()>=deadline:raise TimeoutError('Header deadline')
                try:part=peer.recv(min(1024,8193-len(data)))
                except socket.timeout:continue
                if not part:raise EOFError('Peer closed before complete headers')
                data+=part
                if len(data)>8192:raise ValueError('Headers exceed 8192 bytes')
            head,extra=data.split(b'\r\n\r\n',1)
            lines=head.decode('ascii').split('\r\n');method,target,version=lines[0].split(' ')
            match=ROUTE.fullmatch(target)
            headers=[line.split(':',1) for line in lines[1:]]
            valid=(method in ('GET','HEAD') and version in ('HTTP/1.0','HTTP/1.1') and match and not extra and
                   all(len(h)==2 for h in headers))
            if valid:
                hosts=[v.strip() for k,v in headers if k.lower()=='host']
                valid=(len(hosts)==1 and bool(hosts[0]) and
                       all(k.lower()!='transfer-encoding' and (k.lower()!='content-length' or v.strip()=='0') for k,v in headers))
            if not valid:
                peer.sendall(b'HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
                event('request_rejected');return
            case,route=match.groups();event('request',case=case,route=route,method=method)
            peer.settimeout(2)
            if route=='delay-headers':self.hold(peer,event);return
            if route=='chunked':
                header=b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n'
                payload=b''.join(('%x\r\n'%len(BODY[i:i+257])).encode()+BODY[i:i+257]+b'\r\n' for i in range(0,len(BODY),257))+b'0\r\n\r\n'
            else:
                header=('HTTP/1.1 200 OK\r\nContent-Length: %d\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n'%len(BODY)).encode()
                payload=BODY
            peer.sendall(header)
            if method=='HEAD':event('response_sent',body_bytes=0);return
            if route in ('early-close','read-stall'):
                peer.sendall(BODY[:16]);event('partial_body_sent',body_bytes=16,declared_bytes=len(BODY))
                if route=='read-stall':self.hold(peer,event)
                return
            peer.sendall(payload);event('response_sent',body_bytes=len(BODY),body_sha256=BODY_SHA256)
        except (OSError,ValueError,EOFError) as error:
            event('connection_error',error_type=type(error).__name__,error=str(error))
        finally:
            try:peer.close()
            finally:event('connection_closed')

    def hold(self,peer,event,allow_input=False):
        deadline=time.monotonic()+self.server.fault_seconds;peer.settimeout(0.1)
        event('stall_started',seconds=self.server.fault_seconds)
        while time.monotonic()<deadline and not self.server.stop.is_set():
            try:
                incoming=peer.recv(1024)
                if not incoming:event('peer_closed_during_stall');return
                if not allow_input:event('unexpected_input_during_stall',bytes=len(incoming));return
            except socket.timeout:continue
        event('stall_finished',operator_stop=self.server.stop.is_set())

def context(cert,key):
    result=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    result.minimum_version=ssl.TLSVersion.TLSv1_2
    result.load_cert_chain(cert,key)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bind',default='127.0.0.1');p.add_argument('--port',type=int,default=9443)
    p.add_argument('--allow-peer',action='append',default=[])
    p.add_argument('--mode',choices=['http','tls','handshake-stall'],default='tls')
    p.add_argument('--cert');p.add_argument('--key');p.add_argument('--log',required=True)
    p.add_argument('--stop-file',required=True);p.add_argument('--seconds',type=int,default=900)
    p.add_argument('--fault-seconds',type=float,default=36)
    args=p.parse_args()
    if not 1<=args.seconds<=900:p.error('Run duration must be 1..900 seconds')
    if args.bind!='127.0.0.1' and not args.allow_peer:p.error('Non-loopback binding requires explicit peer allowlist')
    if args.mode=='tls' and not (args.cert and args.key):p.error('TLS requires --cert and --key')
    if Path(args.stop_file).exists():p.error('Stop marker exists; no listener started')
    ctx=context(args.cert,args.key) if args.mode=='tls' else None
    server=Endpoint((args.bind,args.port),args.log,context=ctx,mode=args.mode,
                    allowed=args.allow_peer or ['127.0.0.1'],fault_seconds=args.fault_seconds)
    server.timeout=0.1;end=time.monotonic()+args.seconds
    print(json.dumps(dict(listening=server.server_address,mode=args.mode,body_sha256=BODY_SHA256)),flush=True)
    try:
        while time.monotonic()<end and not Path(args.stop_file).exists():server.handle_request()
    finally:server.close_fixture()

if __name__=='__main__':main()
