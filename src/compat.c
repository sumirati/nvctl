#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nvctl.h"

static volatile sig_atomic_t g_bd_stop = 0;

static void
on_bd_signal(int sig)
{
	(void)sig;
	g_bd_stop = 1;
}

static int
daemonize_proc(void)
{
	pid_t pid;
	int fd;

	pid = fork();
	if (pid < 0)
		return (-1);
	if (pid > 0)
		_exit(0);
	if (setsid() < 0)
		return (-1);
	(void)chdir("/");
	fd = open("/dev/null", O_RDWR);
	if (fd >= 0) {
		(void)dup2(fd, STDIN_FILENO);
		(void)dup2(fd, STDOUT_FILENO);
		(void)dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO)
			close(fd);
	}
	return (0);
}

static int
parse_u32_local(const char *s, uint32_t *out)
{
	char *end;
	unsigned long val;

	if (s == NULL || s[0] == '\0' || s[0] == '-')
		return (-1);
	errno = 0;
	val = strtoul(s, &end, 10);
	if (errno != 0 || *end != '\0' || val > UINT32_MAX)
		return (-1);
	*out = (uint32_t)val;
	return (0);
}

static void
smi_usage(void)
{
	fprintf(stderr,
	    "usage: nvidia-smi (nvctl --sm) [-L] [-i index] [-pl watts] "
	    "[--query-gpu=fields --format=csv[,noheader][,nounits]]\n");
}

static void
bd_usage(void)
{
	fprintf(stderr,
	    "usage: nvidia-persistenced (nvctl --bd) [--no-daemon] [--verbose] "
	    "[--pid-file path] [--user name]\n");
}

static void
smi_driver_short(const char *full, char *out, size_t sz)
{
	const char *p;
	const char *start;
	size_t n;

	for (p = full; *p != '\0'; ) {
		while (*p == ' ' || *p == '\t')
			p++;
		start = p;
		while (*p != '\0' && *p != ' ' && *p != '\t')
			p++;
		if (start[0] >= '0' && start[0] <= '9' &&
		    memchr(start, '.', (size_t)(p - start)) != NULL) {
			n = (size_t)(p - start);
			if (n >= sz)
				n = sz - 1;
			memcpy(out, start, n);
			out[n] = '\0';
			return;
		}
	}
	snprintf(out, sz, "%s", full);
}

