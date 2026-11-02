# VisorMesh

A MeshCore client for the Handspring Visor (Palm OS 3.5.2H3.0).

The Visor works as a terminal for a MeshCore **companion radio**. The radio (a Seeed XIAO nRF52840 with a Wio-SX1262) handles LoRa, routing and crypto. The Visor only speaks the companion framing protocol over a byte stream. The radio sits on a custom Springboard module with its own LiPo, so it keeps meshing while the Visor sleeps.

See [docs/design.md](docs/design.md) for the architecture and the reasons behind each decision, and [docs/progress.md](docs/progress.md) for the current status and log.

## Repo layout

| Path | What |
|---|---|
| `hardware/sb-lora/` | SB-LoRa Springboard carrier board: KiCad, fab outputs (JLCPCB), generator scripts. See its [README](hardware/sb-lora/README.md) |
| `firmware/` | Patches to MeshCore: companion firmware on the XIAO's hardware UART (D6/D7) |
| `palm-client/` | Palm OS client app (`.prc`), not started yet |
| `docs/design.md` | Architecture, protocol notes and decisions |
| `docs/progress.md` | Status, milestones and a dated log |
| `docs/upstream-issues.md` | Problems found in pilot-link, PilRC/prc-tools and MeshCore, with evidence and workarounds, ready to report |
| `docs/reference/` | Third-party reference: Handspring Springboard dev guide and mechanical spec, springSD v1.1 hardware |

## Versioning

Hardware revisions are tagged in git (`hw-v0.1`, `hw-v0.2`, ...), so the files that went to fab for each order stay recoverable. Each board's README records its revision and status.
