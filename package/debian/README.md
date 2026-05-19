# Debian Packaging for SendSpin Client

This directory contains the Debian packaging files to build a `.deb` package for the SendSpin client.

## Building the Debian Package

### Prerequisites

```bash
sudo apt update
sudo apt install build-essential debhelper devscripts dh-make cmake \
                libportaudio2 portaudio19-dev libavahi-compat-libdnssd-dev \
                git patch
```

### Build Steps

```bash
# Make sure you're in the project root directory
cd /path/to/sendspin-client

# Apply the disable-examples patch
cd sendspin-cpp
patch -p1 < ../disable-examples.patch
cd ..

# Build the project
mkdir -p build
cd build
cmake .. -DBUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release
make
cd ..

# Build the Debian package
debuild -us -uc

# Or for a cleaner build:
dpkg-buildpackage -us -uc
```

### Build Output

The build process will create several files in the parent directory:
- `sendspin-client_0.1.0-1_amd64.deb` - The main package
- `sendspin-client-dbgsym_0.1.0-1_amd64.ddeb` - Debug symbols (optional)
- `sendspin-client_0.1.0-1_amd64.buildinfo` - Build information

## Installing the Package

```bash
# Install the package
sudo dpkg -i ../sendspin-client_0.1.0-1_amd64.deb

# Fix any missing dependencies
sudo apt --fix-broken install
```

## Package Contents

The package installs:
- `/usr/bin/sendspin-client` - Main executable
- `/lib/systemd/system/sendspin-client.service` - Systemd service
- `/etc/sendspin-client/sendspin-client.conf` - Configuration file

## Using the Systemd Service

### Enable and start the service

```bash
sudo systemctl enable sendspin-client
sudo systemctl start sendspin-client
```

### Check service status

```bash
sudo systemctl status sendspin-client
journalctl -u sendspin-client -f  # Follow logs
```

### Configure the service

Edit the configuration file:

```bash
sudo nano /etc/sendspin-client/sendspin-client.conf
```

Then restart the service:

```bash
sudo systemctl restart sendspin-client
```

## Customizing the Package

### Change Version

Edit `debian/changelog` and update the version number.

### Add Dependencies

Edit `debian/control` and add any additional dependencies to the `Depends:` field.

### Modify Installation

Edit `debian/rules` to change installation paths or add additional files.

## Troubleshooting

### Missing Build Dependencies

```bash
sudo apt build-dep .
```

### Clean Build

```bash
git clean -xdf
rm -rf build
```

### Lintian Checks

```bash
lintian ../sendspin-client_0.1.0-1_amd64.deb
```

## Package Structure

```
debian/
├── changelog              # Package version history
├── control                # Package metadata and dependencies
├── rules                  # Build rules
├── sendspin-client.service # Systemd service file
├── sendspin-client.conf   # Default configuration
├── install                # Installation script
├── compat                 # Debhelper compatibility level
└── README.md              # This file
```

## Advanced: Creating a PPA

If you want to create a Personal Package Archive (PPA):

```bash
# Install required tools
sudo apt install ubuntu-dev-tools

# Create a PPA on Launchpad
# Upload the source package
dput ppa:your-ppa-name/ppa ../sendspin-client_0.1.0-1_source.changes
```

## Notes

- The package is configured to build with `BUILD_EXAMPLES=OFF`
- Release build type is used for optimization
- Systemd service is included for easy management
- Configuration file provides common settings
