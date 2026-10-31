import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import check_deployment as check
import service


def capabilities():
    return {"version": service.VERSION, "fields": copy.deepcopy(service.FIELDS),
            "coverage": {"2026103106": {"weather": list(range(97)),
                                       "waves": list(range(97))}}}


class Readiness(unittest.TestCase):
    def test_deployed_four_field_service_cannot_qualify_new_routing_client(self):
        value = capabilities()
        value["version"] = "0.2.1"
        del value["fields"]["weather"]["gust"]
        with self.assertRaisesRegex(check.DeploymentError, "0.2.1.*gust"):
            check.ready_cycle(value)

    def test_field_allowlist_does_not_qualify_a_cold_cache(self):
        value = capabilities()
        value["coverage"]["2026103106"]["weather"] = [0]
        with self.assertRaisesRegex(check.DeploymentError, "warm"):
            check.ready_cycle(value)

    def test_weather_and_wave_coverage_must_belong_to_same_cycle(self):
        value = capabilities()
        value["coverage"]["2026103106"]["waves"] = [0]
        value["coverage"]["2026103100"] = {"weather": [0], "waves": list(range(97))}
        with self.assertRaises(check.DeploymentError):
            check.ready_cycle(value)

    def test_ready_previous_cycle_can_serve_while_latest_is_publishing(self):
        value = capabilities()
        value["coverage"]["2026103112"] = {"weather": [0], "waves": [0]}
        self.assertEqual(check.ready_cycle(value), "2026103106")

    def test_unsupported_forecast_hour_is_not_reported_as_covered(self):
        with self.assertRaises(check.DeploymentError):
            check.ready_cycle(capabilities(), 122)

    def test_legacy_service_is_rejected_before_creating_any_regional_job(self):
        class Legacy(check.Client):
            def get(self, path, body=None, binary=False):
                self.paths.append(path)
                value = capabilities()
                del value["fields"]["weather"]["gust"]
                return value
        client = Legacy("https://example.invalid")
        client.paths = []
        with self.assertRaises(check.DeploymentError):
            client.check(crop=True)
        self.assertEqual(client.paths, ["/v1/capabilities"])

    def test_crop_must_keep_all_requested_messages_and_checksum(self):
        message = b"GRIB\0\0\0\2" + (20).to_bytes(8, "big") + b"7777"
        request = {"fields": {product: sorted(names)
                              for product, names in check.REQUIRED_FIELDS.items()}}
        data = message * 8
        result = {"request": request, "bytes": len(data),
                  "sha256": hashlib.sha256(data).hexdigest()}
        check.validate_result({"result": result}, request, data)
        for broken in [data[:-1], message * 7, b"not GRIB"]:
            malformed = {"request": request, "bytes": len(broken),
                         "sha256": hashlib.sha256(broken).hexdigest()}
            with self.assertRaises(check.DeploymentError):
                check.validate_result({"result": malformed}, request, broken)
        with self.assertRaises(check.DeploymentError):
            check.validate_result({"result": result}, request, data[:-20])


class DeploymentImages(unittest.TestCase):
    def run_deployment(self, override=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "service"
            shutil.copytree(ROOT, source, ignore=shutil.ignore_patterns(
                "grib-crop", "__pycache__", "data", "secrets"))
            commands = root / "commands.jsonl"
            binary = root / "bin"
            binary.mkdir()
            docker = binary / "docker"
            docker.write_text("#!" + sys.executable + "\nimport os,json,sys\n"
                              "with open(os.environ['XGRIB_DEPLOY_TEST_LOG'],'a') as f: "
                              "f.write(json.dumps(sys.argv[1:])+'\\n')\n")
            docker.chmod(0o755)
            environment = dict(os.environ, PATH=str(binary) + os.pathsep + os.environ["PATH"],
                               XGRIB_DEPLOY_TEST_LOG=str(commands))
            environment.pop("XGRIB_IMAGE", None)
            if override:
                environment["XGRIB_IMAGE"] = override
            subprocess.run(["sh", str(source / "deploy/start-private.sh")],
                           env=environment, check=True, capture_output=True)
            return [json.loads(line) for line in commands.read_text().splitlines()]

    def test_build_api_and_ingester_use_actual_source_version(self):
        commands = self.run_deployment()
        image = "xgrib-grib-service:" + service.VERSION
        self.assertEqual(len(commands), 3)
        self.assertEqual(commands[0][:3], ["build", "-t", image])
        self.assertEqual(commands[1][-4:], [image, "serve", "--bind", "127.0.0.1"])
        self.assertEqual(commands[2][-2:], [image, "watch"])

    def test_explicit_image_is_preserved_for_operators_and_rollback(self):
        commands = self.run_deployment("xgrib-verified:previous")
        self.assertTrue(all("xgrib-verified:previous" in command for command in commands))


if __name__ == "__main__":
    unittest.main()
