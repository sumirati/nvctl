# `nvctl(8)` — Headless NVIDIA GPU Control & Telemetry Utility for FreeBSD

## 1. Purpose & Scope
`nvctl` is a standalone Beerware-licensed C utility that talks directly to `/dev/nvidiactl` (NVIDIA Resource Manager ioctl interface) to provide headless telemetry, runtime power capping, and GPU persistence on FreeBSD without a running X server, `nvidia-settings`, `nvidia-smi`, or `nvidia-persistenced`.

* **Primary Target:** FreeBSD 14/15 (`x11/nvidia-driver-470` EOL Kepler/Maxwell/Pascal cards, plus newer `nvidia-driver` branches where RM permits).
* **Distribution:** Standalone repository + canonical FreeBSD Port (`sysutils/nvctl` in a separate `freebsd/freebsd-ports` fork) with `nvctl(8)` man page.

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
| `-d <N\|all>` | Target GPU device index `N` or `all` attached GPUs (default: all on RO; required on RW when >1 GPU). |
| `-j` | Machine-readable JSON output (for `node_exporter`, dashboards, BMC). |
| `-q` | Quiet / raw scalar output for shell scripting (`temp`: `<core_c>`, `power`: `<cap_w>`, `status`: `<core_c> <cap_w>`). On failure, nothing is printed to `stdout`; errors go to `stderr` with a non-zero exit code. |
| `-v` | Verbose human-readable diagnostic trace on `stderr`. Stackable (`-vv`) for raw RM ioctl hex word dumps. |

### Command Verbs
| Command | Mode | Description |
| :--- | :---: | :--- |
| `nvctl` / `nvctl status` | RO | Multi-GPU summary table (index, `gpuId`, temperature, slowdown/shutdown, min/cur/def/max power cap). |
| `nvctl list` | RO | Enumerate attached GPUs (`/dev/nvidia<N>`, PCI BDF, `gpuId`, model name). |
| `nvctl info` | RO | Detailed `sysctl`-style key/value dump (device node, PCI BDF/IDs, PCIe gen/width/GB/s, model, UUID, VBIOS, bus, IRQ, driver, SM/GPC/CUDA cores, VRAM/L2/bandwidth, live clocks, P-state, thermal, and power limits). |
| `nvctl temp` | RO | Core temperature + hardware slowdown and emergency shutdown thresholds. |
| `nvctl power` / `nvctl power get` | RO | Active power cap and hardware min / default / max bounds (watts). |
| `nvctl power set <watts>` | **RW** | Set runtime power cap (bounds-checked against hardware min..max; requires `sudo` and `-d <N\|all>` when >1 GPU). Always reads back the live cap to detect driver clamping (exit `4`). On `470+`, closing all `/dev/nvidia*` clients (stopping `persist` or rebooting without `rc.d/nvctl_persist`) resets the cap to VBIOS default. |
| `nvctl persist` | RO | Zero-CPU (`pause()`) persistence holder keeping `/dev/nvidiactl` + `/dev/nvidia*` open so runtime power caps survive idle periods (`SIGINT`/`SIGTERM` clean teardown). |
| `nvctl probe` | RO | Read-only RM ioctl capability & ABI probe (`-vv` dumps non-zero `u32` words). |

### Exit Codes
* `0`: Success.
* `1`: System / ioctl / RM runtime failure.
* `2`: CLI usage error, invalid argument, or invalid device index.
* `3`: Feature unsupported or gated by RM/hardware (`NV_ERR_NOT_SUPPORTED` / `0x56`; no state changed).
* `4`: Driver accepted the write and updated GPU state, but clamped the value away from the requested target.
* `5`: Requested value rejected against hardware `min..max` safety bounds (no state changed).

