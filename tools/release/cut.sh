#!/bin/bash
# The cut: one squash of the release branch's tree on origin/main, main and macos-build set to it, the
# annotated tag; with --push, the forks' snap64 branches first (a fresh clone must be able to fetch
# every gitlink), then main, macos-build and the tag, then the release workflow on macos-build.
#
#   tools/release/cut.sh <version> <squash message file> [--push]
#
# Without --push nothing leaves the machine. --push needs the go marker .claude/GO (made on the
# author's word, less than three hours old). Environment: SNAP64_RELEASE_BRANCH (default
# release-1.1.0), SNAP64_REPO (default from origin).
set -e
V="$1"; MSG="$2"; PUSH="$3"
test -n "$V" -a -f "$MSG" || { echo "usage: cut.sh <version> <squash message file> [--push]"; exit 2; }
cd "$(git rev-parse --show-toplevel)"
BR="${SNAP64_RELEASE_BRANCH:-release-1.1.0}"
test "$(git rev-parse --abbrev-ref HEAD)" = "$BR" || { echo "not on $BR"; exit 1; }
test -z "$(git status --short | grep -v '^?? zz_')" || { echo "the tree is not clean:"; git status --short | grep -v '^?? zz_'; exit 1; }
grep -q "VERSION $V " CMakeLists.txt || { echo "CMakeLists.txt does not say $V"; exit 1; }
grep -q "^## $V -- [0-9]" CHANGELOG.md || { echo "CHANGELOG.md's first entry is not dated for $V"; exit 1; }
git fetch -q origin
git merge-base --is-ancestor origin/macos-build origin/main || { echo "origin/macos-build is not behind origin/main"; exit 1; }
if git rev-parse -q --verify "refs/tags/v$V" >/dev/null; then
  NEW=$(git rev-parse "v$V^{commit}")
  echo "v$V exists: $(git rev-parse --short "$NEW")"
else
  NEW=$(TZ=UTC git commit-tree "$BR^{tree}" -p origin/main -F "$MSG")
  git branch -f main "$NEW"
  git branch -f macos-build "$NEW"
  TZ=UTC git tag -a "v$V" -m "Snap64 Recomp $V" "$NEW"
  echo "squash $(git rev-parse --short "$NEW") on main and macos-build; tag v$V"
fi
test "$(git rev-parse "$NEW^{tree}")" = "$(git rev-parse "$BR^{tree}")"
git log -1 --format='%h %s' "$NEW"
git diff --stat origin/main "$NEW" | tail -1
if [ "$PUSH" != "--push" ]; then echo "not pushed (no --push)"; exit 0; fi
# the go marker
GO=.claude/GO
test -f "$GO" || { echo "no go marker: nothing is pushed without the author's go (touch $GO on his word)"; exit 1; }
test $(( $(date +%s) - $(stat -c %Y "$GO" 2>/dev/null || stat -f %m "$GO") )) -lt 10800 || { echo "the go marker is older than three hours"; exit 1; }
# the forks' branches: every gitlink the squash records must be fetchable from its fork
for sub in lib/rt64/src/contrib/plume lib/rt64 lib/N64ModernRuntime/N64Recomp lib/N64ModernRuntime; do
  test -d "$sub/.git" -o -f "$sub/.git" || continue
  url=$(git -C "$sub" remote get-url origin 2>/dev/null || true)
  case "$url" in *JackandBeans/*) ;; *) continue ;; esac
  want=$(git -C "$(dirname "$sub")" ls-tree HEAD "$(basename "$sub")" 2>/dev/null | awk '{print $3}')
  test -n "$want" || want=$(git ls-tree HEAD "$sub" | awk '{print $3}')
  git -C "$sub" fetch -q origin snap64 || true
  if git -C "$sub" merge-base --is-ancestor "$want" origin/snap64 2>/dev/null; then
    echo "$sub: $want is on the fork's snap64 already"
  else
    git -C "$sub" push origin "$want:refs/heads/snap64"
    echo "$sub: pushed $want to snap64"
  fi
done
git push origin main macos-build "v$V"
REPO="${SNAP64_REPO:-$(git remote get-url origin | sed -E 's#.*github.com[:/]##; s#\.git$##')}"
gh workflow run release.yml --ref macos-build -R "$REPO"
sleep 20
gh run list --workflow=release.yml -R "$REPO" --limit 1 --json databaseId,status,headSha,createdAt --jq '.[0]'
