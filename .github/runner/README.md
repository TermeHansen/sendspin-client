# Self-hosted ARM64 runner (Docker Compose)

Runs the GitHub Actions self-hosted runner inside a 64-bit (`linux/arm64`)
container on the OSMC Pi. The Pi boots a 64-bit kernel but a 32-bit userland,
so the aarch64 runner binary cannot run on the host directly
(`libstdc++.so.6: cannot open shared object file`). The container supplies the
missing arm64 userland; no kernel change and no QEMU are involved.

## Why not a native arm64 runner

`./config.sh` on the host fails with three signals:

- `not a dynamic executable` - the 32-bit `ldd` cannot parse the aarch64 ELFs.
- `ld.so.preload ... wrong ELF class: ELFCLASS32` - the *aarch64* loader
  started and read the 32-bit `libarmmem.so`; the 64-bit kernel is in use.
- `libstdc++.so.6: cannot open shared object file` - the 64-bit loader runs,
  but no arm64 libraries are installed.

The kernel is fine. Only the 64-bit libraries are missing, which is exactly
what the container provides.

## Setup

1. Confirm the host runs arm64 containers natively (expect `aarch64`; the
   `--platform` flag is mandatory, the image has no `linux/arm/v7` variant):

   ```sh
   docker run --rm --platform linux/arm64 arm64v8/debian:trixie uname -m
   ```

2. Create the work directory and the env file:

   ```sh
   sudo mkdir -p /srv/sendspin-runner/_work
   cp .env.example .env
   $EDITOR .env            # set RUNNER_TOKEN
   ```

3. Register and start:

   ```sh
   docker compose up -d
   docker compose logs -f
   ```

   A fresh registration token is needed each time the container is
   re-created, because it is single-use and expires after about an hour:

   ```sh
   gh api -X POST repos/TermeHansen/sendspin-client/actions/runners/registration-token --jq .token
   ```

## Operations

| Task | Command |
|---|---|
| Status | `docker compose ps` |
| Logs | `docker compose logs -f` |
| Stop / start | `docker compose stop` / `docker compose start` |
| Update the runner image | `docker compose pull && docker compose up -d` |
| Remove registration | `docker compose down` then delete the runner in GitHub settings |

Restart and reboot preserve the registration: the container keeps its
`/home/runner` state while it exists, so `config.sh` is skipped on subsequent
starts. Only `docker compose down` (container removed) forces a re-register,
and the script then re-runs `config.sh --replace` with a fresh token.

## Notes

- `user: "0:0"` plus `RUNNER_ALLOW_RUNASROOT=1` - the image's default user is
  not in the host's `docker` group and the bind-mounted work dir is root-owned.
  The runner refuses to start as root unless `RUNNER_ALLOW_RUNASROOT=1` is set;
  without it `config.sh`/`run.sh` abort with `Must not run with sudo` and the
  container restart-loops under `restart: unless-stopped`. This is a dedicated
  build host and the workflows already run their builds as `0:0`, so root-owned
  artifacts are expected.

  To run as the unprivileged `runner` user instead: drop `user:` and
  `RUNNER_ALLOW_RUNASROOT`, add the image's `runner` uid to the host's `docker`
  group, and `chown` the work directory to that uid.

- The work directory is bound at an identical host/container path. Workflow
  steps execute nested `docker run -v $GITHUB_WORKSPACE:...`, which the *host*
  daemon resolves; a mismatched path would mount a non-existent directory.
- GitHub requires a job's `container:` image to match the runner
  architecture. On this arm64 runner the `armhf` leg cannot use a job-level
  `container: arm32v7/...`; it uses an explicit
  `docker run --platform linux/arm/v7` step instead (runs natively via kernel
  `CONFIG_COMPAT`).

## Workflow wiring (applied)

`build-multi-debian.yml` selects this runner with
`runs-on: [self-hosted, Linux, ARM64]`:

- `build-arm` and `upload-all` run on this arm64 runner.
- `build-arm` has no job-level `container:`; each matrix leg starts its own
  container with `docker run --platform` (`linux/arm/v7` for armhf,
  `linux/arm64` for arm64). Both run natively - arm64 on the 64-bit kernel,
  armhf via kernel `CONFIG_COMPAT`, no QEMU.
- `build-arm` uses `actions/checkout@v7` (the old `@v1` was a workaround for
  the 32-bit runner, which could not execute the Node 20 action).
- `build-armv6` is unchanged (x86_64 host + QEMU).
- The workflow actions (`checkout@v7`, `download-artifact@v8`,
  `upload-artifact@v7`, `action-gh-release@v3`, `setup-{qemu,buildx}-action@v4`)
  run on the Node 24 Actions runtime, which needs runner >= 2.327.1. The
  compose service tracks `ghcr.io/actions/actions-runner:latest`; if a job
  fails with "requires a minimum Actions Runner version", pull and recreate
  the container (`docker compose pull && docker compose up -d`).