### Compatibility Modes (`nvidia-smi` / `nvidia-persistenced`)
Compatibility personalities must strictly mirror native NVIDIA CLI semantics and units (no invented units, no silent faking of unsupported flags):
* **Invocation:** Triggered automatically via `basename(argv[0])` (`nvidia-smi` or `nvidia-persistenced` symlink) or explicitly via `nvctl --sm ...` / `nvctl --bd ...` (safe when native binaries are installed).
* **`nvidia-smi` (`--sm`) Contract:**
  * **Units:** Always report VRAM in `MiB` (never auto-scale to `GiB`; 6-digit `MiB`-safe up to `999999MiB`), power in `W` (`%.2f W` in CSV), clocks in `MHz`, and temperature in bare `°C` integer (`temperature.gpu`).
  * **Supported Flags:** No args (90-column aligned summary table with short driver version), `-L`/`--list-gpus` (`GPU <N>: <Model> (UUID: <UUID>)`), `-i <N>`/`--id=<N>`, `-pl <W>`/`--power-limit=<W>` (exact `Power limit for GPU 00000000:BB:DD.0 was set to ...` output + exit codes `0`/`4`/`5`), and `--query-gpu=<fields> --format=csv[,noheader][,nounits]` (`index`, `name`, `uuid`, `pci.bus_id`, `vbios_version`, `driver_version`, `temperature.gpu`, `power.{limit,min_limit,default_limit,max_limit}`, `memory.{total,free,used}`, `pstate`, `clocks.{gr,mem,current.graphics,current.memory}`, `pcie.link.{gen,width}.{current,max}`).
  * **Fail-Loudly Rule:** Any unsupported flag (e.g. `-pm`, `topo`, `dmon`, `-e`) or unknown `--query-gpu` field MUST print an explicit error to `stderr` and exit `3` (`NVCTL_EXIT_UNSUP`).
* **`nvidia-persistenced` (`--bd`) Contract:**
  * Daemonizes cleanly **before** opening `/dev/nvidiactl` (so RM `hClient` belongs to the daemon PID), supports `--no-daemon`, `-V`/`--verbose`, `-p`/`--pid-file <path>`, `-u`/`--user <name>`, and `--[no-]persistence-mode`.
  * Sleeps at 0% CPU in `pause()` and performs clean RM teardown + PID-file removal on `SIGINT`/`SIGTERM`.
  * Unknown options fail explicitly with exit `2` (`NVCTL_EXIT_USAGE`).

---

## 3. Verified `470.256.02` RM Control Map (FreeBSD 15.1 / `GK104`)

| RM Command | Size (B) | Privilege | Function & Verified Offsets |
| :--- | :---: | :---: | :--- |
| `0x20800501` | `1036` | User | Thermal settings/reading (`words[0]=0xFFFFFFFF` mask; `words[1]` = core °C; `words[13]>>8` = slowdown °C; `words[13]>>16` = shutdown °C). |
| `0x20801001` | `8` | User | Clock domains bitmask (`MCLK=0x10`, `GPC2CLK=0x10000`, `GPCCLK=0x1`). |
| `0x20801002` | `16` | User | Live clock frequencies in kHz (`CLK_GET_INFO` list: `MCLK` DDR rate, `GPC2CLK`/`GPCCLK` core rate). |
| `0x20801201` | `32`/`16` | User | Graphics engine info (`GR_GET_INFO`: `SM_VERSION`, `GPC`, `SM`, `CUDA_CORES`, `MAX_WARPS_PER_SM`). |
| `0x20801301` | `16` | User | Framebuffer info (`FB_GET_INFO`: `RAM_SIZE`, `HEAP_FREE`, `BUS_WIDTH`, `RAM_TYPE`, `PARTITION_COUNT`, `L2CACHE_SIZE`). |
| `0x20801801` | `16` | User | PCI IDs (`vendor`, `device`, `subvendor`, `subdevice`, `rev`). |
| `0x20801802` | `16` | User | PCIe link caps & live control/status (`BUS_GET_INFO`: current/max gen & width). |
| `0x20802068` | `4` | User | Current P-state bitmask (`1 << N` → `P<N>`). |
| `0x2080261C` | `136`/`72` | User | Power cap hardware bounds (`136 B` on `470+`, `72 B` on `390.xx`; `[0x0C]` = min mW, `[0x10]` = default mW, `[0x14]` = max mW). |
| `0x2080261F` | `8` | User | Active power cap get (`[0x04]` = current cap mW). |
| `0x2080261E` | `12` | **Root** | Active power cap set (`[0x00]=0`, `[0x04]` = target mW, `[0x08]=0`). Persists while any client holds `/dev/nvidia<N>` open. |

*Note on FreeBSD Multi-GPU Device Nodes:* FreeBSD numbers `/dev/nvidia<N>` to match `vgapci<N>` (e.g., `/dev/nvidia2` when two AMD GPUs occupy `vgapci0..1`). `nv_ctx_open()` scans and opens all present `/dev/nvidia0..31` nodes **before** invoking `GPU_ATTACH_IDS`.

---

## 4. Coding & Repository Standards

