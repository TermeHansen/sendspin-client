# SendSpin Client - Packaging and Deployment Guide

This guide covers how to package and deploy the SendSpin client as a Debian service.

## 📦 Package Structure

```
sendspin-client/
├── disable-examples.patch          # Patch to disable example builds
├── disable-examples/README.md      # Patch documentation
├── debian/                         # Debian packaging files
│   ├── changelog                  # Version history
│   ├── control                    # Package metadata
│   ├── rules                      # Build rules
│   ├── sendspin-client.service    # Systemd service
│   ├── sendspin-client.conf       # Configuration template
│   ├── install                    # Installation script
│   ├── compat                     # Debhelper version
│   └── README.md                  # Packaging guide
└── PACKAGING.md                   # This file
```

## 🔧 Quick Start

### 1. Apply the Patch

```bash
cd sendspin-cpp
patch -p1 < ../disable-examples.patch
cd ..
```

### 2. Build the Package

```bash
# Install build dependencies
sudo apt install build-essential debhelper devscripts cmake \
                libportaudio2 portaudio19-dev libavahi-compat-libdnssd-dev

# Build
mkdir -p build && cd build
cmake .. -DBUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release
make
cd ..

# Create Debian package
dpkg-buildpackage -us -uc
```

### 3. Install the Package

```bash
sudo dpkg -i ../sendspin-client_*.deb
sudo apt --fix-broken install  # Install any missing dependencies
```

### 4. Start the Service

```bash
sudo systemctl enable sendspin-client
sudo systemctl start sendspin-client
sudo systemctl status sendspin-client
```

## 🎯 Systemd Service Features

### Service Configuration

The service file includes:
- **Automatic restart** on failure (5-second delay)
- **Audio group membership** for device access
- **Real-time priority** for audio processing
- **Network dependency** to ensure connectivity

### Customizing the Service

Edit `/lib/systemd/system/sendspin-client.service`:

```ini
[Service]
# Change the executable path if needed
ExecStart=/usr/local/bin/sendspin-client "My Custom Name"

# Add environment variables
Environment="PULSE_CLIENT=sendspin-client"
Environment="ALSA_CARD=1"

# Adjust user/group if needed
User=sendspin
Group=audio
```

After changes:
```bash
sudo systemctl daemon-reload
sudo systemctl restart sendspin-client
```

## 🔊 Audio Configuration

### ALSA Setup

For Raspberry Pi or other ALSA systems:

```bash
# List available audio devices
aplay -l
arecord -l

# Test a specific device
speaker-test -D plughw:1,0 -c 2

# Set default device in .asoundrc
nano ~/.asoundrc
```

Example `.asoundrc`:
```conf
defaults.pcm.card 1
defaults.pcm.device 0
defaults.ctl.card 1
```

### PulseAudio Setup

```bash
# Install PulseAudio
sudo apt install pulseaudio

# List PulseAudio devices
pactl list sinks

# Set default sink
pactl set-default-sink alsa_output.platform-soc_sound.stereo
```

## 📝 Configuration File

Edit `/etc/sendspin-client/sendspin-client.conf`:

```ini
# Friendly name (shown to other devices)
FRIENDLY_NAME="Living Room SendSpin"

# Specific audio device
AUDIO_DEVICE=1

# ALSA hardware mixer for volume control
ALSA_MIXER_SPEC="1:Digital"

# Log level (none, error, warn, info, debug, verbose)
LOG_LEVEL="info"

# Connect to specific server instead of listening
# CONNECT_URL="ws://192.168.1.100:8928/sendspin"

# Enable/disable mDNS advertisement
ENABLE_MDNS=true
```

## 🔄 Update Process

### Manual Update

```bash
# Stop the service
sudo systemctl stop sendspin-client

# Install new version
sudo dpkg -i sendspin-client_*.deb

# Restart the service
sudo systemctl start sendspin-client
```

### Automatic Updates (via APT Repository)

1. Set up a PPA or personal repository
2. Add the repository to sources:
   ```bash
   sudo add-apt-repository ppa:your-repo/ppa
   sudo apt update
   ```
3. Update normally:
   ```bash
   sudo apt upgrade sendspin-client
   ```

## 🐛 Debugging

### Check Service Logs

```bash
# View recent logs
journalctl -u sendspin-client -n 50

# Follow logs in real-time
journalctl -u sendspin-client -f

# Check status
sudo systemctl status sendspin-client
```

