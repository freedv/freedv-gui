#!/bin/sh
#
# Submits a file to Apple's notary service and waits for the result.
# notarytool exits successfully even when the submission is rejected, so
# check the status ourselves and print the notary log when it isn't
# accepted; otherwise the reason for a rejection never shows up anywhere.
#
# Usage: macos_notarize.sh <file> <keychain profile>

FILE=$1
PROFILE=$2

RESULT=$(xcrun notarytool submit "$FILE" --keychain-profile "$PROFILE" --wait --output-format json)
SUBMIT_STATUS=$?
echo "$RESULT"

SUBMISSION_ID=$(echo "$RESULT" | plutil -extract id raw - 2>/dev/null)
STATUS=$(echo "$RESULT" | plutil -extract status raw - 2>/dev/null)

if [ $SUBMIT_STATUS -eq 0 ] && [ "$STATUS" = "Accepted" ]; then
    exit 0
fi

echo "Notarization of $FILE did not succeed (status: ${STATUS:-unknown})"
if [ -n "$SUBMISSION_ID" ]; then
    xcrun notarytool log "$SUBMISSION_ID" --keychain-profile "$PROFILE"
fi
exit 1
