"""The pinned runtime adaptation must fail closed when its source changes."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CMAKE = sys.argv.pop(1) if len(sys.argv) > 1 else "cmake"
SOURCE = ROOT / "snesrecomp/runner/src/snes/interp_bridge.c"
PREDICATES = ("bridge_is_apu_port(adr)", "(uint16_t)adrl == 0x2140",
              "bridge_is_apu_port(adrl)")


class RuntimePerfAdaptation(unittest.TestCase):
    def configure(self, source):
        with tempfile.TemporaryDirectory() as temporary:
            work = pathlib.Path(temporary)
            upstream = work / "upstream/runner/src/snes/interp_bridge.c"
            upstream.parent.mkdir(parents=True)
            upstream.write_text(source)
            (work / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.20)\n'
                'project(runtime_adaptation NONE)\n'
                'set(SNESRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/upstream")\n'
                f'include("{ROOT.as_posix()}/cmake/Dkc3RuntimePerf.cmake")\n')
            result = subprocess.run(
                [CMAKE, "-S", str(work), "-B", str(work / "build")],
                capture_output=True, text=True)
            output = work / "build/dkc3-runtime/interp_bridge.c"
            return result, output.read_text() if output.exists() else None

    def test_current_pin_changes_only_diagnostic_predicate_order(self):
        source = SOURCE.read_text()
        result, output = self.configure(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        for predicate in PREDICATES:
            self.assertEqual(output.count(
                f'{predicate} && getenv("SNESRECOMP_APU_PORT_DIAG")'), 1)
            self.assertNotIn(
                f'getenv("SNESRECOMP_APU_PORT_DIAG") && {predicate}', output)
            output = output.replace(
                f'{predicate} && getenv("SNESRECOMP_APU_PORT_DIAG")',
                f'getenv("SNESRECOMP_APU_PORT_DIAG") && {predicate}')
        self.assertEqual(output, source)

    def test_missing_anchor_is_rejected(self):
        for predicate in PREDICATES:
            with self.subTest(predicate=predicate):
                source = SOURCE.read_text().replace(
                    f'getenv("SNESRECOMP_APU_PORT_DIAG") && {predicate}',
                    'changed_upstream_predicate')
                result, output = self.configure(source)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("anchor moved or is not unique", result.stderr)
                self.assertIsNone(output)

    def test_duplicated_anchor_is_rejected(self):
        for predicate in PREDICATES:
            with self.subTest(predicate=predicate):
                source = SOURCE.read_text() + (
                    f'\ngetenv("SNESRECOMP_APU_PORT_DIAG") && {predicate}\n')
                result, output = self.configure(source)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("anchor moved or is not unique", result.stderr)
                self.assertIsNone(output)


if __name__ == "__main__":
    unittest.main()
