"""The runtime adaptations must apply to the pinned snesrecomp and fail closed.

Every hunk under cmake/runtime-patches is checked against the submodule at its
pinned revision. A moved, missing, or duplicated anchor must stop the build
rather than silently dropping an adaptation, and every patched copy must
differ from its source only where a hunk says so.
"""
import importlib.util
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/apply_dkc3_runtime_patches.py"
PATCHES = ROOT / "cmake/runtime-patches"
SNESRECOMP = ROOT / "snesrecomp"

spec = importlib.util.spec_from_file_location("apply_dkc3_runtime_patches", SCRIPT)
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)


def hunk_files():
    return sorted(PATCHES.glob("*.hunks"))


class RuntimePatches(unittest.TestCase):
    def setUp(self):
        if not (SNESRECOMP / "runner/runner.cmake").exists():
            self.skipTest("snesrecomp submodule is not initialized")

    def test_every_hunk_file_names_one_pinned_source(self):
        self.assertTrue(hunk_files())
        targets = set()
        for hunk_file in hunk_files():
            target, hunks = patches.parse_hunks(hunk_file)
            self.assertTrue((SNESRECOMP / target).is_file(), target)
            self.assertNotIn(target, targets, f"{target} appears in two hunk files")
            targets.add(target)
            self.assertTrue(hunks, hunk_file)
            names = [hunk.name for hunk in hunks]
            self.assertEqual(len(names), len(set(names)), hunk_file)

    def test_pinned_sources_apply_and_change_only_where_stated(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = pathlib.Path(temporary)
            results = patches.apply_directory(PATCHES, SNESRECOMP, out)
            self.assertEqual(len(results), len(hunk_files()))
            manifest = (out / "manifest.txt").read_text().splitlines()
            self.assertEqual(len(manifest), len(results))
            by_target = {}
            for hunk_file in hunk_files():
                target, hunks = patches.parse_hunks(hunk_file)
                by_target[SNESRECOMP / target] = hunks
            for source, output in results:
                original = source.read_text()
                patched = output.read_text()
                self.assertNotEqual(original, patched, source)
                # Reversing every hunk restores the pinned source byte for byte.
                restored = patched
                for hunk in reversed(by_target[source]):
                    self.assertEqual(restored.count(hunk.new), hunk.count, hunk.name)
                    restored = restored.replace(hunk.new, hunk.old)
                self.assertEqual(restored, original, source)

    def test_ppu_mode2_priorities_are_adapted(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = pathlib.Path(temporary)
            patches.apply_directory(PATCHES, SNESRECOMP, out)
            ppu = (out / "ppu.c").read_text()
        self.assertIn(
            "PpuDrawBackground_4bpp_opt(ppu, y, sub, 0, 0xc000, 0x4000);", ppu)
        self.assertIn(
            "PpuDrawBackground_4bpp_opt(ppu, y, sub, 1, 0x8100, 0x1100);", ppu)
        self.assertNotIn(
            "PpuDrawBackground_4bpp_opt(ppu, y, sub, 0, 0xc000, 0x8000);", ppu)

    def test_ppu_windows_honors_combine_logic_and_presentation_bias(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = pathlib.Path(temporary)
            patches.apply_directory(PATCHES, SNESRECOMP, out)
            ppu = (out / "ppu.c").read_text()
        self.assertIn("switch ((ppu->wbgobjlog >> (layer * 2)) & 3u)", ppu)
        self.assertIn("if (ppu->wsPresentationXBias != 0)", ppu)
        self.assertNotIn("  win->bits = w1_bits | w2_bits;\n  debug_server_on_ppu_window", ppu)

    def test_apu_diagnostic_predicates_precede_environment_lookup(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = pathlib.Path(temporary)
            patches.apply_directory(PATCHES, SNESRECOMP, out)
            bridge = (out / "interp_bridge.c").read_text()
        for predicate in ("bridge_is_apu_port(adr)", "(uint16_t)adrl == 0x2140",
                          "bridge_is_apu_port(adrl)"):
            self.assertEqual(bridge.count(
                f'{predicate} && getenv("SNESRECOMP_APU_PORT_DIAG")'), 1)
            self.assertNotIn(
                f'getenv("SNESRECOMP_APU_PORT_DIAG") && {predicate}', bridge)

    def test_dsp_diagnostics_are_gated_on_the_trace_value(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = pathlib.Path(temporary)
            patches.apply_directory(PATCHES, SNESRECOMP, out)
            for name in ("dsp.c", "dsp_shadow.c"):
                text = (out / name).read_text()
                self.assertNotIn("defined(SNESRECOMP_TRACE)", text, name)

    def test_balanced_dispatch_unwinds_nonlocal_stack_pops(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = pathlib.Path(temporary)
            patches.apply_directory(PATCHES, SNESRECOMP, out)
            bridge = (out / "interp_bridge.c").read_text()
        self.assertNotIn(
            "interp_bridge_run_ex2(cpu, target_pc24 & 0xFFFFFF, entry_s,\n"
            "                                   &landing, NULL, 0, 0, 0, 0, NULL, 0, 0);",
            bridge,
        )
        self.assertIn(
            "static int interp_s_crossed_into_compiled_ancestor(uint16_t s) {\n"
            "    const uint16_t owner_delta = (uint16_t)(s - s_interp_owner_exit_s);\n"
            "    return s_interp_bounce_recomp_base > 0 &&\n"
            "           s_interp_owner_exit_valid &&\n"
            "           owner_delta != 0 && owner_delta < 0x8000u;\n"
            "}\n",
            bridge,
        )
        self.assertIn(
            "        const uint16_t pre_s_delta = (uint16_t)(cpu->S - entry_s);\n"
            "        if (pre_s_delta != 0 && pre_s_delta < 0x8000u &&\n"
            "            s_interp_bounce_owner_depth > 0 &&\n"
            "            !cpu_interrupt_context_active() &&\n"
            "            !interp_s_crossed_into_compiled_ancestor(cpu->S)) {\n"
            "            (void)hrv;\n"
            "            return interp_bridge_lle_yield_unwind(cpu, target_pc24 & 0xFFFFFFu);\n"
            "        }\n",
            bridge,
        )
        self.assertIn(
            "    uint32_t post_return_pc = 0xFFFFFFFFu;\n"
            "    const uint16_t pre_s_delta = (uint16_t)(cpu->S - entry_s);\n"
            "    const int pre_s_above = pre_s_delta != 0 && pre_s_delta < 0x8000u;\n"
            "    const uint16_t run_s_exit = pre_s_above ? cpu->S : entry_s;\n"
            "    s_run_hrv = (!pre_s_above && (hrv == 2 || hrv == 3)) ? hrv : 0;\n"
            "    int ok = interp_bridge_run_ex2(cpu, target_pc24 & 0xFFFFFF, run_s_exit,\n"
            "                                   &landing, &post_return_pc, 0, 0, 0, 0, NULL, 0, 0);\n"
            "    s_run_hrv = 0;\n",
            bridge,
        )
        self.assertIn(
            "            const uint16_t over_delta = (uint16_t)(cpu->S - expected_post_s);\n"
            "            if ((hrv == 2 || hrv == 3) &&\n"
            "                over_delta < 0x8000u &&\n"
            "                !interp_s_crossed_into_compiled_ancestor(cpu->S) &&\n"
            "                post_return_pc != 0xFFFFFFFFu &&\n"
            "                s_interp_bounce_owner_depth > 0 &&\n"
            "                !cpu_interrupt_context_active()) {\n"
            "                return interp_bridge_lle_yield_unwind(cpu, post_return_pc);\n"
            "            }\n",
            bridge,
        )

    def test_apu_timeline_uses_single_clock_and_clears_port_queue_on_load(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = pathlib.Path(temporary)
            patches.apply_directory(PATCHES, SNESRECOMP, out)
            bridge = (out / "interp_bridge.c").read_text()
            rtl = (out / "common_rtl.c").read_text()
        self.assertNotIn("interp_bridge_use_absolute_apu_timeline(", bridge)
        self.assertIn("if (rtl_apu_frame_timeline_active()) {", bridge)
        self.assertIn("if (!rtl_apu_frame_timeline_active()) {", bridge)
        self.assertIn("if (g_snes && g_snes->apu && !g_snes->apu->portTimeValid) {", rtl)
        self.assertEqual(rtl.count("apu_clearPortQueue(g_snes->apu);"), 3)

    def _apply_edited(self, edit):
        """Apply the patch set to a copy of the pinned runtime after `edit`."""
        with tempfile.TemporaryDirectory() as temporary:
            work = pathlib.Path(temporary)
            root = work / "snesrecomp"
            for hunk_file in hunk_files():
                target, _ = patches.parse_hunks(hunk_file)
                copy = root / target
                copy.parent.mkdir(parents=True, exist_ok=True)
                copy.write_text(edit(target, (SNESRECOMP / target).read_text()))
            with self.assertRaises(patches.PatchError) as raised:
                patches.apply_directory(PATCHES, root, work / "out")
            return str(raised.exception)

    def test_missing_anchor_is_rejected(self):
        target, hunks = patches.parse_hunks(hunk_files()[0])
        first = hunks[0]

        def edit(name, text):
            if name == target:
                return text.replace(first.old, "changed upstream", 1)
            return text

        message = self._apply_edited(edit)
        self.assertIn(first.name, message)
        self.assertIn("found 0 time(s)", message)

    def test_duplicated_anchor_is_rejected(self):
        target, hunks = patches.parse_hunks(hunk_files()[0])
        first = hunks[0]

        def edit(name, text):
            if name == target:
                return text + "\n" + first.old + "\n"
            return text

        message = self._apply_edited(edit)
        self.assertIn(first.name, message)
        self.assertIn(f"expected {first.count}", message)

    def test_malformed_hunk_file_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            bad = pathlib.Path(temporary) / "bad.hunks"
            bad.write_text("file: runner/src/snes/ppu.c\n@@ broken\n<<<<\nold\n====\nold\n>>>>\n")
            with self.assertRaises(patches.PatchError):
                patches.parse_hunks(bad)
            bad.write_text("@@ nofile\n<<<<\nold\n====\nnew\n>>>>\n")
            with self.assertRaises(patches.PatchError):
                patches.parse_hunks(bad)


if __name__ == "__main__":
    sys.exit(unittest.main())
