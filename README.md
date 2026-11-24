# `nvctl` — Headless NVIDIA GPU Control & Telemetry Utility for FreeBSD

`nvctl` is a zero-dependency, BSD-2-Clause C utility that talks directly to `/dev/nvidiactl` (NVIDIA Resource Manager ioctls) for headless FreeBSD systems—filling the gap left by the absence of `nvidia-smi` in FreeBSD's `x11/nvidia-driver-470` (Kepler/Maxwell/Pascal) and providing a lightweight native CLI for newer driver branches.

## Documentation
* **[`SPEC.md`](SPEC.md):** Architecture, CLI verb specification, RM ABI strategy (`470.xx` vs `5xx.xx`), FreeBSD `style(9)` coding rules, and port layout.
* **[`man/nvctl.8`](man/nvctl.8):** FreeBSD `mdoc(7)` manual page (preview on FreeBSD with `mandoc man/nvctl.8 | less`).

## Quick Usage
```sh
nvctl                       # Multi-GPU summary table (alias: nvctl status)
nvctl list                  # Enumerate attached GPUs (gpuId, BDF, UUID, VBIOS)
nvctl temp                  # GPU core / hotspot / memory temps
nvctl -q -d 0 temp          # Bare numeric temp for shell scripts
nvctl -j status             # JSON output for node_exporter / dashboards
nvctl power                 # Current draw + min/default/current/max caps
sudo nvctl -d 0 power set 150  # Set runtime power cap to 150W
nvctl probe                 # Read-only RM ioctl ABI & capability probe
```

## Status
* **Phase:** Documentation & Specification locked (`SPEC.md` + `man/nvctl.8`).
* **Next:** Scaffold `src/` (`rm_ioctl.c`, `rm_thermal.c`, `rm_pmgr.c`, `main.c`) in strict FreeBSD `style(9)` and verify `470.256.02` struct sizes on live GTX 680 (`GK104`) silicon on FreeBSD 15.1.
