#!/bin/bash
# Build script for virtmacosvf-helper

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../../../../../build/src/macosvf/helper"
HELPER_SOURCES=(
  "virtmacosvf-helper_main.m"
  "VZManager.m"
  "VZXPCService.m"
)

echo "Building virtmacosvf-helper..."

# Create build directory
mkdir -p "$BUILD_DIR"

# Compile helper executable
clang -fobjc-arc \
  -framework Cocoa \
  -framework Virtualization \
  -framework Foundation \
  -I"${SCRIPT_DIR}/.." \
  -o "${BUILD_DIR}/virtmacosvf-helper" \
  "${SCRIPT_DIR}/virtmacosvf-helper_main.m" \
  "${SCRIPT_DIR}/VZManager.m" \
  "${SCRIPT_DIR}/VZXPCService.m"

echo "Helper executable built: ${BUILD_DIR}/virtmacosvf-helper"

# Create app bundle structure
APP_BUNDLE="${BUILD_DIR}/virtmacosvf-helper.app"
CONTENTS_DIR="${APP_BUNDLE}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"

rm -rf "$APP_BUNDLE"
mkdir -p "$MACOS_DIR"
mkdir -p "$RESOURCES_DIR"

# Copy executable
cp "${BUILD_DIR}/virtmacosvf-helper" "${MACOS_DIR}/virtmacosvf-helper"
chmod +x "${MACOS_DIR}/virtmacosvf-helper"

# Create Info.plist
cat > "${CONTENTS_DIR}/Info.plist" << 'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>virtmacosvf-helper</string>
    <key>CFBundleIdentifier</key>
    <string>org.libvirt.virtmacosvf-helper</string>
    <key>CFBundleName</key>
    <string>virtmacosvf-helper</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>LSBackgroundOnly</key>
    <true/>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
PLIST

# Copy entitlements
cp "${SCRIPT_DIR}/helper_entitlements.plist" "${CONTENTS_DIR}/entitlements.plist"

# Code sign with entitlements
codesign --force --deep --sign - --entitlements "${CONTENTS_DIR}/entitlements.plist" "${APP_BUNDLE}"

echo "Helper app bundle created: ${APP_BUNDLE}"
