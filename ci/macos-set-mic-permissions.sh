#!/bin/bash

# Grants the GitHub runner's provisioner microphone access so the audio
# tests can record. Some runner images (e.g. xcode-27) don't let us open
# the user TCC database at all, so give up after a limited number of tries
# instead of looping until the job times out; the tests that follow will
# show whether the permission was actually needed on that image.

TCC_DB="$HOME/Library/Application Support/com.apple.TCC/TCC.db"
MAX_TRIES=10

for ((i = 1; i <= MAX_TRIES; i++)); do
    if sqlite3 "$TCC_DB" "BEGIN TRANSACTION; INSERT OR IGNORE INTO access VALUES ('kTCCServiceMicrophone','/usr/local/opt/runner/provisioner/provisioner',1,2,4,1,NULL,NULL,0,'UNUSED',NULL,0,1687786159,NULL,NULL,'UNUSED',1687786159); INSERT OR IGNORE INTO access VALUES ('kTCCServiceMicrophone','/opt/off/opt/runner/provisioner/provisioner',1,2,4,1,NULL,NULL,0,'UNUSED',NULL,0,1687786159,NULL,NULL,'UNUSED',1687786159); COMMIT;"; then
        exit 0
    fi
    echo "Failed (attempt $i of $MAX_TRIES), trying again"
    sleep 1
done

echo "::warning::Could not update $TCC_DB after $MAX_TRIES attempts; continuing without granting microphone permission"
exit 0
