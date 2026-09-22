"""Synthetic contract tests for the optional private TerminalColors workflow.

No real catalog, snapshot, or network request is used. Fixtures are deliberately
small and generated in temporary directories so public tests cannot reproduce
the retained private 112-variant bundle.
"""

from __future__ import annotations

import html
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

# The importer under test is deliberately NOT published: `tools/terminalcolors.py` and its
# siblings are gitignored and excluded from the source tarball, because the 112-variant
# TerminalColors catalog is redistribution-blocked. This test therefore ships without its
# subject. Exit 77 (CTest's SKIP_RETURN_CODE) so a published tarball reports a clean SKIP
# instead of a spurious failure, while a local checkout — which has the importer — still
# runs the contract in full.
try:
    from terminalcolors import (  # noqa: E402
        ImportRefused,
        SOURCE_ORIGIN,
        import_catalog,
        validate_source_url,
        verify_bundle,
    )
    import terminalcolors as terminalcolors_module  # noqa: E402
except ModuleNotFoundError:
    print(
        "SKIP: tools/terminalcolors.py is not present. It is intentionally excluded from "
        "publication; this contract runs only in a local checkout.",
        file=sys.stderr,
    )
    raise SystemExit(77)


def _homepage(declared: int = 2) -> bytes:
    """Return one synthetic Astro island with a declared family count."""
    props = {
        "theme": {
            "id": "sample",
            "name": "Sample",
            "totalVariants": declared,
            "variants": [
                {"id": "dark", "name": "Dark"},
                {"id": "light", "name": "Light"},
            ],
        }
    }
    encoded = html.escape(json.dumps(props), quote=True)
    return f'<html><astro-island props="{encoded}"></astro-island></html>'.encode()


def _toml(background: str, foreground: str) -> bytes:
    """Return a complete synthetic Alacritty color record."""
    return (
        "[colors.primary]\n"
        f'background = "{background}"\n'
        f'foreground = "{foreground}"\n'
        "[colors.normal]\n"
        'black = "#111111"\n'
        'blue = "#334455"\n'
    ).encode()


class FixtureFetcher:
    """Deterministic in-memory fetcher with an optional forced failure."""

    def __init__(self, *, sitemap: bytes | None = None) -> None:
        """Build a complete two-variant synthetic site.

        Args:
            sitemap: Optional sitemap body; defaults to the homepage HTML soft
                fallback exercised by the real-source audit.
        """
        self.responses = {
            SOURCE_ORIGIN + "/": _homepage(),
            SOURCE_ORIGIN + "/sitemap.xml": sitemap or _homepage(),
            SOURCE_ORIGIN + "/themes/sample/": (
                '<a href="/themes/sample/dark/">Dark</a>'
                '<a href="/themes/sample/light/">Light</a>'
            ).encode(),
            SOURCE_ORIGIN + "/themes/sample/dark/": (
                '<a href="/downloads/alacritty/sample-dark.toml">download</a>'
            ).encode(),
            SOURCE_ORIGIN + "/themes/sample/light/": (
                '<a href="/downloads/alacritty/sample-light.toml">download</a>'
            ).encode(),
            SOURCE_ORIGIN + "/downloads/alacritty/sample-dark.toml": _toml(
                "#101112", "#f0f1f2"
            ),
            SOURCE_ORIGIN + "/downloads/alacritty/sample-light.toml": _toml(
                "#fafafa", "#202122"
            ),
        }
        self.fail_url: str | None = None

    def __call__(self, url: str) -> bytes:
        """Return one fixture response or raise a network-style refusal.

        Args:
            url: Exact canonical URL requested by the importer.

        Returns:
            Fixture bytes.

        Raises:
            ImportRefused: For a forced or unmapped request.
        """
        if url == self.fail_url:
            raise ImportRefused("synthetic network denial")
        try:
            return self.responses[url]
        except KeyError as error:
            raise ImportRefused(f"unexpected synthetic request: {url}") from error


