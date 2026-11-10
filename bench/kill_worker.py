"""Kill -9 verification for LogGrid task recovery.

Runs the docker compose cluster (master + 2 workers), starts a search and kills
worker-1 with SIGKILL once a given fraction of chunks is done. Each run checks
that the session still finishes and that totalMatches equals `grep -c`.

This is a correctness check, not a performance benchmark: chunk size comes from
DOCKER_DEBUG and timings come from one local machine.

Usage (from the repository root):
    python3 bench/kill_worker.py                     # baseline + kills at 25/50/75%
    python3 bench/kill_worker.py --kill-at 0.5 --repeat 3

Results go to bench/results/kill_worker_<date>-<time>.csv, one file per series
(or to the file given with --csv).
"""

import argparse
import csv
import datetime
import json
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path

MASTER = "http://localhost:8080"
PRINTER = "http://localhost:8081"   # Crazy Printer API (separate project)
DATA_ROOT = Path("/data/logs")      # mounted into the master and workers
KEYWORD = "ERROR"


def post_json(url, data):
    body = json.dumps(data).encode()
    req = urllib.request.Request(url, data=body,
                                 headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=5) as resp:
        return json.loads(resp.read())


def get_json(url):
    with urllib.request.urlopen(url, timeout=5) as resp:
        return json.loads(resp.read())


def run(params):
    try:
        res = subprocess.run(params, capture_output=True, text=True, check=True)
        return res.stdout
    except subprocess.CalledProcessError as e:
        print("Error occurred:", e.stderr)
        raise


def wait_for_master(url, timeout_s):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            get_json(url)
            return  # 200 OK: master is listening
        except urllib.error.HTTPError:
            return  # 404: master is listening, the session just does not exist
        except (urllib.error.URLError, ConnectionResetError):
            time.sleep(0.5)  # no connection yet

    raise TimeoutError("Master not ready")


def wait_for_done(search_id, url_base, timeout_s):
    deadline = time.monotonic() + timeout_s
    url = f"{url_base.rstrip('/')}/{search_id}"

    while time.monotonic() < deadline:
        try:
            answer = get_json(url)

            if answer["state"] == "Done":
                return answer

            time.sleep(0.05)
        except urllib.error.URLError as e:
            print("GET Error:", e)
            raise

    raise TimeoutError("Search not ready")


# ---------------------------------------------------------------------------
# Test data
# ---------------------------------------------------------------------------

def ensure_dataset(dataset, file_count, lines_per_file, timeout_s):
    """Generates the dataset with Crazy Printer unless it already exists."""
    data_dir = DATA_ROOT / dataset
    if data_dir.is_dir() and any(data_dir.glob("*.log")):
        print(f"Using existing data in {data_dir}")
        return

    print(f"Generating {file_count} x {lines_per_file} lines into {data_dir} ...")
    try:
        job = post_json(f"{PRINTER}/api/printer/start", {
            "outputDir": dataset,
            "fileCount": file_count,
            "linesPerFile": lines_per_file,
            "producerThreads": 4,
            "consumerThreads": 4,
        })
    except urllib.error.URLError as e:
        raise SystemExit(f"Crazy Printer is not reachable at {PRINTER} ({e}). "
                         "Start it with `docker compose up -d` in the crazy-printer-api repo.")

    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        status = get_json(f"{PRINTER}/api/printer/status/{job['jobId']}")
        if status["status"] == "done":
            return
        time.sleep(1)

    raise TimeoutError("Data generation did not finish")


def reference_count(dataset):
    """Number of lines containing KEYWORD, counted by grep (the reference)."""
    data_dir = DATA_ROOT / dataset
    # The shell expands *.log; grep -c on stdin prints a single total
    out = run(["sh", "-c", f"cat {data_dir}/*.log | grep -c {KEYWORD}"])
    return int(out)


def dataset_size_mb(dataset):
    return sum(f.stat().st_size for f in (DATA_ROOT / dataset).glob("*.log")) / 1024**2


# ---------------------------------------------------------------------------
# One scenario
# ---------------------------------------------------------------------------

def run_scenario(dataset, kill_at, expected, timeout_s, build):
    """Runs one search on a fresh cluster.

    kill_at: fraction of chunks done (0..1) at which worker-1 gets SIGKILL,
             or None for a run without a failure.
    """
    result = {"kill_at": kill_at, "expected": expected, "killed_at_chunk": None}

    try:
        run(["docker", "compose", "up", "-d"] + (["--build"] if build else []))
        wait_for_master(f"{MASTER}/api/status/0", 30)
        time.sleep(1)  # let both workers connect and send Hello

        start = time.monotonic()
        search_id = post_json(f"{MASTER}/api/search",
                              {"path": dataset, "keyword": KEYWORD,
                               "outputDir": "bench_kill_worker"})["searchId"]

        if kill_at is not None:
            deadline = start + timeout_s
            while time.monotonic() < deadline:
                status = get_json(f"{MASTER}/api/status/{search_id}")
                done, total = status["chunksDone"], status["chunksTotal"]
                if status["state"] == "Done":
                    break  # finished before the kill point: the run is not valid
                if total and done >= kill_at * total:
                    run(["docker", "compose", "kill", "worker-1"])  # SIGKILL by default
                    result["killed_at_chunk"] = f"{done}/{total}"
                    break
                time.sleep(0.05)

        remaining = timeout_s - (time.monotonic() - start)
        status = wait_for_done(search_id, f"{MASTER}/api/status", remaining)
        result["time_s"] = time.monotonic() - start
        result["found"] = status["totalMatches"]
        result["chunks"] = f"{status['chunksDone']}/{status['chunksTotal']}"

        # The master logs this once per lost worker that had tasks in flight
        logs = run(["docker", "compose", "logs", "master-node"])
        result["reclaimed"] = logs.count("Reclaimed task")

    except TimeoutError as e:
        result["error"] = str(e)  # the session hung: this is a FAIL, not a crash
    finally:
        run(["docker", "compose", "down"])

    result["ok"] = verdict(result)
    return result


