# ref.py: reference statistics

The script reads LogGrid CSV files and computes count, sum, min and max of the valid values, plus how many lines were skipped and why. It is slow and simple on purpose: its output is the ground truth that LogGrid and Flink results are checked against.

## Usage

Run from the repository root:

```bash
python tools/reference/ref.py data.csv         # one file
python tools/reference/ref.py data/            # all *.csv in a directory
```

A directory means all its `*.csv` files, sorted by name, without subdirectories. Requirements: Python 3, no extra packages.

## Output

One JSON line:

```json
{"count": 2, "sum": -149.75, "min": -150.0, "max": 0.25, "skipped_empty": 1, "skipped_nan": 3, "skipped_malformed": 11}
```

- `count`, `sum`, `min`, `max`: computed only from `ok` lines. `min` and `max` are `null` when no line is `ok`.
- `skipped_empty`, `skipped_nan`, `skipped_malformed`: lines skipped in each category. Blank lines are not counted anywhere.

When comparing with LogGrid or Flink, `sum` needs a tolerance, because they add values in a different order. All other fields must match exactly.

## Line rules

Each line has the form `id,timestamp,channel,value` and ends with `\n`. The checks run in this order, and the first one that matches decides:

| # | Check | Category |
|---|---|---|
| 1 | Blank line | ignored |
| 2 | No trailing `\n` (truncated last line) | malformed |
| 3 | Non-ASCII bytes | malformed |
| 4 | Not exactly 4 fields | malformed |
| 5 | `id` is not digits, or is above uint64 | malformed |
| 6 | `timestamp` is not an optional `-` and digits, or is outside int64 | malformed |
| 7 | `channel` has characters other than `A–Z a–z 0–9 _` | malformed |
| 8 | `value` is empty | empty |
| 9 | `value` is `nan`, `inf` or `infinity` (any case, optional sign) | nan |
| 10 | `value` is not a plain number: `-?digits[.digits][e±digits]` | malformed |
| 11 | `value` overflows to infinity, e.g. `1e400` | nan |
| 12 | Everything else | ok |

Because of the order, a bad `id` wins over an empty value: `abc,1,ch,` is malformed, not empty. Forms such as `+1.0`, `.5`, ` 1.0` and `1_000` are malformed, although Python's `float()` would accept them.

## Test data

`make_test.py` writes `test.csv`, a small file with one line for every rule above. Each line in the script has a comment with its expected category.

```bash
cd tools/reference
python make_test.py              # writes test.csv
python ref.py test.csv
```

Expected output is the JSON shown in [Output](#output).

## Why it is written this way

- **Binary mode**: files are opened with `"rb"`. Text mode would turn `\r\n` into `\n`, so a line ending in `\r` would count as `ok` instead of malformed.
- **`math.fsum`**: gives the exact sum, independent of the order of values. That makes it a fixed point to compare the other systems against.
- **`set_int_max_str_digits(0)`**: Python 3.11+ refuses `int()` on strings longer than 4300 digits. Without this, a very long `id` would crash the script instead of being counted as malformed.
