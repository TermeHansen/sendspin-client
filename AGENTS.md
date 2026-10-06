# AGENTS.md — sendspin-client

Host-side SendSpin audio streaming client (C++20/CMake) built on the
[sendspin-cpp](https://github.com/Sendspin/sendspin-cpp) library, which is
consumed as a git submodule. Primary target is Linux (Debian/Ubuntu packages,
x86_64 + arm64 + armhf + armv6/Raspberry Pi 1).

## Layout

```
CMakeLists.txt      Top-level build; also CPack/DEB metadata
VERSION             Single source of truth for the version string
src/                Client application (main.cpp, config_parser, utils)
src/common/         PortAudio audio sink (portaudio_sink.{h,cpp})
sendspin-cpp/       Git submodule — the library; has its own CLAUDE.md
debian/             debhelper packaging (control, rules, service, changelog)
cmake/              armv6-toolchain.cmake for Raspberry Pi 1 cross builds
.github/workflows/  build-multi-debian.yml — package build matrix
CONFIGURATION.md    Config file keys and CLI flags (user-facing docs)
```

## Build

Submodule required first:

```sh
git submodule update --init --recursive
cmake -B build -DBUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Binary: `build/sendspin-client`. Build dirs (`build/`, `build_test/`,
`obj-*`) are gitignored working areas — never commit them.

Dependencies (Debian): `build-essential cmake git libportaudio2
portaudio19-dev libasound2-dev libavahi-compat-libdnssd-dev avahi-daemon
libatomic1`.

PortAudio is optional: without it the build succeeds and the client runs with
a null audio sink (`SENDSPIN_HAS_PORTAUDIO` is not defined).

## Architecture notes

- `src/main.cpp` is the whole app: CLI parsing, mDNS advertisement
  (`dns_sd.h`/Avahi), wiring of `SendspinClient` + roles, and the main loop.
- The client enables only the player and metadata roles; controller, color,
  artwork, and visualizer roles are forced OFF in the top-level
  `CMakeLists.txt` to keep dependencies minimal. Don't re-enable them
  without a reason.
- Config file parsing lives in `src/config_parser.cpp` (simple `KEY = value`
  format). CLI flags override config file values. Default config path:
  `/etc/sendspin-client/sendspin-client.conf`.
- Sentinel conventions: `AUDIO_DEVICE`/`IDLE_TIMEOUT` of `-1`/`0` mean
  "default"/"disabled" respectively; `LIVENESS_TIMEOUT` uses `-1` for
  "library default" and `0` for disabled. Preserve these when touching the
  parser or `main.cpp`.
- ARMv6 has no hardware atomics; `CMakeLists.txt` links `libatomic`
  explicitly for `armv6l` and `debian/rules` adds a runtime
  `libatomic1` dependency when `MATRIX_ARCH=armv6`.

## Versioning and release

1. Bump `VERSION` (plain text, e.g. `0.2.1`). It flows into the compile
   definition `PROJECT_VERSION`, CPack, and the `.deb` filename.
2. Add a matching entry at the top of `debian/changelog` (correct debhelper
   date format, maintainer `TermeHansen <terme@hansen>`).
3. Packages are built by `.github/workflows/build-multi-debian.yml`
   (manual dispatch or on release publish), matrixed over
   debian:trixie / ubuntu:24.04 / ubuntu:26.04 and amd64/armhf/arm64/armv6.

Local package build: `dpkg-buildpackage -us -uc` from the repo root after a
CMake build (see `debian/README.md`).

## Submodule discipline

- `sendspin-cpp/` is pinned to a fixed commit; bumping it is a deliberate
  change that should be its own commit/PR with a changelog note.
- The submodule working tree may carry local patches (check
  `git submodule status` for the `m`/`+` flags and
  `git -C sendspin-cpp diff`). Historical pattern: prototype a library fix
  locally, upstream it as a PR to Sendspin/sendspin-cpp, then point the
  submodule at the merged upstream commit and drop the local patch. Do not
  let local submodule patches silently become the long-term state.
- For library internals, read `sendspin-cpp/CLAUDE.md` first — it documents
  the class layout, role composition, and threading model.

## Conventions

- Apache License 2.0 header at the top of new source files (copy from
  `src/main.cpp`).
- C++20; Doxygen `///` comments for public APIs, matching the submodule
  style.
- Tests: `tests/reconnect_test.py` drives the real binary against a
  WebSocket stub and is registered with CTest. Run with
  `cmake -B build -DBUILD_TESTING=ON && ctest --test-dir build`. The
  submodule has its own unit tests under `sendspin-cpp/tests/`.
- When adding or changing a config key, update all three places:
  `src/main.cpp` (where keys are read via the generic parser),
  `CONFIGURATION.md`, and the example in `debian/sendspin-client.conf.example`.

## Repo hygiene

- Untracked local files (build dirs, `.deb`/`.zip` artifacts, scratch
  patches, helper scripts) must never be committed or included in PRs.
- Never commit credentials, SSH targets, or tokens found in local scratch
  files.
