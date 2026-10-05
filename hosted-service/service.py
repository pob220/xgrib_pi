#!/usr/bin/env python3
"""Scheduled global GFS field cache and bounded, cache-only regional GRIB API.

Only the ingester accesses AWS. API requests never trigger upstream downloads.
Python standard library only; ecCodes performs GRIB decoding and cropping.
"""
import argparse
import collections
import concurrent.futures
import contextlib
import datetime as dt
import fcntl
import hashlib
import hmac
import http.server
import ipaddress
import json
import logging
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import time
import urllib.error
import urllib.request
import uuid

LOG = logging.getLogger("xgrib")
UTC = dt.timezone.utc
HOURS = tuple(range(121)) + tuple(range(123, 385, 3))
FIELDS = {
    "weather": {"10u": "UGRD:10 m above ground", "10v": "VGRD:10 m above ground",
                "prmsl": "PRMSL:mean sea level", "2t": "TMP:2 m above ground"},
    "waves": {"swh": "HTSGW:surface", "perpw": "PERPW:surface", "dirpw": "DIRPW:surface"},
}
BASE = "https://noaa-gfs-bdp-pds.s3.amazonaws.com"
VERSION = "0.2.0"


class Problem(Exception):
    def __init__(self, message, status=400):
        super().__init__(message)
        self.status = status


def cycle_time(cycle):
    if not isinstance(cycle, str) or not re.fullmatch(r"[0-9]{10}", cycle):
        raise Problem("cycle must be YYYYMMDDHH in UTC")
    try:
        value = dt.datetime.strptime(cycle, "%Y%m%d%H").replace(tzinfo=UTC)
    except ValueError:
        raise Problem("invalid cycle date") from None
    if value.hour not in (0, 6, 12, 18):
        raise Problem("GFS cycle must be 00, 06, 12 or 18 UTC")
    return value


def validate_request(data):
    if not isinstance(data, dict) or set(data) - {"cycle", "hours", "bbox", "fields", "stride"}:
        raise Problem("unrecognised request keys")
    cycle_time(data.get("cycle"))
    hours = data.get("hours")
    if (not isinstance(hours, list) or not hours or len(hours) > len(HOURS) or
            any(type(h) is not int or h not in HOURS for h in hours)):
        raise Problem("hours must be native GFS forecast hours, 0..120 hourly then 123..384 every 3h")
    bbox = data.get("bbox")
    if not isinstance(bbox, dict) or set(bbox) != {"west", "south", "east", "north"}:
        raise Problem("bbox needs west, south, east and north")
    if any(type(v) not in (int, float) or not math.isfinite(v) for v in bbox.values()):
        raise Problem("bbox coordinates must be finite numbers")
    w, s, e, n = (bbox[k] for k in ("west", "south", "east", "north"))
    if not (-180 <= w <= 180 and -180 <= e <= 180 and -90 <= s < n <= 90 and w != e):
        raise Problem("invalid bounding box")
    span = e - w if e > w else e + 360 - w
    if span <= 0:
        raise Problem("invalid longitude span")
    stride = data.get("stride", 1)
    if type(stride) is not int or not 1 <= stride <= 8:
        raise Problem("stride must be 1..8 native grid points")
    fields = data.get("fields")
    if not isinstance(fields, dict) or not fields or set(fields) - set(FIELDS):
        raise Problem("fields must select weather and/or waves")
    for product, names in fields.items():
        if (not isinstance(names, list) or not names or len(names) > len(FIELDS[product]) or
                any(not isinstance(name, str) or name not in FIELDS[product] for name in names)):
            raise Problem("unsupported fields for " + product)
    # Conservative cost bound, including every field and time. Reject work
    # before admitting a job, even if it would subsequently hit the cache.
    points = math.ceil(span / (0.25 * stride)) * (math.ceil((n - s) / (0.25 * stride)) + 1)
    if points < 4 or points * len(set(hours)) * sum(len(set(v)) for v in fields.values()) > 10000000:
        raise Problem("region/time/field combination exceeds the 10-million-value job limit", 413)
    return {"cycle": data["cycle"], "hours": sorted(set(hours)),
            "bbox": {k: float(bbox[k]) for k in ("west", "south", "east", "north")},
            "fields": {k: sorted(set(fields[k])) for k in sorted(fields)}, "stride": stride}


