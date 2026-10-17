#!/bin/bash
set -e

VERSION="1.0.0"
ARCH="amd64"
PKG_NAME="asharp_${VERSION}_${ARCH}"

# 1. Clean and compile the release build
make clean
make

# 2. Set up the debian package structure
mkdir -p dist/${PKG_NAME}/usr/bin
mkdir -p dist/${PKG_NAME}/DEBIAN

# 3. Copy the binary and metadata
cp asharp dist/${PKG_NAME}/usr/bin/
cp debian/control dist/${PKG_NAME}/DEBIAN/

# 4. Strip debug symbols and set execution permissions
strip dist/${PKG_NAME}/usr/bin/asharp
chmod 755 dist/${PKG_NAME}/usr/bin/asharp

# 5. Build the .deb package
cd dist
dpkg-deb --build ${PKG_NAME}

# 6. Generate the APT repository index
dpkg-scanpackages . /dev/null | gzip -9c > Packages.gz