static int
smi_print_field(struct nvctl_ctx *ctx, int idx, const struct nvctl_hwinfo *hw,
    const struct nvctl_thermal *th, const struct nvctl_power *pw,
    const char *field, int nounits)
{
	char drv[32];
	uint32_t bus;
	uint32_t slot;
	uint32_t used_mb;

	bus = (ctx->gpus[idx].gpu_id >> 8) & 0xFFU;
	slot = (ctx->gpus[idx].gpu_id >> 3) & 0x1FU;
	used_mb = (hw->vram_total_mb >= hw->vram_free_mb) ?
	    (hw->vram_total_mb - hw->vram_free_mb) : 0U;

	if (strcmp(field, "index") == 0) {
		printf("%d", idx);
	} else if (strcmp(field, "name") == 0) {
		printf("%s", ctx->gpus[idx].model);
	} else if (strcmp(field, "uuid") == 0) {
		printf("%s", ctx->gpus[idx].uuid);
	} else if (strcmp(field, "pci.bus_id") == 0) {
		printf("00000000:%02X:%02X.0", bus, slot);
	} else if (strcmp(field, "vbios_version") == 0) {
		printf("%s", ctx->gpus[idx].vbios);
	} else if (strcmp(field, "driver_version") == 0) {
		smi_driver_short(ctx->driver_version, drv, sizeof(drv));
		printf("%s", drv);
	} else if (strcmp(field, "temperature.gpu") == 0) {
		printf("%d", th->valid ? th->core_temp_c : -1);
	} else if (strcmp(field, "power.limit") == 0) {
		if (nounits)
			printf("%.2f", (double)pw->current_mw / 1000.0);
		else
			printf("%.2f W", (double)pw->current_mw / 1000.0);
	} else if (strcmp(field, "power.min_limit") == 0) {
		if (nounits)
			printf("%.2f", (double)pw->min_mw / 1000.0);
		else
			printf("%.2f W", (double)pw->min_mw / 1000.0);
	} else if (strcmp(field, "power.default_limit") == 0) {
		if (nounits)
			printf("%.2f", (double)pw->default_mw / 1000.0);
		else
			printf("%.2f W", (double)pw->default_mw / 1000.0);
	} else if (strcmp(field, "power.max_limit") == 0) {
		if (nounits)
			printf("%.2f", (double)pw->max_mw / 1000.0);
		else
			printf("%.2f W", (double)pw->max_mw / 1000.0);
	} else if (strcmp(field, "memory.total") == 0) {
		if (nounits)
			printf("%u", hw->vram_total_mb);
		else
			printf("%u MiB", hw->vram_total_mb);
	} else if (strcmp(field, "memory.free") == 0) {
		if (nounits)
			printf("%u", hw->vram_free_mb);
		else
			printf("%u MiB", hw->vram_free_mb);
	} else if (strcmp(field, "memory.used") == 0) {
		if (nounits)
			printf("%u", used_mb);
		else
			printf("%u MiB", used_mb);
	} else if (strcmp(field, "pstate") == 0) {
		if (hw->pstate >= 0)
			printf("P%d", hw->pstate);
		else
			printf("[N/A]");
	} else if (strcmp(field, "clocks.gr") == 0 ||
	    strcmp(field, "clocks.current.graphics") == 0) {
		if (nounits)
			printf("%u", hw->core_clk_khz / 1000U);
		else
			printf("%u MHz", hw->core_clk_khz / 1000U);
	} else if (strcmp(field, "clocks.mem") == 0 ||
	    strcmp(field, "clocks.current.memory") == 0) {
		if (nounits)
			printf("%u", hw->mem_clk_khz / 1000U);
		else
			printf("%u MHz", hw->mem_clk_khz / 1000U);
	} else if (strcmp(field, "pcie.link.gen.current") == 0) {
		printf("%u", hw->pcie_gen);
	} else if (strcmp(field, "pcie.link.gen.max") == 0) {
		printf("%u", hw->pcie_max_gen);
	} else if (strcmp(field, "pcie.link.width.current") == 0) {
		printf("%u", hw->pcie_width);
	} else if (strcmp(field, "pcie.link.width.max") == 0) {
		printf("%u", hw->pcie_max_width);
	} else {
		return (-1);
	}
	return (0);
}

static int
smi_run_query(struct nvctl_ctx *ctx, int dev_idx, const char *query,
    int noheader, int nounits)
{
	struct nvctl_hwinfo hw;
	struct nvctl_thermal th;
	struct nvctl_power pw;
	char qbuf[512];
	char *fields[32];
	char *p;
	char *tok;
	int nfields;
	int f;
	int i;

	if (strlen(query) >= sizeof(qbuf)) {
		fprintf(stderr, "nvidia-smi: query string too long\n");
		return (NVCTL_EXIT_USAGE);
	}
	snprintf(qbuf, sizeof(qbuf), "%s", query);
	nfields = 0;
	p = qbuf;
	while ((tok = strsep(&p, ",")) != NULL) {
		while (*tok == ' ')
			tok++;
		if (*tok == '\0')
			continue;
		if (nfields >= 32) {
			fprintf(stderr, "nvidia-smi: too many query fields\n");
			return (NVCTL_EXIT_USAGE);
		}
		fields[nfields++] = tok;
	}
	if (nfields == 0) {
		fprintf(stderr, "nvidia-smi: empty --query-gpu list\n");
		return (NVCTL_EXIT_USAGE);
	}

	if (!noheader) {
		for (f = 0; f < nfields; f++)
			printf("%s%s", (f > 0) ? ", " : "", fields[f]);
		printf("\n");
	}

	for (i = 0; i < ctx->gpu_count; i++) {
		if (dev_idx >= 0 && dev_idx != i)
			continue;
		nv_hwinfo_read(ctx, i, &hw);
		(void)nv_thermal_read(ctx, i, &th, 0);
		(void)nv_power_read(ctx, i, &pw, 0);

		for (f = 0; f < nfields; f++) {
			if (f > 0)
				printf(", ");
			if (smi_print_field(ctx, i, &hw, &th, &pw,
			    fields[f], nounits) != 0) {
				fprintf(stderr,
				    "\nnvidia-smi: unsupported query field '%s'\n",
				    fields[f]);
				return (NVCTL_EXIT_UNSUP);
			}
		}
		printf("\n");
	}
	return (NVCTL_EXIT_OK);
}

