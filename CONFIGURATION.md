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
| `LOG_LEVEL` | string | "info" | Log level: none, error, warn, info, debug, verbose |
| `CONNECT_URL` | string | "" (empty) | WebSocket URL to connect to (leave empty to listen) |
| `ENABLE_MDNS` | boolean | true | Enable mDNS service advertisement |

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
```

## 🚀 Usage

### Command-Line Only

```bash
# Basic usage
./sendspin-client "My Client Name"

# With specific device
./sendspin-client -d 2 "My Client"

# With ALSA mixer
./sendspin-client -m "1:Digital" "My Client"
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
