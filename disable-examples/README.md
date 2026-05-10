# Disable Examples Patch

This patch modifies the sendspin-cpp CMakeLists.txt to make example building optional.

## Purpose

When using sendspin-cpp as a submodule in another project (like sendspin-client), you typically don't want to build the example applications. This patch adds a `BUILD_EXAMPLES` option that allows controlling whether examples are built.

## Application

By default, examples are built unless `BUILD_EXAMPLES` is explicitly set to `OFF`.

### To disable examples (recommended for submodule usage):

```cmake
# In your main CMakeLists.txt, before adding the submodule:
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable building sendspin-cpp examples" FORCE)

add_subdirectory(sendspin-cpp)
```

### To enable examples (for development/testing):

```cmake
set(BUILD_EXAMPLES ON CACHE BOOL "Enable building sendspin-cpp examples" FORCE)
```

## Changes Made

- Modified `CMakeLists.txt` to wrap example subdirectories in a conditional block
- Added status message when examples are skipped
- Maintains backward compatibility (examples still build by default if `BUILD_EXAMPLES` is not defined)

## Applying the Patch

```bash
cd sendspin-cpp
patch -p1 < ../disable-examples.patch
```

## Reverting the Patch

```bash
cd sendspin-cpp
git checkout CMakeLists.txt
```

## Benefits

- **Faster builds**: Skip unnecessary example compilation
- **Cleaner output**: Only build what you need
- **Better separation**: Library vs. examples
- **Flexible**: Can be enabled/disabled as needed
