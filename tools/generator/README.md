# gen.py: ESA-ADB → CSV

The script takes one telemetry channel from ESA-ADB (Mission 1, `channel_60`) and writes it to a CSV file for LogGrid, at a requested size. When one copy of the data is not enough, it appends further copies shifted in time.

## Usage

Run from the `esa-adb/` directory, because the data path is relative (`raw/ESA-Mission1.zip`):

```bash
python generator/gen.py --size 1G --out data.csv       # ≥ 1 GiB: 2 copies, 1.29 GB
python generator/gen.py --copies 5 --out data5.csv     # exactly 5 full copies
python generator/gen.py --rows 1000 --out sample.csv   # first 1000 lines
python generator/gen.py --dump --out ch60.csv          # whole channel, 1 copy
```

Requirements: Python 3, `numpy`, `pandas` and `raw/ESA-Mission1.zip` (about 3.8 GB).

## Parameters

| Parameter | Default | What it does |
|---|---|---|
| `--size` | `1G` | Target file size. Accepts `K`/`M`/`G` in binary units (1G = 1024³ B) or a plain number of bytes. |
| `--out` | `data.csv` | Output file. |
| `--copies N` | – | Writes exactly N full copies of the channel. Ignores `--size`. Expected results are easy to check: count = N · 14,639,410, min and max are the same as for one copy, sum ≈ N · sum of one copy (floating-point rounding, so compare with the spec tolerance). |
| `--rows N` | – | Writes only the first N lines of one copy, without adding more copies. Ignores `--size` and `--copies`. |
| `--dump` | – | Writes the whole channel once (14,639,410 lines, 637 MB), same as `--copies 1`. Ignores `--size`, `--copies` and `--rows`. |

## Format

The file has no header. Each line has the form `id,timestamp,channel,value`:

```
1,946684823847,channel_60,0.7188446
2,946684853847,channel_60,0.7197575
3,946684883847,channel_60,0.7307469
```

- `id`: line number starting at 1, continuous across all copies.
- `timestamp`: Unix time in milliseconds (UTC). The first sample is 2000-01-01 00:00:23.847.
- `channel`: always `channel_60`.
- `value`: channel value (float32, range about 0.011–0.938). It is written in the shortest form that reads back as exactly the same float32. An `assert` checks this, so a float32 sum computed in the reference program matches bit for bit.

## Where the numbers come from

- **Channel 60**: chosen on 14 Nov based on `analiza/kanaly-statystyki.csv` and `analiza/kandydaci-kanalow.png`. The data is continuous from 2000-01-01 to 2013-12-31 and has no NaNs.
- **30,000 ms**: the sampling step of channel 60. 99.1% of the gaps between samples are exactly 30 s (column `step_mode_s` in `kanaly-statystyki.csv`). Copy number `c` is shifted by `c · span`, where `span = (last ts − first ts) + 30,000`. This way each copy starts 30 s after the last sample of the previous one. The time axis has no gap or overlap, and timestamps keep increasing.
- **Copy size**: one copy is 14,639,410 lines, 637 MB. Later copies are slightly larger because `id` and timestamps get longer. The size is checked only after a whole copy is written, so the file ends up larger than `--size`, by less than one copy: `1G` gives 2 copies (1.29 GB), `10G` gives 17 copies (11.2 GB). Use `--copies` when you need an exact number of copies instead of a size.

## Security

ESA-ADB channels are pandas pickles. A plain `pickle.load` can run arbitrary code from the file, so `Safe` allows only classes from `pandas`, `numpy` and `builtins.slice`, and rejects everything else.
