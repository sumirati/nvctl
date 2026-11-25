# `nvctl` — Headless NVIDIA GPU Control & Telemetry Utility for FreeBSD

`nvctl` is a Beerware-licensed C utility that talks directly to `/dev/nvidiactl` (NVIDIA Resource Manager ioctls) for headless FreeBSD systems—filling the gap left by the absence of `nvidia-smi` and `nvidia-persistenced` in FreeBSD's `x11/nvidia-driver-470` (Kepler/Maxwell/Pascal) and providing a lightweight native CLI for newer driver branches.

## Verified Hardware
* **Host:** Gigabyte GA-X79-UP4 Rev 1.1 + Intel Core i7-4930K, FreeBSD 15.1 (`nvidia-driver-470` `470.256.02`)
* **GPU:** Gigabyte GeForce GTX 680 OC-4GD (`GK104`, `/dev/nvidia2` in 3-GPU mixed AMD+NVIDIA rig)
* **Verified Capabilities:** Live die temperature (`36 C`, slowdown `94 C`, shutdown `125 C`), hardware power bounds (`120..260 W`, default `235 W`), live runtime power capping (`sudo nvctl -d 0 power set 150`), and zero-CPU persistence (`nvctl persist`).

## Build
```sh
make
```
Compiles cleanly with `cc -O2 -Wall -Wextra -Werror -std=c11`.

## Quick Usage
```sh
nvctl                             # Multi-GPU summary table (alias: nvctl status)
nvctl list                        # Enumerate attached GPUs (/dev/nvidia<N>, PCI BDF, gpuId, model)
nvctl info                        # Detailed sysctl-style per-GPU identity & telemetry dump
nvctl temp                        # GPU core temperature + slowdown/shutdown thresholds
nvctl -q -d 0 temp                # Bare numeric temperature for shell scripts
nvctl -j status                   # JSON output for node_exporter / dashboards
nvctl power                       # Active power cap + min/default/max bounds
nvctl persist &                   # Zero-CPU persistence holder (keeps power cap sticky)
sudo nvctl -d 0 power set 150     # Set runtime power cap to 150W (bounds-checked)
nvctl -v status                   # Human-readable RM diagnostic trace
nvctl -vv probe                   # Raw RM ioctl struct-size & hex word dump
nvctl --sm                        # Emulated nvidia-smi summary (or symlink as nvidia-smi)
nvctl --bd --pid-file /tmp/nv.pid # Emulated nvidia-persistenced daemon (or symlink)
```

## Examples

### Apply and Persist a Runtime Power Cap
Because FreeBSD's `nvidia.ko` resets GPU state to VBIOS defaults when the last open client closes `/dev/nvidia<N>`, start `nvctl persist` (or `nvctl --bd`) before setting a cap so it remains active across idle periods:
```sh
nvctl list                        # Verify GPU index and /dev/nvidia<N> mapping
nvctl power                       # Check current cap and hardware min/default/max bounds
nvctl persist &                   # Hold /dev/nvidia* open at 0% CPU
sudo nvctl -d 0 power set 150     # Cap GPU 0 to 150 W (exit 0 = exact, 4 = clamped, 5 = out of bounds)
nvctl power                       # Confirm the 150 W cap stays put
```

### Boot-Time Persistence & Power Caps (`rc.d/nvctl_persist`)
Copy `rc.d/nvctl_persist` into `/usr/local/etc/rc.d/nvctl_persist` (`chmod 555`) and enable in `/etc/rc.conf`:
```sh
nvctl_persist_enable="YES"
nvctl_persist_power_caps="0:150"  # Or "all:150" for homogeneous multi-GPU rigs
```

### `nvidia-smi` & `nvidia-persistenced` Compatibility Mode
Invoke directly via `--sm` / `--bd` (safe alongside a real `nvidia-smi`), or via symlink:
```sh
nvctl --sm -L
nvctl --sm --query-gpu=index,name,temperature.gpu,power.limit,memory.used,memory.total --format=csv,noheader,nounits
nvctl --bd --pid-file /var/run/nvctl_persist.pid
```

### Headless Thermal Guard & Telemetry Scripting
Use `-q` for bare scalar output in shell watchdogs, or `-j` for structured JSON ingestion:
```sh
# Shell thermal guard (nvctl -q prints a bare Celsius integer, e.g. 36)
temp=$(nvctl -q -d 0 temp)
if [ "$temp" -ge 85 ]; then
    logger -p kern.crit "GPU 0 temperature ${temp} C exceeded threshold"
fi

# Structured JSON snapshot for dashboards or collectors
nvctl -j status
```

## Documentation
* **[`SPEC.md`](SPEC.md):** Architecture, CLI verb grammar, verified `470.256.02` RM ioctl map, FreeBSD `style(9)` coding rules, and canonical FreeBSD Ports separation.
* **[`man/nvctl.8`](man/nvctl.8):** FreeBSD `mdoc(7)` manual page (`mandoc man/nvctl.8 | less`).

---
*Developed with AI-assisted coding by Gemini 4 Neon; reviewed, tested, and verified on live FreeBSD 15.1 hardware by the author.*
