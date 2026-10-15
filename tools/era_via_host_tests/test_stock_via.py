#!/usr/bin/env python3
"""Inventory and negative fixtures for firmware-local STOCK VIA definitions."""

import json
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import era_stock_via as stock

BOARD = "era/sirind/brick60"
FOLDER = stock.BOARD_ROOT + "/sirind/brick60"
DEFINITION = FOLDER + "/json/BRICK60-H7S-VIA.JSON"
CONFIG = FOLDER + "/config.h"


class StockViaTests(unittest.TestCase):
    def mutation(self, rel, change):
        tree = stock.Tree()
        original = tree.read
        changed = change(original(rel))
        with patch.object(tree, "read", side_effect=lambda p: changed if p == rel else original(p)):
            _, errors = stock.check_all(tree)
        return "\n".join(errors)

    def definition_change(self, change):
        def edit(text):
            value = json.loads(text)
            change(value)
            return json.dumps(value)
        return self.mutation(DEFINITION, edit)

    def test_current_stock(self):
        count, errors = stock.check_all(stock.Tree())
        self.assertGreater(count, 0)
        self.assertEqual(errors, [])

    def test_forbidden_qmk_lighting(self):
        self.assertIn("forbidden qmk_lighting", self.definition_change(
            lambda d: d["keycodes"].append("qmk_lighting")))

    def test_wrong_lighting_module(self):
        self.assertIn("lighting module", self.definition_change(
            lambda d: d.update(keycodes=["qmk_backlight_keycodes"])))

    def test_wrong_product_id(self):
        self.assertIn("productId", self.definition_change(
            lambda d: d.update(productId="0xB00C")))

    def test_wrong_vendor_id(self):
        self.assertIn("vendorId", self.definition_change(
            lambda d: d.update(vendorId="0x0000")))

    def test_wrong_matrix_size(self):
        for axis in ("rows", "cols"):
            with self.subTest(axis=axis):
                self.assertIn("matrix", self.definition_change(
                    lambda d: d["matrix"].update({axis: d["matrix"][axis] + 1})))

    def test_unknown_feature_combination_names_board(self):
        errors = self.mutation(CONFIG, lambda text: text + "\n#define RGB_MATRIX_ENABLE\n")
        self.assertIn(BOARD + ": unsupported lighting feature combination", errors)

    def test_malformed_json(self):
        self.assertIn(DEFINITION, self.mutation(DEFINITION, lambda _: "{"))

    def test_required_module_mapping(self):
        for features, expected in (
            ((), None), (("backlight",), "qmk_backlight_keycodes"),
            (("rgblight",), "qmk_rgblight_keycodes"),
            (("backlight", "rgblight"), "qmk_backlight_rgblight_keycodes"),
            (("rgb_matrix",), "qmk_rgb_matrix_keycodes"),
        ):
            self.assertEqual(stock.lighting_module("fixture", features), expected)

    def test_config_change_is_not_hidden(self):
        errors = self.mutation(CONFIG, lambda text: text.replace(
            "USB_PID                     0x000C", "USB_PID                     0x000D"))
        self.assertIn("productId", errors)
        self.assertIn("0x000D", errors)

    def test_no_indicator_only_board(self):
        self.assertEqual(stock.INDICATOR_ONLY, {})
        tree = stock.Tree()
        for rel in tree.definitions():
            self.assertEqual(stock.board_facts(tree, rel)[1], {"rgblight"})

    def test_conditional_facts_refused(self):
        errors = self.mutation(CONFIG, lambda text: text + "\n#if 0\n#define USB_PID 1\n#endif\n")
        self.assertIn("conditional USB_PID", errors)


if __name__ == "__main__":
    unittest.main()
