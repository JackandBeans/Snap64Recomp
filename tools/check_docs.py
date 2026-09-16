#!/usr/bin/env python3
"""Check every relative link and anchor in the repository's Markdown.

The documentation is spread over the README, the pages under docs/ and the
notes beside the code, and a section that moves takes its anchors with it.
This walks every tracked .md file, collects its headings the way GitHub
turns them into anchors, and checks each relative link: the file must exist,
and if the link names an anchor, the target file must have that heading.
Links to the web are not fetched; a build with no network has to be able to
run this, and a dead web link is a different kind of fault from a broken
page. Exit status 1 with a list if anything fails, so it can run in CI.

    python tools/check_docs.py            # the whole tree
    python tools/check_docs.py README.md  # named files only
"""
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def tracked_markdown():
    out = subprocess.check_output(["git", "ls-files", "*.md", "**/*.md"], cwd=ROOT)
    files = sorted(set(out.decode("utf-8").split()))
    return [f for f in files if not f.startswith("lib/")]


def slug(heading):
    """GitHub's anchor for a heading: lowercase, punctuation dropped, spaces to hyphens."""
    text = heading.strip()
    text = re.sub(r"<[^>]+>", "", text)                 # inline HTML
    text = re.sub(r"`([^`]*)`", r"\1", text)            # code spans keep their text
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)  # links keep their text
    text = text.lower()
    text = re.sub(r"[^\w\- ]", "", text)                # keep letters, digits, _, -, space
    text = text.replace(" ", "-")
    return text


def strip_code(text):
    text = re.sub(r"```.*?```", "", text, flags=re.S)
    text = re.sub(r"`[^`\n]*`", "", text)
    return text


def headings(text):
    seen = {}
    anchors = set()
    for line in text.split("\n"):
        m = re.match(r"^\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$", line)
        if not m:
            continue
        s = slug(m.group(2))
        n = seen.get(s, 0)
        seen[s] = n + 1
        anchors.add(s if n == 0 else "%s-%d" % (s, n))
    # <a name="..."> and id="..." anchors written by hand.
    for m in re.finditer(r'(?:name|id)="([^"]+)"', text):
        anchors.add(m.group(1))
    return anchors


LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
HTML = re.compile(r'(?:href|src)="([^"]+)"')


def links(text):
    body = strip_code(text)
    for m in LINK.finditer(body):
        yield m.group(1)
    for m in HTML.finditer(body):
        yield m.group(1)


def main(argv):
    files = argv or tracked_markdown()
    docs = {}
    for f in files:
        p = os.path.join(ROOT, f)
        docs[f.replace("\\", "/")] = io.open(p, encoding="utf-8").read()
    anchors = {f: headings(t) for f, t in docs.items()}

    failures = []
    checked = 0
    for f, text in docs.items():
        base = os.path.dirname(f)
        for target in links(text):
            if re.match(r"^[a-z][a-z0-9+.-]*:", target) or target.startswith("//"):
                continue  # the web, mailto, and the like
            if target.startswith("#"):
                path, anchor = f, target[1:]
            else:
                path, _, anchor = target.partition("#")
                path = os.path.normpath(os.path.join(base, path)).replace("\\", "/")
            checked += 1
            full = os.path.join(ROOT, path)
            if not os.path.exists(full):
                failures.append("%s: %s -> no such file %s" % (f, target, path))
                continue
            if anchor:
                if path not in anchors:
                    if path.endswith(".md"):
                        anchors[path] = headings(io.open(full, encoding="utf-8").read())
                    else:
                        continue  # an anchor into a non-Markdown file is not ours to judge
                if anchor not in anchors[path]:
                    failures.append("%s: %s -> %s has no heading for #%s" % (f, target, path, anchor))

    for line in failures:
        print(line)
    print("%d files, %d relative links checked, %d broken" % (len(docs), checked, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