def source_url(cycle, product, hour):
    cycle_time(cycle)
    if product not in FIELDS or type(hour) is not int or hour not in HOURS:
        raise Problem("invalid source selection")
    prefix = f"{BASE}/gfs.{cycle[:8]}/{cycle[8:]}/"
    if product == "weather":
        return prefix + f"atmos/gfs.t{cycle[8:]}z.pgrb2.0p25.f{hour:03d}"
    return prefix + f"wave/gridded/gfswave.t{cycle[8:]}z.global.0p25.f{hour:03d}.grib2"


def inventory_ranges(text, selected):
    records = []
    for line in text.splitlines():
        parts = line.split(":")
        if len(parts) < 6 or not parts[1].isdigit():
            raise Problem("malformed upstream inventory", 502)
        records.append((int(parts[1]), parts[3] + ":" + parts[4]))
    if not records or any(b[0] <= a[0] for a, b in zip(records, records[1:])):
        raise Problem("upstream inventory offsets are not strictly increasing", 502)
    found, ranges = [], []
    for i, (start, key) in enumerate(records):
        if key not in selected:
            continue
        if i + 1 == len(records):
            raise Problem("selected final inventory field has no bounded length", 502)
        found.append(key)
        end = records[i + 1][0] - 1
        if ranges and start == ranges[-1][1] + 1:
            ranges[-1] = (ranges[-1][0], end)
        else:
            ranges.append((start, end))
    if sorted(found) != sorted(selected):
        raise Problem("missing or duplicate upstream fields", 502)
    return ranges


def validate_grib_bytes(data):
    offset, count = 0, 0
    while offset < len(data):
        if data[offset:offset+4] != b"GRIB" or data[offset+7:offset+8] != b"\x02":
            raise Problem("upstream response is not GRIB2", 502)
        length = int.from_bytes(data[offset+8:offset+16], "big")
        if length < 20 or offset + length > len(data) or data[offset+length-4:offset+length] != b"7777":
            raise Problem("truncated or invalid GRIB2 message", 502)
        offset += length
        count += 1
    if not count:
        raise Problem("empty GRIB response", 502)
    return count


def fetch(url, start=None, end=None):
    headers = {"User-Agent": "xGRIB-hosted-cache/" + VERSION, "Accept-Encoding": "identity"}
    expected = None
    if start is not None:
        expected = end - start + 1
        if expected <= 0 or expected > 20000000:
            raise Problem("upstream byte range exceeds limit", 502)
        headers["Range"] = f"bytes={start}-{end}"
    for attempt in range(3):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=45) as response:
                if expected is not None:
                    if response.status != 206 or not re.fullmatch(
                            rf"bytes {start}-{end}/[0-9]+", response.headers.get("Content-Range", "")):
                        raise Problem("upstream ignored or changed requested byte range", 502)
                maximum = expected if expected is not None else 1000000
                data = response.read(maximum + 1)
                if len(data) > maximum or expected is not None and len(data) != expected:
                    raise Problem("upstream response length mismatch", 502)
                return data
        except urllib.error.HTTPError as exc:
            if exc.code in (403, 404):
                raise Problem("requested upstream forecast is not available", 503) from None
            if exc.code not in (429, 500, 502, 503, 504) or attempt == 2:
                raise Problem("upstream HTTP " + str(exc.code), 502) from None
        except (OSError, TimeoutError):
            if attempt == 2:
                raise Problem("upstream connection failed", 502) from None
        time.sleep(2 ** attempt)


def atomic_json(path, value):
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(value, sort_keys=True) + "\n")
    os.replace(tmp, path)