class TerminalColorsTest(unittest.TestCase):
    """Exercise completeness, safety, hashes, reference equality, and rollback."""

    def setUp(self) -> None:
        """Create isolated XDG and source roots for one test."""
        self.temporary = Path(tempfile.mkdtemp(prefix="fanfold-terminalcolors-test-"))
        self.data_home = self.temporary / "data"
        self.source_root = self.temporary / "source"
        self.source_root.mkdir()
        self.output = self.data_home / "fanfold" / "theme-imports" / "candidate"

    def tearDown(self) -> None:
        """Remove only this test's temporary tree."""
        shutil.rmtree(self.temporary)

    def import_valid(self, fetcher: FixtureFetcher | None = None) -> dict[str, int]:
        """Import the standard complete fixture and return its inventory."""
        return import_catalog(
            self.output,
            fetch=fetcher or FixtureFetcher(),
            data_home=self.data_home,
            source_root=self.source_root,
            retrieved="2026-09-16T00:00:00+00:00",
        )

    def test_complete_soft_sitemap_import_matches_runtime_schema(self) -> None:
        """HTML sitemap fallback still requires complete family-page enumeration."""
        counts = self.import_valid()
        self.assertEqual(counts["families"], 1)
        self.assertEqual(counts["variants_imported"], 2)
        self.assertEqual(counts["named_role_tokens"], 8)
        verified = verify_bundle(self.output)
        self.assertEqual(
            verified,
            {key: value for key, value in counts.items() if key != "raw_snapshots"},
        )
        manifest = json.loads((self.output / "manifest.json").read_text())
        self.assertEqual(manifest["schemaVersion"], 1)
        self.assertFalse(manifest["enumeration"]["sitemapXmlValid"])
        self.assertEqual(set(path.name for path in self.output.iterdir()), {
            "catalog.json", "manifest.json", "NOTICES.md", "raw"
        })

    def test_network_denial_leaves_no_output_or_temporary_directory(self) -> None:
        """A first-request failure publishes nothing."""
        fetcher = FixtureFetcher()
        fetcher.fail_url = SOURCE_ORIGIN + "/"
        with self.assertRaisesRegex(ImportRefused, "network denial"):
            self.import_valid(fetcher)
        self.assertFalse(self.output.exists())
        parent = self.output.parent
        self.assertFalse(parent.exists() and any(parent.iterdir()))

    def test_partial_enumeration_is_rejected_atomically(self) -> None:
        """Declared totals cannot be satisfied by a truncated family page."""
        fetcher = FixtureFetcher()
        fetcher.responses[SOURCE_ORIGIN + "/themes/sample/"] = (
            '<a href="/themes/sample/dark/">Dark</a>'
        ).encode()
        with self.assertRaisesRegex(ImportRefused, "partial enumeration"):
            self.import_valid(fetcher)
        self.assertFalse(self.output.exists())

    def test_valid_xml_sitemap_must_equal_homepage_families(self) -> None:
        """A real sitemap cannot silently add or omit a declared family."""
        sitemap = (
            '<?xml version="1.0"?><urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">'
            '<url><loc>https://terminalcolors.com/themes/other/default/</loc></url></urlset>'
        ).encode()
        with self.assertRaisesRegex(ImportRefused, "family mismatch"):
            self.import_valid(FixtureFetcher(sitemap=sitemap))
        self.assertFalse(self.output.exists())

    def test_bad_toml_and_partial_failure_roll_back(self) -> None:
        """A late parse failure removes already-fetched temporary snapshots."""
        fetcher = FixtureFetcher()
        fetcher.responses[
            SOURCE_ORIGIN + "/downloads/alacritty/sample-light.toml"
        ] = b"not = [valid"
        with self.assertRaisesRegex(ImportRefused, "bad TOML"):
            self.import_valid(fetcher)
        self.assertFalse(self.output.exists())
        self.assertFalse(any(self.output.parent.glob(".candidate.tmp-*")))

    def test_unsafe_urls_and_path_traversal_are_rejected(self) -> None:
        """Origin, credentials, private ports, and traversal stay outside fetches."""
        for unsafe in (
            "http://terminalcolors.com/themes/sample/",
            "https://example.invalid/themes/sample/",
            "https://user@terminalcolors.com/themes/sample/",
            "https://terminalcolors.com:444/themes/sample/",
            "https://terminalcolors.com/themes/../private/",
            "https://terminalcolors.com/themes/%2e%2e/private/",
            "https://terminalcolors.com/themes/sample%2Fprivate/",
            "https://terminalcolors.com/themes/sample%5cprivate/",
            "https://terminalcolors.com/themes/sample\\private/",
        ):
            with self.subTest(url=unsafe):
                with self.assertRaises(ImportRefused):
                    validate_source_url(unsafe)
        fetcher = FixtureFetcher()
        fetcher.responses[SOURCE_ORIGIN + "/themes/sample/dark/"] = (
            '<a href="https://example.invalid/downloads/alacritty/dark.toml">bad</a>'
        ).encode()
        with self.assertRaises(ImportRefused):
            self.import_valid(fetcher)

    def test_existing_or_non_private_destination_is_refused(self) -> None:
        """Imports never overwrite a catalog or write into arbitrary paths."""
        self.output.mkdir(parents=True)
        sentinel = self.output / "keep.txt"
        sentinel.write_text("keep")
        with self.assertRaisesRegex(ImportRefused, "already exists"):
            self.import_valid()
        self.assertEqual(sentinel.read_text(), "keep")
        outside = self.temporary / "outside"
        with self.assertRaisesRegex(ImportRefused, "must be below"):
            import_catalog(
                outside,
                fetch=FixtureFetcher(),
                data_home=self.data_home,
                source_root=self.source_root,
            )

    def test_atomic_publish_does_not_replace_concurrent_empty_directory(self) -> None:
        """A destination winning the final race remains untouched and unpublished."""
        actual_rename = terminalcolors_module._rename_noreplace

        def race(source: Path, destination: Path) -> None:
            """Create the competing destination immediately before the real rename."""
            destination.mkdir()
            actual_rename(source, destination)

        with mock.patch.object(terminalcolors_module, "_rename_noreplace", side_effect=race):
            with self.assertRaisesRegex(ImportRefused, "appeared during acquisition"):
                self.import_valid()
        self.assertTrue(self.output.is_dir())
        self.assertEqual(list(self.output.iterdir()), [])
        self.assertFalse(any(self.output.parent.glob(".candidate.tmp-*")))

    def test_duplicate_download_identity_is_rejected(self) -> None:
        """Two variants may not alias one downloaded source record."""
        fetcher = FixtureFetcher()
        fetcher.responses[SOURCE_ORIGIN + "/themes/sample/light/"] = (
            '<a href="/downloads/alacritty/sample-dark.toml">duplicate</a>'
        ).encode()
        with self.assertRaisesRegex(ImportRefused, "duplicate download"):
            self.import_valid(fetcher)
        self.assertFalse(self.output.exists())

    def test_verifier_rejects_corruption_and_unsupported_schema(self) -> None:
        """Stored bytes and schema versions are fail-closed."""
        self.import_valid()
        raw = self.output / "raw" / "themes" / "sample-dark.toml"
        raw.write_bytes(raw.read_bytes() + b"\n")
        with self.assertRaisesRegex(ImportRefused, "snapshot hash/size mismatch"):
            verify_bundle(self.output)

        shutil.rmtree(self.output)
        self.import_valid()
        manifest_path = self.output / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["schemaVersion"] = 999
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ImportRefused, "unsupported"):
            verify_bundle(self.output)

    def test_reference_comparison_requires_exact_hash_and_role_projection(self) -> None:
        """Counts alone cannot satisfy private reference comparison."""
        self.import_valid()
        reference = self.temporary / "reference"
        shutil.copytree(self.output, reference)
        self.assertEqual(verify_bundle(self.output, reference)["variants_imported"], 2)
        catalog_path = reference / "catalog.json"
        catalog = json.loads(catalog_path.read_text())
        catalog["families"]["sample"]["variants"]["dark"]["roles"]["normal"]["blue"] = "#abcdef"
        catalog_path.write_text(json.dumps(catalog))
        with self.assertRaisesRegex(ImportRefused, "differs from reference"):
            verify_bundle(self.output, reference)


if __name__ == "__main__":
    unittest.main()
