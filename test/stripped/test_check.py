"""librarian/check.py's pure parts: the quote check and the A/B mapping. (STRIP1/9, inferred)"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "librarian"))
import check  # noqa: E402

NOTE = "The fix was defeated by padding:\ncolumn 0 of a CHUNK is not column 0 of a LINE."


class QuoteCheck(unittest.TestCase):
    def test_strip1_9_a_quote_is_found_up_to_whitespace(self):
        self.assertTrue(check.quote_found("defeated by padding: column 0", NOTE))

    def test_strip1_9_an_altered_or_empty_quote_is_not_found(self):
        self.assertFalse(check.quote_found("defeated by padding: column 1", NOTE))
        self.assertFalse(check.quote_found("  ", NOTE))


class Mapping(unittest.TestCase):
    def test_strip1_9_direction_maps_back_through_the_shown_order(self):
        self.assertEqual(check.as_row_relation("A corrects B", "ba"), "b corrects a")
        self.assertEqual(check.as_row_relation("B corrects A", "ba"), "a corrects b")
        self.assertEqual(check.as_row_relation("A corrects B", "ab"), "a corrects b")
        self.assertEqual(check.as_row_relation("same", "ba"), "same")

    def test_strip1_9_review_rows_parse_with_their_lines(self):
        rows = check.parse_review("| 7/8 | corrects | 3253 `X` is NOT | 3660 Fossil's | |\n| junk |")
        self.assertEqual([(r["runs"], r["kind"], r["a"], r["b"]) for r in rows], [("7/8", "corrects", 3253, 3660)])


if __name__ == "__main__":
    unittest.main()
