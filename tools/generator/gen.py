import pickle, zipfile, io, numpy as np, pandas as pd, warnings, argparse
warnings.filterwarnings('ignore', category=DeprecationWarning)
class Safe(pickle.Unpickler):
    def find_class(self, m, n):
        if m.split('.')[0] in ('pandas','numpy') or (m=='builtins' and n=='slice'): return super().find_class(m,n)
        raise pickle.UnpicklingError(f'blocked {m}.{n}')

def parse_size(s):
    units = {'K': 1024, 'M': 1024**2, 'G': 1024**3}
    s = s.upper()
    if s[-1] in units:
        return int(float(s[:-1]) * units[s[-1]])
    return int(s)

def make_line(c, i):
    return f'{c * n + i + 1},{ts[i] + c * span},channel_60,{val_str[i]}\n'

parser = argparse.ArgumentParser(description='Generate CSV from ESA-ADB')
parser.add_argument('--size', default='1G', help='sizes, np. 500M, 1G, 10G')
parser.add_argument('--out', default='data.csv', help='output file')
parser.add_argument('--input', default='raw/ESA-Mission1.zip', help='path to ESA-Mission1.zip')
parser.add_argument('--rows', type=int, help='save only first N lines, np. 1000 (ignores --size)')
parser.add_argument('--dump', action='store_true', help='save whole channel at once, without copies (ignores --size i --rows)')
parser.add_argument('--copies', type=int, help='save exactly N full copies, np. 5 (ignores --size)')
args = parser.parse_args()
target = parse_size(args.size)

outer = zipfile.ZipFile(args.input)
inner = zipfile.ZipFile(io.BytesIO(outer.read('ESA-Mission1/channels/channel_60.zip')))

df = Safe(io.BytesIO(inner.read(inner.namelist()[0]))).load()
ts = df.index.as_unit('ms').asi8
vals = df['channel_60'].to_numpy()
val_str = np.array([str(v) for v in vals])

assert np.array_equal(val_str.astype(np.float32), vals)

n = len(ts)
span = ts[-1] - ts[0] + 30_000
written = 0
lines = 0
c = 0

with open(args.out, 'w') as f:
    if args.dump or args.rows:
        for i in range(n if args.dump else min(args.rows, n)):
            line = make_line(0, i)
            f.write(line)
            written += len(line)
            lines += 1
        c = 1
    else:
        while c < args.copies if args.copies else written < target:
            for i in range(n):
                line = make_line(c, i)
                f.write(line)
                written += len(line)
                lines += 1
            c += 1
print(f'{c} kopii, {lines} linii, {written} B')