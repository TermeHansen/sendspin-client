# CI workflows

Two workflows build and test the client:

| File | Purpose | Trigger |
|---|---|---|
| `build-multi-debian.yml` | Build `.deb` packages for every architecture and publish a release | `release: published`, or manual dispatch |
| `tests.yml` | Build and run the reconnect integration test | push to `main`, PR to `main` |

The self-hosted ARM runner these workflows depend on is configured separately
in `../runner/` (Docker Compose). See that README for setup.

## `build-multi-debian.yml`

### Triggers

- **`release: published`** - builds every architecture and attaches the
  non-dbgsym packages to the release.
- **`workflow_dispatch`** - manual run with an `arch` input:
  `all` (default), `amd64`, `armhf`, `arm64` or `armv6`. Selecting one arch
  runs only the jobs that produce it; the release step is skipped unless the
  event is a release.

### Jobs

```
build-amd64  (ubuntu-latest)              ─┐
build-arm    (self-hosted, ARM64)          ├─→ upload-all (self-hosted, ARM64)
build-armv6  (self-hosted, X64)           ─┘
```

**`build-amd64`** - GitHub-hosted. Matrix over `debian:trixie`,
`ubuntu:24.04`, `ubuntu:26.04`, each as a job-level `container:`. Runs
`dpkg-buildpackage -us -uc -b`, renames the `.deb` with a `_<distro>` suffix,
and uploads it as artifact `amd64-<distro>` (retention: 1 day).

**`build-arm`** - the self-hosted arm64 runner. Matrix over `debian:trixie`,
`ubuntu:26.04`, and `arch` from the input.

The runner is an arm64 container, and GitHub requires a job's `container:`
image to match the runner architecture, so there is no job-level `container:`.
Each leg starts its own container with `docker run --platform`:

- `armhf` → `linux/arm/v7` on `arm32v7/<distro>`
- `arm64` → `linux/arm64` on `<distro>`

Both run natively (arm64 on the 64-bit kernel, armhf via kernel
`CONFIG_COMPAT`); no QEMU. Packages are written to `PARENT_DIR/upload` on the
runner host rather than uploaded as an artifact, because `upload-all` runs on
the same host.

**`build-armv6`** - the self-hosted x86_64 runner. Matrix over
`raspian:trixie`. Sets up QEMU, then runs `dpkg-buildpackage` inside
`vascoguita/raspios:armhf-trixie` with `--platform linux/arm/v6`, renames the
package `armhf` → `armv6`, and uploads artifact `armv6-<distro>`.

**`upload-all`** - needs all three build jobs. Downloads the `amd64-*` and
`armv6-*` artifacts, merges them with the arm packages already on the runner
host, and on a release publishes everything except `*dbgsym*` via
`softprops/action-gh-release`.

### Environment

- `DEB_PACKAGE_NAME=sendspin-client`
- `DEB_BUILD_OPTIONS=noddebs nocheck` - skips debug packages and the CTest run
  during packaging, since `tests.yml` covers testing.

### Runner requirements

The actions (`checkout@v7`, `download-artifact@v8`, `upload-artifact@v7`,
`action-gh-release@v3`, `setup-{qemu,buildx}-action@v4`) use the Node 24
runtime, which requires Actions Runner **>= 2.327.1**. The compose service
tracks `ghcr.io/actions/actions-runner:latest`; if a job fails with a minimum
runner version error, pull and recreate the container.

Runner labels used:

- `[self-hosted, Linux, ARM64]` - `build-arm`, `upload-all`
- `[self-hosted, Linux, X64]` - `build-armv6`

## `tests.yml`

Runs on GitHub-hosted `ubuntu-24.04` and `ubuntu-22.04`. Installs the build
dependencies, configures with `-DBUILD_TESTING=ON`, builds, and runs
`ctest --test-dir build --output-on-failure`. The suite is
`tests/reconnect_test.cpp`, which drives the real binary against an
in-process fake Sendspin server that plays the Noise initiator.

## Building a release

1. Bump `VERSION`.
2. Add a matching entry at the top of `debian/changelog` (debhelper date
   format, maintainer `TermeHansen <terme@hansen>`).
3. Publish a GitHub release - the workflow builds all architectures and
   attaches the packages.

Or run the workflow manually with `arch: all` to produce packages without
creating a release.

## Troubleshooting

- **`no matching manifest`** on the arm jobs - the `--platform` value does not
  match an image that publishes that architecture. `arm64` legs need
  `linux/arm64`, `armhf` legs need `linux/arm/v7` with the `arm32v7/` image.
- **Job queued forever** - no online runner carries the required labels. Check
  the runner is registered and idle in the repository's Settings → Actions →
  Runners.
- **`requires a minimum Actions Runner version`** - update the runner image
  (`docker compose pull && docker compose up -d` in `../runner/`).
- **`download-artifact` fails on hash mismatch** - `@v8` errors by default on
  a digest mismatch rather than warning. Re-run; if it persists, pin
  `download-artifact` to `v7`.
- **Release step skipped** - expected unless the run was triggered by a
  published release.

## References

- [GitHub Actions documentation](https://docs.github.com/en/actions)
- [Debian packaging guide](https://www.debian.org/doc/manuals/packaging-manual/)
- [Runner configuration](../runner/README.md)