static void
smi_print_table(struct nvctl_ctx *ctx, int dev_idx)
{
	struct nvctl_hwinfo hw;
	struct nvctl_thermal th;
	struct nvctl_power pw;
	char drv[32];
	uint32_t bus;
	uint32_t slot;
	uint32_t used_mb;
	int i;

	smi_driver_short(ctx->driver_version, drv, sizeof(drv));
	printf("+-----------------------------------------------------------------------------+\n");
	printf("| nvctl (nvidia-smi compat)          Driver Version: %-24.24s |\n",
	    drv);
	printf("|----+------------------------+------------------+----------------------------|\n");
	printf("| GPU| Name                   | Bus-Id           | Temp Perf Cap       Memory |\n");
	printf("|====+========================+==================+============================|\n");

	for (i = 0; i < ctx->gpu_count; i++) {
		if (dev_idx >= 0 && dev_idx != i)
			continue;
		nv_hwinfo_read(ctx, i, &hw);
		(void)nv_thermal_read(ctx, i, &th, 0);
		(void)nv_power_read(ctx, i, &pw, 0);
		bus = (ctx->gpus[i].gpu_id >> 8) & 0xFFU;
		slot = (ctx->gpus[i].gpu_id >> 3) & 0x1FU;
		used_mb = (hw.vram_total_mb >= hw.vram_free_mb) ?
		    (hw.vram_total_mb - hw.vram_free_mb) : 0U;

		printf("| %2d | %-22.22s | 00000000:%02X:%02X.0 | "
		    "%3dC P%-2d %3uW %5u/%5uM |\n",
		    i, ctx->gpus[i].model, bus, slot,
		    th.valid ? th.core_temp_c : -1,
		    (hw.pstate >= 0) ? hw.pstate : 0,
		    pw.current_mw / 1000U, used_mb, hw.vram_total_mb);
	}
	printf("+-----------------------------------------------------------------------------+\n");
}

