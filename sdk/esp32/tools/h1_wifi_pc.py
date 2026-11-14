"""PC reference for H1NET/1: run H1's HTTP demo before the ESP32 arrives."""
from __future__ import annotations
import argparse
import dataclasses
import http.client
import json
from pathlib import Path
import socket
import struct
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import zlib

MAGIC = b'H1NW'
IDENTITY = b'H1NET/1 ESP32 HTTP'
MAXIMUM = 8 * 1024 * 1024
MAX_HEADERS = 1024
MAX_REQUEST_BODY = 16384
MAX_REQUEST = 2048 + MAX_HEADERS + MAX_REQUEST_BODY
CAP_HTTP_REQUEST = 8
CAP_HTTP_HEADERS = 32
UNKNOWN = 0xffffffff
OK, INVALID, BUSY, OFFLINE, BAD_JOB, INTERNAL = range(6)
IDLE, CONNECTING, RECEIVING, DONE, FAILED, CANCELLED = range(6)
NETWORK, LIMIT, TIMEOUT, CANCEL, WIFI, HTTP_PROTOCOL = range(1, 7)

def valid_url(url, maximum=400):
    try:
        raw = url.encode('utf-8')
        parts = urllib.parse.urlsplit(url)
        return (1 <= len(raw) <= maximum and parts.scheme in ('http', 'https') and parts.hostname
                and url.startswith(parts.scheme + '://') and parts.username is None
                and not any(c <= 32 or c == 127 or c == 92 for c in raw))
    except (ValueError, UnicodeError):
        return False

def parse_headers(raw):
    """Apply the same strict framing/header policy as common/h1net_wire.c."""
    if isinstance(raw, str):raw=raw.encode('ascii')
    if len(raw)>MAX_HEADERS:raise ValueError('headers too large')
    if not raw:return {}
    denied={'host','content-length','transfer-encoding','connection','proxy-connection','keep-alive',
            'te','trailer','upgrade','expect','accept-encoding','proxy-authorization','proxy-authenticate'}
    token=b"!#$%&'*+-.^_`|~0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    lines=raw.split(b'\r\n')
    if not lines[-1]:lines.pop()
    result={};seen=set()
    for line in lines:
        name,sep,value=line.partition(b':')
        if not sep or not name or any(c not in token for c in name) or any(c<32 or c>126 for c in value):
            raise ValueError('invalid header')
        key=name.decode('ascii');lower=key.lower()
        if lower in denied or lower in seen:raise ValueError('reserved or duplicate header')
        result[key]=value.decode('ascii').lstrip(' ');seen.add(lower)
    return result

def encode(op, sequence, payload=b'', status=0):
    if len(payload) > 512:
        raise ValueError('payload too large')
    packet = struct.pack('<4sBBHHH', MAGIC, 1, op, sequence, len(payload), status) + payload
    return packet + struct.pack('<I', zlib.crc32(packet))

class Parser:
    def __init__(self):
        self.buffer = bytearray()
        self.bad_crc = self.bad_headers = 0

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while len(self.buffer) >= 4:
            if self.buffer[:4] != MAGIC:
                del self.buffer[0]
                continue
            if len(self.buffer) < 12:
                break
            _, version, op, seq, size, status = struct.unpack_from('<4sBBHHH', self.buffer)
            if version != 1 or size > 512:
                self.bad_headers += 1
                del self.buffer[:12]
                continue
            total = size + 16
            if len(self.buffer) < total:
                break
            frame = bytes(self.buffer[:total]);del self.buffer[:total]
            if zlib.crc32(frame[:-4]) != struct.unpack_from('<I', frame, total-4)[0]:
                self.bad_crc += 1
                continue
            frames.append((op, seq, status, frame[12:-4]))
        return frames

@dataclasses.dataclass
class Info:
    job: int = 0
    state: int = IDLE
    http_status: int = 0
    content_length: int = UNKNOWN
    received: int = 0
    delivered: int = 0
    buffered: int = 0
    error: int = 0

class Redirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if getattr(req,'_h1_no_redirect',False):return None
        count = getattr(req, '_h1_redirects', 0)
        limit=getattr(req,'_h1_url_limit',400)
        if count >= 3 or not valid_url(newurl,limit) or (req.full_url.startswith('https://') and not newurl.startswith('https://')):
            raise ValueError('invalid redirect')
        request = super().redirect_request(req, fp, code, msg, headers, newurl)
        if request is not None:
            request._h1_redirects = count + 1
            request._h1_url_limit = limit
        return request

class GzipReader:
    """Decode RFC 1952 members with bounded input/output and verified trailers."""
    def __init__(self, response, check):
        self.response = response
        self.check = check
        self.decoder = zlib.decompressobj(16 + zlib.MAX_WBITS)
        self.pending = b''
        self.wire_bytes = 0

    def read1(self, wanted):
        while True:
            self.check()
            if self.decoder.eof:
                if not self.pending:
                    self.pending = self.response.read1(512)
                    self.wire_bytes += len(self.pending)
                    if not self.pending:
                        return b''
                self.decoder = zlib.decompressobj(16 + zlib.MAX_WBITS)
            block = self.decoder.decompress(self.pending, wanted)
            self.pending = self.decoder.unused_data if self.decoder.eof else self.decoder.unconsumed_tail
            if block:
                return block
            if self.decoder.eof:
                continue
            if self.pending:
                raise ValueError('gzip decoder made no progress')
            self.pending = self.response.read1(512)
            self.wire_bytes += len(self.pending)
            if not self.pending:
                raise ValueError('truncated gzip response')

