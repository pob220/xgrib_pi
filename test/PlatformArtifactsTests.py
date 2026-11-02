import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

repo = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('preflight', repo / 'ci/validate-platform-artifacts.py')
preflight = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preflight)


class PlatformArtifactsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = json.loads((repo / 'ci/catalogue-platforms.json').read_text())
        for name, fields in self.manifest['catalogue'].items():
            folder = self.root / name / 'package'
            folder.mkdir(parents=True)
            xml = ET.Element('plugin')
            for key, value in dict(fields, name='xGRIB', version='0.4.3.0', **{'api-version': '1.21'}).items():
                ET.SubElement(xml, key).text = '15.3.2' if value == '*' else value
            ET.ElementTree(xml).write(folder / 'xgrib_pi-test.xml')
            (folder / 'xgrib_pi-test.tar.gz').write_bytes(b'archive fixture')

    def check(self):
        return preflight.validate(self.root, self.manifest, '0.4.3.0')

    def change(self, platform, field, value):
        path = self.root / platform / 'package/xgrib_pi-test.xml'
        xml = ET.parse(path)
        xml.getroot().find(field).text = value
        xml.write(path)

    def test_complete_matrix(self):
        self.assertEqual(set(self.check()), set(self.manifest['platforms']))

    def test_missing_arm_package(self):
        (self.root / 'debian13-arm64/package/xgrib_pi-test.xml').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing.*debian13-arm64'):
            self.check()

    def test_duplicate_mac_architecture(self):
        self.change('macos-x86_64', 'target-arch', 'arm64')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.check()

    def test_wrong_ubuntu_catalogue_abi(self):
        self.change('ubuntu24.04-arm64', 'target', 'ubuntu-gtk3-arm64')
        with self.assertRaisesRegex(ValueError, 'unexpected'):
            self.check()

    def test_header_check_package_cannot_be_published(self):
        self.change('debian12-armhf', 'api-version', '1.22')
        with self.assertRaisesRegex(ValueError, 'catalogue API'):
            self.check()

    def test_missing_archive(self):
        (self.root / 'macos-x86_64/package/xgrib_pi-test.tar.gz').unlink()
        with self.assertRaisesRegex(ValueError, 'nonempty catalogue archive'):
            self.check()

    def test_stale_version(self):
        self.change('windows-x64', 'version', '0.4.2.0')
        with self.assertRaisesRegex(ValueError, 'release'):
            self.check()


if __name__ == '__main__':
    unittest.main()
