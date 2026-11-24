# `nvctl(8)` — Headless NVIDIA GPU Control & Telemetry Utility for FreeBSD

## 1. Purpose & Scope
`nvctl` is a standalone, zero-dependency, BSD-2-Clause C utility that talks directly to `/dev/nvidiactl` (NVIDIA Resource Manager ioctl interface) to provide headless telemetry, runtime power capping, and GPU persistence on FreeBSD (and Linux) without X11, `nvidia-settings`, `nvidia-smi`, or `nvidia-persistenced`.

* **Primary Target:** FreeBSD 14/15 (`x11/nvidia-driver-470` EOL Kepler/Maxwell/Pascal cards, plus newer `nvidia-driver` branches where RM permits).
* **Distribution:** Standalone repository (`local` GitLab $\rightarrow$ public GitHub) + canonical FreeBSD Port (`sysutils/nvctl` in a separate `freebsd/freebsd-ports` fork) with `nvctl(8)` man page.

---

## 2. CLI Interface Semantics (FreeBSD Verb Style)

Follows FreeBSD base tool conventions (`nvmecontrol(8)`, `camcontrol(8)`, `zpool(8)`, `devctl(8)`): **verb-first subcommands**, explicit read vs. write separation, and a zero-argument default (`status`).

### Synopsis
```text
nvctl [-jqv] [-d index] [command [subcommand] [args...]]
```

### Global Flags
| Flag | Description |
| :---: | :--- |
| `-d <N>` | Target GPU device index `N` (default: all attached GPUs). |
| `-j` | Machine-readable JSON output (for `node_exporter`, dashboards, BMC). |
| `-q` | Quiet / raw scalar output for shell scripting (e.g., `36` instead of `36 C`). |
| `-v` | Verbose human-readable diagnostic trace on `stderr`. Stackable (`-vv`) for raw RM ioctl hex word dumps. |

### Command Verbs
| Command | Mode | Description |
| :--- | :---: | :--- |
| `nvctl` / `nvctl status` | RO | Multi-GPU summary table (index, `gpuId`, temp, slowdown/shutdown, min/cur/def/max power cap). |
| `nvctl list` | RO | Enumerate attached GPUs (`gpuId`, `device_instance`, `subdevice_instance`). |
| `nvctl temp` | RO | Core temperature + hardware slowdown and emergency shutdown thresholds. |
| `nvctl power` / `nvctl power get` | RO | Active power cap and hardware min / default / max bounds (watts). |
| `nvctl power set <watts>` | **RW** | Set runtime power cap (bounds-checked against hardware min..max; requires `sudo` and `-d <N>` when >1 GPU). |
| `nvctl persist` | RO | Zero-CPU (`pause()`) persistence holder keeping `/dev/nvidiactl` + `/dev/nvidia*` open so runtime power caps survive idle periods (`SIGINT`/`SIGTERM` clean teardown). |
| `nvctl probe` | RO | Read-only RM ioctl capability & ABI probe (`-vv` dumps non-zero `u32` words). |

### Exit Codes
* `0`: Success.
* `1`: System / ioctl / RM runtime failure.
* `2`: CLI usage / invalid argument / out-of-bounds wattage error.
* `3`: Feature unsupported or gated by RM/hardware (`NV_ERR_NOT_SUPPORTED` / `0x56`).

---

## 3. Verified `470.256.02` RM Control Map (FreeBSD 15.1 / `GK104`)

| RM Command | Size (B) | Privilege | Function & Verified Offsets |
| :--- | :---: | :---: | :--- |
| `0x20800501` | `1036` | User | Thermal settings/reading (`words[0]=0xFFFFFFFF` mask; `words[1]` = core °C; `words[13]>>8` = slowdown °C; `words[13]>>16` = shutdown °C). |
| `0x2080261C` | `136` | User | Power cap hardware bounds (`[0x0C]` = min mW, `[0x10]` = default mW, `[0x14]` = max mW). |
| `0x2080261F` | `8` | User | Active power cap get (`[0x04]` = current cap mW). |
| `0x2080261E` | `12` | **Root** | Active power cap set (`[0x00]=0`, `[0x04]` = target mW, `[0x08]=0`). Persists while any client holds `/dev/nvidia<N>` open. |

*Note on FreeBSD Multi-GPU Device Nodes:* FreeBSD numbers `/dev/nvidia<N>` to match `vgapci<N>` (e.g., `/dev/nvidia2` when two AMD GPUs occupy `vgapci0..1`). `nv_ctx_open()` scans and opens all present `/dev/nvidia0..31` nodes **before** invoking `GPU_ATTACH_IDS`.

---

## 4. Coding & Repository Standards

1. **FreeBSD `style(9)` Compliance:** Top-of-block variable declarations, explicit `NV_ESC_RM_FREE` teardown on every exit path, zero warnings under `-O2 -Wall -Wextra -Werror -std=c11`.
2. **Canonical Packaging Separation:** `nvctl` contains only upstream source + man page and cuts signed tags (`v0.1.0`). The FreeBSD Port (`sysutils/nvctl`) lives separately on a branch of a `freebsd/freebsd-ports` fork referencing the tagged release tarball (`USE_GITHUB=yes`) to avoid circular `distinfo` hashing.

```text
nvctl/
├── .gitignore
├── Makefile            # BSD make + GNU make compatible
├── README.md           # Overview & quick usage
├── SPEC.md             # This specification
├── src/
│   ├── nvctl.h         # Shared types, RM constants, prototypes
│   ├── main.c          # CLI verb dispatcher, formatters (table/raw/JSON), persist
│   ├── rm_ioctl.c      # /dev/nvidiactl + /dev/nvidia* lifecycle & NV_ESC_RM_FREE
│   ├── rm_thermal.c    # Thermal sensor & threshold queries (0x20800501)
│   └── rm_pmgr.c       # PMGR power cap bounds/get/set (0x2080261C/E/F) & probe
└── man/
    └── nvctl.8         # FreeBSD mdoc(7) manual page
```
