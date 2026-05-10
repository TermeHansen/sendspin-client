# SendSpin Client

A feature-rich SendSpin client implementation based on the [sendspin-cpp](https://github.com/Sendspin/sendspin-cpp) library.

## Features

- **Full SendSpin Protocol Support**: Complete implementation of the SendSpin audio streaming protocol
- **Enhanced Audio Playback**: Advanced PortAudio integration with device selection and format detection
- **Hardware Volume Control**: ALSA mixer support for direct hardware volume control on Linux
- **Automatic Discovery**: mDNS/zeroconf service advertisement for easy device discovery
- **Multi-Format Support**: Automatic detection and negotiation of supported audio formats (FLAC, OPUS, PCM)
- **Cross-Platform**: Works on Linux, macOS, and Windows (with appropriate dependencies)

## Requirements

### Core Dependencies
- C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)
- CMake 3.10+
- [sendspin-cpp](https://github.com/Sendspin/sendspin-cpp) (included as submodule)

### Optional Dependencies
- **PortAudio** (for audio playback): `libportaudio2` and development headers
- **ALSA** (for hardware volume control on Linux): `libasound2-dev`
- **Avahi/mDNS** (for zeroconf discovery on Linux): `libavahi-compat-libdnssd-dev`

## Building

```bash
# Clone with submodules
git clone --recursive https://github.com/TermeHansen/sendspin-client.git
=======
cd sendspin-client

# Or if you already cloned without --recursive:
git submodule update --init --recursive

# Configure and build
mkdir build
cd build
cmake ..
make
```

## Usage

```bash
# Basic usage - starts server and advertises via mDNS
./sendspin-client "My Client Name"

# Connect to a specific server
./sendspin-client -u ws://192.168.1.100:8928/sendspin "My Client"

# List available audio devices
./sendspin-client -L

# Use specific audio device
./sendspin-client -d 2 "My Client"

# Use ALSA hardware mixer for volume control
./sendspin-client -m "1:Digital" "My Client"

# Verbose logging
./sendspin-client -v "My Client"

# See all options
./sendspin-client -h
```

## Advanced Features

### Audio Device Management
- Automatic format detection and negotiation
- Device-specific sample rate support
- Multi-channel audio support (up to device capabilities)

### Volume Control
- Software volume mixing (cross-platform)
- Hardware volume control via ALSA (Linux only)
- Independent mute control
- Volume synchronization between software and hardware controls

### Protocol Features
- Player role with comprehensive audio format support
- Controller role for remote control capabilities
- Metadata role for track information display
- Time synchronization for precise audio playback

## Architecture

The client is built on top of the sendspin-cpp library and extends it with:

1. **Enhanced PortAudio Integration**: Device selection, format detection, and hardware control
2. **Automatic Format Negotiation**: Intelligent selection of supported audio formats
3. **Cross-Platform Compatibility**: Works with various audio backends
4. **Discovery Services**: mDNS advertisement for easy network discovery

## License

**IMPORTANT LICENSE NOTICE**: This project currently has a license compatibility issue.

The sendspin-cpp library (included as a submodule) is licensed under **Apache License 2.0**, but this project is currently licensed under **GPLv3**. These licenses are not directly compatible, which may cause issues for distribution and integration.

### Recommended Action

You should **change this project's license to Apache 2.0** to match the sendspin-cpp library. I've created a `LICENSE-APACHE` file for you that contains the full Apache License 2.0 text.

To fix the license issue:

1. **Replace the current LICENSE file**:
   ```bash
   mv LICENSE LICENSE-GPLv3  # Keep old license as reference
   mv LICENSE-APACHE LICENSE  # Make Apache 2.0 the main license
   ```

2. **Update all source files** to include the Apache 2.0 header instead of GPLv3

3. **Update this README** to reflect the Apache 2.0 license

### License Files Provided

- `LICENSE`: Currently GPLv3 (should be replaced with Apache 2.0)
- `LICENSE-APACHE`: Apache License 2.0 (ready to use as main license)
- `LICENSE-GPLv3`: Will contain the old GPLv3 license after replacement (for reference)

### Why Apache 2.0 is Recommended

1. **Compatibility**: Matches the sendspin-cpp library license
2. **Permissive**: Allows broader use in commercial and open-source projects
3. **Industry Standard**: Widely used in the audio/streaming industry
4. **No Copyleft Conflicts**: Avoids GPL's viral licensing requirements

The sendspin-cpp submodule remains under its original Apache 2.0 license regardless of this project's license choice.
=======

## Contributing

Contributions are welcome! Please open issues for bugs and feature requests, and submit pull requests for improvements.

## Support

For support, please open a GitHub issue or check the [SendSpin documentation](https://sendspin.com/docs).
=======
