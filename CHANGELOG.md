# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

## [0.2.0] - 2026-08-XX

### Added
- Auto-release of audio device on idle timeout for player role

### Changed
- Unified version management to use single source from `version.txt`
- Updated sendspin-cpp dependency from v0.6.1 to v0.7.0


## [0.1.2] - 2026-06-01

### Fixed
- Fixed Ubuntu 24.04 and 26.04 release packages


## [0.1.1] - 2026-05-31

### Added
- Added Ubuntu 24.04 and 26.04 support

### Changed
- New upstream release
- Improved CI/CD workflow with matrix builds
- Enhanced package naming with distribution suffixes

### Fixed
- Excluded debug symbol packages from GitHub releases


## [0.1.0] - 2024-05-15

### Added
- Initial release
- Based on sendspin-cpp v0.4.0
- Enhanced PortAudio support with ALSA hardware volume control
- Systemd service integration
- Fixed default system service lib on Debian

