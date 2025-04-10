# torrent-s3

Downloads files from torrent and saves to S3 storage. Allows to use limited intermediate server storage, given files are small enough.
Supports automatical archives (zip, rar, 7zip) extraction before uploading to S3.

# Installation

## Windows

Get binaries from the Gihub [Releases](https://github.com/blackyblack/torrent-s3/releases)

## Linux

- Install libraries

  ```sh
  sudo apt install curl zip unzip tar libarchive-dev
  ```

- Install [vcpkg](https://github.com/microsoft/vcpkg)

  ```sh
  git clone https://github.com/microsoft/vcpkg.git
  cd vcpkg
  ./bootstrap-vcpkg.sh
  VCPKG_ROOT=$(pwd)
  ```

- Install [gtest](https://github.com/google/googletest/tree/main)

  ```sh
  ./vcpkg install gtest
  ```

- Install [minio-cpp](https://github.com/minio/minio-cpp)

  ```sh
  ./vcpkg install minio-cpp
  ```

- Install [libtorrent](https://libtorrent.org/index.html)

  ```sh
  ./vcpkg install libtorrent
  ```

  - Install [sqlite3](https://sqlite.org/)

  ```sh
  ./vcpkg install sqlite3
  ```

- Download torrent-s3 source

  ```sh
  git clone https://github.com/blackyblack/torrent-s3.git
  cd ./torrent-s3
  ```

- Build torrent-s3

  ```sh
  cmake . -B ./build/Debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
  cmake --build ./build/Debug
  ```

## Docker

Run image from [here](https://hub.docker.com/repository/docker/blackyblacky/torrent-s3/general)

# Command line parameters

1. `--help` or `-h` - Display help screen;
2. `--version` or `-v` - Display version;
3. `--torrent` or `-t` - Torrent file or magnet link to sync with S3;
> [!NOTE]
> Magnet link or HTTP URL might contain special symbols. Put Torrent link in double quotes (`"`) in this case.

    Torrent magnet link example: `./torrent-s3 --torrent="magnet:?xt=urn:btih:01FF5A2C8261D32B2F83007ECA4C5A94EFA66EC3"`

    Torrent file example: `./torrent-s3 --torrent=./1.torrent`

    Torrent HTTP URL example: `./torrent-s3 --torrent=https://webtorrent.io/torrents/big-buck-bunny.torrent`
4. `--s3-url` or `-s` - S3 service URL;
> [!NOTE]
> S3 URL should not contain bucket or region.

    S3 URL example: `./torrent-s3 --s3-url=play.min.io`
5. `--s3-bucket` or `-b` - S3 bucket;

    S3 bucket example: `./torrent-s3 --s3-bucket=asiatrip`
6. `--s3-region` or `-r` - S3 region;

    S3 region example: `./torrent-s3 --s3-region=ru-central-1`
7. `--s3-upload-path` or `-u` - S3 upload path. If upload path is not set, files are stored in the root of the S3 bucket;

    S3 upload path example: `./torrent-s3 --s3-upload-path=upload`
8. `--s3-access-key` or `-a` - S3 access key;

    S3 access key example: `./torrent-s3 --access-key=Q3AM3UQ867SPQQA43P2F`
9. `--s3-secret-key` or `-k` - S3 secret key;

    S3 secret key example: `./torrent-s3 --s3-secret-key=zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG`
10. `--download-path` or `-d` - Path for temporary file storage. Files are stored in the current directory, if not set;

    Download path example: `./torrent-s3 --download-path=download`
11. `--limit-size` or `-l` - Temporary file storage maximum size in bytes. Unlimited if not set;
> [!NOTE]
> Temporary storage can become slightly larger than specified limit. Large files will be downloaded to temporary storage disregard
> the limit.

    Limit size example: `./torrent-s3 --limit-size=50000000`
12. `--extract-files` or `-x` - Extract archives before uploading to S3;
> [!NOTE]
> 7zip, zip, rar and rar5 archives are supported.
> Folders for extracted files are generated automatically, i.e. for `archive.zip` the folder will be `archive_zip`.

> [!NOTE]
> Limit size only applies to downloaded files. Make sure to have additional space for extracted files.

    Extract archives example: `./torrent-s3 --extract-files`
13. `--state-file` or `-q` - Path for application state SQLite database. Application state is stored in `<download-path>/default.sqlite`, if not set. Set  `--state-file=:memory:` to use in-memory storage;
> [!NOTE]
> Application state allows to track torrent contents modifications. New files are synced with S3 and deleted files are removed from S3. Also sync process can be terminated at any moment and resumed later.

> [!NOTE]
> Since sync process works in `mirror` mode, i.e. adds to S3 all files from .torrent file and removes all files, which are not in file, different torrent files should be synced
> with different states. For example, run sync for `torrent_a` with `./torrent-s3 --state-file=./tmp/torrent_a.sqlite` and for `torrent_b` with `./torrent-s3 --state-file=./tmp/torrent_b.sqlite`.

    Application state example: `./torrent-s3 --state-file=./tmp/default.sqlite`

# Usage example

```sh
torrent-s3 -d download -t magnet:?xt=urn:btih:01FF5A2C8261D32B2F83007ECA4C5A94EFA66EC3 -l 50000000 -s play.min.io -b asiatrip -a Q3AM3UQ867SPQQA43P2F -k zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG
```

Download files from a given magnet link using `download` folder to as a temporary storage. Limit temporary storage to `50000000` bytes (47 MB).
