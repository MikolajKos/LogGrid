"""Kill -9 verification for LogGrid task recovery.

Runs the docker compose cluster (master + N workers), starts a search and kills
K workers with SIGKILL once a given fraction of chunks is done. Each run checks
that the session still finishes and that totalMatches equals `grep -c`.

This is a correctness check, not a performance benchmark: chunk size comes from
the master build and timings come from one local machine.

Verdicts: PASS, FAIL (wrong count, hung session or the master failed after the
search started), INVALID (the kill did not test recovery) and ERROR (the
cluster could not be started, so the run tested nothing).

Usage (from the repository root):
    python3 bench/kill_worker.py                     # baseline + kills at 25/50/75%
    python3 bench/kill_worker.py --kill-at 0.5 --repeat 3
    python3 bench/kill_worker.py --workers 2 4 --kill-count 1 --worker-cpus 2

Results go to bench/results/kill_worker_<date>-<time>.csv, one file per series
(or to the file given with --csv).
"""

import argparse
import csv
import datetime
import json
import os
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path

MASTER = "http://localhost:8080"
PRINTER = "http://localhost:8081"   # Crazy Printer API (separate project)
DATA_ROOT = Path("/data/logs")      # mounted into the master and workers
KEYWORD = "ERROR"


class SessionHung(Exception):
    """The search did not reach Done before the deadline."""


class ClusterNotReady(Exception):
    """The master or the workers did not start; the run tested nothing."""


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

    raise ClusterNotReady("Master not ready")


def get_status(url):
    """Reads the session status, or returns None if this one request was too slow.

    A slow reply is not a failure: the next poll tries again. A refused
    connection or an HTTP error means the master is broken and is raised.
    """
    try:
        return get_json(url)
    except TimeoutError:            # urlopen read timeout (socket.timeout)
        return None
    except urllib.error.URLError as e:
        if isinstance(e.reason, TimeoutError):  # connect timeout
            return None
        raise


def wait_for_done(search_id, url_base, timeout_s):
    deadline = time.monotonic() + timeout_s
    url = f"{url_base.rstrip('/')}/{search_id}"

    while time.monotonic() < deadline:
        answer = get_status(url)
        if answer is not None and answer["state"] == "Done":
            return answer
        time.sleep(0.05)

    raise SessionHung(f"session not Done after {timeout_s:.0f} s")


# ---------------------------------------------------------------------------
# Test data
# ---------------------------------------------------------------------------

def dataset_files(dataset):
    """All regular files under the dataset directory, like the master's recursive scan."""
    return [f for f in (DATA_ROOT / dataset).rglob("*") if f.is_file()]


def ensure_dataset(dataset, file_count, lines_per_file, timeout_s):
    """Generates the dataset with Crazy Printer unless it already exists.

    An existing directory is used as it is. The file count and size go into
    every CSV row, so a smaller, interrupted dataset is visible in the results.
    """
    data_dir = DATA_ROOT / dataset
    if data_dir.is_dir() and dataset_files(dataset):
        print(f"Using existing data in {data_dir} ({len(dataset_files(dataset))} files)")
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
        if status["status"] != "running":
            raise SystemExit(f"Data generation failed: status {status['status']!r}")
        time.sleep(1)

    raise TimeoutError("Data generation did not finish")


def reference_count(dataset):
    """Number of lines containing KEYWORD, counted by grep (the reference).

    grep -r reads every file separately, so files are never joined across a
    missing trailing newline, and it covers subdirectories like the master.
    """
    res = subprocess.run(["grep", "-r", "-c", "--", KEYWORD, str(DATA_ROOT / dataset)],
                         capture_output=True, text=True)
    if res.returncode > 1:          # 1 only means "no match in any file"
        raise SystemExit(f"grep failed: {res.stderr}")
    # One "path:count" line per file; rsplit keeps paths that contain ':'
    return sum(int(line.rsplit(":", 1)[1]) for line in res.stdout.splitlines())


