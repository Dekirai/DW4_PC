"""Synthetic tests of the remaster pipeline's building blocks (no game data,
no models, no GPU):

    python tools/remaster/test_remaster.py     (or python -m unittest discover -s tools/remaster -p "test_*.py")
"""
import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rmcommon  # noqa: E402
import build_pack  # noqa: E402
import textpath  # noqa: E402
import upscale  # noqa: E402


def row(**kw):
    base = {"hash": "0" * 16, "alpha": "blend", "blend": "SRC_ALPHA/ONE_MINUS_SRC_ALPHA", "alpha_test": "GREATER 0.1",
            "tiles": "no", "uv_edges": "no"}
    base.update(kw)
    return base


class ColourMetricTests(unittest.TestCase):
    def test_ciede2000_reference_pairs(self):
        # Sharma, Wu, Dalal (2005), table 1: pairs 1, 7, 17 and 25.
        pairs = [((50.0, 2.6772, -79.7751), (50.0, 0.0, -82.7485), 2.0425),
                 ((50.0, 0.0, 0.0), (50.0, -1.0, 2.0), 2.3669),
                 ((50.0, 2.5, 0.0), (73.0, 25.0, -18.0), 27.1492),
                 ((60.2574, -34.0099, 36.2677), (60.4626, -34.1751, 39.4387), 1.2644)]
        for a, b, expected in pairs:
            with self.subTest(a=a, b=b):
                self.assertAlmostEqual(float(rmcommon.delta_e2000(np.array(a), np.array(b))), expected, places=4)

    def test_fidelity_of_exact_upscale(self):
        rng = np.random.default_rng(1)
        src = np.concatenate([rng.random((8, 8, 3)), np.ones((8, 8, 1))], axis=2).astype(np.float32)
        m = rmcommon.fidelity(src, rmcommon.nearest(src, 4), "opaque")
        self.assertAlmostEqual(m["de_mean"], 0.0, places=3)
        self.assertEqual(m["psnr"], float("inf"))

    def test_fidelity_mask_limits_the_check(self):
        src = np.ones((4, 4, 4), np.float32)
        out = rmcommon.nearest(src, 4).copy()
        out[:8, :, :3] = 0.0  # the upper half wrong
        mask = np.zeros((4, 4), bool)
        mask[2:] = True
        self.assertGreater(rmcommon.fidelity(src, out, "opaque")["de_mean"], 10)
        self.assertAlmostEqual(rmcommon.fidelity(src, out, "opaque", mask)["de_mean"], 0.0, places=3)


