"""IMA ADPCM frames, the twin of firmware/src/adpcm.h.

frame = [type][predictor lo][predictor hi][step index][nibbles, low nibble first]
type 0x01 = mic (device -> PC), 0x02 = speaker (PC -> device). 16 kHz mono, 4 bits per sample.
"""
import struct

MIC, SPEAKER = 0x01, 0x02

STEP = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
    97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def _step(pred, index, code):
    st = STEP[index]
    diff = st >> 3
    if code & 4:
        diff += st
    if code & 2:
        diff += st >> 1
    if code & 1:
        diff += st >> 2
    pred = pred - diff if code & 8 else pred + diff
    pred = max(-32768, min(32767, pred))
    index = max(0, min(88, index + INDEX[code]))
    return pred, index


class Encoder:
    def __init__(self):
        self.pred, self.index = 0, 0

    def _code(self, sample):
        st = STEP[self.index]
        diff = sample - self.pred
        code = 0
        if diff < 0:
            code, diff = 8, -diff
        if diff >= st:
            code |= 4
            diff -= st
        if diff >= st >> 1:
            code |= 2
            diff -= st >> 1
        if diff >= st >> 2:
            code |= 1
        self.pred, self.index = _step(self.pred, self.index, code)
        return code

    def frame(self, kind, samples):
        """samples: sequence of int16 (even count) -> one frame (bytes)."""
        out = bytearray(struct.pack("<Bhb", kind, self.pred, 0))
        out[3] = self.index
        for i in range(0, len(samples) - 1, 2):
            out.append(self._code(samples[i]) | self._code(samples[i + 1]) << 4)
        return bytes(out)


def decode(frame):
    """One frame -> (kind, list of int16 samples)."""
    if len(frame) < 4:
        return None, []
    kind = frame[0]
    pred = struct.unpack_from("<h", frame, 1)[0]
    index = min(frame[3], 88)
    out = []
    for b in frame[4:]:
        pred, index = _step(pred, index, b & 0x0F)
        out.append(pred)
        pred, index = _step(pred, index, b >> 4)
        out.append(pred)
    return kind, out
