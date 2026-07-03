#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <path-to-dmg>"
    exit 1
fi

for tool in xcrun plutil; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "❌ Required tool not found: $tool"
        exit 1
    fi
done

DMG_PATH="$1"
if [[ ! -f "$DMG_PATH" ]]; then
    echo "❌ DMG not found: $DMG_PATH"
    exit 1
fi

NOTARY_PROFILE="${NOTARY_PROFILE:-}"
NOTARY_TIMEOUT="${NOTARY_TIMEOUT:-3600}"

if [[ -z "$NOTARY_PROFILE" ]]; then
    echo "⚠️  Using plaintext env-var credentials."
    echo "   Consider 'xcrun notarytool store-credentials' + NOTARY_PROFILE=<name>."
fi

SUBMIT_ARGS=(--wait)
if [[ -n "$NOTARY_PROFILE" ]]; then
    SUBMIT_ARGS+=(--keychain-profile "$NOTARY_PROFILE")
    echo "🔐 Using keychain profile: $NOTARY_PROFILE"
else
    : "${APPLE_ID:?APPLE_ID env var is required (or set NOTARY_PROFILE)}"
    : "${TEAM_ID:?TEAM_ID env var is required}"
    : "${APP_PASSWORD:?APP_PASSWORD env var is required}"
    SUBMIT_ARGS+=(--apple-id "$APPLE_ID" --team-id "$TEAM_ID" --password "$APP_PASSWORD")
    echo "🔐 Using APPLE_ID=$APPLE_ID, TEAM_ID=$TEAM_ID"
fi

echo "📤 Submitting to Apple notary service (timeout: ${NOTARY_TIMEOUT}s)…"

TIMEOUT_BIN=""
if command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_BIN="gtimeout"
elif command -v timeout >/dev/null 2>&1; then
    TIMEOUT_BIN="timeout"
else
    echo "ℹ️  'timeout'/'gtimeout' not installed — submission will wait indefinitely."
fi

SUBMIT_OUTPUT=""
rc=0
if [[ -n "$TIMEOUT_BIN" ]]; then
    SUBMIT_OUTPUT="$("$TIMEOUT_BIN" "$NOTARY_TIMEOUT" xcrun notarytool submit "$DMG_PATH" \
        "${SUBMIT_ARGS[@]}" --output-format plist 2>&1)" || rc=$?
else
    SUBMIT_OUTPUT="$(xcrun notarytool submit "$DMG_PATH" \
        "${SUBMIT_ARGS[@]}" --output-format plist 2>&1)" || rc=$?
fi

echo "$SUBMIT_OUTPUT"

if [[ $rc -ne 0 ]]; then
    if [[ $rc -eq 124 ]]; then
        echo "❌ Notarization timed out after ${NOTARY_TIMEOUT}s."
    else
        echo "❌ notarytool submit failed (exit $rc)."
    fi
    exit $rc
fi

STATUS="$(echo "$SUBMIT_OUTPUT" | plutil -extract status raw - 2>/dev/null || echo "unknown")"
SUBMISSION_ID="$(echo "$SUBMIT_OUTPUT" | plutil -extract id raw - 2>/dev/null || echo "")"

if [[ "$STATUS" != "Accepted" ]]; then
    echo "❌ Notarization did not succeed (status: $STATUS)."
    if [[ -n "$SUBMISSION_ID" ]]; then
        echo "📋 Fetching log for submission $SUBMISSION_ID…"
        LOG_ARGS=()
        if [[ -n "$NOTARY_PROFILE" ]]; then
            LOG_ARGS+=(--keychain-profile "$NOTARY_PROFILE")
        else
            LOG_ARGS+=(--apple-id "$APPLE_ID" --team-id "$TEAM_ID" --password "$APP_PASSWORD")
        fi
        if ! xcrun notarytool log "$SUBMISSION_ID" "${LOG_ARGS[@]}"; then
            echo "⚠️  Could not fetch the notarization log (check credentials or submission ID)."
        fi
    else
        echo "⚠️  No submission ID was returned; cannot fetch log automatically."
    fi
    exit 1
fi

echo "✅ Notarization accepted. Stapling ticket…"
xcrun stapler staple "$DMG_PATH"
xcrun stapler validate "$DMG_PATH"

echo ""
echo "🎉 Done. Distributable DMG: $DMG_PATH"
echo "   Gatekeeper check:"
spctl -a -vv -t open --context context:primary-signature "$DMG_PATH" || true