class PreprocessTests(unittest.TestCase):
    def test_fill_under_keeps_valid_and_spreads_neighbours(self):
        rgb = np.zeros((16, 16, 3), np.float32)
        rgb[:, :8] = (1.0, 0.0, 0.0)
        rgb[:, 8:] = (0.0, 0.0, 1.0)  # garbage under alpha 0
        valid = np.zeros((16, 16), bool)
        valid[:, :8] = True
        out = rmcommon.fill_under(rgb, valid)
        np.testing.assert_array_equal(out[:, :8], rgb[:, :8])
        self.assertTrue(np.allclose(out[:, 8:], (1.0, 0.0, 0.0), atol=1e-5))

    def test_pad_wrap_is_periodic(self):
        img = np.arange(12, dtype=np.float32).reshape(3, 4)
        p = rmcommon.pad(img, 5, 6, True, False)
        np.testing.assert_array_equal(p[5:8, 6:10], img)
        np.testing.assert_array_equal(p[2:5, 6:10], img)  # the row above repeats the bottom
        np.testing.assert_array_equal(p[5:8, 0], img[:, 0])  # edge extension on x

    def test_material_parsing(self):
        self.assertEqual(rmcommon.alpha_mode(row(alpha="opaque")), "opaque")
        self.assertEqual(rmcommon.alpha_mode(row(blend="SRC_ALPHA/ONE")), "additive")
        self.assertEqual(rmcommon.alpha_mode(row(blend="SRC_ALPHA/ONE,SRC_ALPHA/ONE_MINUS_SRC_ALPHA")), "blend")
        self.assertEqual(rmcommon.alpha_mode(row(blend="off", alpha_test="off")), "ignore")
        self.assertEqual(rmcommon.alpha_mode(row(alpha="cutout", blend="off")), "cutout")
        self.assertEqual(rmcommon.alpha_mode(row(blend="-", alpha_test="-")), "blend")  # no draws: alpha counts
        self.assertEqual(rmcommon.cutout_threshold(row(alpha_test="GEQUAL 0.6", blend="off")), (0.6, True))
        self.assertEqual(rmcommon.cutout_threshold(row(alpha_test="GREATER 0.6", blend="off")), (0.6, False))
        self.assertEqual(rmcommon.cutout_threshold(row(alpha_test="GREATER 0.1")), (0.5, True))  # blended: half coverage
        # Without classify.py's decision only `tiles` counts; the decision wins.
        self.assertEqual(rmcommon.wrap_axes(row(tiles="s", uv_edges="t")), (False, True))
        self.assertEqual(rmcommon.wrap_axes(row(tiles="no", uv_edges="s")), (False, False))
        self.assertEqual(rmcommon.wrap_axes(row(), {"wrap": "ts"}), (True, True))
        self.assertEqual(rmcommon.wrap_axes(row(tiles="st"), {"wrap": "-"}), (False, False))

    def test_tiling_axes_edge_unless_continuous(self):
        y, x = np.mgrid[0:16, 0:16].astype(np.float32)
        ramp = np.zeros((16, 16, 4), np.float32)
        ramp[..., :3] = (y / 15.0)[..., None]  # a beam: black first row, bright last row
        ramp[..., 3] = 1.0
        tile = ramp.copy()
        tile[..., :3] = (0.5 + 0.1 * np.sin(2 * np.pi * x / 16.0))[..., None]  # repeats along s
        r = row(alpha="opaque", uv_edges="st")
        self.assertEqual(rmcommon.tiling_axes(r, ramp, "environment")[0], (False, True))
        self.assertEqual(rmcommon.tiling_axes(r, tile, "environment")[0], (True, True))
        self.assertEqual(rmcommon.tiling_axes(r, tile, "ui")[0], (False, False))  # flat quads never wrap
        self.assertEqual(rmcommon.tiling_axes(row(alpha="opaque", tiles="t"), ramp, "ui")[0], (True, False))


