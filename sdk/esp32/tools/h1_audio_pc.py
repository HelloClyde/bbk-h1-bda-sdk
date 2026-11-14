"""Bounded AAC compute service for the PC H1NET simulator (operations 13..19).

The transport supplies complete wire payloads. This module only consumes AAC
AudioSpecificConfig and raw AAC packets; it does not fetch or demux media.
Install its optional backend with ``python -m pip install --target .tools/pyav av``.
"""
from __future__ import annotations

from fractions import Fraction
from pathlib import Path
from array import array
import struct
import sys
import threading

try:
    import av as _av
except (ImportError, OSError):
    _local_backend = Path(__file__).resolve().parents[1] / '.tools' / 'pyav'
    if _local_backend.is_dir():
        sys.path.insert(0, str(_local_backend))
    try:
        import av as _av
    except (ImportError, OSError) as _import_error:
        _av = None
        _backend_error = str(_import_error)

OK, INVALID, BUSY, OFFLINE, BAD_JOB, INTERNAL = range(6)
READY, EOF, FAILED = range(1, 4)
OPEN, PUSH, READ, INFO, RESET, CLOSE, FINISH = range(13, 20)
OUT_RATE = 32768
OUT_CHANNELS = 1
MAX_FRAME = 16384
MAX_FRAGMENT = 492
MAX_READ = 500
PCM_LIMIT = 128 * 1024
# A single AAC access unit (including HE-AAC's doubled frame) and the
# resampler's tail fit within this reserve, even at AAC's lowest sample rate.
PCM_RESERVE = 64 * 1024


def _trunc_div(value, divisor):
    return value // divisor if value >= 0 else -((-value) // divisor)


class _BoxResampler:
    """The integer weighted-box algorithm in src/platform/audio.c.

    PyAV only converts the decoder's sample format to native-rate s16; it
    never changes channels or rate. Mono folding truncates toward zero.
    A fractional output sample remains pending across access units and is
    discarded at EOF, matching the H1 audio writer without zero padding.
    """
    def __init__(self):
        self.converter = None
        self.rate = self.channels = self.phase = self.accumulator = 0

    def _convert(self, frame):
        if self.converter is None:
            self.rate = frame.sample_rate
            self.channels = len(frame.layout.channels)
            if not 7350 <= self.rate <= 192000 or self.channels not in (1, 2):
                raise ValueError('unsupported AAC rate or channel layout')
            self.converter = _av.AudioResampler(format='s16', layout=frame.layout.name, rate=self.rate)
        elif frame.sample_rate != self.rate or len(frame.layout.channels) != self.channels:
            raise ValueError('AAC format changed without RESET')
        return self.converter.resample(frame)

    def convert(self, frames, flush=False):
        result = array('h')
        converted = []
        for frame in frames:
            converted.extend(self._convert(frame))
        if flush and self.converter is not None:
            converted.extend(self.converter.resample(None))
        for frame in converted:
            values = array('h')
            values.frombytes(bytes(frame.planes[0])[:frame.samples * self.channels * 2])
            for at in range(0, len(values), self.channels):
                sample = values[at] if self.channels == 1 else _trunc_div(values[at] + values[at + 1], 2)
                remaining = OUT_RATE
                while remaining:
                    weight = min(self.rate - self.phase, remaining)
                    self.accumulator += sample * weight
                    self.phase += weight
                    remaining -= weight
                    if self.phase == self.rate:
                        if len(result) * 2 >= PCM_RESERVE:
                            raise ValueError('AAC access unit exceeds PCM reserve')
                        result.append(_trunc_div(self.accumulator, self.rate))
                        self.phase = self.accumulator = 0
        if sys.byteorder != 'little':
            result.byteswap()
        return result.tobytes()


