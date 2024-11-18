# torrent-s3

Downloads files from torrent and saves to S3 storage. Allows to use limited intermediate server storage, given files are small enough.

# Installation

TODO

# Command line parameters

TODO

# Usage example

```sh
torrent-s3 -d download -m magnet:?xt=urn:btih:01FF5A2C8261D32B2F83007ECA4C5A94EFA66EC3 -l 50000000
```

Download files from a given magnet link using `download` folder to as a temporary storage. Limit temporary storage to `50000000` bytes (47 MB).
