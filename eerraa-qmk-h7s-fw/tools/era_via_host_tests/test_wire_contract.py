#!/usr/bin/env python3
"""Positive inventory and intentional negative wire-contract fixtures."""

import re
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import era_wire_contract as wire


class WireContractTests(unittest.TestCase):
    def mutation(self, rel, change):
        tree = wire.Tree()
        original = tree.read
        changed = change(original(rel))
        self.assertNotEqual(changed, original(rel), "fixture did not mutate its input")
        with patch.object(tree, "read", side_effect=lambda p: changed if p == rel else original(p)):
            errors = wire.check_all(tree)
        self.assertTrue(errors)
        return "\n".join(errors)

    def test_current_contract(self):
        self.assertEqual(wire.check_all(wire.Tree()), [])

    def test_wrong_official_value(self):
        self.assertIn("via_command_id name/value set", self.mutation(wire.VIA_H, lambda text:
            re.sub(r"(id_get_protocol_version\s*=\s*)0x01", r"\g<1>0x02", text)))

    def test_missing_official_name(self):
        self.assertIn("name/value set", self.mutation(wire.VIA_H, lambda text:
            re.sub(r"^\s*id_device_indication\s*=.*\n", "", text, flags=re.M)))

    def test_extra_official_name(self):
        self.assertIn("via_channel_id name/value set", self.mutation(wire.VIA_H, lambda text:
            text.replace("enum via_channel_id {", "enum via_channel_id {\n id_extra = 99,")))

    def test_wrong_versions(self):
        for rel, alias in ((wire.VIA_H, "VIA_PROTOCOL_VERSION"), (wire.KEYCODES_H, "QMK_KEYCODES_VERSION_BCD")):
            with self.subTest(alias=alias):
                self.assertIn(alias, self.mutation(rel, lambda text: re.sub(
                    r"(#define " + alias + r") [^\n]+", r"\1 0", text)))

    def test_literal_alias(self):
        rel = wire.QMK + "port/era_state_sync.h"
        self.assertIn("literal redefinition", self.mutation(rel, lambda text:
            text.replace("ERA_WIRE_STATE_SYNC_SELECTORS_CURRENT", "(0x80U)")))

    def test_implicit_enum_alias(self):
        self.assertIn("implicit number", self.mutation(wire.VIA_H, lambda text: re.sub(
            r"id_qmk_mousekey_cursor_min_speed\s*=\s*ERA_WIRE_MOUSEKEY_CURSOR_MIN_SPEED",
            "id_qmk_mousekey_cursor_min_speed", text)))

    def test_generated_name_redefinition(self):
        rel = wire.QMK + "port/era_state_sync.h"
        self.assertIn("redefines a generated", self.mutation(rel, lambda text:
            text + "\n#define ERA_WIRE_STATE_SYNC_SELECTORS_CURRENT 0x80U\n"))

    def test_era_extension_is_not_official(self):
        self.assertIn("via_keyboard_value_id name/value set", self.mutation(wire.VIA_H, lambda text:
            text.replace("enum via_keyboard_value_id {",
                         "enum via_keyboard_value_id {\n id_private = ERA_WIRE_STATE_SYNC_SELECTORS_CURRENT,")))

    def test_representation_is_not_a_conflict(self):
        tree = wire.Tree()
        original = tree.read
        changed = re.sub(r"(id_get_protocol_version\s*=\s*)0x01", r"\g<1>1", original(wire.VIA_H))
        with patch.object(tree, "read", side_effect=lambda p: changed if p == wire.VIA_H else original(p)):
            self.assertEqual(wire.check_all(tree), [])


if __name__ == "__main__":
    unittest.main()
