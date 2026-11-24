# `nvctl` — Headless NVIDIA GPU Control & Telemetry Utility for FreeBSD

`nvctl` is a zero-dependency, BSD-2-Clause C utility that talks directly to `/dev/nvidiactl` (NVIDIA Resource Manager ioctls) for headless FreeBSD systems—filling the gap left by the absence of `nvidia-smi` and `nvidia-persistenced` in FreeBSD's `x11/nvidia-driver-470` (Kepler/Maxwell/Pascal) and providing a lightweight native CLI for newer driver branches.

## Verified Hardware
* **Host:** Gigabyte GA-X79-UP4 Rev 1.1 + Intel Core i7-4930K, FreeBSD 15.1 (`nvidia-driver-470` `470.256.02`)
* **GPU:** Gigabyte GeForce GTX 680 OC-4GD (`GK104`, `/dev/nvidia2` in 3-GPU mixed AMD+NVIDIA rig)
* **Verified Capabilities:** Live die temp (`36 C`, slowdown `94 C`, shutdown `125 C`), hardware power bounds (`120..260 W`, default `235 W`), live runtime power capping (`sudo nvctl -d 0 power set 150`), and zero-CPU persistence (`nvctl persist`).

## Build
```sh
make
```
Compiles cleanly with `cc -O2 -Wall -Wextra -Werror -std=c11` (zero external dependencies).

## Quick Usage
```sh
nvctl                             # Multi-GPU summary table (alias: nvctl status)
nvctl list                        # Enumerate attached GPUs (gpuId, dev, subdev)
nvctl temp                        # GPU core temp + slowdown/shutdown thresholds
nvctl -q -d 0 temp                # Bare numeric temp for shell scripts
nvctl -j status                   # JSON output for node_exporter / dashboards
nvctl power                       # Active power cap + min/default/max bounds
nvctl persist &                   # Zero-CPU persistence holder (keeps power cap sticky)
sudo nvctl -d 0 power set 150     # Set runtime power cap to 150W (bounds-checked)
nvctl -v status                   # Human-readable RM diagnostic trace
nvctl -vv probe                   # Raw RM ioctl struct-size & hex word dump
```

## Documentation
* **[`SPEC.md`](SPEC.md):** Architecture, CLI verb grammar, verified `470.256.02` RM ioctl map, FreeBSD `style(9)` coding rules, and canonical FreeBSD Ports separation.
* **[`man/nvctl.8`](man/nvctl.8):** FreeBSD `mdoc(7)` manual page (`mandoc man/nvctl.8 | less`).
