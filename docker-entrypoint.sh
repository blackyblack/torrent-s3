#!/bin/bash

# turn on bash's job control
set -m

exec torrent-s3 ${DOWNLOAD_PATH:+-d "$DOWNLOAD_PATH"} ${TORRENT_URL:+-t "$TORRENT_URL"} ${DOWNLOAD_LIMIT_BYTES:+-l $DOWNLOAD_LIMIT_BYTES} ${S3_URL:+-s "$S3_URL"} \
     ${S3_BUCKET:+-b "$S3_BUCKET"} ${S3_REGION:+-r "$S3_REGION"} ${S3_UPLOAD_PATH:+-u "$S3_UPLOAD_PATH"} ${S3_ACCESS_KEY:+-a "$S3_ACCESS_KEY"} ${S3_SECRET_KEY:+-k "$S3_SECRET_KEY"} \
     ${STATE_PATH:+-q "$STATE_PATH"} ${EXTRACT_ARCHIVES:+-x}
