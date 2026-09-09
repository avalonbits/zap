#!/usr/bin/env python3
"""Checks that every code link in the design document still points at its symbol.

docs/DESIGN.md links to the definition of what it describes -- `[`name()`](../src/zap.c#L123)`
-- and a line number goes stale the moment the source moves. This reads every
such link and requires the symbol named in the link text to appear on the line
the link points at.

    python3 tools/check_doc_links.py [docs/DESIGN.md ...]

Prints one line per broken link and exits non-zero if there were any.
"""
import re
import sys

LINK = re.compile(r'\[`?([^`\]]+)`?\]\(\.\./([^)#]+)#L(\d+)\)')

# Link text that is prose rather than a symbol, and what to look for instead.
ALIAS = {'one table': 'zap_err_text'}


def check(path):
    bad = 0
    doc = open(path).read()
    for m in LINK.finditer(doc):
        text, target, line = m.group(1), m.group(2), int(m.group(3))
        name = ALIAS.get(text, text).replace('()', '').strip()
        try:
            src = open(target).read().split('\n')[line - 1]
        except (OSError, IndexError):
            print('%s: %s#L%d does not exist' % (path, target, line))
            bad += 1
            continue
        if name not in src:
            print('%s: %s#L%d is not %s -- %s'
                  % (path, target, line, text, src.strip()[:50]))
            bad += 1
    return bad


def main(argv):
    docs = argv[1:] or ['docs/DESIGN.md']
    return 1 if sum(check(d) for d in docs) else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