class SmoothingAndMaskTests(unittest.TestCase):
    def test_dequantised_alpha_stays_in_its_bins(self):
        import upscale
        a = np.repeat((np.arange(32) // 4 * 36 / 255.0).astype(np.float32)[:, None], 8, axis=1)  # 8 steps
        s, _ = upscale.dequantize_alpha(a, (False, False))
        q = np.round(a * 255)
        self.assertTrue(np.all(np.abs(s * 255 - q) <= 18.0 + 1e-3))
        self.assertLess(np.abs(np.diff(s, axis=0)).max(), np.abs(np.diff(a, axis=0)).max())

    def test_cutout_mask_from_source_shape(self):
        import upscale
        src = np.zeros((8, 8, 4), np.float32)
        src[..., :3] = 0.5
        src[2:6, 3:4, 3] = 1.0  # a window column one texel wide
        r = row(alpha="cutout", blend="off", alpha_test="GREATER 0.5")
        lz = lambda img: rmcommon.lanczos4(img, 4)  # noqa: E731
        out = upscale.run_picture(src, r, "cutout", lz, lz, (False, False))
        self.assertEqual(set(np.unique(out[..., 3])), {0.0, 1.0})
        self.assertAlmostEqual(out[..., 3].mean() * 64, 4.0, delta=1.0)  # same coverage
        sq = upscale.run_picture(src, r, "cutout", lz, lz, (False, False), mask_nearest=True)
        self.assertTrue(np.array_equal(sq[..., 3], rmcommon.nearest(src[..., 3], 4)))


class TextPathTests(unittest.TestCase):
    def test_ring_keeps_its_hole_and_area(self):
        y, x = np.mgrid[0:20, 0:20] + 0.5
        r = np.hypot(x - 10, y - 10)
        ring = (np.clip(6.5 - r, 0, 1) * np.clip(r - 2.5, 0, 1)).astype(np.float32)
        m = textpath.recon_mask(ring)
        self.assertEqual(m.shape, (80, 80))
        self.assertEqual(m[40, 40], 0.0)
        self.assertEqual(m[40, 58], 1.0)
        self.assertAlmostEqual(m.sum() / 16, np.pi * (6 ** 2 - 3 ** 2), delta=2.0)

    def test_two_tones_and_gradients(self):
        rgb = np.zeros((16, 16, 3), np.float32)
        rgb[4:12, 4:12] = 1.0  # white fill in a black outline
        self.assertIsNotNone(textpath.two_tones(rgb, np.ones((16, 16), np.float32)))
        ramp = np.repeat(np.linspace(0, 1, 16, dtype=np.float32)[None, :, None], 16, axis=0).repeat(3, axis=2)
        self.assertIsNone(textpath.two_tones(ramp, np.ones((16, 16), np.float32)))

    def test_flat_glyph_detection(self):
        glyph = np.zeros((8, 8, 4), np.float32)
        glyph[2:6, 2:6] = (1, 1, 1, 1)
        self.assertTrue(textpath.is_flat_glyph(glyph))
        icon = glyph.copy()
        icon[2:6, 2:6, 0] = np.linspace(0.1, 0.9, 16).reshape(4, 4)
        self.assertFalse(textpath.is_flat_glyph(icon))


class PipelineTests(unittest.TestCase):
    def test_merge_cells(self):
        cells = [("quad", "-", 0, 0, 64, 64),  # the whole picture: dropped
                 ("uvkey", "0", 1, 1, 21, 11), ("uvkey", "1", 20, 1, 40, 11),  # 1 texel overlap: kept apart
                 ("uvkey", "2", 0, 20, 30, 30), ("uvkey", "3", 10, 22, 40, 32)]  # big overlap: merged
        self.assertEqual(upscale.merge_cells(cells, 64, 64), [(1, 1, 21, 11), (20, 1, 40, 11), (0, 20, 40, 32)])

    def test_cutout_alpha_is_binary_at_the_reference(self):
        src = np.zeros((16, 16, 4), np.float32)
        src[..., :3] = 0.5
        src[4:12, 4:12, 3] = 1.0
        out = upscale.run_picture(src, row(alpha="cutout", blend="off", alpha_test="GEQUAL 0.6"), "cutout",
                                  upscale.lanczos_sr, upscale.lanczos_sr, (False, False))
        self.assertEqual(out.shape, (64, 64, 4))
        self.assertEqual(set(np.unique(out[..., 3]).tolist()), {0.0, 1.0})
        self.assertEqual(out[32, 32, 3], 1.0)
        self.assertEqual(out[2, 2, 3], 0.0)

    def test_additive_keeps_visible_colour_and_invents_no_alpha(self):
        src = np.zeros((16, 16, 4), np.float32)
        src[..., 0] = 1.0  # red everywhere, alpha only in the middle
        src[6:10, 6:10, 3] = 0.8
        out = upscale.run_picture(src, row(blend="SRC_ALPHA/ONE"), "additive", upscale.lanczos_sr, upscale.lanczos_sr,
                                  (False, False))
        self.assertEqual(out[..., 3][:12, :].max(), 0.0)  # far from the source alpha: none
        self.assertLessEqual(out[..., 3].max(), 0.8 + 1e-6)
        m = rmcommon.fidelity(src, out, "additive")
        self.assertLess(m["de_mean"], 2.0)

    def test_soft_alpha_stays_within_the_source_range(self):
        src = np.zeros((16, 16, 4), np.float32)
        src[..., 3] = np.linspace(0, 1, 16, dtype=np.float32)[None, :]

        def sharpen(img):  # a stand-in for a model that steepens ramps
            up = upscale.lanczos_sr(img)
            return np.clip((up - 0.5) * 3 + 0.5, 0, 1)

        out = upscale.run_picture(src, row(), "blend", sharpen, sharpen, (False, False))
        lan = upscale.run_picture(src, row(), "blend", upscale.lanczos_sr, upscale.lanczos_sr, (False, False))
        # A gentle ramp (1/15 a texel) takes the Lanczos alpha, not the steepened one.
        np.testing.assert_allclose(out[..., 3], lan[..., 3], atol=1e-6)

    def test_cells_keep_a_solid_edge(self):
        # A frame piece that fills its cell: its border must not fade out.
        src = np.zeros((16, 16, 4), np.float32)
        src[:, :8] = (1.0, 0.0, 0.0, 1.0)
        cell = upscale.cut_cell(src, (0, 0, 8, 16), "blend")
        self.assertEqual(cell.shape, (16 + 2 * upscale.CELL_BORDER, 8 + 2 * upscale.CELL_BORDER, 4))
        self.assertEqual(float(cell[..., 3].min()), 1.0)  # no transparent margin, no bleed from the right half


class FinishingTests(unittest.TestCase):
    def test_down2_keeps_flat_colour_and_wraps(self):
        flat = np.full((16, 8, 3), 0.3, np.float32)
        np.testing.assert_allclose(rmcommon.down2(flat, (False, False)), 0.3, atol=1e-6)
        stripes = np.zeros((8, 16, 1), np.float32)
        stripes[:, ::4] = 1.0  # period 4: a 2x level of it is periodic as well
        d = rmcommon.down2(stripes, (False, True))
        self.assertEqual(d.shape, (4, 8, 1))
        np.testing.assert_allclose(d[:, 0], d[:, 2], atol=1e-5)  # the left edge sees the right one (wrap)

    def test_downscale2x_cutout_stays_binary_and_keeps_coverage(self):
        img = np.zeros((32, 32, 4), np.float32)
        img[..., :3] = 0.6
        img[8:24, 8:24, 3] = 1.0
        d = rmcommon.downscale2x(img, "cutout", (False, False))
        self.assertEqual(set(np.unique(d[..., 3]).tolist()), {0.0, 1.0})
        self.assertAlmostEqual(float(d[..., 3].mean()), float(img[..., 3].mean()), delta=0.02)
        np.testing.assert_allclose(d[..., :3], 0.6, atol=1e-5)  # no dark fringe from colour under alpha 0

    def test_colour_fix_removes_a_slow_shift_and_keeps_detail(self):
        rng = np.random.default_rng(3)
        src = np.concatenate([rng.uniform(0.3, 0.7, (16, 16, 3)), np.ones((16, 16, 1))], axis=2).astype(np.float32)
        detail = rng.uniform(-0.05, 0.05, (64, 64, 1)).astype(np.float32)
        out = rmcommon.nearest(src, 4).copy()
        out[..., :3] = np.clip(out[..., :3] + 0.12 + detail, 0, 1)  # brightened, with fine detail
        before = rmcommon.fidelity(src, out, "opaque")["de_mean"]
        fixed = rmcommon.colour_fix(src, out, "opaque", (False, False), sigma=2.0)
        self.assertGreater(before, 5.0)
        self.assertLess(rmcommon.fidelity(src, fixed, "opaque")["de_mean"], 1.5)

        def high_pass(x):  # what is left inside each 4 x 4 block
            return x[..., 0] - rmcommon.nearest(rmcommon.box_down(x[..., :1]), 4)[..., 0]
        self.assertGreater(np.corrcoef(high_pass(fixed).ravel(), high_pass(out).ravel())[0, 1], 0.95)

    def test_hard_edge_fraction(self):
        y, x = np.mgrid[0:32, 0:32]
        glow = np.exp(-((x - 16) ** 2 + (y - 16) ** 2) / 60.0).astype(np.float32)
        soft = np.dstack([glow, glow, glow, np.ones_like(glow)])
        hard = np.dstack([((x // 2 + y // 2) % 2).astype(np.float32)] * 3 + [np.ones_like(glow)])
        self.assertLess(rmcommon.hard_edge_fraction(soft, "opaque"), 0.01)
        self.assertGreater(rmcommon.hard_edge_fraction(hard, "opaque"), 0.4)


class PackBuildTests(unittest.TestCase):
    def test_chain_sizes_down_to_one_texel(self):
        l0 = np.ones((16, 64, 4), np.float32)
        chain = build_pack.mip_chain(l0, None, "opaque", (False, False))
        self.assertEqual([x.shape[:2] for x in chain], [(16, 64), (8, 32), (4, 16), (2, 8), (1, 4), (1, 2), (1, 1)])
        self.assertEqual(len(chain), build_pack.level_count(64, 16))

    def test_given_level_one_is_kept(self):
        l0 = np.zeros((8, 8, 4), np.float32)
        l1 = np.full((4, 4, 4), 0.25, np.float32)
        chain = build_pack.mip_chain(l0, l1, "blend", (False, False))
        self.assertIs(chain[1], l1)
        self.assertTrue(np.allclose(chain[2], 0.25))

    def test_wrap_axis_tiles_its_small_levels(self):
        # A ramp that wraps on s: the last column's level-2 value mixes in the
        # first columns (as the repeating texture does), clamped it does not.
        x = np.tile(np.linspace(0, 1, 32, dtype=np.float32), (32, 1))
        img = np.dstack([x, x, x, np.ones_like(x)])
        wrapped = build_pack.next_level(img, "opaque", (False, True))
        clamped = build_pack.next_level(img, "opaque", (False, False))
        self.assertLess(wrapped[0, -1, 0], clamped[0, -1, 0] - 0.01)
        self.assertAlmostEqual(float(wrapped[0, 3, 0]), float(clamped[0, 3, 0]), places=5)

    def test_cutout_keeps_binary_alpha_and_coverage(self):
        rng = np.random.default_rng(3)
        a = (rng.random((64, 64)) < 0.3).astype(np.float32)  # thin, scattered: a fixed 0.5 would thin it out
        img = np.dstack([np.full_like(a, 0.5)] * 3 + [a])
        chain = build_pack.mip_chain(img, None, "cutout", (True, True))
        for lv in chain[1:5]:
            self.assertTrue(set(np.unique(lv[..., 3])) <= {0.0, 1.0})
            self.assertAlmostEqual(build_pack.coverage(lv[..., 3]), build_pack.coverage(a), delta=0.03)
        fixed = build_pack.next_level(img, "cutout", (True, True))  # share None: threshold 0.5
        self.assertLess(build_pack.coverage(fixed[..., 3]), 0.2)
        # Mostly drawn with small holes (a grate): the holes must not close up.
        a = (rng.random((64, 64)) >= 0.14).astype(np.float32)
        img = np.dstack([np.full_like(a, 0.5)] * 3 + [a])
        for lv in build_pack.mip_chain(img, None, "cutout", (True, True))[1:5]:
            self.assertAlmostEqual(build_pack.coverage(lv[..., 3]), build_pack.coverage(a), delta=0.03)

    def test_colour_under_transparent_texels_does_not_darken(self):
        img = np.zeros((8, 8, 4), np.float32)
        img[..., :3] = (1.0, 0.5, 0.0)
        img[:, :4, 3] = 1.0  # the right half is transparent, its colour filled alike
        lv = build_pack.next_level(img, "blend", (False, False))
        self.assertTrue(np.allclose(lv[..., :3], (1.0, 0.5, 0.0), atol=1e-5))

    def test_rgba8_dds_round_trip(self):
        import tempfile
        rng = np.random.default_rng(5)
        levels = [rng.integers(0, 256, (max(1, 8 >> k), max(1, 32 >> k), 4), dtype=np.uint8) for k in range(6)]
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "t.dds")
            build_pack.write_rgba8_dds(path, levels)
            enc, w, h, n, off, size = build_pack.dds_header(path)
            self.assertEqual((enc, w, h, n, off), ("rgba8", 32, 8, 6, 128))
            self.assertEqual(size, off + sum(b for _, _, b in build_pack.level_sizes("rgba8", 32, 8, 6)))
            back = build_pack.read_rgba8_levels(path)
        for a, b in zip(levels, back):
            self.assertTrue(np.array_equal(a, b))

    def test_block_level_sizes(self):
        self.assertEqual(build_pack.level_sizes("bc7", 8, 4, 4), [(8, 4, 32), (4, 2, 16), (2, 1, 16), (1, 1, 16)])
        self.assertEqual(build_pack.level_sizes("bc1", 4, 4, 1), [(4, 4, 8)])


if __name__ == "__main__":
    unittest.main()
