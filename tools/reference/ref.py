"""LogGrid reference program: count, sum, min, max and bad-line counters.

Its output is the ground truth for LogGrid and Flink.
Usage: python ref.py <file.csv | directory>
A directory means all its *.csv files, sorted by name, without subdirectories.
Output: one JSON line. min and max are null when no line is ok.
"""

import json, math, sys, re
from pathlib import Path

#line format: id,timestamp,channel,value
#everything not listed here is classified as malformed

ID = re.compile(r"[0-9]+")
TIMESTAMP = re.compile(r"-?[0-9]+")
CHANNEL = re.compile(r"[a-zA-Z0-9_]+")
VALUE = re.compile(r"-?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?")

NAN_INF = re.compile(r"[+-]?(nan|inf|infinity)", re.IGNORECASE)

UINT64_MAX = 2**64 - 1
INT64_MIN = -(2**63)
INT64_MAX = 2**63 - 1

def classify(line):
    """Return (category, value) for one line without its trailing \\n.
    The order of checks is the spec's rule order: the first match wins.
    """
    if line == b"":
        return "ignore", None
    if not line.isascii():
        return "malformed", None
    fields = line.decode("ascii").split(",")
    if len(fields) !=4:
        return "malformed", None
    id_, ts, channel, value = fields
    if not(ID.fullmatch(id_) and int(id_) <= UINT64_MAX):
        return "malformed", None
    if not (TIMESTAMP.fullmatch(ts) and INT64_MIN <= int(ts) <= INT64_MAX):
        return "malformed", None
    if not CHANNEL.fullmatch(channel):
        return "malformed", None
    if value == "":
        return "empty", None
    if NAN_INF.fullmatch(value):
        return "nan", None
    if not VALUE.fullmatch(value):
        return "malformed", None
    number = float(value)
    if not math.isfinite(number):
        return "nan", None
    return "ok", number

def valid_values(paths, stats):
    """Yield every ok value and update stats in place.
    stats is complete only after the generator is exhausted.
    """
    for path in paths:
        with open(path, "rb") as f:
            for raw in f:
                if raw.endswith(b"\n"):
                    category, value = classify(raw[:-1])
                else:
                    category, value = "malformed", None
                if category == "ok":
                    stats["count"] += 1
                    stats["min"] = value if stats["min"] is None else min(stats["min"], value)
                    stats["max"] = value if stats["max"] is None else max(stats["max"], value)
                    yield value
                elif category != "ignore":
                    stats["skipped_" + category] += 1

def input_files(arg):
    path = Path(arg)
    if not path.is_dir():
        return [path]
    files = sorted(path.glob("*.csv"))
    if not files:
        sys.exit(f"error: no *.csv files in {path}")
    return files

def main():
    if len(sys.argv) != 2:
        sys.exit("usage: python ref.py <file.csv | directory>")
    if hasattr(sys, "set_int_max_str_digits"):
        sys.set_int_max_str_digits(0)
    stats = {"count": 0, "sum": 0.0, "min": None, "max": None,
             "skipped_empty": 0, "skipped_nan": 0, "skipped_malformed": 0}
    stats["sum"] = math.fsum(valid_values(input_files(sys.argv[1]), stats))
    print(json.dumps(stats))

if __name__ == "__main__":
    main()
