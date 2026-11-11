#!/usr/bin/env python3
"""Regression tests for the independently frozen ABI and fail-closed generation."""
import copy
import json
import unittest
from unittest.mock import patch
import generate_api as generator


class GenerationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.definition = json.loads(generator.DEFINITION.read_text())
        self.frozen = json.loads(generator.FROZEN.read_text())

    def test_existing_outputs_match(self) -> None:
        for path, content in generator.outputs(self.definition, self.frozen).items():
            self.assertEqual(path.read_text(), content, str(path))

    def test_frozen_signature_order_and_ownership_cannot_change(self) -> None:
        for mutation in ("signature", "order", "ownership", "removal"):
            definition = copy.deepcopy(self.definition)
            if mutation == "signature":
                definition["functions"][0]["returns"] = "long"
            elif mutation == "order":
                definition["functions"][:2] = reversed(definition["functions"][:2])
            elif mutation == "ownership":
                definition["functions"][0]["result"] = "gc"
            else:
                del definition["functions"][0]
            with self.assertRaisesRegex(ValueError, "frozen ABI v1"):
                generator.outputs(definition, self.frozen)

    def test_new_macro_is_a_generation_error(self) -> None:
        inventory = generator.macro_inventory()
        inventory["UnexpectedDisplayAccessor"] = ("dpy", "((_XPrivDisplay)dpy)->fd")
        with patch.object(generator, "macro_inventory", return_value=inventory):
            with self.assertRaisesRegex(ValueError, "unhandled Xlib macros"):
                generator.outputs(self.definition, self.frozen)

    def test_append_preserves_old_sdk_bytes(self) -> None:
        definition = copy.deepcopy(self.definition)
        definition["version"] = 3
        definition["functions"].append({
            "name": "wmbFutureFeature", "returns": "uint32_t", "result": "u",
            "params": [], "since": 3,
        })
        before = generator.outputs(self.definition, self.frozen)
        after = generator.outputs(definition, self.frozen)
        for path, content in before.items():
            if "/libX11-app/v1/" in str(path):
                self.assertEqual(after[path], content)
        current = after[generator.ROOT / "transport/api/wmb_api.h"]
        self.assertIn("WMB_API_SLOT_COUNT 124u", current)
        self.assertLess(current.index("wmbApiRevision)"), current.index("wmbFutureFeature)"))

    def test_unknown_rpc_shape_is_not_advertised(self) -> None:
        definition = copy.deepcopy(self.definition)
        definition["functions"][-1]["params"] = [
            {"type": "void*", "kind": "unsupported_object"}
        ]
        with self.assertRaisesRegex(ValueError, "unhandled proxy parameter"):
            generator.outputs(definition, self.frozen)

    def test_oversized_argument_vector_is_rejected(self) -> None:
        definition = copy.deepcopy(self.definition)
        definition["functions"][-1]["params"] = [{"type": "int", "kind": "i"}] * 17
        with self.assertRaisesRegex(ValueError, "parameter limit"):
            generator.outputs(definition, self.frozen)


if __name__ == "__main__":
    unittest.main()
