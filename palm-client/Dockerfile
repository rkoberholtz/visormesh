# Palm OS 68k build environment: prc-tools-remix (m68k-palmos-gcc), PilRC,
# and the Palm OS SDKs. The packages are amd64-only; on Apple Silicon Docker
# runs this under emulation (build/run with --platform linux/amd64), which is fine for builds this small.
FROM debian:bookworm-slim

ARG PRC_TOOLS_VER=2.3.6
ARG PILRC_TAG=2.3.91
ARG PILRC_VER=3.2.91
ARG PALM_SDK_COMMIT=1fa22066ca0f8b74949c14dd1d626294145d1c09

RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl unzip make gcc libc6-dev \
 && curl -fsSLo /tmp/prc.deb https://github.com/jichu4n/prc-tools-remix/releases/download/v${PRC_TOOLS_VER}/prc-tools-remix_${PRC_TOOLS_VER}.bookworm_amd64.deb \
 && curl -fsSLo /tmp/pilrc.deb https://github.com/jichu4n/pilrc/releases/download/${PILRC_TAG}/pilrc_${PILRC_VER}.bookworm_amd64.deb \
 && apt-get install -y --no-install-recommends /tmp/prc.deb /tmp/pilrc.deb \
 && curl -fsSLo /tmp/sdk.zip https://github.com/jichu4n/palm-os-sdk/archive/${PALM_SDK_COMMIT}.zip \
 && unzip -q /tmp/sdk.zip -d /tmp \
 && mv /tmp/palm-os-sdk-${PALM_SDK_COMMIT} /opt/palmdev \
 && palmdev-prep -d sdk-3.5 \
 && rm -rf /tmp/*.deb /tmp/sdk.zip /var/lib/apt/lists/*

WORKDIR /src
