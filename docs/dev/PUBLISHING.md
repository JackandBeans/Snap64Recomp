# Publishing 1.0.0 on GitHub, step by step

Written for a first release on a GitHub account made for the project.
Everything below happens on your machine and in a browser; the port's
build is already done and tagged locally. "Before pushing" records what
is done and what is yours; everything from "Create the repository" on
is yours to press.

## Before pushing, once

1. **Identity: done.** The project's address is snap64recomp@gmail.com and
   the author name is Jack & Beans. On 2026-09-04 every commit was rewritten
   to that name and address with `git filter-repo --mailmap` (the tag
   `v1.0.0` re-pointed with them, the Co-Authored-By trailers untouched),
   and `git config user.name` / `user.email` set for the commits to come.
   A second pass the same day scrubbed the author's Windows username
   from every historical file version that carried it and dropped two
   unused dumps from the first commit; `git grep -i <username>
   $(git rev-list --all)` is the check, and it finds nothing.
   Create the GitHub account with that address; the username is yours to
   choose (the walkthrough below writes `<your-username>`).

2. **Screenshots: done.** Thirteen are in `docs/screenshots/` (named 01 to 13),
   cropped to the game area; the README shows six. For the release page,
   drag `01-title.png`, `04-beach.png` and `13-printer-stars.png` into the
   description.
3. **Try the archive on a machine that has never built the port**, with a
   ROM: `build-win/Snap64Recomp-1.0.0-win64.zip`. It should start, find its
   folder, play, and print. This is the one check the suite cannot do.

## Create the repository

1. github.com > "+" > New repository. Name: `Snap64Recomp` (the name of
   the executable and the archive). Public. **Do not** tick "Add a README",
   "Add .gitignore" or "Choose a license": the repository already has all
   three, and a generated one would conflict.
2. Description: `Native Windows port of Pokémon Snap (N64) by static
   recompilation. You supply your own cartridge dump; nothing of the game
   is included.` Topics: `n64`, `recompilation`, `pokemon-snap`, `rt64`,
   `n64recomp`.
3. Settings > General: leave Issues on (bug reports land there; the README
   points people to it, and `.github/ISSUE_TEMPLATE/bug_report.yml` is the
   form they get). Discussions optional. Wiki off.
4. Settings > General > Social preview: upload `docs/dev/social-preview.png`
   (1280x640, the icon and wordmark on GitHub's dark ground). It is what a
   shared link shows on Discord, Twitter and the like.

## Push

From the repository folder, in the app's terminal:

```bash
git remote add origin https://github.com/<your-username>/Snap64Recomp.git
```

```bash
git push -u origin main
```

```bash
git push origin v1.0.0
```

`main` was fast-forwarded to the release commit and `v1.0.0` tagged there;
`snap-port` is the same commit and need not be pushed. GitHub will ask you
to sign in the first time (a browser window, or a personal access token
if it asks for a password: Settings > Developer settings > Personal access
tokens > Tokens (classic), scope `repo`).

## The release

1. On the repository page: Releases > "Draft a new release".
2. "Choose a tag": `v1.0.0` (it exists once pushed). Title: `Snap64 Recomp
   1.0.0`.
3. Description: paste `docs/dev/RELEASE-NOTES-1.0.0.md` from its `---` line
   down, and drop the screenshots in above it (drag into the text box).
4. Attach `build-win/Snap64Recomp-1.0.0-win64.zip` and
   `build-win/Snap64Recomp-1.0.0-win64.zip.sha256`.
5. Leave "Set as the latest release" ticked; "Pre-release" unticked.
   Publish.

## After

- Watch Issues for the first reports from other GPUs; the README asks
  reporters for `snap64.log`, and the map file decodes crash reports.
- The `.map` and `.sha256` beside the zip are per build: rebuild, re-run
  the suite, re-`cpack`, re-tag for any later version (`BUILDING.md`, "To
  cut a release").
- If a rights holder writes: reply promptly, take the repository private
  while you read it, and do not argue in public. The project distributes no
  game data, which is the line that matters; the README says so in its
  first paragraph.
