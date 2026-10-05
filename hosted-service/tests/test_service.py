import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import urllib.error
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import service
from eccodes_native import Handle, decoded

REQUEST = {"cycle": "2026100412", "hours": [0],
           "bbox": {"west": -90, "south": -60, "east": 90, "north": 60},
           "fields": {"weather": ["10u"]}}


def fixture(path, cycle="2026100412", hour=0, northward=False):
    with Handle() as h:
        for key, value in {"Ni": 8, "Nj": 5, "latitudeOfFirstGridPointInDegrees": -60.0 if northward else 60.0,
                           "latitudeOfLastGridPointInDegrees": 60.0 if northward else -60.0,
                           "longitudeOfFirstGridPointInDegrees": 0.0,
                           "longitudeOfLastGridPointInDegrees": 315.0,
                           "iDirectionIncrementInDegrees": 45.0, "jDirectionIncrementInDegrees": 30.0,
                           "jScansPositively": int(northward), "dataDate": int(cycle[:8]),
                           "dataTime": int(cycle[8:])*100, "step": hour,
                           "typeOfLevel": "heightAboveGround", "level": 10, "shortName": "10u",
                           "packingType": "grid_simple", "bitsPerValue": 24, "bitmapPresent": 1}.items():
            h.set(key, value)
        values = [i + 0.125 for i in range(40)]
        values[9] = h.number("missingValue")
        h.values(values)
        Path(path).write_bytes(h.data())


class Validation(unittest.TestCase):
    def test_valid_request_is_canonical(self):
        request = copy.deepcopy(REQUEST)
        request["hours"] = [3, 0, 3]
        self.assertEqual(service.validate_request(request)["hours"], [0, 3])

    def test_forecast_calendar(self):
        self.assertEqual(len(service.HOURS), 209)
        for cycle in ("2026023012", "2026100401", "../../2026", None):
            with self.assertRaises(service.Problem):
                service.cycle_time(cycle)

    def test_invalid_forecast_hours(self):
        for hours in ([121], [122], [385], [True], [0.0], [], "0"):
            r = copy.deepcopy(REQUEST); r["hours"] = hours
            with self.assertRaises(service.Problem): service.validate_request(r)

    def test_invalid_coordinates_and_fields(self):
        for west in (float("nan"), float("inf"), True, -181):
            r = copy.deepcopy(REQUEST); r["bbox"]["west"] = west
            with self.assertRaises(service.Problem): service.validate_request(r)
        for field in ("../../passwd", [], 1):
            r = copy.deepcopy(REQUEST); r["fields"]["weather"] = [field]
            with self.assertRaises(service.Problem): service.validate_request(r)

    def test_cost_limit(self):
        r = copy.deepcopy(REQUEST); r["hours"] = list(service.HOURS)
        r["bbox"] = {"west": -180, "east": 180, "south": -90, "north": 90}
        with self.assertRaises(service.Problem) as error: service.validate_request(r)
        self.assertEqual(error.exception.status, 413)

    def test_inventory_coalescing_and_missing(self):
        idx = "1:0:d=x:UGRD:10 m above ground:anl:\n2:24:d=x:VGRD:10 m above ground:anl:\n3:48:d=x:TMP:2 m above ground:anl:\n"
        self.assertEqual(service.inventory_ranges(idx, ["UGRD:10 m above ground", "VGRD:10 m above ground"]), [(0, 47)])
        for text, selected in ((idx, ["FOO:surface"]), (idx, ["TMP:2 m above ground"]),
                               ("1:0:d=x:UGRD:10 m above ground:anl:\n2:0:d=x:X:Y:anl:\n", ["UGRD:10 m above ground"])):
            with self.assertRaises(service.Problem): service.inventory_ranges(text, selected)

    def test_non_grib_rejected(self):
        for data in (b"", b"<html>Over Rate Limit</html>", b"GRIB\0\0\0\2" + (999).to_bytes(8, "big")):
            with self.assertRaises(service.Problem): service.validate_grib_bytes(data)

    def test_admission_limits_and_recovery(self):
        a = service.Admission()
        for i in range(6): self.assertTrue(a.allow("a", True, now=10000+i*.1))
        self.assertFalse(a.allow("a", True, now=10001))
        for i in range(1, 115): self.assertTrue(a.allow("a", True, now=10000+i*11))
        self.assertFalse(a.allow("a", True, now=12000))
        self.assertFalse(a.allow("a", True, now=13000))
        self.assertTrue(a.allow("a", True, now=14000))
        for i in range(600): self.assertTrue(a.allow("b", now=15000))
        self.assertFalse(a.allow("b", now=15000))
        self.assertTrue(a.allow("b", now=15061))


class CacheTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.cache = service.Cache(self.directory.name)

    def test_two_complete_runs_and_one_incoming(self):
        for cycle in ("2026100400", "2026100406", "2026100412"):
            run = self.cache.root / "runs" / cycle; run.mkdir()
            (run / "complete.json").write_text("{}")
        self.cache.prune("2026100418")
        self.assertEqual(self.cache.runs(), ["2026100406", "2026100412"])
        incoming = self.cache.root / "runs" / "2026100418"; incoming.mkdir()
        self.cache.prune("2026100418")
        self.assertEqual(len(self.cache.runs()), 3)
        (incoming / "complete.json").write_text("{}")
        self.cache.prune()
        self.assertEqual(self.cache.complete(), ["2026100412", "2026100418"])

    def test_active_cycle_cannot_be_evicted(self):
        for cycle in ("2026100400", "2026100406", "2026100412"):
            p = self.cache.root / "runs" / cycle; p.mkdir(); (p / "complete.json").write_text("{}")
        with self.cache.lock("2026100400"):
            self.cache.prune()
            self.assertTrue((self.cache.root / "runs" / "2026100400").exists())
        self.cache.prune()
        self.assertFalse((self.cache.root / "runs" / "2026100400").exists())

    def test_result_cache_is_bounded(self):
        self.cache.result_quota = 5
        for i in range(3):
            p = self.cache.root / "results" / (str(i) + ".grib2")
            p.write_bytes(b"1234"); p.with_suffix(".json").write_text("{}")
        self.cache.prune_results()
        self.assertEqual(len(list((self.cache.root / "results").glob("*.grib2"))), 1)

    def test_missing_exact_cycle_never_downloads_or_substitutes(self):
        request = service.validate_request(REQUEST)
        with self.assertRaises(service.Problem) as error: self.cache.make_result(request)
        self.assertEqual(error.exception.status, 503)
        self.assertEqual(self.cache.runs(), [])

    def test_download_budget_survives_restart_and_resets_daily(self):
        with patch.dict("os.environ", {"XGRIB_DAILY_EGRESS_BYTES": "30", "XGRIB_CLIENT_EGRESS_BYTES": "20"}):
            self.cache.reserve_download("a", "192.0.2.1", 12, "2026-10-04")
            restored = service.Cache(self.directory.name)
            with self.assertRaises(service.Problem): restored.reserve_download("a", "192.0.2.2", 9, "2026-10-04")
            with self.assertRaises(service.Problem): restored.reserve_download("b", "192.0.2.1", 9, "2026-10-04")
            restored.reserve_download("b", "192.0.2.2", 12, "2026-10-04")
            with self.assertRaises(service.Problem): restored.reserve_download("c", "192.0.2.3", 7, "2026-10-04")
            restored.reserve_download("a", "192.0.2.1", 12, "2026-10-05")

    def test_partial_cycle_serves_weather_without_waiting_for_full_horizon_or_waves(self):
        for cycle in ("2026100400", "2026100406"):
            run = self.cache.root / "runs" / cycle; run.mkdir(); (run / "complete.json").write_text("{}")
        def download(cycle, product, hour):
            if product == "waves" or hour > 3:
                raise service.Problem("not yet published", 503)
            path = self.cache.source(cycle, product, hour)
            path.parent.mkdir(exist_ok=True); path.write_bytes(b"fixture")
        with patch.object(self.cache, "download", side_effect=download):
            self.cache.advance("2026100412")
        self.assertEqual(self.cache.complete(), ["2026100400", "2026100406"])
        self.assertEqual(len(self.cache.runs()), 3)
        request = service.validate_request(REQUEST)
        self.assertEqual(self.cache.coverage(request), [])
        request["fields"] = {"waves": ["swh"]}
        self.assertEqual(self.cache.coverage(request), ["waves:0"])