int
nv_compat_smi(int argc, char *argv[])
{
	struct nvctl_ctx ctx;
	struct nvctl_power pw_before;
	struct nvctl_power pw_after;
	const char *query;
	const char *fmt;
	uint32_t tmp;
	uint32_t pl_watts;
	uint32_t st;
	uint32_t bus;
	uint32_t slot;
	int dev_idx;
	int do_list;
	int do_pl;
	int noheader;
	int nounits;
	int target;
	int i;
	int rc;

	dev_idx = -1;
	do_list = 0;
	do_pl = 0;
	pl_watts = 0;
	query = NULL;
	fmt = NULL;
	noheader = 0;
	nounits = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 ||
		    strcmp(argv[i], "--help") == 0) {
			smi_usage();
			return (NVCTL_EXIT_OK);
		} else if (strcmp(argv[i], "-L") == 0 ||
		    strcmp(argv[i], "--list-gpus") == 0) {
			do_list = 1;
		} else if (strcmp(argv[i], "-i") == 0 ||
		    strcmp(argv[i], "--id") == 0) {
			if (i + 1 >= argc ||
			    parse_u32_local(argv[i + 1], &tmp) != 0 ||
			    tmp >= NVCTL_MAX_GPUS) {
				fprintf(stderr,
				    "nvidia-smi: invalid GPU index\n");
				return (NVCTL_EXIT_USAGE);
			}
			dev_idx = (int)tmp;
			i++;
		} else if (strncmp(argv[i], "--id=", 5) == 0) {
			if (parse_u32_local(argv[i] + 5, &tmp) != 0 ||
			    tmp >= NVCTL_MAX_GPUS) {
				fprintf(stderr,
				    "nvidia-smi: invalid GPU index '%s'\n",
				    argv[i] + 5);
				return (NVCTL_EXIT_USAGE);
			}
			dev_idx = (int)tmp;
		} else if (strcmp(argv[i], "-pl") == 0 ||
		    strcmp(argv[i], "--power-limit") == 0) {
			if (i + 1 >= argc ||
			    parse_u32_local(argv[i + 1], &pl_watts) != 0) {
				fprintf(stderr,
				    "nvidia-smi: invalid power limit\n");
				return (NVCTL_EXIT_USAGE);
			}
			do_pl = 1;
			i++;
		} else if (strncmp(argv[i], "--power-limit=", 14) == 0) {
			if (parse_u32_local(argv[i] + 14, &pl_watts) != 0) {
				fprintf(stderr,
				    "nvidia-smi: invalid power limit '%s'\n",
				    argv[i] + 14);
				return (NVCTL_EXIT_USAGE);
			}
			do_pl = 1;
		} else if (strcmp(argv[i], "-pm") == 0 ||
		    strncmp(argv[i], "--persistence-mode", 18) == 0) {
			fprintf(stderr,
			    "nvidia-smi: -pm is unsupported on FreeBSD; "
			    "run 'nvctl --bd' (nvidia-persistenced) instead\n");
			return (NVCTL_EXIT_UNSUP);
		} else if (strncmp(argv[i], "--query-gpu=", 12) == 0) {
			query = argv[i] + 12;
		} else if (strncmp(argv[i], "--format=", 9) == 0) {
			fmt = argv[i] + 9;
		} else {
			fprintf(stderr,
			    "nvidia-smi: unsupported option '%s'\n", argv[i]);
			smi_usage();
			return (NVCTL_EXIT_UNSUP);
		}
	}

	if (fmt != NULL) {
		if (strstr(fmt, "csv") == NULL) {
			fprintf(stderr,
			    "nvidia-smi: only --format=csv[,noheader][,nounits] "
			    "is supported\n");
			return (NVCTL_EXIT_UNSUP);
		}
		if (strstr(fmt, "noheader") != NULL)
			noheader = 1;
		if (strstr(fmt, "nounits") != NULL)
			nounits = 1;
	}

	if (nv_ctx_open(&ctx, 0) != 0)
		return (NVCTL_EXIT_ERR);
	if (ctx.gpu_count == 0) {
		fprintf(stderr, "nvidia-smi: no NVIDIA GPUs found\n");
		nv_ctx_close(&ctx);
		return (NVCTL_EXIT_ERR);
	}
	if (dev_idx >= ctx.gpu_count) {
		fprintf(stderr, "nvidia-smi: invalid GPU index %d (found %d)\n",
		    dev_idx, ctx.gpu_count);
		nv_ctx_close(&ctx);
		return (NVCTL_EXIT_USAGE);
	}

	if (do_list) {
		for (i = 0; i < ctx.gpu_count; i++) {
			if (dev_idx >= 0 && dev_idx != i)
				continue;
			printf("GPU %d: %s (UUID: %s)\n",
			    i, ctx.gpus[i].model, ctx.gpus[i].uuid);
		}
		rc = NVCTL_EXIT_OK;
	} else if (do_pl) {
		if (ctx.gpu_count > 1 && dev_idx < 0) {
			fprintf(stderr,
			    "nvidia-smi: -i <index> required with multiple GPUs\n");
			nv_ctx_close(&ctx);
			return (NVCTL_EXIT_USAGE);
		}
		target = (dev_idx >= 0) ? dev_idx : 0;
		(void)nv_power_read(&ctx, target, &pw_before, 0);
		rc = nv_power_set(&ctx, target, pl_watts, &st, 0);
		if (rc == -2) {
			nv_ctx_close(&ctx);
			return (NVCTL_EXIT_RANGE);
		}
		if (rc != 0) {
			if (st == NV_ERR_INSUFFICIENT_PERM)
				fprintf(stderr,
				    "nvidia-smi: permission denied "
				    "(run with sudo)\n");
			else
				fprintf(stderr,
				    "nvidia-smi: power limit failed "
				    "(status=0x%X)\n", st);
			nv_ctx_close(&ctx);
			return (st == NV_ERR_NOT_SUPPORTED) ?
			    NVCTL_EXIT_UNSUP : NVCTL_EXIT_ERR;
		}
		if (nv_power_read(&ctx, target, &pw_after, 0) != 0) {
			nv_ctx_close(&ctx);
			return (NVCTL_EXIT_ERR);
		}
		if (pw_after.current_mw / 1000U != pl_watts) {
			fprintf(stderr,
			    "nvidia-smi: GPU %d requested %u W, but driver "
			    "clamped limit to %u W\n",
			    target, pl_watts, pw_after.current_mw / 1000U);
			nv_ctx_close(&ctx);
			return (NVCTL_EXIT_CLAMPED);
		}
		bus = (ctx.gpus[target].gpu_id >> 8) & 0xFFU;
		slot = (ctx.gpus[target].gpu_id >> 3) & 0x1FU;
		printf("Power limit for GPU 00000000:%02X:%02X.0 was set to "
		    "%.2f W from %.2f W.\n",
		    bus, slot, (double)pw_after.current_mw / 1000.0,
		    (double)pw_before.current_mw / 1000.0);
		rc = NVCTL_EXIT_OK;
	} else if (query != NULL) {
		rc = smi_run_query(&ctx, dev_idx, query, noheader, nounits);
	} else {
		smi_print_table(&ctx, dev_idx);
		rc = NVCTL_EXIT_OK;
	}

	nv_ctx_close(&ctx);
	return (rc);
}

