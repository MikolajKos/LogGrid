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