class _HttpJob:
    """Bounded streaming job. A new session cannot reuse a live worker's storage."""
    def __init__(self):
        self.cv = threading.Condition()
        self.info = Info()
        self.body = bytearray()
        self.running = self.cancelled = False
        self.next_job = 0
        self.thread = None
        self.final_url = self.content_type = '';self.response_fields=None;self.request_job=False

    def reset(self):
        with self.cv:
            if self.running:
                self.cancelled = True;self.cv.notify_all()
                return BUSY
            self.info = Info();self.body.clear();self.cancelled = False
            self.final_url = self.content_type = '';self.response_fields=None;self.request_job=False
            return OK

    def open(self, url, maximum, timeout):
        return self._open(url,maximum,timeout,None,'')

    def open_range(self,url,offset,length,total,timeout,referer=''):
        if (not valid_url(url,2048) or (referer and not valid_url(referer)) or
            not 0<=offset<total<=0xffffffff or not 1<=length<=min(MAXIMUM,total-offset) or not 1000<=timeout<=60000):
            return INVALID,0
        return self._open(url,length,timeout,(offset,length,total),referer)

    def _open(self, url, maximum, timeout, range_spec, referer, request_spec=None):
        with self.cv:
            if self.running or self.body:
                return BUSY, 0
            self.next_job = (self.next_job + 1) & 0xffffffff or 1
            self.info = Info(job=self.next_job, state=CONNECTING)
            self.final_url = self.content_type = '';self.response_fields=None;self.request_job=False
            self.cancelled = False;self.running = True
            # Immutable copies belong to the new worker, not the upload buffer.
            if request_spec is not None:
                self.request_job=True
                method,headers,body=request_spec
                request_spec=(str(method),dict(headers),bytes(body))
            self.thread = threading.Thread(target=self._worker, args=(url, maximum, timeout,range_spec,referer,request_spec), daemon=True)
            self.thread.start()
            return OK, self.next_job

    def snapshot(self, job):
        with self.cv:
            return dataclasses.replace(self.info) if job and job == self.info.job else None

    def metadata(self, job):
        with self.cv:
            if not job or job != self.info.job:return BAD_JOB, b''
            if not self.final_url:return BUSY, b''
            url=self.final_url.encode('utf-8');kind=self.content_type.encode('ascii')
            if len(url)>400:return INVALID,b''  # Extended jobs use INFO/READ, not legacy META.
            return OK,struct.pack('<IHH',job,len(url),len(kind))+url+kind

    def headers(self,job,offset,wanted):
        with self.cv:
            if not job or job!=self.info.job:return BAD_JOB,b''
            if not self.request_job:return INVALID,b''
            if self.response_fields is None:return BUSY,b''
            if offset>len(self.response_fields):return INVALID,b''
            return OK,struct.pack('<III',job,offset,len(self.response_fields))+self.response_fields[offset:offset+wanted]

    def read(self, job, wanted):
        with self.cv:
            if not job or job != self.info.job:
                return None
            data = bytes(self.body[:wanted]);del self.body[:wanted]
            offset = self.info.delivered
            self.info.delivered += len(data);self.info.buffered = len(self.body)
            self.cv.notify_all()
            return offset, data

    def cancel(self, job):
        with self.cv:
            if not job or job != self.info.job:
                return BAD_JOB
            self.cancelled = True;self.cv.notify_all()
            if not self.running:
                self.info.state = CANCELLED;self.info.error = CANCEL
            return OK

    def close(self):
        with self.cv:
            self.cancelled = True;self.cv.notify_all()
        if self.thread:
            self.thread.join(6)

    def _worker(self, url, maximum, timeout, range_spec=None, referer='', request_spec=None):
        deadline = time.monotonic() + timeout/1000
        error = 0
        response = None
        try:
            headers={'Accept-Encoding':'identity' if range_spec or request_spec else 'gzip, identity','User-Agent':'H1NET/1 PC reference'}
            if range_spec:
                start,length,total=range_spec;headers['Range']=f'bytes={start}-{start+length-1}'
            if referer:headers['Referer']=referer
            if request_spec:
                method,custom_headers,body=request_spec;headers.update(custom_headers)
                request = urllib.request.Request(url,data=body if method=='POST' else None,headers=headers,method=method)
                request._h1_no_redirect=True
            else:request = urllib.request.Request(url, headers=headers)
            request._h1_url_limit=2048 if range_spec or request_spec else 400
            opener = urllib.request.build_opener(Redirects())
            try:
                response = opener.open(request, timeout=min(timeout/1000, 5))
            except urllib.error.HTTPError as e:
                response = e  # Preserve status and body, including non-2xx.
            lengths = response.headers.get_all('Content-Length', [])
            content_length = UNKNOWN
            if lengths:
                if any(not s.strip().isdigit() for s in lengths) or len({int(s) for s in lengths}) != 1:
                    raise ValueError('bad Content-Length')
                content_length = int(lengths[0])
                if content_length > 0xffffffff:
                    raise ValueError('Content-Length overflow')
            encodings = response.headers.get_all('Content-Encoding', [])
            encoding = ','.join(encodings).strip().lower() if encodings else 'identity'
            compressed = encoding in ('gzip', 'x-gzip')
            with self.cv:
                self.info.http_status = response.code;self.info.content_length = UNKNOWN if compressed else content_length;self.info.state = RECEIVING
                raw_fields=''.join(k+': '+v+'\r\n' for k,v in response.headers.raw_items()).encode('latin1') if request_spec else b''
                if len(raw_fields)>8192:raise ValueError('response headers exceed limit')
                self.response_fields=raw_fields
                self.final_url=response.geturl();self.content_type=response.headers.get('Content-Type','')
            if not valid_url(self.final_url,2048 if range_spec or request_spec else 400) or len(self.content_type)>96 or not self.content_type.isascii() or any(ord(c)<32 or ord(c)==127 for c in self.content_type):
                raise ValueError('invalid response metadata')
            if range_spec:
                expected=f'bytes {start}-{start+length-1}/{total}'
                if response.code!=206 or response.headers.get_all('Content-Range',[])!=[expected] or compressed or (content_length!=UNKNOWN and content_length!=length):
                    raise ValueError('invalid partial response')
            if encoding not in ('identity', 'gzip', 'x-gzip'):
                raise ValueError('unsupported Content-Encoding')
            if request_spec and compressed:raise ValueError('request extension requires identity encoding')
            if not compressed and content_length != UNKNOWN and content_length > maximum:
                error = LIMIT
                return
            def check():
                if self.cancelled:
                    raise InterruptedError('cancelled')
                if time.monotonic() >= deadline:
                    raise TimeoutError('HTTP deadline exceeded')
            reader = GzipReader(response, check) if compressed else response
            while True:
                with self.cv:
                    if self.cancelled:
                        error = CANCEL;break
                    if time.monotonic() >= deadline:
                        error = TIMEOUT;break
                    space = 8192-len(self.body)
                    if not space:
                        self.cv.wait(min(0.05, max(0, deadline-time.monotonic())))
                        continue
                    wanted = min(512, space, maximum-self.info.received+1)
                block = reader.read1(wanted)
                if not block:
                    break
                with self.cv:
                    if len(block) > maximum-self.info.received:
                        error = LIMIT;break
                    self.body.extend(block);self.info.received += len(block);self.info.buffered = len(self.body)
                    # A slow USB consumer applies backpressure. Bound stalls,
                    # not the total duration of a progressing transfer.
                    deadline = time.monotonic() + timeout/1000
            with self.cv:
                wire_bytes = reader.wire_bytes if compressed else self.info.received
                if not error and content_length != UNKNOWN and wire_bytes != content_length:
                    error = HTTP_PROTOCOL
                if not error and compressed:
                    self.info.content_length = self.info.received
        except InterruptedError:
            error = CANCEL
        except (TimeoutError, socket.timeout):
            error = TIMEOUT
        except (ValueError, zlib.error, http.client.IncompleteRead):
            error = HTTP_PROTOCOL
        except (OSError, urllib.error.URLError, http.client.HTTPException):
            error = NETWORK
        finally:
            if response is not None:
                response.close()
            with self.cv:
                if self.cancelled:
                    error = CANCEL
                self.info.error = error
                self.info.state = CANCELLED if error == CANCEL else FAILED if error else DONE
                self.running = False;self.cv.notify_all()

