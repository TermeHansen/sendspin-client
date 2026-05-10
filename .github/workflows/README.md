# GitHub Actions Workflow for Debian Package

This directory contains the GitHub Actions workflow for automatically building Debian packages.

## 🚀 Workflow: `build-debian-package.yml`

### Triggers

The workflow runs on:
- **Pushes to main branch** (excluding documentation changes)
- **Pull requests to main branch**
- **Published releases** (creates GitHub release with package)

### Build Process

1. **Checkout** - Gets the code with submodules
2. **Install Dependencies** - Sets up build environment
3. **Apply Patch** - Applies the disable-examples patch
4. **Configure CMake** - Sets up the build with optimizations
5. **Build Project** - Compiles the SendSpin client
6. **Create Package** - Generates .deb file using CPack
7. **Lint Package** - Runs lintian for quality checks
8. **Upload Artifact** - Makes package available for download
9. **Create Release** - Publishes package on GitHub releases (for tags)

### Output

- **Debian Package**: `sendspin-client_0.1.0-1_amd64.deb`
- **Artifact**: Available for 7 days after build
- **Release**: Automatically created when pushing tags

## 📋 Usage

### Building on Every Push

Just push to the `main` branch:
```bash
git push origin main
```

The workflow will automatically:
- Build the Debian package
- Upload it as a build artifact
- Show package contents in the logs

### Creating a Release

1. **Tag your commit**:
   ```bash
   git tag -a v0.1.0 -m "Release v0.1.0"
   git push origin v0.1.0
   ```

2. **Workflow will**:
   - Build the package
   - Create a GitHub Release
   - Attach the .deb file to the release
   - Generate release notes automatically

### Downloading Artifacts

After any build:
1. Go to **Actions** tab in GitHub
2. Click on the latest workflow run
3. Download the `debian-package` artifact

## 🔧 Customization

### Change Package Version

Edit in multiple places:
1. **CMakeLists.txt**: `CPACK_DEBIAN_PACKAGE_VERSION`
2. **Workflow file**: `DEB_VERSION` environment variable
3. **debian/changelog**: First line

### Change Architecture

Modify in `CMakeLists.txt`:
```cmake
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")  # For Raspberry Pi
```

### Add Dependencies

Update in `CMakeLists.txt`:
```cmake
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libportaudio2, libasound2, avahi-daemon, pulseaudio")
```

## 📦 Package Information

### Package Contents

The generated package includes:
- `/usr/bin/sendspin-client` - Main executable
- Systemd service file
- Configuration file template
- Documentation

### Dependencies

Runtime dependencies (automatically installed):
- `libportaudio2` - Audio I/O
- `libasound2` - ALSA support
- `avahi-daemon` - mDNS/zeroconf

Build dependencies (for building from source):
- `build-essential`
- `debhelper`
- `cmake`
- `portaudio19-dev`
- `libavahi-compat-libdnssd-dev`

## 🎯 Best Practices

### Version Tagging

Use semantic versioning:
```bash
# For releases
git tag v1.0.0
git tag v1.1.0
git tag v2.0.0

# For pre-releases
git tag v1.0.0-rc1
git tag v1.0.0-beta
```

### Branch Protection

Recommended branch protection rules:
- Require pull request reviews
- Require status checks to pass
- Include Administrators

### Workflow Optimization

The workflow:
- **Ignores documentation changes** to avoid unnecessary builds
- **Uses Ubuntu 20.04** (compatible with Debian Bullseye)
- **Parallel builds** with `-j$(nproc)` for faster compilation
- **Continues on lintian warnings** (doesn't fail build)

## 🐛 Troubleshooting

### Workflow Fails on Dependencies

Check the error message and install missing dependencies in the workflow file.

### Package Not Generated

Ensure:
- CPack is properly configured in CMakeLists.txt
- `include(CPack)` is present
- CMake configuration succeeds

### Release Not Created

Verify:
- You pushed a tag (not just a branch)
- Tag format is correct (e.g., `v1.0.0`)
- GitHub token has proper permissions

## 📈 Monitoring

### Workflow Status Badge

Add this to your README.md:
```markdown
![Build Status](https://github.com/TermeHansen/sendspin-client/actions/workflows/build-debian-package.yml/badge.svg)
```

### Viewing Logs

1. Go to **Actions** tab
2. Click on the workflow run
3. Expand each step to see detailed logs

## 🔄 CI/CD Integration

### Automatic Deployment

Extend the workflow to deploy to:
- **PPA** (Personal Package Archive)
- **PackageCloud**
- **Your own APT repository**

### Multiple Architectures

Add matrix build for arm64 (Raspberry Pi):
```yaml
strategy:
  matrix:
    arch: [amd64, arm64]
```

## 📚 References

- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [CPack Documentation](https://cmake.org/cmake/help/latest/module/CPack.html)
- [Debian Packaging Guide](https://www.debian.org/doc/manuals/packaging-manual/packaging-manual.html)

## 🎉 Workflow Features

✅ **Automatic builds** on every push
✅ **Release management** with GitHub Releases
✅ **Artifact storage** for manual downloads
✅ **Lintian checks** for package quality
✅ **Parallel builds** for speed
✅ **Submodule support** for sendspin-cpp
✅ **Patch management** for clean builds
✅ **Multi-architecture ready** (can be extended)

This workflow provides a complete CI/CD pipeline for building and distributing your SendSpin client as a professional Debian package!
