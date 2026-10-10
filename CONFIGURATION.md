# SendSpin Client Configuration

The SendSpin client supports both command-line arguments and configuration files, with command-line arguments taking precedence over configuration file settings.

## 📖 Configuration File Format

The configuration file uses a simple key-value format:

```conf
# This is a comment
KEY = value
ANOTHER_KEY = "quoted value"
```

### Supported Configuration Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `FRIENDLY_NAME` | string | "SendSpin Client" | Friendly name shown to other devices |
| `AUDIO_DEVICE` | integer | -1 (default) | Audio device index to use |
| `ALSA_MIXER_SPEC` | string | "" (empty) | ALSA mixer specification (format: "card:control") |
| `IDLE_TIMEOUT` | integer | 0 (disabled) | Idle timeout in seconds before releasing the audio device |
| `LIVENESS_TIMEOUT` | integer | 60 (library default) | Seconds of inbound silence before an established connection is dropped as dead (0 = disable) |
| `RECONNECT_ON_LOSS` | boolean | true | Reconnect to `CONNECT_URL` after the liveness watchdog drops the connection. Outbound connections only; inbound (discovery) connections are left to their server. Backoff: 1 s doubling up to 30 s. |
| `LOG_LEVEL` | string | "info" | Log level: none, error, warn, info, debug, verbose |
| `CONNECT_URL` | string | "" (empty) | WebSocket URL to connect to (leave empty to listen) |
| `ENABLE_MDNS` | boolean | true | Enable mDNS service advertisement |
| `UNPAIRED_ACCESS` | boolean | false | Admit servers that have not paired. sendspin-cpp v0.9 encrypts every connection (Noise) and requires a server to pair before it may play; set true to allow unpaired servers to play (the `-a` flag). |
| `PAIRING_PSK_HEX` | string | unset | Factory-style Pairing PSK as 64 hex characters (32 bytes). When set, it replaces the library-generated Pairing PSK on every start and is never written to the state directory; remove the key to return to the stored one. Must be drawn from a CSPRNG per device (an all-zero key or the published Sentinel PSK is refused at startup). The pairing token (`-T`) carries whichever PSK is in use. |

### Example Configuration

```conf
# /etc/sendspin-client/sendspin-client.conf

# Friendly name for this client
FRIENDLY_NAME = "Living Room SendSpin"

# Use specific audio device
AUDIO_DEVICE = 1

# ALSA hardware mixer for volume control
ALSA_MIXER_SPEC = "1:Digital"

# Set log level
LOG_LEVEL = "info"

# Connect to a specific server instead of listening
# CONNECT_URL = "ws://192.168.1.100:8928/sendspin"

# Enable mDNS advertisement
ENABLE_MDNS = true

# Allow servers that have not paired to play (default: false).
# With the v0.9 Noise protocol a server must pair before it may play;
# set true to admit unpaired servers (same as the -a flag).
UNPAIRED_ACCESS = false

# Factory-style Pairing PSK, 64 hex characters (32 bytes). Optional:
# without it the client generates one on first boot and prints the
# matching pairing token at startup (and with the -T flag). Must be
# unique per device and randomly generated, never reused across devices.
# PAIRING_PSK_HEX = ""

# Release the audio hardware so other apps can use it when stopped for 60s
IDLE_TIMEOUT = 60

# Drop a silent connection after 30 seconds (0 disables the watchdog)
LIVENESS_TIMEOUT = 30
```

## 🔑 Pairing

sendspin-cpp v0.9 encrypts every connection (Noise), so a server must pair with the client
before it may play. On first boot the client generates a Pairing PSK, stores it in the state
directory, and derives a **pairing token** from it — the `SP:...` string a server such as Music
Assistant asks for. The token carries the client identity and the PSK; entering it in the
server pairs the two, after which the server reconnects with a long-term record and is trusted
automatically.

```bash
# Print the pairing token and exit
sendspin-client -T
```

The token is also printed at every startup, next to the Client ID. It is stable for the
lifetime of the PSK, so it can be entered once and reused after reboots.

To run the client with a specific PSK instead of the generated one (factory-style
provisioning), set `PAIRING_PSK_HEX` in the config file to 64 hex characters. It must be
unique per device and randomly generated; removing the key returns the client to the stored
PSK. Servers that should play without pairing at all can be admitted with `UNPAIRED_ACCESS =
true` (the `-a` flag).

## 🚀 Usage

### Command-Line Only

```bash
# Basic usage
./sendspin-client "My Client Name"

# With specific device
./sendspin-client -d 2 "My Client"

# With ALSA mixer
./sendspin-client -m "1:Digital" "My Client"

# With Idle Timeout (e.g. 30 seconds)
./sendspin-client -t 30 "My Client"

# Show the pairing token for a server to pair with this client
./sendspin-client -T
```

### Configuration File Only

```bash
# Use default config file
./sendspin-client

# Or specify custom config file
./sendspin-client -c /path/to/custom.conf
```

### Mixed Usage (Command-Line Overrides Config)

```bash
# Config file provides defaults, command-line overrides
./sendspin-client -c /etc/sendspin-client.conf -d 3 "Custom Name"
```

## 📁 Configuration File Locations

### Default Location

```
/etc/sendspin-client/sendspin-client.conf
```

### Custom Location

```bash
./sendspin-client -c /path/to/your/config.conf
```

### Systemd Service Configuration

When running as a systemd service, the configuration file is automatically loaded from the default location.

## 🎛️ Configuration Priority

1. **Command-line arguments** (highest priority)
2. **Configuration file** (medium priority)
3. **Default values** (lowest priority)

### Example Priority

```bash
# Config file has: AUDIO_DEVICE = 1
# Command line: -d 2
# Result: Uses device 2 (command-line overrides config file)
```