### Common Issues

**Issue: Audio device not found**
```bash
# Check ALSA devices
aplay -l

# Check PortAudio devices
./sendspin-client -L
```

**Issue: Permission denied for audio device**
```bash
# Add user to audio group
sudo usermod -a -G audio sendspin
sudo usermod -a -G pulse-access sendspin

# Restart service
sudo systemctl restart sendspin-client
```

**Issue: Network connectivity problems**
```bash
# Check mDNS advertisement
avahi-browse -a | grep sendspin

# Test WebSocket connection manually
wscat -c ws://localhost:8928/sendspin
```

## 📦 Advanced Packaging

### Creating a PPA

```bash
# Install tools
sudo apt install ubuntu-dev-tools

# Create PPA on Launchpad
# Upload source package
dput ppa:your-username/ppa ../sendspin-client_*.source.changes
```

### Building for Multiple Architectures

```bash
# Cross-compile for ARM
sudo apt install gcc-arm-linux-gnueabihf
CC=arm-linux-gnueabihf-gcc CXX=arm-linux-gnueabihf-g++ dpkg-buildpackage -aarmhf
```

## 🎯 Best Practices

### Service Management

```bash
# Check if service is running
systemctl is-active sendspin-client

# Enable auto-start on boot
sudo systemctl enable sendspin-client

# Disable auto-start
sudo systemctl disable sendspin-client
```

### Multiple Instances

To run multiple clients:

```bash
# Create separate service files
cp /lib/systemd/system/sendspin-client.service /lib/systemd/system/sendspin-client@.service

# Edit the template to use %i
ExecStart=/usr/bin/sendspin-client "Client %i"

# Start instances
sudo systemctl start sendspin-client@1
sudo systemctl start sendspin-client@2
```

### Resource Limits

Add to service file:
```ini
[Service]
MemoryLimit=500M
CPUQuota=50%
Nice=10
```

## 📚 References

- [Debian Packaging Guide](https://www.debian.org/doc/manuals/packaging-manual/packaging-manual.html)
- [Systemd Service Documentation](https://www.freedesktop.org/software/systemd/man/systemd.service.html)
- [ALSA Configuration](https://www.alsa-project.org/wiki/Main_Page)
- [PortAudio Documentation](http://www.portaudio.com/docs.html)

## 🔧 Development Notes

### Patch Management

The `disable-examples.patch` should be applied during:
- Package build process
- CI/CD pipelines
- Local development

### Versioning

Update version in:
- `debian/changelog`
- `CMakeLists.txt` (if applicable)
- `sendspin-client.conf` (comment)

### Dependencies

Key dependencies:
- `libportaudio2` - Audio I/O
- `libavahi-compat-libdnssd-dev` - mDNS/zeroconf
- `sendspin-cpp` (v0.4.0) - Core library

## 🎉 Deployment Checklist

- [ ] Apply disable-examples.patch
- [ ] Test build with `cmake .. -DBUILD_EXAMPLES=OFF`
- [ ] Create Debian package with `dpkg-buildpackage`
- [ ] Install package with `dpkg -i`
- [ ] Enable and start service
- [ ] Verify audio device access
- [ ] Test mDNS advertisement
- [ ] Check logs for errors
- [ ] Test client connectivity

## 📈 Monitoring

### Service Metrics

```bash
# CPU usage
top -p $(pgrep sendspin-client)

# Memory usage
ps -p $(pgrep sendspin-client) -o %mem,rss

# Network connections
ss -tp | grep sendspin
```

### Log Rotation

Add to `/etc/logrotate.d/sendspin-client`:

```conf
/var/log/sendspin-client.log {
    daily
    rotate 7
    compress
    missingok
    notifempty
    create 644 sendspin sendspin
}
```

## 🔒 Security

### Service Hardening

```ini
[Service]
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=full
ProtectHome=true
ReadWritePaths=/var/lib/sendspin-client
```

### User Isolation

```bash
# Create dedicated user
sudo useradd --system --no-create-home --shell /bin/false sendspin
sudo usermod -a -G audio,pulse,pulse-access sendspin
```

This comprehensive guide should help you package, deploy, and manage the SendSpin client as a proper Debian service! The setup includes systemd integration, configuration management, and all the necessary packaging files.
