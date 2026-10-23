#!/bin/bash
# The release's APK signed on the release machine, when the workflow built it unsigned (the release
# key's secrets are not on GitHub): apksigner signs Snap64Recomp-<version>-android-unsigned.apk with
# the release key (v2 and v3), checks the signature, writes Snap64Recomp-<version>-android.apk beside
# it with its sidecar (one line, LF), runs the package check on it and prints the certificate's
# SHA-256 digest. apksigner asks for the key's password; nothing here prints it. BUILDING.md, step 16.
#
#   tools/release/sign_apk.sh <...-android-unsigned.apk> [<keystore>]
#
# Runs where the SDK's build-tools are (Linux, or WSL on the Windows release machine). Environment:
# SNAP64_ANDROID_KEYSTORE (the keystore's path, when none is given), SNAP64_ANDROID_KEY_ALIAS (default
# snap64), ANDROID_HOME (default ~/android-sdk; its newest build-tools), JAVA_HOME (else java on the
# PATH), SNAP64_KS_PASS (apksigner's --ks-pass form, env:NAME or file:PATH, for a run with no one at
# the prompt).
set -u
IN="${1:-}"
KS="${2:-${SNAP64_ANDROID_KEYSTORE:-}}"
case "$IN" in *-android-unsigned.apk) ;; *) echo "usage: sign_apk.sh <...-android-unsigned.apk> [<keystore>]"; exit 2;; esac
test -f "$IN" || { echo "no $IN"; exit 1; }
test -n "$KS" -a -f "$KS" || { echo "no keystore: give its path, or set SNAP64_ANDROID_KEYSTORE"; exit 1; }
SDK="${ANDROID_HOME:-$HOME/android-sdk}"
BT=$(ls -d "$SDK"/build-tools/*/ 2>/dev/null | sort -V | tail -1)
test -n "$BT" -a -x "${BT}apksigner" || { echo "no apksigner under $SDK/build-tools"; exit 1; }
test -n "${JAVA_HOME:-}" && export PATH="$JAVA_HOME/bin:$PATH"
OUT="${IN%-unsigned.apk}.apk"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
rm -f "$OUT" "$OUT.idsig" "$OUT.sha256"
PASS=()
test -n "${SNAP64_KS_PASS:-}" && PASS=(--ks-pass "$SNAP64_KS_PASS")
"${BT}apksigner" sign --ks "$KS" --ks-key-alias "${SNAP64_ANDROID_KEY_ALIAS:-snap64}" "${PASS[@]}" \
  --v1-signing-enabled false --v2-signing-enabled true --v3-signing-enabled true --in "$IN" --out "$OUT" \
  || { echo "apksigner sign failed"; rm -f "$OUT"; exit 1; }
rm -f "$OUT.idsig"
VERIFY=$("${BT}apksigner" verify --verbose --print-certs "$OUT" 2>&1) || { echo "$VERIFY"; echo "the signature does not verify"; exit 1; }
echo "$VERIFY" | grep -E '^Verifies|Verified using v[23] scheme|certificate DN|certificate SHA-256 digest'
echo "$VERIFY" | grep -q 'Verified using v3 scheme (APK Signature Scheme v3): true' || { echo "no v3 signature"; exit 1; }
printf '%s  %s\n' "$(sha256sum "$OUT" | cut -c1-64)" "$(basename "$OUT")" > "$OUT.sha256"
CHECK=$(python3 "$ROOT/tools/release_check.py" --only package --zip "$OUT" 2>&1); RC=$?
echo "$CHECK" | grep '^FAIL'
echo "$CHECK" | tail -1
echo "$(basename "$OUT"): $(stat -c %s "$OUT") bytes, sha256 $(cut -c1-64 "$OUT.sha256")"
exit $RC
