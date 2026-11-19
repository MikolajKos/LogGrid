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
    pass