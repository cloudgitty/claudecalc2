# Part B progress (ICMKit)

Session start: 2026-10-28 00:05:37 UTC (`date -u`)

Note: the repo-root `PROGRESS.md` belongs to the unrelated PLO6 project and is left untouched;
Part B progress lives here in `ICMKit/PROGRESS.md`.

## Done

- Step 0: spec saved unchanged as `docs/PART_B_SPEC.md`.

## Stopped: Swift toolchain could not be installed

- `swift` is not installed on the machine and is not in the Ubuntu 24.04 apt repositories.
- `download.swift.org` is blocked by the session's egress proxy (HTTP 403 on CONNECT).
- Fallback tried: the official Docker Hub image `swift:6.1-noble` (Swift 6.1.3, amd64 manifest
  `sha256:b47218fa…59fd56`). The toolchain layer
  (`sha256:ef2b06c5…37c856`, 898 MB) downloaded and its sha256 matched the manifest.
  Apt dependencies from the image's Dockerfile were installed.
- Extracting and running that toolchain was refused by the session's permission policy
  ("code from external"), so no Swift toolchain is available and nothing can be compiled or tested.
- Per the session rules, work stops here. No ICMKit code was written.

## What the next session needs

- Either allow `download.swift.org` in the environment's network policy, or allow
  installing the Swift toolchain from the Docker Hub `swift` image (or pre-install Swift 6 in the
  environment's setup script). Then restart Part B1 from task 1 (ICMEngine); step 0 is done.
