# Quest development

## Build and test

- Windows host: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_quest.ps1 -Install -Launch -Serial SERIAL` builds and deploys the optimized release. The ROM is transferred separately, never packaged.
- Benchmark builds use `-Benchmark`, package `org.snap64.quest.benchmark`, and isolated saves/settings. Never replace the user's release save with a benchmark fixture.
- Run `python tools/quest_benchmark.py --serial SERIAL --refresh 80` for automated Beach measurement. Run 72 Hz separately if 80 Hz fails; do not describe a 72 Hz result as an 80 Hz pass.
- Routine performance iterations stop approximately halfway through Beach (77 seconds after confirmed entry). Add `--full-course` for final qualification; short iterations cannot satisfy the complete-course acceptance requirement.
- Native tests: `cmake -S tests/vr -B build-win/quest-vr-tests`, `cmake --build build-win/quest-vr-tests --config Release`, then `ctest --test-dir build-win/quest-vr-tests -C Release --output-on-failure`.
- Changes to guest wrappers require updating `tools/hook_funcs.py` and running it. Generated `RecompiledFuncs/` files are ignored; do not hand-edit them as the sole implementation.

## Evidence and acceptance

- Target the connected Quest 3 at 80 Hz; sustained 72 Hz is an explicitly accepted fallback. Preserve 100% OpenXR-recommended eye dimensions, normal game speed, and full scene content. Do not use dynamic resolution or foveated quality reduction to pass.
- Require three complete Beach runs, including one after 15 minutes of continuous play. Each must average at least 99% of selected refresh with fewer than 1% missed display intervals and no recurring animation freezes. Report cold-start runs separately.
- Record actual application submissions, source-frame age and animation progression, CPU/GPU timing, display refresh, focus/tracking, thermal state, and build identity. Compositor refresh, synthetic preview, and passing compilation are not device performance evidence.
- Sleeping/unfocused headsets, ADB loss, failed course entry, or incomplete routes invalidate a run. Preserve failure evidence. Never claim acceptance when any required measurement is absent.
- Keep screenshots/readback out of timed qualification; use separate visual runs. Check both eyes, sky occlusion, tutorials, held-camera updates, photos/scoring, and production input.
- All generated screenshots, recordings, test results, logs, reports, and traces belong under ignored `artifacts/quest/<timestamp>-<commit>/{logs,metrics,captures,traces}/`. Build intermediates remain under ignored build directories. Do not add test imagery, ROMs, saves, signing keys, or credentials to Git.
- Keep desktop behavior working. Do not introduce per-eye/per-pass CPU fence waits when a frame-owned resource and completion fence can safely cover the dependency. Preserve OpenXR acquire/release and GPU resource lifetime rules.

## Workflow

Measure the baseline, make attributable changes, repeat the same route, and retain comparison reports. Benchmark automation must be absent from release builds. Controller-free testing must still render through real OpenXR; do not bypass focus/tracking checks to manufacture a pass. Install the validated release only after qualification; otherwise report the outstanding bottleneck and incomplete acceptance.