class _Job:
    def __init__(self, number: int, asc: bytes):
        self.number = number
        self.asc = asc
        self.decoder = _av.CodecContext.create('aac', 'r')
        self.decoder.extradata = asc
        self.decoder.open()
        self.resampler = _BoxResampler()
        self.state = READY
        self.error = 0
        self.sequence = 0
        self.frame_total = self.frame_pts = 0
        self.fragment = bytearray()
        self.pcm = bytearray()
        self.input_bytes = self.output_bytes = self.delivered = 0

    def fail(self):
        self.state = FAILED
        self.error = INTERNAL
        self.fragment.clear()

    def convert(self, frames, flush=False):
        """Stage no more than one bounded packet's PCM before committing it."""
        output = self.resampler.convert(frames, flush)
        if len(self.pcm) + len(output) > PCM_LIMIT:
            raise ValueError('PCM queue overflow')
        if self.output_bytes + len(output) > 0xffffffff:
            raise ValueError('PCM byte offset exhausted')
        self.pcm.extend(output)
        self.output_bytes += len(output)


class AudioService:
    """One AAC job, one compressed frame and a bounded PCM queue.

    Frames start at sequence zero and increase by one (modulo 2**32).
    Fragments must be contiguous; a BUSY response never consumes a fragment.
    Empty READ succeeds with a zero count; INFO disambiguates READY/EOF/FAILED.
    RESET retains the job ID/configuration and restarts sequence and offsets.
    The synchronous decoder supplies backpressure before accepting a final
    fragment, so callers can READ and retry that exact fragment safely.
    """
    def __init__(self):
        self.available = _av is not None
        self.backend_version = getattr(_av, '__version__', '')
        self.unavailable_reason = '' if self.available else globals().get('_backend_error', 'PyAV unavailable')
        self._lock = threading.RLock()
        self._job = None
        self._next_job = 0

    def dispatch(self, op: int, payload: bytes) -> tuple[int, bytes]:
        if not isinstance(payload, (bytes, bytearray)):
            return INVALID, b''
        with self._lock:
            if op == OPEN:
                return self._open(payload)
            if op not in (PUSH, READ, INFO, RESET, CLOSE, FINISH):
                return INVALID, b''
            expected = 8 if op == READ else None if op == PUSH else 4
            if (expected is not None and len(payload) != expected) or (op == PUSH and not 21 <= len(payload) <= 512):
                return INVALID, b''
            number = struct.unpack_from('<I', payload)[0]
            job = self._job
            if not number or job is None or number != job.number:
                return BAD_JOB, b''
            if op == PUSH:
                return self._push(job, payload)
            if op == READ:
                wanted = struct.unpack_from('<I', payload, 4)[0]
                if not 1 <= wanted <= MAX_READ:
                    return INVALID, b''
                data = bytes(job.pcm[:wanted])
                del job.pcm[:len(data)]
                offset = job.delivered
                job.delivered += len(data)
                return OK, struct.pack('<3I', number, offset, len(data)) + data
            if op == INFO:
                return OK, struct.pack('<8I', number, job.state, OUT_RATE, OUT_CHANNELS,
                                       job.input_bytes, job.output_bytes, len(job.pcm), job.error)
            if op == CLOSE:
                self._job = None
                return OK, b''
            if op == RESET:
                try:
                    self._job = _Job(number, job.asc)
                except Exception:
                    job.fail()
                    return INTERNAL, b''
                return OK, b''
            return self._finish(job)

    def _open(self, payload):
        if len(payload) < 17:
            return INVALID, b''
        version, rate, channels, size = struct.unpack_from('<4I', payload)
        if version != 1 or rate != OUT_RATE or channels != OUT_CHANNELS or not 1 <= size <= 64 or len(payload) != 16 + size:
            return INVALID, b''
        if not self.available:
            return OFFLINE, b''
        if self._job is not None:
            return BUSY, b''
        number = (self._next_job + 1) & 0xffffffff or 1
        try:
            job = _Job(number, bytes(payload[16:]))
        except Exception:
            return INVALID, b''
        self._next_job = number
        self._job = job
        return OK, struct.pack('<I', number)

    def _push(self, job, payload):
        _, sequence, total, offset, pts = struct.unpack_from('<5I', payload)
        fragment = payload[20:]
        if (not 1 <= total <= MAX_FRAME or not 1 <= len(fragment) <= MAX_FRAGMENT or
                sequence != job.sequence or offset != len(job.fragment) or
                offset + len(fragment) > total):
            return INVALID, b''
        if job.fragment and (total != job.frame_total or pts != job.frame_pts):
            return INVALID, b''
        if job.state != READY:
            return (INTERNAL if job.state == FAILED else INVALID), b''
        complete = offset + len(fragment) == total
        if complete and len(job.pcm) > PCM_LIMIT - PCM_RESERVE:
            return BUSY, b''
        if job.input_bytes + len(fragment) > 0xffffffff:
            return INVALID, b''
        if not job.fragment:
            job.frame_total, job.frame_pts = total, pts
        job.fragment.extend(fragment)
        job.input_bytes += len(fragment)
        next_offset = len(job.fragment)
        if complete:
            try:
                packet = _av.Packet(bytes(job.fragment))
                packet.pts = packet.dts = pts
                packet.time_base = Fraction(1, 1000)
                job.convert(job.decoder.decode(packet))
            except Exception:
                job.fail()
                return INTERNAL, b''
            job.fragment.clear()
            job.sequence = (job.sequence + 1) & 0xffffffff
            job.frame_total = job.frame_pts = 0
        return OK, struct.pack('<I', next_offset)

    def _finish(self, job):
        if job.fragment:
            return INVALID, b''
        if job.state == EOF:
            return OK, b''
        if job.state == FAILED:
            return INTERNAL, b''
        if len(job.pcm) > PCM_LIMIT - PCM_RESERVE:
            return BUSY, b''
        try:
            job.convert(job.decoder.decode(None), flush=True)
        except Exception:
            job.fail()
            return INTERNAL, b''
        job.state = EOF
        return OK, b''

    def close(self):
        """Release decoder, resampler, partial AAC and PCM storage."""
        with self._lock:
            self._job = None