## 🔧 Advanced Configuration

### Environment Variables

You can also use environment variables to override settings:

```bash
SENDSPIN_FRIENDLY_NAME="My Client" ./sendspin-client
```

### Multiple Configuration Files

Chain multiple configuration files:

```bash
# Load base config, then override with local config
./sendspin-client -c /etc/sendspin-base.conf -c ~/.sendspin.conf
```

## 📋 Configuration Examples

### Raspberry Pi with HDMI Audio

```conf
FRIENDLY_NAME = "Raspberry Pi HDMI"
AUDIO_DEVICE = 0
LOG_LEVEL = "debug"
```

### Headless Server with USB Audio

```conf
FRIENDLY_NAME = "Music Server"
AUDIO_DEVICE = 1
ALSA_MIXER_SPEC = "1:PCM"
ENABLE_MDNS = true
IDLE_TIMEOUT = 10
```

### Development Configuration

```conf
FRIENDLY_NAME = "Dev Client"
LOG_LEVEL = "verbose"
CONNECT_URL = "ws://localhost:8928/sendspin"
```

## 🐛 Troubleshooting

### Configuration File Not Found

```
Warning: Could not read configuration file /etc/sendspin-client/sendspin-client.conf
```

**Solution:** Create the configuration file or specify a different path with `-c`.

### Invalid Configuration Values

```
Warning: Unknown log level in config: debugg
```

**Solution:** Check your configuration file for typos and valid values.

### Permission Issues

```
Warning: Could not read configuration file /etc/sendspin-client/sendspin-client.conf
```

**Solution:** Ensure the file has proper permissions:
```bash
sudo chmod 644 /etc/sendspin-client/sendspin-client.conf
sudo chown root:root /etc/sendspin-client/sendspin-client.conf
```

## 🔄 Configuration Management

### Backup Configuration

```bash
sudo cp /etc/sendspin-client/sendspin-client.conf /etc/sendspin-client/sendspin-client.conf.backup
```

### Restore Configuration

```bash
sudo cp /etc/sendspin-client/sendspin-client.conf.backup /etc/sendspin-client/sendspin-client.conf
```

### Validate Configuration

```bash
# Test with verbose logging
./sendspin-client -v -c /etc/sendspin-client/sendspin-client.conf
```

## 📚 Reference

### All Command-Line Options

```
Usage: sendspin-client [options] [name]
  name          Friendly name (default: "SendSpin Client")

Options:
  -u URL        Connect to a WebSocket URL
  -l LEVEL      Log level: none, error, warn, info (default), debug, verbose
  -v            Verbose logging (same as -l verbose)
  -q            Quiet logging (same as -l error)
  -d DEVICE     Select audio device by index (use -L to list devices)
  -m MIXER      Use ALSA hardware mixer for volume control
  -c FILE       Use configuration file (default: /etc/sendspin-client/sendspin-client.conf)
  -t SECONDS    Idle timeout in seconds before releasing audio device (0 = disable, default)
  -a            Allow unpaired servers to play (default: off; they must pair first)
  -T            Print the pairing token and exit
  -L            List available audio devices and exit
  -h            Show this help
```

### Configuration File Template

```conf
# SendSpin Client Configuration
# Place this file in /etc/sendspin-client/sendspin-client.conf

# Default friendly name for the client
FRIENDLY_NAME="SendSpin Client"

# Default audio device index (-1 for default)
AUDIO_DEVICE=-1

# ALSA mixer specification for hardware volume control
# Format: "card:control" (e.g., "1:Digital")
ALSA_MIXER_SPEC=""

# Log level: none, error, warn, info, debug, verbose
LOG_LEVEL="info"

# WebSocket URL to connect to (leave empty to listen for servers)
CONNECT_URL=""

# Enable/disable mDNS service advertisement
ENABLE_MDNS=true

# Idle timeout in seconds before releasing the audio device (0 = disable)
IDLE_TIMEOUT=0

# Seconds of inbound silence before a dead connection is dropped (0 = disable)
# (0 = disable; library default is 60)
# LIVENESS_TIMEOUT=60

# Reconnect to CONNECT_URL after the connection is dropped as dead (default true)
# RECONNECT_ON_LOSS=true
```

## 🎯 Best Practices

### 1. Use Configuration Files for Production

```bash
# Production: Use config file
sudo cp sendspin-client.conf /etc/sendspin-client/
sudo systemctl start sendspin-client
```

### 2. Use Command-Line for Testing

```bash
# Development: Use command-line for quick testing
./sendspin-client -v -d 1 "Test Client"
```

### 3. Version Control Configurations

```bash
# Store configurations in version control
git add configs/
git commit -m "Add production configuration"
```

### 4. Document Your Configuration

```conf
# Add comments to explain your settings
# Using device 1 for USB audio interface
AUDIO_DEVICE=1

# Digital mixer for hardware volume control
ALSA_MIXER_SPEC="1:Digital"
```

## 📈 Advanced Usage

### Dynamic Configuration Reloading

The client currently doesn't support live configuration reloading. To apply configuration changes:

```bash
sudo systemctl restart sendspin-client
```

### Configuration Validation

Add validation to your configuration:

```bash
# Test configuration before deploying
./sendspin-client -c /path/to/config.conf -L
```

### Environment-Specific Configurations

```bash
# Development environment
cp configs/dev.conf /etc/sendspin-client/sendspin-client.conf

# Production environment  
cp configs/prod.conf /etc/sendspin-client/sendspin-client.conf
```

This comprehensive configuration system gives you flexibility in how you deploy and manage your SendSpin client, whether you prefer command-line arguments for quick testing or configuration files for production deployments!
