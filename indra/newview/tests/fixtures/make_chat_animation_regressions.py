"""Regenerate tiny authored GIFs; Pillow independently supplies expected pixels.

Not needed to build/run tests. Clear between literal LZW codes keeps fixtures
simple and avoids a dependency on an encoder's frame/palette optimization.
"""
from io import BytesIO
from pathlib import Path
import struct
from PIL import Image, ImageSequence


def gif(size, frames):
    data = bytearray(b'GIF89a' + struct.pack('<HHBBB', *size, 0x81, 0, 0))
    data += bytes([0, 0, 255, 255, 0, 0, 0, 255, 0, 255, 255, 0])
    data += b'\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00'
    for left, top, width, height, color, disposal in frames:
        data += b'\x21\xf9\x04' + bytes([disposal << 2]) + b'\x01\x00\x00\x00'
        data += b'\x2c' + struct.pack('<HHHHB', left, top, width, height, 0) + b'\x02'
        codes = [code for _ in range(width * height) for code in [4, color]] + [5]
        bits = sum(code << (3 * i) for i, code in enumerate(codes))
        block = bits.to_bytes((len(codes) * 3 + 7) // 8, 'little')
        data += bytes([len(block)]) + block + b'\x00'
    return data + b'\x3b'


def array(name, data):
    rows = [','.join(str(v) for v in data[i:i + 20]) for i in range(0, len(data), 20)]
    return 'static const std::vector<unsigned char> ' + name + '{\n' + ',\n'.join(rows) + '\n};\n'


opaque = gif((3, 2), [(0, 0, 3, 2, 0, 1), (1, 0, 1, 1, 1, 2), (2, 1, 1, 1, 2, 1)])
partial = gif((3, 2), [(0, 0, 1, 1, 1, 1), (2, 1, 1, 1, 2, 1)])
large = gif((2048, 2048), [(0, 0, 1, 1, 1, 1), (1, 0, 1, 1, 2, 1)])
output = '// Authored fixtures; regenerate with make_chat_animation_regressions.py.\n'
for name, data in [('opaqueBackground', opaque), ('partialBackground', partial), ('largeLoop', large)]:
    output += array(name + 'Bytes', data)
    if name != 'largeLoop':
        pixels = [frame.convert('RGBA').transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
                  for frame in ImageSequence.Iterator(Image.open(BytesIO(data)))]
        output += 'static const std::vector<std::vector<unsigned char>> ' + name + 'Pixels{\n'
        output += ',\n'.join('{' + ','.join(str(v) for v in frame) + '}' for frame in pixels) + '\n};\n'
Path(__file__).with_name('chat_animation_regressions.inc').write_text(output)