class _AsyncJob:
    """Protocol state; its decoder core belongs exclusively to the worker."""
    def __init__(self, core):
        self.core = core
        self.number, self.asc = core.number, core.asc
        self.state, self.error = READY, 0
        self.sequence = self.frame_total = self.frame_pts = 0
        self.fragment = bytearray()
        self.pending = []
        self.pcm = bytearray()
        self.input_bytes = self.output_bytes = self.delivered = 0
        self.finish_requested = False

    def fail(self):
        self.state, self.error = FAILED, INTERNAL
        self.fragment.clear()
        self.pending.clear()


class AsyncAudioService(AudioService):
    """The same AAC/box algorithm on one independent decoder worker.

    Two input slots include the partial frame and completed queued frames;
    the worker may additionally own one frame. Output remains capped at
    PCM_LIMIT. A full input queue returns BUSY without consuming a fragment.
    READ/INFO never acquire a decoder lock: decoding and sample conversion
    run outside the short protocol-state lock.

    RESET replaces the generation while retaining the public job ID. CLOSE
    and transport HELLO invalidate it immediately; an old in-flight decoder
    may finish its current packet, but can never publish into a newer job.
    One worker serves successive generations and exits after close. FINISH
    acknowledges a drain request; INFO reaches EOF after queued input and
    decoder/resampler flush have produced their output.
    """
    INPUT_SLOTS = 2

    def __init__(self):
        super().__init__()
        self._cv = threading.Condition(self._lock)
        self._thread = None

    @staticmethod
    def _discard(job):
        if job is not None:
            job.pending.clear()
            job.fragment.clear()
            job.pcm.clear()

    def _ensure_worker(self):
        if self._thread is None:
            self._thread = threading.Thread(target=self._worker, name='H1 AAC worker', daemon=True)
            try:
                self._thread.start()
            except RuntimeError:
                self._thread = None
                raise

    def dispatch(self, op: int, payload: bytes) -> tuple[int, bytes]:
        with self._cv:
            previous = self._job
            if op == RESET:
                if not isinstance(payload, (bytes, bytearray)) or len(payload) != 4:
                    return INVALID, b''
                number = struct.unpack('<I', payload)[0]
                if not number or previous is None or number != previous.number:
                    return BAD_JOB, b''
                try:
                    replacement = _AsyncJob(_Job(number, previous.asc))
                    self._ensure_worker()
                except Exception:
                    previous.fail()
                    self._cv.notify_all()
                    return INTERNAL, b''
                self._job = replacement
                result = OK, b''
            else:
                result = super().dispatch(op, payload)
            if previous is not None and self._job is not previous:
                self._discard(previous)
            self._cv.notify_all()
            return result

    def _open(self, payload):
        result = super()._open(payload)
        if result[0] == OK:
            self._job = _AsyncJob(self._job)
            try:
                self._ensure_worker()
            except RuntimeError:
                self._job = None
                return INTERNAL, b''
        return result

    def _push(self, job, payload):
        _, sequence, total, offset, pts = struct.unpack_from('<5I', payload)
        fragment = payload[20:]
        if (not 1 <= total <= MAX_FRAME or not 1 <= len(fragment) <= MAX_FRAGMENT or
                sequence != job.sequence or offset != len(job.fragment) or
                offset + len(fragment) > total):
            return INVALID, b''
        if job.fragment and (total != job.frame_total or pts != job.frame_pts):
            return INVALID, b''
        if job.state != READY or job.finish_requested:
            return (INTERNAL if job.state == FAILED else INVALID), b''
        if not job.fragment and len(job.pending) >= self.INPUT_SLOTS:
            return BUSY, b''
        if job.input_bytes + len(fragment) > 0xffffffff:
            return INVALID, b''
        if not job.fragment:
            job.frame_total, job.frame_pts = total, pts
        job.fragment.extend(fragment)
        job.input_bytes += len(fragment)
        next_offset = len(job.fragment)
        if next_offset == total:
            job.pending.append((bytes(job.fragment), pts))
            job.fragment.clear()
            job.sequence = (job.sequence + 1) & 0xffffffff
            job.frame_total = job.frame_pts = 0
        return OK, struct.pack('<I', next_offset)

    def _finish(self, job):
        if job.fragment:
            return INVALID, b''
        if job.state == FAILED:
            return INTERNAL, b''
        job.finish_requested = True
        return OK, b''

    def _worker(self):
        while True:
            with self._cv:
                while True:
                    job = self._job
                    if job is None:
                        self._thread = None
                        self._cv.notify_all()
                        return
                    if (job.state == READY and len(job.pcm) <= PCM_LIMIT - PCM_RESERVE and
                            (job.pending or job.finish_requested)):
                        flush = not job.pending
                        item = None if flush else job.pending.pop(0)
                        break
                    self._cv.wait()
            # Never hold the protocol lock across PyAV or per-sample Python.
            # The synchronous core is private to this generation/worker.
            try:
                if flush:
                    job.core.convert(job.core.decoder.decode(None), flush=True)
                else:
                    frame, pts = item
                    packet = _av.Packet(frame)
                    packet.pts = packet.dts = pts
                    packet.time_base = Fraction(1, 1000)
                    job.core.convert(job.core.decoder.decode(packet))
                output = bytes(job.core.pcm)
                job.core.pcm.clear()
            except Exception:
                with self._cv:
                    if self._job is job:
                        job.fail()
                    self._cv.notify_all()
                continue
            with self._cv:
                if self._job is not job:
                    continue
                # No other producer can use the headroom reserved before decode.
                if len(output) > PCM_LIMIT - len(job.pcm):
                    job.fail()
                else:
                    job.pcm.extend(output)
                    job.output_bytes += len(output)
                    if flush:
                        job.state = EOF
                self._cv.notify_all()

    def close(self):
        with self._cv:
            self._discard(self._job)
            self._job = None
            self._cv.notify_all()
