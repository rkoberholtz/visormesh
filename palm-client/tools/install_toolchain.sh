#!/bin/sh
# Installs the Palm OS 68k toolchain on Debian bookworm (amd64): prc-tools-remix,
# PilRC and the Palm OS SDKs. Shared by the Dockerfile and the release workflow
# (.gitea/workflows/release.yml), so both build with the same pinned versions.
set -eu

PRC_TOOLS_VER=2.3.6
PILRC_TAG=2.3.91
PILRC_VER=3.2.91
PALM_SDK_COMMIT=1fa22066ca0f8b74949c14dd1d626294145d1c09

apt-get update
apt-get install -y --no-install-recommends ca-certificates curl unzip make gcc libc6-dev python3-minimal
curl -fsSLo /tmp/prc.deb https://github.com/jichu4n/prc-tools-remix/releases/download/v${PRC_TOOLS_VER}/prc-tools-remix_${PRC_TOOLS_VER}.bookworm_amd64.deb
curl -fsSLo /tmp/pilrc.deb https://github.com/jichu4n/pilrc/releases/download/${PILRC_TAG}/pilrc_${PILRC_VER}.bookworm_amd64.deb
apt-get install -y --no-install-recommends /tmp/prc.deb /tmp/pilrc.deb
curl -fsSLo /tmp/sdk.zip https://github.com/jichu4n/palm-os-sdk/archive/${PALM_SDK_COMMIT}.zip
unzip -q /tmp/sdk.zip -d /tmp
mv /tmp/palm-os-sdk-${PALM_SDK_COMMIT} /opt/palmdev
palmdev-prep -d sdk-3.5
rm -rf /tmp/*.deb /tmp/sdk.zip /var/lib/apt/lists/*