class Cache:
    def __init__(self, root, crop=None, quota=4500000000, result_quota=250000000):
        self.root = Path(root)
        self.crop = crop or os.environ.get("XGRIB_CROP", str(Path(__file__).with_name("grib-crop")))
        self.quota = quota
        self.result_quota = result_quota
        for name in ("runs", "locks", "results"):
            (self.root / name).mkdir(parents=True, exist_ok=True)

    @contextlib.contextmanager
    def lock(self, name, exclusive=False, blocking=True):
        with (self.root / "locks" / (name + ".lock")).open("a") as stream:
            operation = fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH
            fcntl.flock(stream, operation | (0 if blocking else fcntl.LOCK_NB))
            try:
                yield
            finally:
                fcntl.flock(stream, fcntl.LOCK_UN)

    def runs(self):
        return sorted(p.name for p in (self.root / "runs").iterdir()
                      if p.is_dir() and re.fullmatch(r"[0-9]{10}", p.name))

    def complete(self):
        return [c for c in self.runs() if (self.root / "runs" / c / "complete.json").is_file()]

    def source(self, cycle, product, hour):
        return self.root / "runs" / cycle / f"{product}-{hour:03d}.grib2"

    def bytes_used(self):
        return sum(p.stat().st_size for p in self.root.rglob("*") if p.is_file())

    def prune(self, incoming=None):
        keep = set(self.complete()[-2:])
        if incoming:
            keep.add(incoming)
        for cycle in self.runs():
            if cycle in keep:
                continue
            try:
                with self.lock(cycle, exclusive=True, blocking=False):
                    shutil.rmtree(self.root / "runs" / cycle)
                    LOG.info("retired cycle %s", cycle)
            except BlockingIOError:
                LOG.info("cycle %s is pinned by an active job", cycle)
        self.prune_results()

    def prune_results(self):
        # Called under a cross-process result-cache lock; pin readers with the
        # same lock. Results are disposable, forecasts are kept separately.
        with self.lock("results", exclusive=True):
            paths = sorted((self.root / "results").glob("*.grib2"), key=lambda p: p.stat().st_mtime)
            total = sum(p.stat().st_size for p in paths)
            for path in paths:
                if total <= self.result_quota:
                    break
                total -= path.stat().st_size
                path.unlink()
                path.with_suffix(".json").unlink(missing_ok=True)

    def reserve_download(self, owner, client, size, day=None):
        day = day or dt.datetime.now(UTC).strftime("%Y-%m-%d")
        global_limit = int(os.environ.get("XGRIB_DAILY_EGRESS_BYTES", "20000000000"))
        client_limit = int(os.environ.get("XGRIB_CLIENT_EGRESS_BYTES", "1000000000"))
        path = self.root / "egress-budget.json"
        keys = ("token:" + owner, "ip:" + hashlib.sha256(client.encode()).hexdigest())
        with self.lock("egress-budget", exclusive=True):
            budget = json.loads(path.read_text()) if path.is_file() else {}
            if budget.get("day", "") > day:
                raise Problem("download budget clock mismatch; retry later", 503)
            if budget.get("day") != day:
                budget = {"day": day, "total": 0, "clients": {}}
            clients = budget["clients"]
            if (budget["total"] + size > global_limit or
                    any(clients.get(k, 0) + size > client_limit for k in keys) or
                    len(clients) + sum(k not in clients for k in keys) > 4096):
                raise Problem("daily download budget reached; retry after 00:00 UTC", 429)
            # Reserve the whole response before streaming. Interrupted
            # downloads still count, preventing repeat/cancel budget evasion.
            budget["total"] += size
            for key in keys:
                clients[key] = clients.get(key, 0) + size
            atomic_json(path, budget)

    def download(self, cycle, product, hour):
        path = self.source(cycle, product, hour)
        if path.is_file():
            return
        url = source_url(cycle, product, hour)
        ranges = inventory_ranges(fetch(url + ".idx").decode("ascii"), FIELDS[product].values())
        data = b"".join(fetch(url, a, b) for a, b in ranges)
        if validate_grib_bytes(data) != len(FIELDS[product]):
            raise Problem("upstream GRIB field count mismatch", 502)
        if self.bytes_used() + len(data) > self.quota or shutil.disk_usage(self.root).free < len(data) + 200000000:
            raise Problem("cache disk budget reached", 507)
        path.parent.mkdir(exist_ok=True)
        tmp = path.with_suffix(".part")
        try:
            tmp.write_bytes(data)
            process = subprocess.run(["grib_get", "-p", "shortName,dataDate,dataTime,endStep,stepUnits,gridType", str(tmp)],
                                     capture_output=True, text=True, timeout=30)
            expected = {f"{name} {cycle[:8]} {int(cycle[8:])*100} {hour} 1 regular_ll" for name in FIELDS[product]}
            if process.returncode or set(process.stdout.splitlines()) != expected:
                raise Problem("downloaded forecast identity/fields/grid mismatch", 502)
            os.replace(tmp, path)
        finally:
            tmp.unlink(missing_ok=True)

    def ingest(self, cycle, hours=HOURS):
        cycle_time(cycle)
        if any(h not in HOURS for h in hours):
            raise Problem("invalid ingestion forecast hours")
        with self.lock("ingestion", exclusive=True, blocking=False):
            self.prune(incoming=cycle)
            if len(self.runs()) >= 3 and cycle not in self.runs():
                raise Problem("retained runs are pinned; ingestion deferred", 503)
            run = self.root / "runs" / cycle
            run.mkdir(exist_ok=True)
            # Check the furthest requested file in both components before
            # fetching the rest. An unfinished upstream cycle keeps both old
            # complete runs intact. Existing partial data remains reusable.
            for product in FIELDS:
                self.download(cycle, product, max(hours))
            for hour in hours:
                for product in FIELDS:
                    self.download(cycle, product, hour)
                if hour % 24 == 0:
                    LOG.info("cached cycle=%s forecast-hour=%d", cycle, hour)
            if set(hours) == set(HOURS):
                atomic_json(run / "complete.json", {"cycle": cycle, "hours": HOURS, "fields": FIELDS,
                            "source": BASE, "completed_at": dt.datetime.now(UTC).isoformat()})
                self.prune()
                LOG.info("complete cycle %s; retained %s", cycle, self.complete())

    def advance(self, cycle):
        """Publish available prefixes of the newest run, independently per product.

        Only the scheduled ingester calls this. Short forecasts must not wait
        for the upstream f384 file or for the other component to be published.
        """
        cycle_time(cycle)
        with self.lock("ingestion", exclusive=True, blocking=False):
            self.prune(incoming=cycle)
            if len(self.runs()) >= 3 and cycle not in self.runs():
                raise Problem("retained runs are pinned; ingestion deferred", 503)
            run = self.root / "runs" / cycle
            run.mkdir(exist_ok=True)
            for product in FIELDS:
                for hour in HOURS:
                    try:
                        self.download(cycle, product, hour)
                    except Problem as exc:
                        if exc.status not in (502, 503):
                            raise
                        LOG.info("cycle=%s product=%s waiting at forecast-hour=%d: %s", cycle, product, hour, exc)
                        break
                    if hour % 24 == 0:
                        LOG.info("available cycle=%s product=%s forecast-hour=%d", cycle, product, hour)
            missing = [1 for p in FIELDS for h in HOURS if not self.source(cycle, p, h).is_file()]
            if not missing:
                atomic_json(run / "complete.json", {"cycle": cycle, "hours": HOURS, "fields": FIELDS,
                            "source": BASE, "completed_at": dt.datetime.now(UTC).isoformat()})
                self.prune()
                LOG.info("complete cycle %s; retained %s", cycle, self.complete())

    def coverage(self, request):
        return [f"{p}:{h}" for p in request["fields"] for h in request["hours"]
                if not self.source(request["cycle"], p, h).is_file()]

    def available_coverage(self):
        result = {}
        for cycle in self.runs():
            with self.lock(cycle):
                if not (self.root / "runs" / cycle).is_dir():
                    continue
                result[cycle] = {product: [hour for hour in HOURS if self.source(cycle, product, hour).is_file()]
                                 for product in FIELDS}
        return result

    def result_id(self, request):
        return hashlib.sha256(json.dumps({"version": VERSION, "request": request}, sort_keys=True).encode()).hexdigest()

    def make_result(self, request, progress=lambda value: None):
        identity = self.result_id(request)
        output = self.root / "results" / (identity + ".grib2")
        metadata = output.with_suffix(".json")
        with self.lock(request["cycle"]):
            missing = self.coverage(request)
            if missing:
                raise Problem("matching forecast not cached: " + ", ".join(missing[:8]), 503)
            with self.lock("result-" + identity, exclusive=True):
                with self.lock("results"):
                    if output.is_file() and metadata.is_file():
                        os.utime(output, None)
                        return json.loads(metadata.read_text())
                temp = output.with_suffix(".part")
                chunk = output.with_suffix(".chunk")
                try:
                    with temp.open("wb") as stream:
                        total = len(request["fields"]) * len(request["hours"])
                        done = 0
                        for hour in request["hours"]:
                            for product, fields in request["fields"].items():
                                b = request["bbox"]
                                command = [self.crop, str(self.source(request["cycle"], product, hour)), str(chunk),
                                           *(str(b[k]) for k in ("west", "south", "east", "north")),
                                           str(request["stride"]), request["cycle"], str(hour), ",".join(fields)]
                                run = subprocess.run(command, capture_output=True, text=True, timeout=45)
                                if run.returncode:
                                    raise Problem("regional crop failed: " + run.stderr.strip()[:200], 422)
                                with chunk.open("rb") as source:
                                    shutil.copyfileobj(source, stream)
                                if stream.tell() > 64000000:
                                    raise Problem("regional result exceeds 64 MB limit", 413)
                                done += 1
                                progress(round(100 * done / total))
                    result = {"id": identity, "request": request, "bytes": temp.stat().st_size,
                              "sha256": hashlib.sha256(temp.read_bytes()).hexdigest(),
                              "source": "NOAA GFS via public AWS mirror", "service_version": VERSION,
                              "created_at": dt.datetime.now(UTC).isoformat(),
                              "result_url": "/v1/results/" + identity + ".grib2"}
                    with self.lock("results", exclusive=True):
                        if self.bytes_used() + temp.stat().st_size > self.quota:
                            raise Problem("cache disk budget reached", 507)
                        os.replace(temp, output)
                        atomic_json(metadata, result)
                    return result
                finally:
                    temp.unlink(missing_ok=True)
                    chunk.unlink(missing_ok=True)