def dataset_size_mb(dataset):
    return sum(f.stat().st_size for f in dataset_files(dataset)) / 1024**2


# ---------------------------------------------------------------------------
# One scenario
# ---------------------------------------------------------------------------

def wait_for_workers(count, timeout_s):
    """Waits until the master has received Hello from `count` workers."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        logs = run(["docker", "compose", "logs", "master-node"])
        if logs.count("Hello message") >= count:
            return
        time.sleep(0.2)

    raise ClusterNotReady(f"Not all {count} workers connected")


def worker_containers():
    """Names of the running worker containers, sorted (loggrid-worker-1, -2, ...)."""
    out = run(["docker", "compose", "ps", "--format", "{{.Name}}", "worker"])
    return sorted(out.split(), key=lambda name: int(name.rsplit("-", 1)[1]))


def run_scenario(dataset, kill_at, expected, timeout_s, build, workers, kill_count):
    """Runs one search on a fresh cluster.

    kill_at: fraction of chunks done (0..1) at which `kill_count` workers get
             SIGKILL, or None for a run without a failure.
    """
    result = {"kill_at": kill_at, "expected": expected, "killed_at_chunk": None,
              "workers": workers, "kill_count": kill_count if kill_at is not None else 0}

    started = False                 # errors before the search are ERROR, after it FAIL
    try:
        run(["docker", "compose", "up", "-d", "--scale", f"worker={workers}"]
            + (["--build"] if build else []))
        wait_for_master(f"{MASTER}/api/status/0", 30)
        wait_for_workers(workers, 30)

        start = time.monotonic()
        search_id = post_json(f"{MASTER}/api/search",
                              {"path": dataset, "keyword": KEYWORD,
                               "outputDir": "bench_kill_worker"})["searchId"]
        started = True

        if kill_at is not None:
            deadline = start + timeout_s
            while time.monotonic() < deadline:
                status = get_status(f"{MASTER}/api/status/{search_id}")
                if status is None:
                    continue        # slow reply, poll again
                done, total = status["chunksDone"], status["chunksTotal"]
                if status["state"] == "Done":
                    break  # finished before the kill point: the run is not valid
                if total and done >= kill_at * total:
                    victims = worker_containers()[:kill_count]
                    run(["docker", "kill"] + victims)  # SIGKILL by default
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

    except ClusterNotReady as e:
        result["error"] = f"setup: {e}"
        result["setup_error"] = True
    except Exception as e:
        # Anything after the search started (hung session, master crash or
        # refused connection, bad status JSON) is a FAIL of the system under test.
        # It is recorded as a row instead of stopping the series.
        result["error"] = f"{type(e).__name__}: {e}"
        result["setup_error"] = not started
    finally:
        run(["docker", "compose", "down", "--remove-orphans"])

    result["ok"] = verdict(result)
    return result


def verdict(r):
    if r.get("setup_error"):
        return f"ERROR ({r['error']})"
    if "error" in r:
        return f"FAIL ({r['error']})"
    if r["found"] != r["expected"]:
        return "FAIL (wrong count)"
    if r["kill_at"] is not None and r["killed_at_chunk"] is None:
        return "INVALID (finished before kill)"
    if r["kill_at"] is not None and r["reclaimed"] < r["kill_count"]:
        # The master logs one line per lost worker that had tasks in flight
        return (f"INVALID (only {r['reclaimed']} of {r['kill_count']} "
                "killed workers had tasks)")
    return "PASS"


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------

def markdown_table(results):
    lines = [
        "| Workery | Zabite | Moment zabicia | Chunki przy zabiciu | Wykryte awarie | Oczekiwane | Znalezione | Czas sesji [s] | Wynik |",
        "|---|---|---|---|---|---|---|---|---|",
    ]
    for r in results:
        kill = "brak" if r["kill_at"] is None else f"po {r['kill_at']:.0%} chunków"
        time_s = f"{r['time_s']:.1f}" if "time_s" in r else "-"
        lines.append(
            f"| {r['workers']} | {r['kill_count']} | {kill} | {r['killed_at_chunk'] or '-'} "
            f"| {r.get('reclaimed', '-')} "
            f"| {r['expected']} | {r.get('found', '-')} | {time_s} | {r['ok']} |"
        )
    return "\n".join(lines)


CSV_FIELDS = [
    "timestamp", "commit", "dataset", "files", "size_mb", "workers", "worker_cpus",
    "kill_count", "kill_at", "killed_at_chunk",
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


def environment(context, results):
    cpus = (f"limit {context['worker_cpus']:g} CPU na workera"
            if context["worker_cpus"] else "bez limitu CPU")
    # Chunk count comes from the master, so the line stays true if CHUNK_SIZE changes
    chunks = next((r["chunks"].split("/")[1] for r in results if "chunks" in r), "?")
    return (f"Commit `{context['commit']}`, {context['files']} plików, "
            f"{context['size_mb']} MB, {chunks} chunków, docker compose na jednej maszynie, "
            f"{cpus}, wzorzec `{KEYWORD}`, referencja `grep -r -c`.")


def main():
    parser = argparse.ArgumentParser(description="LogGrid kill -9 recovery check")
    parser.add_argument("--dataset", default="killtest",
                        help="directory under /data/logs (default: killtest)")
    parser.add_argument("--files", type=int, default=40)
    parser.add_argument("--lines", type=int, default=400_000,
                        help="lines per generated file (~95 B per line)")
    parser.add_argument("--kill-at", type=float, nargs="*", default=[0.25, 0.5, 0.75],
                        help="fractions of chunks done at which --kill-count workers are killed")
    parser.add_argument("--no-baseline", action="store_true",
                        help="skip the run without a failure")
    parser.add_argument("--workers", type=int, nargs="+", default=[2],
                        help="numbers of workers to test, e.g. --workers 1 2 4 (default: 2)")
    parser.add_argument("--kill-count", type=int, default=1,
                        help="how many workers to kill at each kill point (default: 1)")
    parser.add_argument("--worker-cpus", type=float, default=0,
                        help="CPU limit per worker container, 0 = no limit (default: 0)")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--csv", help="CSV file for the results "
                             "(default: a new timestamped file in bench/results)")
    args = parser.parse_args()

    # Killing every worker leaves nobody to finish the session
    too_small = [w for w in args.workers if w <= args.kill_count]
    if args.kill_at and too_small:
        parser.error(f"--kill-count {args.kill_count} needs more workers than {too_small}")

    # Read by docker-compose.yml for every `docker compose up`
    os.environ["LOGGRID_WORKER_CPUS"] = f"{args.worker_cpus:g}"

    ensure_dataset(args.dataset, args.files, args.lines, timeout_s=600)
    expected = reference_count(args.dataset)
    print(f"Reference: {expected} lines with {KEYWORD}")

    # Same for every run in the series; stored in each CSV row so the file describes itself
    context = {
        "commit": run(["git", "rev-parse", "--short", "HEAD"]).strip(),
        "dataset": args.dataset,
        "files": len(dataset_files(args.dataset)),
        "size_mb": round(dataset_size_mb(args.dataset)),
        "worker_cpus": args.worker_cpus,
    }

    csv_path = args.csv or (Path(__file__).parent / "results" /
                            f"kill_worker_{datetime.datetime.now():%Y%m%d-%H%M%S}.csv")
    print(f"Results: {csv_path}")

    scenarios = ([] if args.no_baseline else [None]) + args.kill_at
    results = []
    build = True
    for _ in range(args.repeat):
        for workers, kill_at in [(w, k) for w in args.workers for k in scenarios]:
            r = run_scenario(args.dataset, kill_at, expected, args.timeout, build,
                             workers, args.kill_count)
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
    print(environment(context, results))

    if any(r["ok"].startswith(("FAIL", "ERROR")) for r in results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()