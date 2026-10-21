# Table layout

Table cells use normal white space in the application's editor theme. Prose wraps
at normal word boundaries without changing the Markdown or inserting line breaks.
The existing automatic column sizing, block table display and overflow remain:
short tables retain their natural cell sizes, and a table whose unbreakable
content is wider than the writing area still scrolls horizontally. No fixed
layout, forced word splitting, column-specific rules or vendor changes are needed.
All existing paint rules are retained.

## Browser regression

The optional `fanfold-table-wrapping` CTest uses Python Playwright and Chromium.
Install them into an isolated project environment, not the system Python:

    python -m venv browser-venv
    browser-venv/bin/pip install playwright
    PLAYWRIGHT_BROWSERS_PATH="$PWD/browsers" browser-venv/bin/playwright install chromium
    PLAYWRIGHT_BROWSERS_PATH="$PWD/browsers" browser-venv/bin/python tests/fanfold/test_table_wrapping.py --evidence evidence/tables

Configure CMake with `-DPython3_EXECUTABLE=<absolute path to browser-venv/bin/python>`
and give CTest the same `PLAYWRIGHT_BROWSERS_PATH`. Without Playwright the test
reports a visible skip (77); with Playwright but without its browser it fails.

The test loads the real vendored renderer and styles and the application's theme
on a short-lived loopback server. It covers IR editing, WYSIWYG editing and static
preview. Editor viewport widths 582 and 422 correspond to default 640 and minimum
480 cards minus NoteCard's 58-pixel inset; the theme adds its normal text padding.
For each surface and width it checks a four-column prose table, a short table and
unbreakable oversized content. It measures text line boxes, actual overflow and
scroll movement, and compares computed paint and short-table geometry with nowrap.
It retains before/after screenshots and JSON measurements. `--baseline-css` can
supply the exact prior stylesheet for a same-DOM comparison instead of restoring
only the original nowrap rule.

This is browser-layout evidence, not full application or desktop acceptance.
The harness does not instantiate the native file store or write a note file.
Before release, verify the packaged application in the nested compositor with a
scratch library at both card widths, inspect the real rendered and focused editor
states, and compare fixture bytes/mtime before and after opening and closing.
Do not test against the user's notes. Package publication and installation follow
the existing review and APT lane; there is no data migration.
