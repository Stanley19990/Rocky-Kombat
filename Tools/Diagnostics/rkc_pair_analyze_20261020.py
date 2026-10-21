"""Compare PrintWindow vs on-screen captures of the same window."""
import sys, os, zlib, struct


def read_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a png: ' + path
    pos, idat, w, h, nch = 8, b'', None, None, None
    while pos < len(data):
        ln = struct.unpack('>I', data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and inter == 0, 'unsupported png ' + path
            nch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
        elif typ == b'IDAT':
            idat += body
        elif typ == b'IEND':
            break
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * nch
    out, prev, p = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if f == 1:
            for i in range(nch, stride):
                line[i] = (line[i] + line[i - nch]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                b = prev[i]
                c = prev[i - nch] if i >= nch else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out.append(bytes(line))
        prev = line
    return w, h, nch, out


def stats(path):
    w, h, nch, rows = read_png(path)
    lum_sum = dark = 0
    dark_by_col = [0] * w
    dark_rows = {}
    for y, row in enumerate(rows):
        for x in range(w):
            o = x * nch
            if nch >= 3:
                r, g, b = row[o], row[o + 1], row[o + 2]
            else:
                r = g = b = row[o]
            lum_sum += (r * 299 + g * 587 + b * 114) // 1000
            if r < 16 and g < 16 and b < 16:
                dark += 1
                dark_by_col[x] += 1
                dark_rows[y] = dark_rows.get(y, 0) + 1
    n = w * h
    cols = [i for i, c in enumerate(dark_by_col) if c > 3]
    return (w, h, round(lum_sum / n, 2), dark, round(100.0 * dark / n, 3),
            cols[0] if cols else -1, cols[-1] if cols else -1,
            (min(dark_rows), max(dark_rows)) if dark_rows else None)


if __name__ == '__main__':
    for path in sys.argv[1:]:
        w, h, mean, dark, pct, c0, c1, rsp = stats(path)
        print(f"{os.path.basename(path):>10}  {w}x{h}  mean={mean:>6}  "
              f"dark={dark:>7} ({pct}%)  cols {c0}..{c1}  rows {rsp}")