1. **FreeBSD `style(9)` Compliance:** Top-of-block variable declarations, explicit `NV_ESC_RM_FREE` teardown on every exit path, zero warnings under `-O2 -Wall -Wextra -Werror -std=gnu11`.
2. **Canonical Packaging Separation:** `nvctl` contains only upstream source + man page and cuts signed tags (`v0.4.0`). The FreeBSD Port (`sysutils/nvctl`) lives separately in `overlay-freebsd-ports` referencing the tagged release tarball (`USE_GITLAB=yes`) to avoid circular `distinfo` hashing.

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
│   ├── rm_pmgr.c       # PMGR power cap bounds/get/set, FB/BUS/GR/CLK info & probe
│   └── compat.c        # nvidia-smi (--sm) & nvidia-persistenced (--bd) personalities
├── rc.d/
│   └── nvctl_persist   # FreeBSD rc.subr(8) boot-time persistence & power-cap service
└── man/
    └── nvctl.8         # FreeBSD mdoc(7) manual page
```

---

## 5. Roadmap (Post-`v0.1.0`)

### 5.1 FreeBSD Port (`sysutils/nvctl`) & `nvidia-smi` Feature Tiers
* Create the port skeleton (`Makefile`, `distinfo`, `pkg-descr`, `files/nvctl_persist.in`) targeting the tagged release (`LICENSE= BEERWARE`, `LICENSE_FILE= ${WRKSRC}/LICENSE`, `USE_GITLAB=yes`, opt-in `NVIDIA_SMI_SYMLINK` / `NVIDIA_PERSISTENCED_SYMLINK`).
* Validate with `make makesum`, `make stage-qa DEVELOPER=yes`, `make check-plist DEVELOPER=yes`, `portlint -AC`, and `poudriere testport`, then submit via FreeBSD Bugzilla (`bugs.freebsd.org`).
* Completed in `v0.2.0`–`v0.4.0`: `rc.d/nvctl_persist`, `nvidia-smi` (`--sm`) & `nvidia-persistenced` (`--bd`) personalities, `-d all`, and `390.157` + `470.256.02` dual-branch support.

#### 5.1.1 Tier 1 — High-Value `nvidia-smi` Staples (Target: `v0.5.0`)
* **Live board power draw (`power.draw`):** Actual watt draw alongside cap (`draw / cap` in `--sm` table and CSV).
* **GPU & memory utilization (`utilization.gpu`, `utilization.memory`):** Live engine busy percentages via RM perfmon/utilization ioctls.
* **Fan speed % (`fan.speed`):** Active fan PWM percentage (e.g. `0x20802703` / cooler RM queries; `N/A` on passive datacenter cards).
* **Loop / watch mode (`-l <sec>` / `--loop=<sec>`):** Continuous terminal polling for `--sm` table and `--query-gpu` CSV.

#### 5.1.2 Tier 2 — Datacenter & HBM2 Extras (Post-P100 Arrival)
* **Active VRAM processes:** PID, process name, and MiB used (`--query-compute-apps`).
* **ECC status & error counters:** Volatile/aggregate corrected & uncorrected HBM2 ECC counters (`ecc.errors.*`).
* **Clock throttle reasons:** Idle vs. thermal slowdown vs. power-cap active reason bitmask.
* **Topology matrix (`topo -m`):** `PIX` / `PHB` / `SYS` multi-GPU interconnect view for `2× P100` / `PEX8747` rigs.

#### 5.1.3 Tier 3 — Explicit Non-Goals (Fail Loudly with Exit `3`)
* `-x` (XML output), `-e` (ECC toggle), `-lgc`/`-lmc` (clock locking), MIG, vGPU, `dmon`/`pmon`, and GPU reset (`-r`).

### 5.2 FreeBSD Version Matrix & Test Strategy
* **Target Releases:**
  * **FreeBSD 14.5-RELEASE** — **verified live** via `bhyve` + `ppt` PCIe passthrough (`390.157` and `470.256.02`).
  * **FreeBSD 15.1** — **verified live** bare-metal (`390.157` and `470.256.02`).
  * **FreeBSD `main` (`15`/`16-CURRENT`)** — compile & port check via `poudriere`.
* **Test Method:**
  * `poudriere` jails for clean build/packaging verification across all supported FreeBSD releases.
  * **`bhyve` with PCIe passthrough (`ppt`)** on VT-d hosts (e.g. `x79` i7-4930K) passing through a GPU (`pci0:3:0:0`) into a FreeBSD 14.5-RELEASE guest VM to test live RM ioctls on 14.x without disturbing the host OS.

### 5.3 NVIDIA Driver Branch Compatibility
* `x11/nvidia-driver-390` (`390.157`) — **verified live** (`CHECK_VERSION_STR` handshake, `/dev/nvidia<N>` `NV01_DEVICE_0` alloc, `72 B` `0x2080261C` bounds).
* `x11/nvidia-driver-470` (`470.256.02`) — **verified live** (Kepler/Maxwell/Pascal EOL branch; lacks `nvidia-smi`).
* `x11/nvidia-driver` (current `550`/`570`/`580` production branches & `open-gpu-kernel-modules` on Pascal/Turing+) — verify on incoming 2× Tesla P100 16GB whether `0x20800501` and `0x2080261C/E/F` remain compatible or need a `0x20800513` (`THERMAL_SYSTEM_EXECUTE_V2`) fallback.

### 5.4 Hardware Compatibility Matrix (Easy / High-Value Targets)
* **Kepler (`GK104`):** GeForce GTX 680 — **verified (`v0.1.0`)**.
* **Low-end / No-Power-Cap Kepler (`GK208` / `GK107`):** GT 710 / GT 730 / Quadro K620 (~€10–25 used) — validates graceful `NVCTL_EXIT_UNSUP` (`3`) handling on cards without software power capping.
* **Maxwell (`GM107` / `GM200`):** GTX 750 Ti / Quadro M2000 / Tesla M40.
* **Pascal (`GP100` / `GP104`):** Tesla P100 16GB (`SM 6.0`, `HBM2`, `4096`-bit bus → verifies `HBM2` RAM type + `732 GB/s` bandwidth + 150 W cap) and GTX 1060 / Tesla P4.
* **Multi-GPU NVIDIA Rig:** 2+ NVIDIA cards simultaneously to verify `-d <N|all>` enforcement on `power set` and replace positional `/dev/nvidia<N>` mapping with PCI-BDF matching (`dev.vgapci.<N>.%location` ↔ `gpu_id`) when `gpu_count > 1`.

### 5.5 Native FreeBSD `libnvidia-ml.so.1` (NVML Shim)
* Build an optional shared library (`libnvidia-ml.so.1`, opt-in Port option `NVML_SHIM`) directly on top of `nvctl`'s verified `rm_ioctl.c` / `rm_thermal.c` / `rm_pmgr.c` core — keeping the entire FreeBSD NVIDIA userland replica in one cohesive codebase rather than duplicating RM ABI logic across repos.
* Implement the ~25 core NVML symbols `dlopen()`'d by `nvtop`, `btop`, `pynvml`, and monitoring exporters (`nvmlInit_v2`, `nvmlDeviceGetCount_v2`, `nvmlDeviceGetHandleByIndex_v2`, `nvmlDeviceGetName`, `nvmlDeviceGetUUID`, `nvmlDeviceGetPciInfo_v3`, `nvmlDeviceGetTemperature`, `nvmlDeviceGetPowerUsage`, `nvmlDeviceGetEnforcedPowerLimit`, `nvmlDeviceGetMemoryInfo`, `nvmlDeviceGetUtilizationRates`, `nvmlDeviceGetFanSpeed`, `nvmlDeviceGetClockInfo`, `nvmlDeviceGetPerformanceState`, `nvmlDeviceGetCurrPcieLinkGeneration`/`Width`), returning `NVML_ERROR_NOT_SUPPORTED` for unimplemented symbols.

### 5.6 Community Announcement Channels (Optional, Post-Port)
* **`freebsd-x11@freebsd.org`** — primary FreeBSD graphics/GPU mailing list (read by `nvidia-driver*` and `drm-kmod` maintainers).
* **`freebsd-ports@freebsd.org`** — once `sysutils/nvctl` is submitted/landed.
* **`freebsd-hackers@freebsd.org`** — brief `[ANN]` note for systems developers.
* **FreeBSD Forums** (`forums.freebsd.org` → *Hardware* / *Display Drivers*) and **`r/freebsd`** — primary reach for headless FreeBSD server/homelab users on `nvidia-driver-390`/`470` (high value for free multi-card hardware QA reports).

### 5.7 Extended Historical & Bleeding-Edge FreeBSD `bhyve` `ppt` Matrix
* **Forward-Looking Releases (from `freebsd.org/where/`):**
  * **FreeBSD 15.2-BETA / RELEASE** (`bhyve` + `ppt` passthrough, verifying `390.157`, `470.256.02`, and `550`/`570`/`580`).
  * **FreeBSD 16-CURRENT** (`bhyve` + `ppt` + `poudriere` build check).
* **Historical / Age-Aligned Archive Releases (from `ftp-archive.freebsd.org`, optional):**
  * Walk backwards through archived EOL releases (`13.5-RELEASE` → `12.4` → `11.4` → `10.4` → `9.x`, age-aligned with the 2012 `GK104` GTX 680 launch era) inside `bhyve` + `ppt` to map the exact historical boundary of `NV_ESC_RM_ALLOC` / `CHECK_VERSION_STR` / `nvidia-driver-304`/`340`/`390` compatibility.
