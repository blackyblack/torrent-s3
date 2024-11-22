# torrent-s3

Downloads files from torrent and saves to S3 storage. Allows to use limited intermediate server storage, given files are small enough.

# Installation

TODO

# Command line parameters

1. `--help` or `-h` - Display help screen;
2. `--torrent` or `-t` - Torrent file or magnet link to sync with S3;

    Torrent magnet link example: `./torrent-s3 --torrent=magnet:?xt=urn:btih:01FF5A2C8261D32B2F83007ECA4C5A94EFA66EC3`

    Torrent file example: `./torrent-s3 --torrent=./1.torrent`

    Torrent HTTP URL example: `./torrent-s3 --torrent=https://webtorrent.io/torrents/big-buck-bunny.torrent`
3. `--s3-url` or `-s` - S3 service URL;

    S3 URL example: `./torrent-s3 --s3-url=play.min.io`
4. `--s3-bucket` or `-b` - S3 bucket;

    S3 bucket example: `./torrent-s3 --s3-bucket=asiatrip`
5. `--s3-upload-path` or `-u` - S3 upload path. If upload path is not set, files are stored in the root of the S3 bucket;

    S3 upload path example: `./torrent-s3 --s3-upload-path=upload`
6. `--s3-access-key` or `-a` - S3 access key;

    S3 access key example: `./torrent-s3 --access-key=Q3AM3UQ867SPQQA43P2F`
7. `--s3-secret-key` or `-k` - S3 secret key;

    S3 secret key example: `./torrent-s3 --s3-secret-key=zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG`
8. `--download-path` or `-d` - Path for temporary file storage. Files are stored in the current directory, if not set;

    Download path example: `./torrent-s3 --download-path=download`
9. `--limit-size` or `-l` - Temporary file storage maximum size in bytes. Unlimited if not set;
> [!NOTE]
> Temporary storage can become slightly larger than specified limit. Large files will be downloaded to temporary storage disregard
> the limit.

    Limit size example: `./torrent-s3 --limit-size=50000000`
10. `--hashlist-file` or `-p` - Path for hashlist file storage. Hashlist is stored in `.torrent_s3_hashlist`, if not set;
> [!NOTE]
> Hashlist allows to track torrent contents modifications. New files are synced with S3 and deleted files are removed from S3.

    Hashlist path example: `./torrent-s3 --hashlist-file=./tmp/.torrent_s3_hashlist`

# Usage example

```sh
torrent-s3 -d download -t magnet:?xt=urn:btih:01FF5A2C8261D32B2F83007ECA4C5A94EFA66EC3 -l 50000000 -s play.min.io -b asiatrip -a Q3AM3UQ867SPQQA43P2F -k zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG
```

Download files from a given magnet link using `download` folder to as a temporary storage. Limit temporary storage to `50000000` bytes (47 MB).
