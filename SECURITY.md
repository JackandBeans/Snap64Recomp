# Security

Snap64 Recomp is a single-player program that opens no network connection,
runs no scripts and reads only the files next to its executable: your ROM,
its own settings and saves, and optional mods and texture packs you put
there yourself. The realistic risks are a crash on unexpected input (a
malformed save, a corrupt ROM, a bad texture pack) and the trust you place
in a download.

**Verify what you download.** Every release ships a `.sha256` file beside
the archive; `certutil -hashfile Snap64Recomp-<version>-win64.zip SHA256`
prints yours to compare. Only archives on this repository's Releases page
are the project's.

**Reporting.** If you find something you believe is a security problem
(memory corruption reachable from a save file, a texture pack or a mod, for
example), write to snap64recomp@gmail.com rather than opening a public
issue, with the file that triggers it if you can share it. Everything else
goes to Issues.

There is no bug bounty; this is a hobby project. Reports are answered as
time allows.
