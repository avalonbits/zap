#!/usr/bin/env python3
"""Checks the links in the project's documents.

Two kinds:

* code links -- `[`name()`](../src/zap.c#L123)` -- where the symbol named in the
  link text has to appear on the line the link points at;
* anchor links -- `[text](#a-heading)` -- where the anchor has to be a heading
  in the same document. Pass --anchors for these.

Original description follows.

Checks that every code link in the design document still points at its symbol.

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


def slug(text):
    """GitHub's heading anchor: lowercased, punctuation dropped, spaces hyphened."""
    t = text.strip().lower().replace('`', '')
    t = re.sub(r'[*_]', '', t)
    t = re.sub(r'[^\w\s-]', '', t)
    return t.replace(' ', '-')


def check_anchors(path):
    bad = 0
    heads = set()
    lines = open(path).read().split('\n')
    for line in lines:
        if line.startswith('#'):
            heads.add(slug(line.lstrip('#')))
    for m in re.finditer(r'\]\(#([^)]+)\)', '\n'.join(lines)):
        if m.group(1) not in heads:
            print('%s: #%s is not a heading in this document' % (path, m.group(1)))
            bad += 1
    return bad


def main(argv):
    args = argv[1:]
    if args and args[0] == '--anchors':
        docs = args[1:] or ['ez80_advanced_optimization_guide.md']
        return 1 if sum(check_anchors(d) for d in docs) else 0
    docs = args or ['docs/DESIGN.md']
    return 1 if sum(check(d) for d in docs) else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