class Admission:
    """Exact local budgets, independently of best-effort Cloudflare limits."""
    def __init__(self):
        self.lock = threading.Lock()
        self.events = collections.OrderedDict()

    def allow(self, client, expensive=False, now=None):
        now = time.monotonic() if now is None else now
        with self.lock:
            while self.events:
                first = next(iter(self.events.values()))
                last = max(first[0][-1] if first[0] else 0, first[1][-1] if first[1] else 0)
                if last >= now - 3600:
                    break
                self.events.popitem(last=False)
            if client not in self.events and len(self.events) >= 4096:
                return False
            reads, jobs = self.events.setdefault(client, (collections.deque(), collections.deque()))
            self.events.move_to_end(client)
            for events, span in ((reads, 60), (jobs, 3600)):
                while events and events[0] <= now - span:
                    events.popleft()
            # Allow paired weather/wave jobs and interactive area changes.
            if len(reads) >= 600 or expensive and (len(jobs) >= 120 or sum(t > now-10 for t in jobs) >= 6):
                return False
            reads.append(now)
            if expensive:
                jobs.append(now)
            return True


class App:
    def __init__(self, cache, token_file, workers=2):
        self.cache = cache
        # File contains one opaque token per line; never log headers/tokens.
        self.tokens = [v.strip() for v in Path(token_file).read_text().splitlines() if v.strip()]
        if not self.tokens:
            raise Problem("a nonempty token file is required")
        self.pool = concurrent.futures.ThreadPoolExecutor(max_workers=workers)
        self.slots = threading.BoundedSemaphore(8)
        self.lock = threading.Lock()
        self.jobs = {}
        self.active = {}
        self.admission = Admission()
        self.downloads = threading.BoundedSemaphore(2)

    def authorised(self, supplied):
        if len(supplied) > 512 or not supplied.isascii():
            return False
        return any(hmac.compare_digest(supplied, "Bearer " + token) for token in self.tokens)

    def submit(self, request, owner):
        result_id = self.cache.result_id(request)
        with self.lock:
            existing = self.active.get((result_id, owner))
            if existing:
                return dict(self.jobs[existing])
            # Bound retained status records as well as queued work.
            for key in list(self.jobs):
                if self.jobs[key]["state"] in ("complete", "failed") and time.monotonic() - self.jobs[key]["_time"] > 3600:
                    del self.jobs[key]
            if len(self.jobs) >= 1024 or not self.slots.acquire(blocking=False):
                raise Problem("regional job queue is full; retry later", 503)
            identity = uuid.uuid4().hex
            job = {"id": identity, "state": "queued", "progress": 0,
                   "status_url": "/v1/jobs/" + identity, "_owner": owner, "_time": time.monotonic()}
            self.jobs[identity] = job
            self.active[(result_id, owner)] = identity
        self.pool.submit(self.work, identity, result_id, request, owner)
        return dict(job)

    def work(self, identity, result_id, request, owner):
        def update(**values):
            with self.lock:
                self.jobs[identity].update(values)
        try:
            update(state="running")
            result = self.cache.make_result(request, lambda value: update(progress=value))
            update(state="complete", progress=100, result=result)
        except Exception as exc:
            LOG.warning("job %s failed: %s", identity, exc)
            update(state="failed", error=str(exc), error_status=getattr(exc, "status", 500))
        finally:
            with self.lock:
                self.active.pop((result_id, owner), None)
            self.slots.release()
            self.cache.prune_results()