int
nv_compat_persistenced(int argc, char *argv[])
{
	struct nvctl_ctx ctx;
	struct sigaction sa;
	struct passwd *pw;
	const char *pid_file;
	const char *run_user;
	FILE *fp;
	int no_daemon;
	int verbose;
	int i;

	no_daemon = 0;
	verbose = 0;
	pid_file = NULL;
	run_user = NULL;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 ||
		    strcmp(argv[i], "--help") == 0) {
			bd_usage();
			return (NVCTL_EXIT_OK);
		} else if (strcmp(argv[i], "--no-daemon") == 0) {
			no_daemon = 1;
		} else if (strcmp(argv[i], "-V") == 0 ||
		    strcmp(argv[i], "--verbose") == 0) {
			verbose++;
		} else if (strcmp(argv[i], "--persistence-mode") == 0 ||
		    strcmp(argv[i], "--no-persistence-mode") == 0) {
			/* Accepted for CLI parity; FreeBSD holds fds open. */
		} else if (strcmp(argv[i], "--pid-file") == 0 ||
		    strcmp(argv[i], "-p") == 0) {
			if (i + 1 >= argc) {
				bd_usage();
				return (NVCTL_EXIT_USAGE);
			}
			pid_file = argv[++i];
		} else if (strcmp(argv[i], "--user") == 0 ||
		    strcmp(argv[i], "-u") == 0) {
			if (i + 1 >= argc) {
				bd_usage();
				return (NVCTL_EXIT_USAGE);
			}
			run_user = argv[++i];
		} else {
			fprintf(stderr,
			    "nvidia-persistenced: unsupported option '%s'\n",
			    argv[i]);
			bd_usage();
			return (NVCTL_EXIT_USAGE);
		}
	}

	if (run_user != NULL) {
		pw = getpwnam(run_user);
		if (pw == NULL) {
			fprintf(stderr,
			    "nvidia-persistenced: unknown user '%s'\n",
			    run_user);
			return (NVCTL_EXIT_USAGE);
		}
		if (setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0) {
			fprintf(stderr,
			    "nvidia-persistenced: failed to switch to '%s': %s\n",
			    run_user, strerror(errno));
			return (NVCTL_EXIT_ERR);
		}
	}

	if (!no_daemon) {
		if (daemonize_proc() != 0) {
			fprintf(stderr,
			    "nvidia-persistenced: daemonize failed: %s\n",
			    strerror(errno));
			return (NVCTL_EXIT_ERR);
		}
	}

	if (nv_ctx_open(&ctx, verbose) != 0)
		return (NVCTL_EXIT_ERR);
	if (ctx.gpu_count == 0) {
		if (no_daemon)
			fprintf(stderr,
			    "nvidia-persistenced: no attached NVIDIA GPUs found\n");
		nv_ctx_close(&ctx);
		return (NVCTL_EXIT_ERR);
	}

	if (pid_file != NULL) {
		fp = fopen(pid_file, "w");
		if (fp != NULL) {
			fprintf(fp, "%d\n", (int)getpid());
			fclose(fp);
		}
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_bd_signal;
	(void)sigaction(SIGINT, &sa, NULL);
	(void)sigaction(SIGTERM, &sa, NULL);

	if (no_daemon) {
		printf("nvidia-persistenced: holding %d GPU(s) initialized "
		    "(PID %d); send SIGTERM/SIGINT to release\n",
		    ctx.gpu_count, (int)getpid());
		fflush(stdout);
	}

	while (!g_bd_stop)
		pause();

	if (pid_file != NULL)
		(void)unlink(pid_file);
	nv_ctx_close(&ctx);
	return (NVCTL_EXIT_OK);
}