class Backend:
    """Four isolated HTTP workers; one slot reserved from background opens.

    CAPABILITIES opts into explicit job lifetime. Legacy clients retain their
    single-job/reuse behavior until they negotiate, including META after EOF.
    Released workers retain their own storage until their I/O returns.
    """
    def __init__(self):
        self.lock=threading.RLock();self.jobs=[];self.last=_HttpJob()
        self.next_job=0;self.multiplex=False

    @property
    def info(self):return self.last.info
    @property
    def body(self):return self.last.body
    @property
    def running(self):return any(j.running for j in self.jobs)
    @property
    def cancelled(self):return any(j.cancelled for j in self.jobs)
    @property
    def thread(self):return self.last.thread

    def capabilities(self):
        with self.lock:self.multiplex=True
        return (1,4,8192,3|CAP_HTTP_REQUEST|CAP_HTTP_HEADERS)

    def reset(self):
        with self.lock:
            for j in self.jobs:j.cancel(j.info.job)
            if any(j.running for j in self.jobs):return BUSY
            self.jobs.clear();self.last=_HttpJob();self.multiplex=False
            return OK

    def _find(self,job):
        return next((j for j in self.jobs if not j.retired and j.info.job==job),None)

    def _open(self,url,maximum,timeout,range_spec=None,referer='',background=False,request_spec=None):
        with self.lock:
            self.jobs[:]=[j for j in self.jobs if not (j.retired and not j.running)]
            if not self.multiplex:
                if background:return INVALID,0
                if any(j.running or j.body for j in self.jobs):return BUSY,0
                self.jobs.clear()
            if len(self.jobs)>=4 or (background and sum(j.background for j in self.jobs)>=3):return BUSY,0
            self.next_job=(self.next_job+1)&0xffffffff or 1
            while any(j.info.job==self.next_job for j in self.jobs):self.next_job=(self.next_job+1)&0xffffffff or 1
            j=_HttpJob();j.next_job=self.next_job-1;j.retired=False;j.background=background
            code,job=j._open(url,maximum,timeout,range_spec,referer,request_spec)
            if not code:self.jobs.append(j);self.last=j
            return code,job

    def open(self,url,maximum,timeout):return self._open(url,maximum,timeout)
    def open_background(self,url,maximum,timeout):return self._open(url,maximum,timeout,background=True)

    def open_request(self,url,method,headers,body,maximum,timeout):
        try:fields=parse_headers(headers)
        except (ValueError,UnicodeError):return INVALID,0
        if (not valid_url(url,2048) or method not in ('GET','POST') or len(body)>MAX_REQUEST_BODY or
            (method=='GET' and body) or not 1<=maximum<=MAXIMUM or not 1000<=timeout<=60000):return INVALID,0
        return self._open(url,maximum,timeout,request_spec=(method,fields,body))

    def open_range(self,url,offset,length,total,timeout,referer=''):
        if (not valid_url(url,2048) or (referer and not valid_url(referer)) or
            not 0<=offset<total<=0xffffffff or not 1<=length<=min(MAXIMUM,total-offset) or not 1000<=timeout<=60000):return INVALID,0
        return self._open(url,length,timeout,(offset,length,total),referer)

    def snapshot(self,job):
        with self.lock:
            j=self._find(job);return j.snapshot(job) if j else None
    def metadata(self,job):
        with self.lock:
            j=self._find(job);return j.metadata(job) if j else (BAD_JOB,b'')
    def headers(self,job,offset,wanted):
        with self.lock:
            j=self._find(job);return j.headers(job,offset,wanted) if j else (BAD_JOB,b'')
    def read(self,job,wanted):
        with self.lock:
            j=self._find(job);return j.read(job,wanted) if j else None
    def cancel(self,job):
        with self.lock:
            j=self._find(job);return j.cancel(job) if j else BAD_JOB
    def release(self,job):
        with self.lock:
            j=self._find(job)
            if j is None:return BAD_JOB
            j.cancel(job);j.retired=True
            with j.cv:j.body.clear();j.info.delivered=j.info.received;j.info.buffered=0;j.cv.notify_all()
            return OK
    def close(self):
        with self.lock:
            jobs=list(self.jobs)
            for j in jobs:j.cancel(j.info.job)
        for j in jobs:j.close()