class Server(http.server.ThreadingHTTPServer):
    # Bound accepted request threads, including slow header/body clients.
    daemon_threads = True
    request_queue_size = 16

    def __init__(self, address, app):
        self.app = app
        self.connections = threading.BoundedSemaphore(24)
        super().__init__(address, Handler)

    def process_request(self, request, client_address):
        if not self.connections.acquire(blocking=False):
            request.close()
            return
        try:
            super().process_request(request, client_address)
        except Exception:
            self.connections.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.connections.release()


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "xGRIB/" + VERSION

    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def log_message(self, *_):
        pass  # No query strings, credentials or user locations in access logs.

    def json(self, value, status=200):
        data = json.dumps({k: v for k, v in value.items() if not k.startswith("_")}, allow_nan=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        if status in (429, 503):
            self.send_header("Retry-After", "60")
        self.end_headers()
        self.wfile.write(data)

    def client(self):
        value = self.client_address[0]
        if os.environ.get("XGRIB_TRUST_PROXY") == "1":
            value = self.headers.get("CF-Connecting-IP", value)
        try:
            return str(ipaddress.ip_address(value))
        except ValueError:
            raise Problem("invalid client address") from None

    def authenticate(self, expensive=False):
        app = self.server.app
        supplied = self.headers.get("Authorization", "")
        public_client = self.headers.get("X-xGRIB-Client-ID", "")
        if app.authorised(supplied):
            owner = hashlib.sha256(supplied.encode()).hexdigest()
        elif (os.environ.get("XGRIB_PUBLIC_ACCESS") == "1" and not supplied and
              re.fullmatch(r"[0-9a-f]{32}", public_client)):
            # A random job-session ID isolates status records. Independent
            # IP/global budgets prevent ID rotation bypassing resource limits.
            owner = "public:" + hashlib.sha256(public_client.encode()).hexdigest()
        else:
            raise Problem("authentication required", 401)
        # Both identity and IP are budgeted; neither can bypass the other.
        if not app.admission.allow("token:" + owner, expensive) or not app.admission.allow("ip:" + self.client(), expensive):
            raise Problem("request budget reached; retry later", 429)
        return owner

    def do_POST(self):
        try:
            owner = self.authenticate(expensive=True)
            if self.path != "/v1/subsets":
                raise Problem("unknown endpoint", 404)
            if self.headers.get("Transfer-Encoding"):
                raise Problem("chunked request bodies are not supported")
            try:
                length = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                raise Problem("invalid Content-Length") from None
            if not 0 < length <= 8192:
                raise Problem("JSON request body must be 1..8192 bytes", 413)
            if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
                raise Problem("Content-Type must be application/json", 415)
            data = self.rfile.read(length)
            if len(data) != length:
                raise Problem("incomplete request body")
            try:
                request = validate_request(json.loads(data))
            except (ValueError, UnicodeError):
                raise Problem("invalid JSON") from None
            if self.server.app.cache.coverage(request):
                raise Problem("exact requested forecast is not cached yet; no alternate cycle was substituted", 503)
            self.json(self.server.app.submit(request, owner), 202)
        except Problem as exc:
            self.json({"error": str(exc)}, exc.status)

    def do_GET(self):
        try:
            app = self.server.app
            if self.path == "/healthz":
                self.json({"status": "ok", "version": VERSION, "complete_cycles": app.cache.complete()})
                return
            owner = self.authenticate()
            if self.path == "/v1/capabilities":
                self.json({"version": VERSION, "hours": HOURS, "fields": FIELDS,
                           "complete_cycles": app.cache.complete(), "forecast_days": 16,
                           "coverage": app.cache.available_coverage(),
                           "native_grid_degrees": 0.25, "max_job_values": 10000000})
            elif re.fullmatch(r"/v1/jobs/[0-9a-f]{32}", self.path):
                with app.lock:
                    job = dict(app.jobs.get(self.path.rsplit("/", 1)[1], {}))
                if not job or job["_owner"] != owner:
                    raise Problem("unknown or expired job", 404)
                self.json(job)
            elif re.fullmatch(r"/v1/results/[0-9a-f]{64}\.grib2", self.path):
                if not app.downloads.acquire(blocking=False):
                    raise Problem("download workers are busy; retry later", 503)
                try:
                    with app.cache.lock("results"):
                        path = app.cache.root / "results" / self.path.rsplit("/", 1)[1]
                        if not path.is_file():
                            raise Problem("result expired; resubmit the request", 404)
                        size = path.stat().st_size
                        app.cache.reserve_download(owner, self.client(), size)
                        source = path.open("rb")
                    # The open descriptor pins the inode even if the cache
                    # evicts its name. Slow downloads do not hold a global
                    # cache lock or block other crops from publishing.
                    with source:
                        self.send_response(200)
                        self.send_header("Content-Type", "application/octet-stream")
                        self.send_header("Content-Length", str(size))
                        self.send_header("Cache-Control", "private, max-age=3600")
                        self.end_headers()
                        shutil.copyfileobj(source, self.wfile)
                finally:
                    app.downloads.release()
            else:
                raise Problem("unknown endpoint", 404)
        except Problem as exc:
            self.json({"error": str(exc)}, exc.status)


def watch(cache):
    while True:
        try:
            now = dt.datetime.now(UTC)
            latest = now.replace(hour=now.hour // 6 * 6, minute=0, second=0, microsecond=0)
            complete = cache.complete()
            if len(complete) >= 2:
                cycle = latest.strftime("%Y%m%d%H")
                if cycle not in complete:
                    cache.advance(cycle)
                time.sleep(900)
                continue
            # Bootstrap the two most recent available complete cycles, then
            # only ingest newer ones. Every partial cycle is bounded by prune.
            candidates = [latest - dt.timedelta(hours=6*i) for i in range(4)]
            for value in candidates:
                cycle = value.strftime("%Y%m%d%H")
                if cycle in complete or len(complete) >= 2 and cycle < complete[-1]:
                    continue
                try:
                    cache.ingest(cycle)
                    complete = cache.complete()
                    if len(complete) >= 2:
                        break
                except Problem as exc:
                    LOG.info("cycle %s deferred: %s", cycle, exc)
        except Exception:
            LOG.exception("ingestion pass failed")
        time.sleep(900)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("serve", "ingest", "watch"))
    parser.add_argument("--cache", default=os.environ.get("XGRIB_CACHE", "./cache"))
    parser.add_argument("--cycle")
    parser.add_argument("--hours", help="testing only: comma-separated forecast hours; does not mark a run complete")
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18743)
    parser.add_argument("--token-file", default=os.environ.get("XGRIB_TOKEN_FILE", "/run/xgrib/tokens"))
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    cache = Cache(args.cache)
    if args.mode == "serve":
        Server((args.bind, args.port), App(cache, args.token_file)).serve_forever()
    elif args.mode == "watch":
        watch(cache)
    else:
        cache.ingest(args.cycle, tuple(int(v) for v in args.hours.split(",")) if args.hours else HOURS)


if __name__ == "__main__":
    main()
