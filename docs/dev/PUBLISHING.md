# Publishing 1.0.0 on GitHub, step by step

Written for a first release on a GitHub account that has never hosted a
repository. Everything below happens on your machine and in a browser; the
port's build is already done and tagged locally. Nothing in this file has
been done yet, because publishing is yours to press.

## Before pushing, once

1. **Decide the email that goes public.** Every commit carries the author's
   email. If you would rather it were not on the internet, GitHub gives
   each account a private address of the form
   `<id>+<username>@users.noreply.github.com` (Settings > Emails > "Keep my
   email addresses private" shows it). Set it for future commits with
   `git config user.email "<that address>"`. Rewriting the 300 existing
   commits is possible only before the first push and is a one-time job;
   say so and it can be done, otherwise the history goes as it is.
2. **Take screenshots** from the playtest copy: the title screen, a course,
   the Graphics page. They go on the release page and in the README later.
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
   points people to it). Discussions optional. Wiki off.

## Push

From `C:\Users\<you>\PokemonSnapRecomp`, in the app's terminal:

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