class CropTests(unittest.TestCase):
    def setUp(self):
        CacheTests.setUp(self)

    def source(self, northward=False):
        path = self.cache.source("2026100412", "weather", 0)
        path.parent.mkdir(exist_ok=True)
        fixture(path, northward=northward)
        return path

    def crop(self, bbox=None, stride=1, northward=False):
        self.source(northward)
        request = copy.deepcopy(REQUEST)
        if bbox: request["bbox"] = bbox
        request["stride"] = stride
        result = self.cache.make_result(service.validate_request(request))
        return decoded(self.cache.root / "results" / (result["id"] + ".grib2"))[0]

    def test_zero_meridian_wrap_and_values(self):
        r = self.crop()
        self.assertEqual((r["ni"], r["nj"]), (4, 5))
        self.assertEqual([v % 360 for v in r["lon"][:4]], [270, 315, 0, 45])
        self.assertEqual(r["values"][:4], [6.125, 7.125, 0.125, 1.125])
        self.assertEqual((r["cycle"], r["step"], r["typeOfLevel"], r["level"]), ((20261004,1200), 0, "heightAboveGround",10))
        self.assertEqual(r["values"][7], r["missing"])

    def test_dateline_wrap(self):
        r = self.crop({"west": 135, "south": -60, "east": -90, "north": 60})
        self.assertEqual(r["lon"][:3], [135, 180, 225])

    def test_stride_and_northward_scanning(self):
        r = self.crop(stride=2, northward=True)
        self.assertEqual((r["ni"], r["nj"]), (2,3))
        self.assertEqual(r["lon"][:2], [270, 0])
        self.assertEqual(r["lat"], [-60, -60, 0, 0, 60, 60])

    def test_empty_and_single_point_crop_fail(self):
        self.source()
        for west, east in ((1, 2), (0, 1)):
            request = copy.deepcopy(REQUEST); request["bbox"]["west"] = west; request["bbox"]["east"] = east
            with self.assertRaises(service.Problem): self.cache.make_result(service.validate_request(request))

    def test_forecast_identity_is_checked(self):
        path = self.source()
        fixture(path, cycle="2026100406")
        with self.assertRaises(service.Problem): self.cache.make_result(service.validate_request(REQUEST))

    def test_warm_result_does_not_run_crop(self):
        self.source()
        r = service.validate_request(REQUEST)
        first = self.cache.make_result(r)
        self.cache.crop = "/nonexistent-helper"
        second = self.cache.make_result(r)
        self.assertEqual(first, second)


class HTTPTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name); (root / "tokens").write_text("test-token\n")
        self.app = service.App(service.Cache(root / "cache"), root / "tokens")
        self.server = service.Server(("127.0.0.1", 0), self.app)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.addCleanup(self.server.server_close); self.addCleanup(self.server.shutdown)
        self.addCleanup(self.app.pool.shutdown)
        self.base = "http://127.0.0.1:" + str(self.server.server_port)

    def call(self, path, token="test-token", data=None):
        headers = {"Authorization": "Bearer " + token, "Content-Type": "application/json"}
        req = urllib.request.Request(self.base + path, data=json.dumps(data).encode() if data is not None else None, headers=headers)
        try:
            with urllib.request.urlopen(req) as response: return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            with error:
                return error.code, json.load(error)

    def test_authentication(self):
        self.assertEqual(self.call("/v1/capabilities", token="wrong")[0], 401)
        self.assertEqual(self.call("/v1/capabilities")[0], 200)
        self.assertEqual(self.call("/healthz", token="wrong")[0], 200)
        self.assertFalse(self.app.authorised("Bearer \u00e9"))

    def test_missing_cycle_is_explicit_and_no_network_fetch(self):
        status, data = self.call("/v1/subsets", data=REQUEST)
        self.assertEqual(status, 503)
        self.assertIn("no alternate cycle", data["error"])

    def test_authenticated_job_lifecycle(self):
        path = self.app.cache.source("2026100412", "weather", 0)
        path.parent.mkdir(); fixture(path)
        status, job = self.call("/v1/subsets", data=REQUEST)
        self.assertEqual(status, 202)
        for _ in range(100):
            _, job = self.call(job["status_url"])
            if job["state"] in ("complete", "failed"): break
            time.sleep(.01)
        self.assertEqual(job["state"], "complete", job)
        self.assertNotIn("_owner", job)
        self.assertEqual(self.call(job["status_url"], token="wrong")[0], 401)

    def test_post_rate_limit(self):
        for _ in range(6): self.assertEqual(self.call("/v1/subsets", data=REQUEST)[0], 503)
        self.assertEqual(self.call("/v1/subsets", data=REQUEST)[0], 429)

    def public_call(self, path, client, data=None):
        headers = {"X-xGRIB-Client-ID": client, "Content-Type": "application/json"}
        req = urllib.request.Request(self.base+path, data=json.dumps(data).encode() if data is not None else None, headers=headers)
        try:
            with urllib.request.urlopen(req) as response: return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            with error: return error.code, json.load(error)

    def test_public_access_is_explicit_and_validated(self):
        self.assertEqual(self.public_call("/v1/capabilities", "a"*32)[0], 401)
        with patch.dict("os.environ", {"XGRIB_PUBLIC_ACCESS": "1"}):
            self.assertEqual(self.public_call("/v1/capabilities", "a"*32)[0], 200)
            for client in ("", "a"*33, "a"*31, "../bad", "\u00e9"):
                self.assertEqual(self.public_call("/v1/capabilities", client)[0], 401)

    def test_rotating_public_ids_does_not_bypass_ip_limits(self):
        with patch.dict("os.environ", {"XGRIB_PUBLIC_ACCESS": "1"}):
            for i in range(6): self.assertEqual(self.public_call("/v1/subsets", f"{i:032x}", REQUEST)[0], 503)
            self.assertEqual(self.public_call("/v1/subsets", "f"*32, REQUEST)[0], 429)

    def test_public_job_ownership_and_partial_coverage(self):
        path = self.app.cache.source("2026100412", "weather", 0)
        path.parent.mkdir(); fixture(path)
        with patch.dict("os.environ", {"XGRIB_PUBLIC_ACCESS": "1"}):
            status, capabilities = self.public_call("/v1/capabilities", "a"*32)
            self.assertEqual(status, 200)
            self.assertEqual(capabilities["coverage"]["2026100412"], {"weather": [0], "waves": []})
            status, job = self.public_call("/v1/subsets", "a"*32, REQUEST)
            self.assertEqual(status, 202)
            self.assertEqual(self.public_call(job["status_url"], "b"*32)[0], 404)
            for _ in range(100):
                _, job = self.public_call(job["status_url"], "a"*32)
                if job["state"] in ("complete", "failed"): break
                time.sleep(.01)
            self.assertEqual(job["state"], "complete", job)
            self.assertNotIn("_owner", job)


if __name__ == "__main__":
    unittest.main()
