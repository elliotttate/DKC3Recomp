import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "scripts" /
          "apply_dkc3_widescreen_overrides.py")
SPEC = importlib.util.spec_from_file_location(
    "apply_dkc3_widescreen_overrides", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


COMPARE_FIXTURE = """\
#include "funcs.h"
RecompReturn CODE_BBAAD4_M0X0(CpuState *cpu) {
  L_AAD4_M0X0:
    cpu_trace_block(cpu, 0xBBAAD4);
    uint16 _v1 = cpu_read16(cpu, 0x00, (uint16)(0x196d));
    uint16 _v2 = cpu_read16(cpu, (uint8)((((uint32)0xbba8be + (uint32)cpu->X)) >> 16), (uint16)(((uint32)0xbba8be + (uint32)cpu->X)));
  L_AAE1_M0X0:
    uint16 _v7 = cpu_read16(cpu, (uint8)((((uint32)0xbba8c0 + (uint32)cpu->X)) >> 16), (uint16)(((uint32)0xbba8c0 + (uint32)cpu->X)));
    uint16 _v9 = cpu_read16(cpu, (uint8)((((uint32)0xbba8c2 + (uint32)cpu->X)) >> 16), (uint16)(((uint32)0xbba8c2 + (uint32)cpu->X)));
}
"""

SCAN_FIXTURE = """\
#include "funcs.h"
RecompReturn CODE_BBA647_M0X0(CpuState *cpu) {
  L_A696_M0X0:
    uint16 _v18 = cpu_read16(cpu, (uint8)((((uint32)0x15fe + (uint32)cpu->X)) >> 16), (uint16)(((uint32)0x15fe + (uint32)cpu->X)));
  L_A6AA_M0X0:
    uint16 _v20 = cpu_read16(cpu, (uint8)((((uint32)0x7e4180 + (uint32)cpu->X)) >> 16), (uint16)(((uint32)0x7e4180 + (uint32)cpu->X)));
}
"""


def renderer_unit(symbol: str, entry_pc: str, cull_pc: str) -> str:
    """A renderer variant whose entry label shares a line with a statement,
    holding one cull block plus the shared block every variant inlines."""
    return f"""\
#include "funcs.h"
RecompReturn {symbol}(CpuState *cpu) {{
  uint16 _entry_s = cpu->S;  L_{entry_pc[4:]}_M0X0:    cpu_trace_block(cpu, {entry_pc});
    uint16 _v1 = 0x30;
  L_{cull_pc[4:]}_M0X0:
    cpu_trace_block(cpu, {cull_pc});
    uint16 _v5 = cpu_read16(cpu, cpu->DB, (uint16)(0x196d));
    uint16 _v6 = 0x30;
    uint16 _v7 = 0x160;
  L_8925_M0X0:
    cpu_trace_block(cpu, 0xB78925);
    uint16 _v10 = cpu_read16(cpu, cpu->DB, (uint16)(0x196d));
    uint16 _v14 = 0x30;
    uint16 _v17 = 0x160;
  L_8940_M0X0:
    cpu_trace_block(cpu, 0xB78940);
    uint16 _v20 = 0x160;
}}
"""


NARROW_FIXTURE = """\
#include "funcs.h"
RecompReturn CODE_B78DFC_M0X0(CpuState *cpu) {
  uint16 _entry_s = cpu->S;  L_8DFC_M0X0:    cpu_trace_block(cpu, 0xB78DFC);
    uint16 _v10 = cpu_read16(cpu, cpu->DB, (uint16)(0x196d));
    uint16 _v14 = 0x10;
    uint16 _v17 = 0x120;
  L_8E1C_M0X0:
    cpu_trace_block(cpu, 0xB78E1C);
}
"""

ONSCREEN_FIXTURE = """\
#include "funcs.h"
RecompReturn CODE_B6F186_M0X0(CpuState *cpu) {
  L_F186_M0X0:
    cpu_trace_block(cpu, 0xB6F186);
    uint16 _v2 = cpu_read16(cpu, cpu->DB, (uint16)(0x196d));
    uint16 _v5 = 0x10;
    uint16 _v8 = 0x110;
  L_F195_M0X0:
    cpu_trace_block(cpu, 0xB6F195);
    uint16 _v13 = cpu_read16(cpu, cpu->DB, (uint16)(0x1973));
    uint16 _v14 = 0x10;
    uint16 _v17 = 0x110;
}
"""

WALK_FIXTURE = """\
#include "funcs.h"
RecompReturn CODE_B7E8E0_M0X0(CpuState *cpu) {
  L_E8E0_M0X0:
    cpu_trace_block(cpu, 0xB7E8E0);
    uint16 _v1 = cpu_read16(cpu, cpu->DB, (uint16)(0x196d));
    uint16 _v2 = 0x100;
  L_E909_M0X0:
    cpu_trace_block(cpu, 0xB7E909);
    uint16 _v24 = 0x10f;
}
"""

DRAW_FIXTURE = """\
#include "funcs.h"
RecompReturn CODE_B7E981_M0X0(CpuState *cpu) {
  L_E9C8_M0X0:
    cpu_trace_block(cpu, 0xB7E9C8);
    uint16 _v35 = 0xf;
  L_E9EF_M0X0:
    cpu_trace_block(cpu, 0xB7E9EF);
    uint16 _v185 = 0x107;
  L_EA41_M0X1:
    cpu_trace_block(cpu, 0xB7EA41);
    cpu_write_a_m(cpu, (uint16)(_v93));
    cpu_write_a_m(cpu, (uint16)(_v96));
    cpu_write_a_m(cpu, (uint16)(_v98));
  L_EAA4_M0X1:
    cpu_trace_block(cpu, 0xB7EAA4);
    cpu_write_a_m(cpu, (uint16)(_v132));
    cpu_write_a_m(cpu, (uint16)(_v135));
}
"""


class WidescreenOverrideTests(unittest.TestCase):
    def make_generated_dir(self, root: Path) -> Path:
        generated = root / "generated"
        generated.mkdir()
        (generated / "compare.c").write_text(COMPARE_FIXTURE, encoding="utf-8")
        (generated / "scan.c").write_text(SCAN_FIXTURE, encoding="utf-8")
        (generated / "narrow.c").write_text(NARROW_FIXTURE, encoding="utf-8")
        (generated / "onscreen.c").write_text(
            ONSCREEN_FIXTURE, encoding="utf-8")
        (generated / "walk.c").write_text(WALK_FIXTURE, encoding="utf-8")
        (generated / "draw.c").write_text(DRAW_FIXTURE, encoding="utf-8")
        sites = [address for address in MODULE.CULL_SITES
                 if MODULE.CULL_SITES[address] == ("0x30", "0x160")]
        for index, address in enumerate(sorted(sites)):
            if address == "0xB78925":
                continue
            entry = f"0xB7F{index:x}00"
            (generated / f"renderer{index}.c").write_text(
                renderer_unit(f"CODE_{entry[2:]}_M0X0", entry, address),
                encoding="utf-8")
        return generated

    def test_applies_expected_adaptations_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            MODULE.apply_overrides(generated)
            first = {path.name: path.read_text(encoding="utf-8")
                     for path in generated.glob("*.c")}
            MODULE.apply_overrides(generated)
            second = {path.name: path.read_text(encoding="utf-8")
                      for path in generated.glob("*.c")}
            self.assertEqual(first, second)
            for name in ("compare.c", "scan.c"):
                self.assertIn(MODULE.INCLUDE, first[name])
            self.assertIn(MODULE.GAME_INCLUDE, first["scan.c"])
            self.assertEqual(
                first["compare.c"].count(
                    "Dkc3VideoExpandCullLeft(cpu_read16"), 1)
            self.assertEqual(
                first["compare.c"].count(
                    "Dkc3VideoExpandCullSpan(cpu_read16"), 1)
            self.assertEqual(
                first["scan.c"].count(
                    "Dkc3PlacementScanBegin(cpu_read16"), 1)
            self.assertEqual(
                first["scan.c"].count(
                    "Dkc3PlacementScanNext(cpu_read16"), 1)
            self.assertIn(", cpu->X);", first["scan.c"])
            self.assertIn("uint16 _v9 = cpu_read16", first["compare.c"])
            renderers = [text for name, text in first.items()
                         if name.startswith("renderer")]
            self.assertEqual(len(renderers), 7)
            for text in renderers:
                # The entry block's $30 and the trailing block's $160 are
                # not culls and stay native; the unit's own cull block and
                # the shared block each widen once.
                self.assertIn("uint16 _v1 = 0x30;", text)
                self.assertIn("uint16 _v20 = 0x160;", text)
                self.assertEqual(
                    text.count("Dkc3VideoExpandCullLeft(0x30)"), 2)
                self.assertEqual(
                    text.count("Dkc3VideoExpandCullSpan(0x160)"), 2)
            self.assertIn(
                "uint16 _v14 = Dkc3VideoExpandCullLeft(0x10);",
                first["narrow.c"])
            self.assertIn(
                "uint16 _v17 = Dkc3VideoExpandCullSpan(0x120);",
                first["narrow.c"])
            self.assertIn(
                "uint16 _v5 = Dkc3VideoExpandCullLeft(0x10);",
                first["onscreen.c"])
            self.assertIn(
                "uint16 _v8 = Dkc3VideoExpandCullSpan(0x110);",
                first["onscreen.c"])
            # The Y test in the next block keeps its constants.
            self.assertIn("uint16 _v14 = 0x10;", first["onscreen.c"])
            self.assertIn("uint16 _v17 = 0x110;", first["onscreen.c"])
            self.assertIn(
                "uint16 _v2 = Dkc3VideoExpandCullRight(0x100);",
                first["walk.c"])
            self.assertIn(
                "uint16 _v24 = Dkc3VideoExpandCullLeft(0x10f);",
                first["walk.c"])
            self.assertIn(
                "uint16 _v35 = Dkc3VideoExpandCullLeft(0xf);",
                first["draw.c"])
            self.assertIn(
                "uint16 _v185 = Dkc3VideoExpandCullRight(0x107);",
                first["draw.c"])
            self.assertIn(
                "cpu_write_a_m(cpu, (uint16)(Dkc3VideoPromoteOamXHigh(_v96)));",
                first["draw.c"])
            self.assertIn(
                "cpu_write_a_m(cpu, (uint16)(Dkc3VideoPromoteOamXHigh(_v135)));",
                first["draw.c"])
            self.assertIn("cpu_write_a_m(cpu, (uint16)(_v93));", first["draw.c"])

    def test_fails_closed_when_a_cull_site_is_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            (generated / "narrow.c").unlink()
            with self.assertRaisesRegex(ValueError, "0xB78DFC"):
                MODULE.apply_overrides(generated)

    def test_fails_closed_when_a_constant_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            walk = generated / "walk.c"
            walk.write_text(WALK_FIXTURE.replace("0x10f", "0x10e"),
                            encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "0x10f"):
                MODULE.apply_overrides(generated)


if __name__ == "__main__":
    unittest.main()
