import json, math, sys, re
from pathlib import Path

ID = re.compile(r"[0-9]+")
TIMESTAMP = re.compile(r"-?[0-9]+")
CHANNEL = re.compile(r"[a-zA-Z0-9_]+")
VALUE = re.compile(r"-?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?")

NAN_INF = re.compile(r"[+-]?(nan|inf|infinity)", re.IGNORECASE)

UINT64_MAX = 2**64 - 1
INT64_MIN = -(2**63)
INT64_MAX = 2**63 - 1

def classify(line):
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
    for path in paths:
        with open(path, "rb") as f:
            for raw in f:
                if raw.endswith(b"\n"):
                    category, value = classify(raw[:-1])
                else:
                    category, value = "malformed", None  # truncated last line
                if category == "ok":
                    stats["count"] += 1
                    stats["min"] = value if stats["min"] is None else min(stats["min"], value)
                    stats["max"] = value if stats["max"] is None else max(stats["max"], value)
                    yield value
                elif category != "ignore":
                    stats["skipped_" + category] += 1