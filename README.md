# SendSpin Client

A feature-rich SendSpin client implementation based on the [sendspin-cpp](https://github.com/Sendspin/sendspin-cpp) library.

## Features

- **Full SendSpin Protocol Support**: Complete implementation of the SendSpin audio streaming protocol
- **Encrypted Connections with Server Pairing**: Noise-encrypted connections (sendspin-cpp v0.9); servers pair with the client through a pairing token before they may play
- **Enhanced Audio Playback**: Advanced PortAudio integration with device selection and format detection
- **Hardware Volume Control**: ALSA mixer support for direct hardware volume control on Linux
- **Automatic Discovery**: mDNS/zeroconf service advertisement for easy device discovery
- **Multi-Format Support**: Automatic detection and negotiation of supported audio formats (FLAC, OPUS, PCM)
- **Auto Device Release**: Optional idle timeout to automatically release audio hardware for other applications
- **Cross-Platform**: Works on Linux, macOS, and Windows (with appropriate dependencies)

## Requirements

### Core Dependencies
- C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)
- CMake 3.10+
- [sendspin-cpp](https://github.com/Sendspin/sendspin-cpp) (included as submodule)

### Optional Dependencies
- **PortAudio** (for audio playback): `libportaudio2` and `portaudio19-dev` development headers
- **ALSA** (for hardware volume control on Linux): `libasound2-dev`
- **Avahi/mDNS** (for zeroconf discovery on Linux): `libavahi-compat-libdnssd-dev`

## Building

```bash
# Clone with submodules
git clone --recursive https://github.com/TermeHansen/sendspin-client.git
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

# Automatically release audio device after 30 seconds of inactivity
./sendspin-client -t 30 "My Client"

# Show the pairing token (for a server such as Music Assistant to pair with this client)
./sendspin-client -T

# Verbose logging
./sendspin-client -v "My Client"

# See all options
./sendspin-client -h
```

## Pairing

sendspin-cpp v0.9 encrypts every connection, so a server must pair with the client before it
may play. On first boot the client generates a Pairing PSK, stores it in its state directory,
and derives the **pairing token** from it — the `SP:...` string a server such as Music
Assistant asks for. Entering it in the server pairs the two; the server then reconnects with a
long-term record and is trusted automatically, across reboots.

```bash
# Print the pairing token and exit
./sendspin-client -T
```

The token is also printed at every startup, next to the Client ID.

**Running as a systemd service:** the packaged service runs as the `sendspin` user with its
state in `/var/lib/sendspin-client` (mode 0700). Read the service's token from its journal, or
run `-T` as the service user with the same state directory:

```bash
# From the service journal (printed at every startup)
journalctl -u sendspin-client | grep "Pairing token"

# Or directly, as the service user
sudo -u sendspin env SENDSPIN_STATE_DIR=/var/lib/sendspin-client sendspin-client -T
```

To run with a specific PSK instead of the generated one (factory-style provisioning), set
`PAIRING_PSK_HEX` to 64 hex characters in the config file; removing the key returns the client
to the stored PSK. To admit servers that have not paired (no token needed), set
`UNPAIRED_ACCESS = true` or pass `-a`. See `CONFIGURATION.md` for details.

## Advanced Features

### Audio Device Management
- Automatic format detection and negotiation
- Device-specific sample rate support
- Multi-channel audio support (up to device capabilities)
- Automatic device release to share hardware with other processes

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

This project is licensed under the **Apache License 2.0**, matching the sendspin-cpp library license for full compatibility.

## Configuration

The SendSpin client supports both command-line arguments and configuration files for flexible deployment.

### Configuration File

Create `/etc/sendspin-client/sendspin-client.conf` with your settings:

```conf
# Friendly name
FRIENDLY_NAME = "My SendSpin Client"

# Audio device
AUDIO_DEVICE = 1

# ALSA mixer for hardware volume
ALSA_MIXER_SPEC = "1:Digital"

# Auto-release hardware after 60 seconds
IDLE_TIMEOUT = 60

# Drop a silent connection after 60 seconds (0 disables the watchdog)
LIVENESS_TIMEOUT = 60

# Allow servers that have not paired to play (default: false; pairing is
# required otherwise - see the Pairing section above)
UNPAIRED_ACCESS = false

# Log level
LOG_LEVEL = "info"
```

### Command-Line Usage

```bash
# Use config file
./sendspin-client

# Override with command-line
./sendspin-client -d 2 "Custom Name"

# Specify custom config file
./sendspin-client -c /path/to/config.conf
```

See `CONFIGURATION.md` for complete configuration documentation.

## Debian Packaging

This project includes complete Debian packaging for easy distribution:

### Build the Package

```bash
mkdir -p build && cd build
cmake .. -DBUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release
make
dpkg-buildpackage -us -uc
```

### Install the Package

```bash
sudo dpkg -i ../sendspin-client_*.deb
sudo apt --fix-broken install
```

### Systemd Service

```bash
sudo systemctl enable sendspin-client
sudo systemctl start sendspin-client
sudo systemctl status sendspin-client
```

The service's pairing token appears in its journal at every startup; see the
[Pairing](#pairing) section above.

The project uses GitHub Actions for automated CI/CD with support for multiple distributions including Debian Trixie, Ubuntu 24.04, and Ubuntu 26.04.

## Contributing

Contributions are welcome! Please open issues for bugs and feature requests, and submit pull requests for improvements.

## Support

For support, please open a GitHub issue or check the [SendSpin documentation](https://sendspin.com/docs).
