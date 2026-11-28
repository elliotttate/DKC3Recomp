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
  L_8904_M0X0:
    cpu_trace_block(cpu, 0xB78904);
    uint16 _v80 = cpu_read_a16(cpu);
    cpu_write16(cpu, 0x00, (uint16)(cpu->D + 0x002a), _v80);
    uint16 _v82 = cpu_read16(cpu, (uint8)(((((uint32)cpu->DB << 16) + (uint32)0x0026 + (uint32)cpu->Y)) >> 16), (uint16)((((uint32)cpu->DB << 16) + (uint32)0x0026 + (uint32)cpu->Y)));
    cpu_write_a_m(cpu, (uint16)(_v82));
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


# The primary renderer holds its own copy of the $B7:8904 block in its loop
# and none of the listed culls.
PRIMARY_RENDERER_FIXTURE = """\
#include "funcs.h"
RecompReturn CODE_B78857_M0X0(CpuState *cpu) {
  uint16 _entry_s = cpu->S;  L_8857_M0X0:    cpu_trace_block(cpu, 0xB78857);
    uint16 _v1 = 0x30;
  L_8904_M0X0:
    cpu_trace_block(cpu, 0xB78904);
    uint16 _v80 = cpu_read_a16(cpu);
    uint16 _v82 = cpu_read16(cpu, (uint8)(((((uint32)cpu->DB << 16) + (uint32)0x0026 + (uint32)cpu->Y)) >> 16), (uint16)((((uint32)cpu->DB << 16) + (uint32)0x0026 + (uint32)cpu->Y)));
    cpu_write_a_m(cpu, (uint16)(_v82));
  L_8908_M0X0:
    cpu_trace_block(cpu, 0xB78908);
}
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

MUSIC_FIXTURE = """\
#include "funcs.h"
RecompReturn CODE_B2800F_M0X0(CpuState *cpu) {
  L_800F_M0X0:
    cpu_trace_block(cpu, 0xB2800F);
    WatchdogCheck();
    goto L_80E5_M0X0;
  L_80E5_M0X0:
    cpu_trace_block(cpu, 0xB280E5);
}
"""

PALETTE_FIXTURE = """#include "funcs.h"
RecompReturn request_palette_direct_M0X0(CpuState *cpu) {
  L_95DF_M0X0:
    cpu_trace_block(cpu, 0xBB95DF);
    goto L_95E6_M0X0;
  L_95E6_M0X0:
    cpu_trace_block(cpu, 0xBB95E6);
    WatchdogCheck();
}
"""


TRUNK_FIXTURE = """#include "funcs.h"
RecompReturn CODE_B3C0F4_M0X0(CpuState *cpu) {
  L_C19C_M0X0:
    cpu_trace_block(cpu, 0xB3C19C);
    uint16 _v50 = cpu_read_a16(cpu);
    cpu_write8(cpu, cpu->DB, (uint16)(0x2128), _v50);
  L_C1B1_M0X0:
    cpu_trace_block(cpu, 0xB3C1B1);
    uint16 _v58 = cpu_read_a16(cpu);
    cpu_write8(cpu, cpu->DB, (uint16)(0x2129), _v58);
    uint16 _v59 = cpu_read16(cpu, cpu->DB, (uint16)(0x0760));
  L_C1C3_M0X0:
    cpu_trace_block(cpu, 0xB3C1C3);
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
        (generated / "music.c").write_text(MUSIC_FIXTURE, encoding="utf-8")
        (generated / "palette.c").write_text(
            PALETTE_FIXTURE, encoding="utf-8")
        (generated / "trunk.c").write_text(TRUNK_FIXTURE, encoding="utf-8")
        (generated / "primary.c").write_text(
            PRIMARY_RENDERER_FIXTURE, encoding="utf-8")
        sites =[address for address in MODULE.CULL_SITES
                 if MODULE.CULL_SITES[address][:2] == ("0x30", "0x160")]
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
            self.assertIn(MODULE.GAME_INCLUDE, first["music.c"])
            self.assertEqual(
                first["music.c"].count(
                    "Dkc3RecordMusicTransition(cpu->A);"), 1)
            self.assertLess(
                first["music.c"].index("cpu_trace_block(cpu, 0xB2800F);"),
                first["music.c"].index(
                    "Dkc3RecordMusicTransition(cpu->A);"))
            self.assertIn(MODULE.GAME_INCLUDE, first["palette.c"])
            self.assertEqual(
                first["palette.c"].count(
                    "Dkc3RecordSpritePaletteOverflow(cpu->D);"), 1)
            self.assertLess(
                first["palette.c"].index("cpu_trace_block(cpu, 0xBB95E6);"),
                first["palette.c"].index(
                    "Dkc3RecordSpritePaletteOverflow(cpu->D);"))
            self.assertIn(MODULE.GAME_INCLUDE, first["trunk.c"])
            self.assertEqual(
                first["trunk.c"].count("Dkc3RecordTrunkWindows(cpu->D);"), 1)
            self.assertLess(
                first["trunk.c"].index("(uint16)(0x2129), _v58);"),
                first["trunk.c"].index("Dkc3RecordTrunkWindows(cpu->D);"))
            self.assertLess(
                first["trunk.c"].index("Dkc3RecordTrunkWindows(cpu->D);"),
                first["trunk.c"].index("(uint16)(0x0760)"))
            self.assertEqual(
                first["compare.c"].count(
                    "Dkc3VideoExpandSpawnLeft(cpu_read16"), 1)
            self.assertEqual(
                first["compare.c"].count(
                    "Dkc3VideoExpandSpawnSpan(cpu_read16"), 1)
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
                    text.count("Dkc3VideoExpandSpriteCullLeft(0x30)"), 2)
                self.assertEqual(
                    text.count("Dkc3VideoExpandSpriteCullSpan(0x160)"), 2)
                # The render word read is wrapped once, after the cull
                # adaptations, and the unit includes the game header.
                self.assertEqual(
                    text.count("Dkc3RevealHiddenSpriteFlags(cpu_read16("), 1)
                self.assertIn("(uint32)cpu->Y))), cpu->Y);", text)
                self.assertIn(MODULE.GAME_INCLUDE, text)
            primary = first["primary.c"]
            self.assertEqual(
                primary.count("Dkc3RevealHiddenSpriteFlags(cpu_read16("), 1)
            self.assertIn("(uint32)cpu->Y))), cpu->Y);", primary)
            self.assertIn("uint16 _v1 = 0x30;", primary)
            self.assertIn(MODULE.GAME_INCLUDE, primary)
            self.assertIn(
                "uint16 _v14 = Dkc3VideoExpandSpriteCullLeft(0x10);",
                first["narrow.c"])
            self.assertIn(
                "uint16 _v17 = Dkc3VideoExpandSpriteCullSpan(0x120);",
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
                "uint16 _v24 = Dkc3VideoExpandCullSpan(0x10f);",
                first["walk.c"])
            self.assertNotIn("Dkc3VideoExpandCullLeft(0x10f)", first["walk.c"])
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

    def test_fails_closed_when_hidden_render_block_is_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            for path in [*generated.glob("renderer*.c"),
                         generated / "primary.c"]:
                path.write_text(
                    path.read_text(encoding="utf-8").replace(
                        "0xB78904", "0xB78905"),
                    encoding="utf-8")
            with self.assertRaisesRegex(ValueError, r"\$B7:8904"):
                MODULE.apply_overrides(generated)

    def test_fails_closed_when_primary_renderer_is_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            (generated / "primary.c").unlink()
            with self.assertRaisesRegex(ValueError, "CODE_B78857"):
                MODULE.apply_overrides(generated)

    def test_fails_closed_when_a_hidden_render_marker_is_unwrapped(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            path = generated / "primary.c"
            # A marker ahead of every block label is never wrapped.
            path.write_text(
                path.read_text(encoding="utf-8").replace(
                    "  uint16 _entry_s = cpu->S;",
                    "  cpu_trace_block(cpu, 0xB78904);\n"
                    "  uint16 _entry_s = cpu->S;"),
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, r"wrapped \d+ of \d+"):
                MODULE.apply_overrides(generated)

    def test_fails_closed_when_hidden_render_read_moves(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            path = sorted(generated.glob("renderer*.c"))[0]
            path.write_text(
                path.read_text(encoding="utf-8").replace(
                    "(uint32)0x0026", "(uint32)0x0028"),
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, r"\+\$26"):
                MODULE.apply_overrides(generated)

    def test_fails_closed_when_a_constant_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            walk = generated / "walk.c"
            walk.write_text(WALK_FIXTURE.replace("0x10f", "0x10e"),
                            encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "0x10f"):
                MODULE.apply_overrides(generated)

    def test_fails_closed_when_music_transition_anchor_moves(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            music = generated / "music.c"
            music.write_text(MUSIC_FIXTURE.replace("0xB2800F", "0xB28010"),
                             encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "B2:800F"):
                MODULE.apply_overrides(generated)

    def test_fails_closed_when_palette_overflow_anchor_moves(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            palette = generated / "palette.c"
            palette.write_text(PALETTE_FIXTURE.replace("0xBB95E6", "0xBB95E7"),
                               encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "BB:95E6"):
                MODULE.apply_overrides(generated)


    def test_fails_closed_when_trunk_window_anchor_moves(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            trunk = generated / "trunk.c"
            trunk.write_text(TRUNK_FIXTURE.replace("0xB3C1B1", "0xB3C1B2"),
                             encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "B3:C1B1"):
                MODULE.apply_overrides(generated)

    def test_fails_closed_when_trunk_window_write_moves(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            trunk = generated / "trunk.c"
            trunk.write_text(
                TRUNK_FIXTURE.replace("(0x2129), _v58", "(0x2127), _v58"),
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, r"\$2129"):
                MODULE.apply_overrides(generated)

if __name__ == "__main__":
    unittest.main()
