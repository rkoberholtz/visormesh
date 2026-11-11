# Palm OS 68k build environment: prc-tools-remix (m68k-palmos-gcc), PilRC,
# and the Palm OS SDKs. The packages are amd64-only; on Apple Silicon Docker
# runs this under emulation (build/run with --platform linux/amd64), which is fine for builds this small.
# The versions are pinned in tools/install_toolchain.sh, which the release workflow also runs.
FROM debian:bookworm-slim

COPY tools/install_toolchain.sh /tmp/install_toolchain.sh
RUN sh /tmp/install_toolchain.sh && rm /tmp/install_toolchain.sh

WORKDIR /src
