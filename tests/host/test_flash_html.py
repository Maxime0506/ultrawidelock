#!/usr/bin/env python3
"""Unit tests for scripts/flash_html.py (the release FLASH.md -> FLASH.html
renderer).

Needs the python-markdown package (the renderer's one dependency); every test
skips cleanly when it is missing. The committed-output drift check additionally
requires markdown==3.8 — the pinned version the committed FLASH.html files were
rendered with — because other versions may legitimately render different bytes.

Run directly or via tests/host/run.sh.
"""

import glob
import os
import pathlib
import shutil
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

try:
    import markdown

    HAVE_MD = True
    MD_38 = markdown.__version__ == "3.8"
except ImportError:  # tests skip; run.sh treats skips as skips, not passes
    HAVE_MD = False
    MD_38 = False

FIXTURE = """# Flash the demo lock

Ten minutes, one cable.

## 1. What you need

| Part | Why |
|---|---|
| nRF5340 DK | the lock |

## 2. Flash it

```bash
make flash
```

> **Warning**: erase first.
"""


@unittest.skipUnless(HAVE_MD, "python-markdown not installed")
class RenderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import flash_html

        cls.flash_html = flash_html
        cls.tmp = tempfile.mkdtemp(prefix="flash_html_test.")
        cls.src = pathlib.Path(cls.tmp, "bundle", "FLASH.md")
        cls.src.parent.mkdir()
        cls.src.write_text(FIXTURE, encoding="utf-8")
        cls.out = cls.flash_html.render(cls.src)
        cls.html = cls.out.read_text(encoding="utf-8")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def test_output_path(self):
        self.assertEqual(self.out, self.src.with_suffix(".html"))

    def test_title_from_first_heading(self):
        self.assertIn("<title>Flash the demo lock</title>", self.html)

    def test_h2_relabeled(self):
        # "## 1. What you need" -> "1 · What you need" section label
        self.assertIn("<h2>1 · What you need</h2>", self.html)
        self.assertIn("<h2>2 · Flash it</h2>", self.html)
        self.assertNotIn("<h2>1. ", self.html)

    def test_table_wrapped_for_phones(self):
        self.assertIn('<div class="tablewrap"><table>', self.html)
        self.assertIn("</table></div>", self.html)

    def test_fenced_code_and_blockquote(self):
        self.assertIn("make flash", self.html)
        self.assertIn("<blockquote>", self.html)

    def test_generated_stamp(self):
        self.assertIn("edit the .md, not this", self.html)

    def test_deterministic(self):
        again = self.flash_html.render(self.src).read_text(encoding="utf-8")
        self.assertEqual(self.html, again)

    def test_title_fallback_is_parent_dir(self):
        src = pathlib.Path(self.tmp, "bundle", "NOHEAD.md")
        src.write_text("no top heading here\n", encoding="utf-8")
        html = self.flash_html.render(src).read_text(encoding="utf-8")
        self.assertIn("<title>bundle</title>", html)


@unittest.skipUnless(HAVE_MD, "python-markdown not installed")
class MainTests(unittest.TestCase):
    def run_main(self, argv):
        import contextlib
        import io

        import flash_html

        old = sys.argv
        sys.argv = argv
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out):
                rc = flash_html.main()
        finally:
            sys.argv = old
        return rc, out.getvalue()

    def test_usage(self):
        rc, out = self.run_main(["flash_html.py"])
        self.assertEqual(rc, 2)
        self.assertIn("FLASH.md", out)

    def test_renders_arguments(self):
        with tempfile.TemporaryDirectory(prefix="flash_main.") as tmp:
            src = pathlib.Path(tmp, "FLASH.md")
            src.write_text(FIXTURE, encoding="utf-8")
            rc, out = self.run_main(["flash_html.py", str(src)])
        self.assertEqual(rc, 0)
        self.assertIn("FLASH.html", out)


@unittest.skipUnless(MD_38, "committed FLASH.html was rendered with markdown==3.8")
class RegenDriftTests(unittest.TestCase):
    """The committed FLASH.html next to each FLASH.md must be its current
    render — catches an edited .md whose .html regen was forgotten."""

    def test_committed_html_up_to_date(self):
        import flash_html

        mds = sorted(glob.glob(os.path.join(ROOT, "release", "*", "FLASH.md")))
        self.assertTrue(mds, "no release/*/FLASH.md found")
        for md in mds:
            committed = pathlib.Path(md).with_suffix(".html")
            with tempfile.TemporaryDirectory(prefix="flash_regen.") as tmp:
                # render() writes next to its source; work on a copy
                work = pathlib.Path(tmp, os.path.basename(os.path.dirname(md)), "FLASH.md")
                work.parent.mkdir()
                shutil.copyfile(md, work)
                fresh = flash_html.render(work).read_text(encoding="utf-8")
            self.assertEqual(
                fresh,
                committed.read_text(encoding="utf-8"),
                f"{committed} is stale — regenerate: python3 scripts/flash_html.py {md}",
            )


if __name__ == "__main__":
    unittest.main(verbosity=1)
