# `nvctl(8)` — Headless NVIDIA GPU Control & Telemetry Utility for FreeBSD

## 1. Purpose & Scope
`nvctl` is a standalone, zero-dependency, BSD-2-Clause C utility that talks directly to `/dev/nvidiactl` (NVIDIA Resource Manager ioctl interface) to provide headless telemetry and power management on FreeBSD (and Linux) without X11, `nvidia-settings`, or `nvidia-smi`.

* **Primary Target:** FreeBSD 14/15 (`x11/nvidia-driver-470` EOL Kepler/Maxwell/Pascal cards, plus newer `nvidia-driver` branches where RM permits).
* **Distribution:** Standalone repository (`local` GitLab $\rightarrow$ public GitHub) + FreeBSD Port (`sysutils/nvctl`) with `nvctl(8)` man page.

---

## 2. CLI Interface Semantics (FreeBSD Verb Style)

Follows FreeBSD base tool conventions (`nvmecontrol(8)`, `camcontrol(8)`, `zpool(8)`, `devctl(8)`): **verb-first subcommands**, explicit read vs. write separation, and a zero-argument default (`status`).

### Synopsis
```text
nvctl [-j] [-q] [-v] [-d index] [command [subcommand] [args...]]
```

### Global Flags
| Flag | Description |
| :---: | :--- |
| `-d <N>` | Target GPU device index `N` (default: all attached GPUs). |
| `-j` | Machine-readable JSON output (for `node_exporter`, dashboards, BMC). |
| `-q` | Quiet / raw scalar output for shell scripting (e.g., `42` instead of `42 C`). |
| `-v` | Verbose RM status/ioctl diagnostics. |

### Command Verbs
| Command | Mode | Description |
| :--- | :---: | :--- |
| `nvctl` / `nvctl status` | RO | Multi-GPU summary table (index, model, PCI BDF, link, temp, fan, power, cap). |
| `nvctl list` | RO | Enumerate attached GPUs (`gpuId`, PCI BDF, UUID, VBIOS, RM driver version). |
| `nvctl temp` | RO | Die / hotspot / memory temperatures (`NV2080_CTRL_CMD_THERMAL_*`). |
| `nvctl power` / `nvctl power get` | RO | Current watt draw + min / default / current / max power policy limits. |
| `nvctl power set <watts>` | **RW** | Set runtime power cap (explicit `set` verb; requires `-d <N>` when >1 GPU). |
| `nvctl fan` | RO | Fan tachometer RPM and PWM duty cycle. |
| `nvctl probe` | RO | Raw RM ioctl capability & ABI struct-size detector (`470.xx` vs `5xx.xx`). |

### Exit Codes
* `0`: Success.
* `1`: System / ioctl / RM runtime failure.
* `2`: CLI usage / invalid argument error.
* `3`: Feature unsupported or gated by RM/hardware (`NV_ERR_NOT_SUPPORTED` / `0x56`).

---

## 3. Coding & Architecture Standards

1. **FreeBSD `style(9)` Compliance:**
   * All variable declarations at the top of the function block, sorted by alignment/size.
   * Zero dead macros, unused structs, or compiler warnings under `-Wall -Wextra -Werror`.
2. **Deterministic Resource Teardown:**
   * Every `NV_ESC_RM_ALLOC` (`NV01_ROOT_USER`, `NV01_DEVICE_0`, `NV20_SUBDEVICE_0`) must have a matching `NV_ESC_RM_FREE` cleanup path (`goto cleanup;`) before `close()`.
3. **True Multi-GPU Enumeration:**
   * Enumerate all attached GPUs via `NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS` + `NV0000_CTRL_CMD_GPU_GET_ID_INFO` (never hardcode `deviceId = 0`).
4. **ABI Version Awareness:**
   * Query driver version string (`NV_ESC_CHECK_VERSION_STR` / `sysctl hw.nvidia.version`) and select the matching `470.xx` vs. `5xx.xx` RM control struct layouts cleanly.

---

## 4. Repository & Canonical Packaging Layout

`nvctl` is maintained as a standalone upstream source repository (pure software + man page, cutting signed release tags such as `v0.1.0`). Per FreeBSD Ports canon, the FreeBSD Port (`sysutils/nvctl`) does **not** live inside this repository—it is maintained separately on a feature branch of a `freebsd/freebsd-ports` fork referencing the tagged release tarball (`USE_GITHUB=yes`, `GH_TAGNAME=v0.1.0`) to avoid circular `distinfo` hashing.

```text
nvctl/
├── LICENSE             # BSD-2-Clause
├── README.md           # Overview & quick usage
├── SPEC.md             # This specification
├── Makefile            # BSD make + GNU make compatible
├── src/
│   ├── main.c          # CLI verb dispatcher & output formatters (table/raw/JSON)
│   ├── rm_ioctl.c      # /dev/nvidiactl transport, client/device lifecycle, NV_ESC_RM_FREE
│   ├── rm_thermal.c    # 470.xx / 5xx.xx thermal sensor queries
│   └── rm_pmgr.c       # PMGR power telemetry & power-cap control
└── man/
    └── nvctl.8         # FreeBSD mdoc(7) manual page
```
