#!/bin/bash

# turn on bash's job control
set -m

ENV_PATH=${ENV_FILE_PATH:-/env/env}

if [ -f "$ENV_PATH" ]
then
    # shellcheck source=/dev/null
    . "${ENV_PATH}"
else
    echo "'$ENV_PATH' not found, env skipped"
fi

exec torrent-s3 ${DOWNLOAD_PATH:+-d "$DOWNLOAD_PATH"} ${TORRENT_URL:+-t "$TORRENT_URL"} ${DOWNLOAD_LIMIT_BYTES:+-l $DOWNLOAD_LIMIT_BYTES} ${S3_URL:+-s "$S3_URL"} \
     ${S3_BUCKET:+-b "$S3_BUCKET"} ${S3_UPLOAD_PATH:+-u "$S3_UPLOAD_PATH"} ${S3_ACCESS_KEY:+-a "$S3_ACCESS_KEY"} ${S3_SECRET_KEY:+-k "$S3_SECRET_KEY"} ${HASHLIST_PATH:+-p "$HASHLIST_PATH"}
