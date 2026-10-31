#!/usr/bin/env python3
"""Check the deployed routing contract and warm cache before publishing xGRIB."""
import argparse
import hashlib
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

REQUIRED_FIELDS = {"weather": {"10u", "10v", "prmsl", "2t", "gust"},
                   "waves": {"swh", "perpw", "dirpw"}}


class DeploymentError(Exception):
    pass


def ready_cycle(capabilities, through_hour=96):
    if not 0 <= through_hour <= 384 or through_hour > 120 and through_hour % 3:
        raise DeploymentError("Choose an available native forecast hour: 0..120, then 123..384 every three hours")
    fields = capabilities.get("fields", {})
    for product, required in REQUIRED_FIELDS.items():
        missing = required - set(fields.get(product, {}))
        if missing:
            raise DeploymentError(
                "Hosted service %s lacks required %s fields: %s. "
                "Deploy service 0.2.2 or later and warm its five-field weather cache."
                % (capabilities.get("version", "unknown"), product,
                   ", ".join(sorted(missing))))
    hours = set(range(min(through_hour, 120) + 1))
    hours.update(range(123, through_hour + 1, 3))
    coverage = capabilities.get("coverage", {})
    for cycle in sorted(coverage, reverse=True):
        if not re.fullmatch(r"[0-9]{10}", cycle):
            continue
        if all(hours <= set(coverage[cycle].get(product, []))
               for product in REQUIRED_FIELDS):
            return cycle
    raise DeploymentError(
        "No same-cycle weather and wave cache is warm through forecast hour %d. "
        "Keep the previous deployment until ingestion and the readiness check pass."
        % through_hour)


def validate_result(job, request, data):
    result = job.get("result", {})
    if (result.get("request") != request or result.get("bytes") != len(data)
            or result.get("sha256") != hashlib.sha256(data).hexdigest()):
        raise DeploymentError("Regional result checksum, length or request does not match")
    offset = count = 0
    while offset < len(data):
        if len(data) - offset < 20 or data[offset:offset + 4] != b"GRIB" or data[offset + 7] != 2:
            raise DeploymentError("Regional result is not a clean GRIB2 stream")
        size = int.from_bytes(data[offset + 8:offset + 16], "big")
        if size < 20 or size > len(data) - offset or data[offset + size - 4:offset + size] != b"7777":
            raise DeploymentError("Regional result is truncated")
        offset += size
        count += 1
    if count != sum(len(fields) for fields in request["fields"].values()):
        raise DeploymentError("Regional result did not retain all eight requested fields")


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, newurl):
        return None


class Client:
    def __init__(self, url):
        self.url = url.rstrip("/")
        parsed = urllib.parse.urlsplit(self.url)
        if (parsed.scheme != "https" and
                not (parsed.scheme == "http" and parsed.hostname in ("127.0.0.1", "localhost", "::1"))):
            raise DeploymentError("Use HTTPS, or HTTP on the local server loopback address")
        self.headers = {"X-xGRIB-Client-ID": uuid.uuid4().hex,
                        "User-Agent": "xGRIBDeploymentCheck/0.4.2",
                        "Content-Type": "application/json"}
        self.opener = urllib.request.build_opener(NoRedirect())

    def get(self, path, body=None, binary=False):
        request = urllib.request.Request(self.url + path,
                                         data=None if body is None else json.dumps(body).encode(),
                                         headers=self.headers)
        try:
            with self.opener.open(request, timeout=30) as response:
                data = response.read(64 * 1024 * 1024 + 1)
        except urllib.error.HTTPError as error:
            raise DeploymentError("Hosted readiness request failed with HTTP %d" % error.code) from None
        except urllib.error.URLError as error:
            raise DeploymentError("Hosted readiness connection failed: %s" % error.reason) from None
        if len(data) > 64 * 1024 * 1024:
            raise DeploymentError("Hosted readiness result exceeds its size limit")
        if binary:
            return data
        try:
            value = json.loads(data)
            if not isinstance(value, dict):
                raise ValueError()
            return value
        except ValueError:
            raise DeploymentError("Hosted readiness response is not a JSON object") from None

    def check(self, through_hour=96, crop=False):
        capabilities = self.get("/v1/capabilities")
        cycle = ready_cycle(capabilities, through_hour)
        if crop:
            request = {"cycle": cycle, "hours": [0],
                       "fields": {product: sorted(names) for product, names in REQUIRED_FIELDS.items()},
                       "bbox": {"west": -4.5, "south": 53.0, "east": -4.0, "north": 53.5},
                       "stride": 1}
            job = self.get("/v1/subsets", request)
            path = job.get("status_url", "")
            if not re.fullmatch(r"/v1/jobs/[0-9a-f]{32}", path):
                raise DeploymentError("Hosted service returned an invalid job path")
            deadline = time.monotonic() + 120
            while job.get("state") in ("queued", "running"):
                if time.monotonic() >= deadline:
                    raise DeploymentError("Hosted regional check timed out")
                time.sleep(1)
                job = self.get(path)
            if job.get("state") != "complete":
                raise DeploymentError("Hosted regional check failed")
            result_path = job.get("result", {}).get("result_url", "")
            if not re.fullmatch(r"/v1/results/[0-9a-f]{64}\.grib2", result_path):
                raise DeploymentError("Hosted service returned an invalid result path")
            validate_result(job, request, self.get(result_path, binary=True))
        return {"version": capabilities.get("version"), "cycle": cycle,
                "through_hour": through_hour, "regional_crop_checked": crop}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:18743")
    parser.add_argument("--through-hour", type=int, default=96, choices=range(385), metavar="0..384")
    parser.add_argument("--check-crop", action="store_true")
    args = parser.parse_args()
    try:
        print(json.dumps(Client(args.url).check(args.through_hour, args.check_crop)))
    except (DeploymentError, TypeError, KeyError) as error:
        print("Hosted GFS deployment is not ready: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
