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
        self.assertEqual(rmcommon.wrap_axes(row(tiles="s", uv_edges="t")), (True, True))
        self.assertEqual(rmcommon.wrap_axes(row(tiles="no", uv_edges="s")), (False, True))
        self.assertEqual(rmcommon.wrap_axes(row()), (False, False))


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


if __name__ == "__main__":
    unittest.main()