def verdict(r):
    if "error" in r:
        return f"FAIL ({r['error']})"
    if r["found"] != r["expected"]:
        return "FAIL (wrong count)"
    if r["kill_at"] is not None and r["killed_at_chunk"] is None:
        return "INVALID (finished before kill)"
    if r["kill_at"] is not None and r["reclaimed"] == 0:
        return "INVALID (killed worker had no tasks)"
    return "PASS"


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------

def markdown_table(results):
    lines = [
        "| Zabicie workera | Chunki przy zabiciu | Wykryte awarie | Oczekiwane | Znalezione | Czas sesji [s] | Wynik |",
        "|---|---|---|---|---|---|---|",
    ]
    for r in results:
        kill = "brak" if r["kill_at"] is None else f"po {r['kill_at']:.0%} chunków"
        time_s = f"{r['time_s']:.1f}" if "time_s" in r else "-"
        lines.append(
            f"| {kill} | {r['killed_at_chunk'] or '-'} | {r.get('reclaimed', '-')} "
            f"| {r['expected']} | {r.get('found', '-')} | {time_s} | {r['ok']} |"
        )
    return "\n".join(lines)


CSV_FIELDS = [
    "timestamp", "commit", "dataset", "files", "size_mb", "kill_at", "killed_at_chunk",
    "reclaimed", "expected", "found", "chunks", "time_s", "ok", "error",
]


def append_csv(path, row):
    """Appends one run to the CSV file, writing the header for a new file.

    Called after every run, so finished runs survive if the series stops midway.
    """
    path = Path(path)
    new_file = not path.exists()
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS, extrasaction="ignore")
        if new_file:
            writer.writeheader()
        writer.writerow(row)


def environment(dataset):
    commit = run(["git", "rev-parse", "--short", "HEAD"]).strip()
    files = len(list((DATA_ROOT / dataset).glob("*.log")))
    return (f"Commit `{commit}`, {files} plików, {dataset_size_mb(dataset):.0f} MB, "
            f"chunk 5 MB (DOCKER_DEBUG), master + 2 workery w docker compose, "
            f"wzorzec `{KEYWORD}`, referencja `grep -c`.")


def main():
    parser = argparse.ArgumentParser(description="LogGrid kill -9 recovery check")
    parser.add_argument("--dataset", default="killtest",
                        help="directory under /data/logs (default: killtest)")
    parser.add_argument("--files", type=int, default=40)
    parser.add_argument("--lines", type=int, default=400_000,
                        help="lines per generated file (~95 B per line)")
    parser.add_argument("--kill-at", type=float, nargs="*", default=[0.25, 0.5, 0.75],
                        help="fractions of chunks done at which worker-1 is killed")
    parser.add_argument("--no-baseline", action="store_true",
                        help="skip the run without a failure")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--csv", help="CSV file for the results "
                             "(default: a new timestamped file in bench/results)")
    args = parser.parse_args()

    ensure_dataset(args.dataset, args.files, args.lines, timeout_s=600)
    expected = reference_count(args.dataset)
    print(f"Reference: {expected} lines with {KEYWORD}")

    # Same for every run in the series; stored in each CSV row so the file describes itself
    context = {
        "commit": run(["git", "rev-parse", "--short", "HEAD"]).strip(),
        "dataset": args.dataset,
        "files": len(list((DATA_ROOT / args.dataset).glob("*.log"))),
        "size_mb": round(dataset_size_mb(args.dataset)),
    }

    csv_path = args.csv or (Path(__file__).parent / "results" /
                            f"kill_worker_{datetime.datetime.now():%Y%m%d-%H%M%S}.csv")
    print(f"Results: {csv_path}")

    scenarios = ([] if args.no_baseline else [None]) + args.kill_at
    results = []
    build = True
    for _ in range(args.repeat):
        for kill_at in scenarios:
            r = run_scenario(args.dataset, kill_at, expected, args.timeout, build)
            build = False  # images do not change between runs
            print(r)
            results.append(r)
            append_csv(csv_path, {
                **context,
                **r,
                "timestamp": datetime.datetime.now().isoformat(timespec="seconds"),
                "time_s": round(r["time_s"], 2) if "time_s" in r else None,
            })

    print()
    print(markdown_table(results))
    print()
    print(environment(args.dataset))

    if any(r["ok"].startswith("FAIL") for r in results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()