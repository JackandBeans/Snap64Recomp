#!/bin/bash
# After a release: snap-port takes main as an ancestor and keeps its own tree (-s ours), then both local
# branches get the next release's number. Nothing here is pushed.
#
#   tools/release/aftercare.sh <released version> <next version>
#
# Environment: SNAP64_RELEASE_BRANCH (default release-1.1.0), SNAP64_PORT_BRANCH (default snap-port).
set -e
V="$1"; NEXT="$2"
test -n "$V" -a -n "$NEXT" || { echo "usage: aftercare.sh <released version> <next version>"; exit 2; }
cd "$(git rev-parse --show-toplevel)"
BR="${SNAP64_RELEASE_BRANCH:-release-1.1.0}"
PORT="${SNAP64_PORT_BRANCH:-snap-port}"
test "$(git rev-parse --abbrev-ref HEAD)" = "$BR" || { echo "not on $BR"; exit 1; }
git fetch -q origin
git merge-base --is-ancestor "v$V^{commit}" origin/main || { echo "v$V is not on origin/main"; exit 1; }
T=$(mktemp -d)
printf 'Merge main into %s; the branch stands\n\nCo-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>\n' "$PORT" > "$T/merge.txt"
printf 'The version is %s\n\nThe next release'"'"'s number, so test builds say what they are.\n\nCo-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>\n' "$NEXT" > "$T/bump.txt"
WT="$T/wt"
git worktree add -q "$WT" "$PORT"
(
  cd "$WT"
  before=$(git rev-parse 'HEAD^{tree}')
  TZ=UTC git merge -q -s ours origin/main -F "$T/merge.txt"
  test "$(git rev-parse 'HEAD^{tree}')" = "$before"
  echo "$PORT merge: $(git log --oneline -1) | tree unchanged"
  sed -i "s/^project(Snap64Recomp VERSION $V LANGUAGES C CXX)\$/project(Snap64Recomp VERSION $NEXT LANGUAGES C CXX)/" CMakeLists.txt
  git diff --stat | tail -1
  git add CMakeLists.txt
  TZ=UTC git commit -q -F "$T/bump.txt"
  echo "$PORT: $(git log --oneline -1)"
  git rev-parse HEAD > "$T/bump.sha"
)
git worktree remove "$WT"
TZ=UTC git cherry-pick -x "$(cat "$T/bump.sha")" > /dev/null
echo "$BR: $(git log --oneline -1)"
grep -n "VERSION 1\." CMakeLists.txt
echo "main is an ancestor of $PORT: $(git merge-base --is-ancestor origin/main "$PORT" && echo yes)"
git worktree list
rm -rf "$T"