class Server:
    def __init__(self, backend=None,audio=None):
        self.backend = backend or Backend()
        self.audio=audio
        self.ready = False;self.sequence = 0;self.range_url=bytearray();self.url_total=0
        self.request_data=bytearray();self.request_total=0

    def clear_request(self):
        self.request_data[:]=b'\0'*len(self.request_data)
        self.request_data.clear();self.request_total=0

    def dispatch(self, frame):
        op, seq, status, payload = frame
        error, data = OK, b''
        try:
            if not 0 < op < 128 or status:
                error = INVALID
            elif op == 1:
                self.clear_request()
                if payload:
                    error = INVALID
                else:
                    self.ready = False;error = self.backend.reset()
                    if not error:
                        if self.audio:self.audio.close()
                        self.ready = True;self.sequence = seq;data = IDENTITY;self.range_url.clear();self.url_total=0
            elif not self.ready:
                error = OFFLINE
            elif seq != (self.sequence + 1) & 0xffff:
                error = INVALID
            else:
                self.sequence = seq
                if op == 2 and not payload:
                    data = struct.pack('<6I', 1, 1, 0x0100007f, (-1)&0xffffffff, 0, MAXIMUM)
                elif op==10 and not payload:
                    capabilities=list(self.backend.capabilities())
                    if self.audio and self.audio.available:capabilities[3]|=4
                    data=struct.pack('<4I',*capabilities)
                elif op==11 and len(payload)==4:
                    error=self.backend.release(struct.unpack('<I',payload)[0])
                elif op==20:
                    if not 5<=len(payload)<=512:
                        self.clear_request();error=INVALID
                    else:
                        offset,total=struct.unpack_from('<HH',payload);chunk=payload[4:]
                        if not 1<=total<=MAX_REQUEST or offset>total or len(chunk)>total-offset:
                            self.clear_request();error=INVALID
                        else:
                            if not offset:self.clear_request();self.request_total=total
                            if offset!=len(self.request_data) or total!=self.request_total:self.clear_request();error=INVALID
                            else:self.request_data.extend(chunk);data=struct.pack('<H',len(self.request_data))
                elif op==21:
                    try:
                        if len(payload)!=24:raise ValueError('invalid commit')
                        method,u,h,b,maximum,timeout=struct.unpack('<6I',payload)
                        if (method not in (1,2) or not 1<=u<=2048 or h>MAX_HEADERS or b>MAX_REQUEST_BODY or
                            len(self.request_data)!=self.request_total or len(self.request_data)!=u+h+b):raise ValueError('invalid request sizes')
                        url=self.request_data[:u].decode('utf-8');headers=bytes(self.request_data[u:u+h]);body=bytes(self.request_data[u+h:])
                        error,job=self.backend.open_request(url,'GET' if method==1 else 'POST',headers,body,maximum,timeout)
                        if not error:data=struct.pack('<I',job)
                    finally:self.clear_request()
                elif op==8 and len(payload)>=5:
                    offset,total=struct.unpack_from('<HH',payload);chunk=payload[4:]
                    if not 1<=total<=2048 or offset>total or len(chunk)>total-offset or any(c<=32 or c==127 or c==92 for c in chunk):
                        error=INVALID
                    else:
                        if offset==0:self.range_url.clear();self.url_total=total
                        if offset!=len(self.range_url) or total!=self.url_total:error=INVALID
                        else:self.range_url.extend(chunk);data=struct.pack('<H',len(self.range_url))
                elif op==9 and 16<=len(payload)<=416:
                    offset,length,total,timeout=struct.unpack_from('<4I',payload)
                    url=self.range_url.decode('utf-8');referer=payload[16:].decode('utf-8')
                    if len(self.range_url)!=self.url_total or not valid_url(url,2048):error=INVALID
                    else:
                        error,job=self.backend.open_range(url,offset,length,total,timeout,referer)
                        if not error:self.range_url.clear();self.url_total=0;data=struct.pack('<I',job)
                elif op in (3,12) and 9 <= len(payload) <= 408:
                    maximum, timeout = struct.unpack_from('<II', payload)
                    url = payload[8:].decode('utf-8')
                    if not 1 <= maximum <= MAXIMUM or not 1000 <= timeout <= 60000 or not valid_url(url):
                        error = INVALID
                    else:
                        error, job = (self.backend.open_background if op==12 else self.backend.open)(url, maximum, timeout)
                        data = struct.pack('<I', job)
                elif op==28 and len(payload)==12:
                    job,offset,wanted=struct.unpack('<III',payload)
                    if not 1<=wanted<=500:error=INVALID
                    else:error,data=self.backend.headers(job,offset,wanted)
                elif op==7 and len(payload)==4:
                    error,data=self.backend.metadata(struct.unpack('<I',payload)[0])
                elif op in (4, 6) and len(payload) == 4:
                    job, = struct.unpack('<I', payload)
                    if op == 6:
                        error = self.backend.cancel(job)
                    else:
                        info = self.backend.snapshot(job)
                        if info is None:
                            error = BAD_JOB
                        else:
                            data = struct.pack('<8I', *dataclasses.astuple(info))
                elif op == 5 and len(payload) == 8:
                    job, wanted = struct.unpack('<II', payload)
                    if not 1 <= wanted <= 500:
                        error = INVALID
                    else:
                        result = self.backend.read(job, wanted)
                        if result is None:
                            error = BAD_JOB
                        else:
                            offset, block = result;data = struct.pack('<III', job, offset, len(block)) + block
                elif 13<=op<=19 and self.audio and self.audio.available:
                    error,data=self.audio.dispatch(op,payload)
                else:
                    error = INVALID
        except (ValueError, UnicodeError, struct.error):
            error = INVALID
        finally:
            if op==21:self.clear_request()
        return encode(op | 128, seq, b'' if error else data, error)

def serial_module():
    import serial
    return serial

def read_available(port):
    # A request is often 16..40 bytes. read(1024) waits out the serial timeout
    # after every short request, throttling otherwise-fast USB transfers.
    return port.read(port.in_waiting or 1)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='H1 COM port, e.g. COM4')
    args = parser.parse_args()
    from h1_audio_pc import AsyncAudioService
    audio=AsyncAudioService();serial = serial_module();backend = Backend();server = Server(backend,audio);wire = Parser()
    port = serial.Serial(port=None, baudrate=115200, timeout=0.05, write_timeout=2)
    port.dtr = False;port.rts = False;port.port = args.port
    try:
        port.open();port.reset_input_buffer();port.dtr = True
        print('PC reference ready. On H1: Space to connect, Enter to GET, D to download. Ctrl+C stops.')
        while True:
            for frame in wire.feed(read_available(port)):
                reply = server.dispatch(frame)
                if port.write(reply) != len(reply):
                    raise OSError('short serial write')
    except KeyboardInterrupt:
        pass
    finally:
        port.close();backend.close();audio.close()

if __name__ == '__main__':
    main()
