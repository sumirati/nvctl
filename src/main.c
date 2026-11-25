#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nvctl.h"

static volatile sig_atomic_t g_stop = 0;

static void
on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static int
parse_u32(const char *s, uint32_t *out)
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
reject_extra_arg(const char *verb, const char *arg)
{
	if (arg[0] == '-')
		fprintf(stderr,
		    "nvctl: unexpected option '%s' after '%s' "
		    "(global flags must precede the command)\n", arg, verb);
	else
		fprintf(stderr,
		    "nvctl: unexpected argument '%s' for '%s'\n", arg, verb);
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: nvctl [-jqv] [-d index] "
	    "[status | list | info | temp | power [get | set watts] | "
	    "persist | probe]\n");
}

static int
cmd_list(struct nvctl_ctx *ctx, const struct nvctl_opts *o)
{
	char devnode[32];
	uint32_t bus;
	uint32_t slot;
	int i;
	int first;

	first = 1;
	if (o->json)
		printf("[");

	for (i = 0; i < ctx->gpu_count; i++) {
		if (o->device_idx >= 0 && o->device_idx != i)
			continue;
		if (ctx->gpus[i].dev_unit >= 0)
			snprintf(devnode, sizeof(devnode), "/dev/nvidia%d",
			    ctx->gpus[i].dev_unit);
		else
			snprintf(devnode, sizeof(devnode), "/dev/nvidia?");

		bus = (ctx->gpus[i].gpu_id >> 8) & 0xFFU;
		slot = (ctx->gpus[i].gpu_id >> 3) & 0x1FU;

		if (o->json) {
			printf("%s{\"index\":%d,\"devnode\":\"%s\","
			    "\"pci\":\"0:%u:%u.0\",\"gpu_id\":\"0x%08X\","
			    "\"model\":\"%s\"}",
			    first ? "" : ",", i, devnode, bus, slot,
			    ctx->gpus[i].gpu_id, ctx->gpus[i].model);
			first = 0;
		} else {
			printf("gpu%d: %-13s pci0:%u:%u:0  gpuId=0x%08X  %s\n",
			    i, devnode, bus, slot,
			    ctx->gpus[i].gpu_id, ctx->gpus[i].model);
		}
	}

	if (o->json)
		printf("]\n");
	return (NVCTL_EXIT_OK);
}

static int
cmd_info(struct nvctl_ctx *ctx, const struct nvctl_opts *o)
{
	struct nvctl_hwinfo hw;
	struct nvctl_thermal th;
	struct nvctl_power pw;
	char devnode[32];
	uint32_t bus;
	uint32_t slot;
	int i;
	int first;

	first = 1;
	if (o->json)
		printf("[");

	for (i = 0; i < ctx->gpu_count; i++) {
		if (o->device_idx >= 0 && o->device_idx != i)
			continue;

		if (ctx->gpus[i].dev_unit >= 0)
			snprintf(devnode, sizeof(devnode), "/dev/nvidia%d",
			    ctx->gpus[i].dev_unit);
		else
			snprintf(devnode, sizeof(devnode), "/dev/nvidia?");

		bus = (ctx->gpus[i].gpu_id >> 8) & 0xFFU;
		slot = (ctx->gpus[i].gpu_id >> 3) & 0x1FU;
		nv_hwinfo_read(ctx, i, &hw);
		(void)nv_thermal_read(ctx, i, &th, o->verbose);
		(void)nv_power_read(ctx, i, &pw, o->verbose);

		if (o->json) {
			printf("%s{\"index\":%d,\"device\":\"%s\","
			    "\"pci\":\"pci0:%u:%u:0\",\"gpu_id\":\"0x%08X\","
			    "\"pci_id\":\"%04x:%04x\","
			    "\"pci_subsys\":\"%04x:%04x\","
			    "\"pci_rev\":\"0x%02x\","
			    "\"model\":\"%s\",\"uuid\":\"%s\",\"vbios\":\"%s\","
			    "\"bus\":\"%s\",\"irq\":%d,\"driver\":\"%s\","
			    "\"pcie\":{\"gen\":%u,\"width\":%u,"
			    "\"max_gen\":%u,\"max_width\":%u},"
			    "\"memory\":{\"total_mb\":%u,\"free_mb\":%u,"
			    "\"type\":\"%s\",\"bus_width_bits\":%u},"
			    "\"pstate\":%d,"
			    "\"temperature\":{\"core_c\":%d,\"slowdown_c\":%u,"
			    "\"shutdown_c\":%u},"
			    "\"power\":{\"cap_w\":%u,\"min_w\":%u,"
			    "\"default_w\":%u,\"max_w\":%u}}",
			    first ? "" : ",", i, devnode, bus, slot,
			    ctx->gpus[i].gpu_id,
			    hw.pci_vendor, hw.pci_device,
			    hw.pci_subvendor, hw.pci_subdevice, hw.pci_rev,
			    ctx->gpus[i].model,
			    ctx->gpus[i].uuid, ctx->gpus[i].vbios,
			    ctx->gpus[i].bus_type, ctx->gpus[i].irq,
			    ctx->driver_version,
			    hw.pcie_gen, hw.pcie_width,
			    hw.pcie_max_gen, hw.pcie_max_width,
			    hw.vram_total_mb, hw.vram_free_mb,
			    nv_ram_type_str(hw.vram_type), hw.vram_bus_width,
			    hw.pstate,
			    th.valid ? th.core_temp_c : -1,
			    th.slowdown_c, th.shutdown_c,
			    pw.current_mw / 1000U, pw.min_mw / 1000U,
			    pw.default_mw / 1000U, pw.max_mw / 1000U);
			first = 0;
		} else {
			printf("gpu%d.device: %s\n", i, devnode);
			printf("gpu%d.pci: pci0:%u:%u:0\n", i, bus, slot);
			if (hw.pci_valid) {
				printf("gpu%d.pci_id: %04x:%04x\n", i,
				    hw.pci_vendor, hw.pci_device);
				printf("gpu%d.pci_subsys: %04x:%04x\n", i,
				    hw.pci_subvendor, hw.pci_subdevice);
				printf("gpu%d.pci_rev: 0x%02x\n", i,
				    hw.pci_rev);
			}
			printf("gpu%d.gpu_id: 0x%08X\n", i,
			    ctx->gpus[i].gpu_id);
			printf("gpu%d.model: %s\n", i, ctx->gpus[i].model);
			printf("gpu%d.uuid: %s\n", i, ctx->gpus[i].uuid);
			printf("gpu%d.vbios: %s\n", i, ctx->gpus[i].vbios);
			printf("gpu%d.bus: %s\n", i, ctx->gpus[i].bus_type);
			if (hw.pcie_valid) {
				printf("gpu%d.pcie.gen: %u\n", i, hw.pcie_gen);
				printf("gpu%d.pcie.width: %u\n", i,
				    hw.pcie_width);
				printf("gpu%d.pcie.max_gen: %u\n", i,
				    hw.pcie_max_gen);
				printf("gpu%d.pcie.max_width: %u\n", i,
				    hw.pcie_max_width);
			}
			if (ctx->gpus[i].irq >= 0)
				printf("gpu%d.irq: %d\n", i, ctx->gpus[i].irq);
			printf("gpu%d.driver: %s\n", i, ctx->driver_version);
			if (hw.fb_valid) {
				printf("gpu%d.memory.total_mb: %u\n", i,
				    hw.vram_total_mb);
				printf("gpu%d.memory.free_mb: %u\n", i,
				    hw.vram_free_mb);
				printf("gpu%d.memory.type: %s\n", i,
				    nv_ram_type_str(hw.vram_type));
				printf("gpu%d.memory.bus_width_bits: %u\n", i,
				    hw.vram_bus_width);
			}
			if (hw.pstate >= 0)
				printf("gpu%d.perf.pstate: P%d\n", i,
				    hw.pstate);
			if (th.valid) {
				printf("gpu%d.temperature.core_c: %d\n", i,
				    th.core_temp_c);
				printf("gpu%d.temperature.slowdown_c: %u\n", i,
				    th.slowdown_c);
				printf("gpu%d.temperature.shutdown_c: %u\n", i,
				    th.shutdown_c);
			}
			if (pw.valid) {
				printf("gpu%d.power.cap_w: %u\n", i,
				    pw.current_mw / 1000U);
				printf("gpu%d.power.min_w: %u\n", i,
				    pw.min_mw / 1000U);
				printf("gpu%d.power.default_w: %u\n", i,
				    pw.default_mw / 1000U);
				printf("gpu%d.power.max_w: %u\n", i,
				    pw.max_mw / 1000U);
			}
		}
	}

	if (o->json)
		printf("]\n");
	return (NVCTL_EXIT_OK);
}

static int
cmd_status(struct nvctl_ctx *ctx, const struct nvctl_opts *o)
{
	struct nvctl_thermal th;
	struct nvctl_power pw;
	int i;
	int first;

	first = 1;
	if (o->json)
		printf("[");
	else if (!o->quiet)
		printf("%-4s %-10s %-6s %-9s %s\n",
		    "GPU", "GPU_ID", "TEMP", "SLOW/SHDN",
		    "CAP (MIN/CUR/DEF/MAX)");

	for (i = 0; i < ctx->gpu_count; i++) {
		if (o->device_idx >= 0 && o->device_idx != i)
			continue;
		(void)nv_thermal_read(ctx, i, &th, o->verbose);
		(void)nv_power_read(ctx, i, &pw, o->verbose);

		if (o->json) {
			printf("%s{\"index\":%d,\"gpu_id\":\"0x%08X\","
			    "\"temp_c\":%d,\"slowdown_c\":%u,\"shutdown_c\":%u,"
			    "\"power_min_w\":%u,\"power_cur_w\":%u,"
			    "\"power_def_w\":%u,\"power_max_w\":%u}",
			    first ? "" : ",", i, ctx->gpus[i].gpu_id,
			    th.valid ? th.core_temp_c : -1,
			    th.slowdown_c, th.shutdown_c,
			    pw.min_mw / 1000U, pw.current_mw / 1000U,
			    pw.default_mw / 1000U, pw.max_mw / 1000U);
			first = 0;
		} else if (o->quiet) {
			printf("%d %u\n", th.valid ? th.core_temp_c : -1,
			    pw.current_mw / 1000U);
		} else {
			printf("%-4d 0x%08X %3d C  %3u/%3u C  "
			    "%u / %u / %u / %u W\n",
			    i, ctx->gpus[i].gpu_id,
			    th.valid ? th.core_temp_c : -1,
			    th.slowdown_c, th.shutdown_c,
			    pw.min_mw / 1000U, pw.current_mw / 1000U,
			    pw.default_mw / 1000U, pw.max_mw / 1000U);
		}
	}

	if (o->json)
		printf("]\n");
	return (NVCTL_EXIT_OK);
}

static int
cmd_temp(struct nvctl_ctx *ctx, const struct nvctl_opts *o)
{
	struct nvctl_thermal th;
	int i;
	int first;
	int rc;

	first = 1;
	rc = NVCTL_EXIT_OK;
	if (o->json)
		printf("[");

	for (i = 0; i < ctx->gpu_count; i++) {
		if (o->device_idx >= 0 && o->device_idx != i)
			continue;

		if (nv_thermal_read(ctx, i, &th, o->verbose) != 0)
			rc = (th.rm_status == NV_ERR_NOT_SUPPORTED) ?
			    NVCTL_EXIT_UNSUP : NVCTL_EXIT_ERR;

		if (o->json) {
			printf("%s{\"index\":%d,\"valid\":%s,\"temp_c\":%d,"
			    "\"slowdown_c\":%u,\"shutdown_c\":%u}",
			    first ? "" : ",", i, th.valid ? "true" : "false",
			    th.core_temp_c, th.slowdown_c, th.shutdown_c);
			first = 0;
		} else if (o->quiet) {
			if (th.valid)
				printf("%d\n", th.core_temp_c);
			else
				printf("ERR\n");
		} else {
			if (th.valid)
				printf("gpu%d: %d C (slowdown %u C, shutdown %u C)\n",
				    i, th.core_temp_c, th.slowdown_c,
				    th.shutdown_c);
			else
				printf("gpu%d: thermal query failed (0x%X)\n",
				    i, th.rm_status);
		}
	}

	if (o->json)
		printf("]\n");
	return (rc);
}

static int
cmd_power(struct nvctl_ctx *ctx, const struct nvctl_opts *o, int argc,
    char *argv[])
{
	struct nvctl_power pw;
	uint32_t st;
	uint32_t watts;
	uint32_t actual_w;
	int target;
	int i;
	int first;
	int rc;

	if (argc >= 2 && strcmp(argv[1], "set") == 0) {
		if (argc != 3) {
			if (argc > 3)
				reject_extra_arg("power set", argv[3]);
			usage();
			return (NVCTL_EXIT_USAGE);
		}
		if (parse_u32(argv[2], &watts) != 0) {
			fprintf(stderr, "nvctl: invalid wattage '%s'\n",
			    argv[2]);
			return (NVCTL_EXIT_USAGE);
		}
		target = (o->device_idx >= 0) ? o->device_idx : 0;
		if (ctx->gpu_count > 1 && o->device_idx < 0) {
			fprintf(stderr,
			    "nvctl: -d <index> required with multiple GPUs\n");
			return (NVCTL_EXIT_USAGE);
		}
		rc = nv_power_set(ctx, target, watts, &st, o->verbose);
		if (rc == -2)
			return (NVCTL_EXIT_RANGE);
		if (rc != 0) {
			if (st == NV_ERR_INSUFFICIENT_PERM)
				fprintf(stderr,
				    "nvctl: permission denied (run with sudo)\n");
			else
				fprintf(stderr,
				    "nvctl: power set failed (status=0x%X)\n",
				    st);
			return (st == NV_ERR_NOT_SUPPORTED) ?
			    NVCTL_EXIT_UNSUP : NVCTL_EXIT_ERR;
		}
		if (nv_power_read(ctx, target, &pw, 0) != 0) {
			fprintf(stderr,
			    "nvctl: power cap readback failed\n");
			return (NVCTL_EXIT_ERR);
		}
		actual_w = pw.current_mw / 1000U;
		if (actual_w != watts) {
			fprintf(stderr,
			    "nvctl: gpu%d requested %u W, but driver clamped "
			    "cap to %u W (default %u W, max %u W)\n",
			    target, watts, actual_w,
			    pw.default_mw / 1000U, pw.max_mw / 1000U);
			return (NVCTL_EXIT_CLAMPED);
		}
		printf("gpu%d: power cap set to %u W\n", target, actual_w);
		return (NVCTL_EXIT_OK);
	}

	if (argc >= 2) {
		if (strcmp(argv[1], "get") != 0) {
			reject_extra_arg("power", argv[1]);
			usage();
			return (NVCTL_EXIT_USAGE);
		}
		if (argc > 2) {
			reject_extra_arg("power get", argv[2]);
			usage();
			return (NVCTL_EXIT_USAGE);
		}
	}

	first = 1;
	rc = NVCTL_EXIT_OK;
	if (o->json)
		printf("[");

	for (i = 0; i < ctx->gpu_count; i++) {
		if (o->device_idx >= 0 && o->device_idx != i)
			continue;
		if (nv_power_read(ctx, i, &pw, o->verbose) != 0)
			rc = NVCTL_EXIT_ERR;

		if (o->json) {
			printf("%s{\"index\":%d,\"min_w\":%u,\"cur_w\":%u,"
			    "\"def_w\":%u,\"max_w\":%u}",
			    first ? "" : ",", i, pw.min_mw / 1000U,
			    pw.current_mw / 1000U, pw.default_mw / 1000U,
			    pw.max_mw / 1000U);
			first = 0;
		} else if (o->quiet) {
			printf("%u\n", pw.current_mw / 1000U);
		} else {
			printf("gpu%d: cap=%u W (min %u W, default %u W, max %u W)\n",
			    i, pw.current_mw / 1000U, pw.min_mw / 1000U,
			    pw.default_mw / 1000U, pw.max_mw / 1000U);
		}
	}

	if (o->json)
		printf("]\n");
	return (rc);
}

static int
cmd_persist(struct nvctl_ctx *ctx, const struct nvctl_opts *o)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	(void)sigaction(SIGINT, &sa, NULL);
	(void)sigaction(SIGTERM, &sa, NULL);

	if (!o->quiet) {
		printf("nvctl: holding %d GPU(s) initialized (PID %d); "
		    "send SIGTERM/SIGINT to release\n",
		    ctx->gpu_count, (int)getpid());
		fflush(stdout);
	}

	while (!g_stop)
		pause();

	return (NVCTL_EXIT_OK);
}

static int
cmd_probe(struct nvctl_ctx *ctx, const struct nvctl_opts *o)
{
	struct nvctl_thermal th;
	int i;

	for (i = 0; i < ctx->gpu_count; i++) {
		if (o->device_idx >= 0 && o->device_idx != i)
			continue;

		printf("gpu%d (gpuId=0x%08X, dev=%u):\n",
		    i, ctx->gpus[i].gpu_id, ctx->gpus[i].device_instance);

		if (nv_thermal_read(ctx, i, &th, o->verbose) == 0)
			printf("  thermal        0x20800501 (1036 B) -> "
			    "0x00 (OK) %d C\n", th.core_temp_c);
		else
			printf("  thermal        0x20800501 -> 0x%02X\n",
			    th.rm_status);

		nv_pmgr_probe(ctx, i, o->verbose);
	}
	return (NVCTL_EXIT_OK);
}

int
main(int argc, char *argv[])
{
	struct nvctl_opts opts;
	struct nvctl_ctx ctx;
	const char *verb;
	uint32_t dev_u32;
	int ch;
	int rc;

	opts.device_idx = -1;
	opts.json = 0;
	opts.quiet = 0;
	opts.verbose = 0;

	while ((ch = getopt(argc, argv, "d:jqvh")) != -1) {
		switch (ch) {
		case 'd':
			if (parse_u32(optarg, &dev_u32) != 0 ||
			    dev_u32 >= NVCTL_MAX_GPUS) {
				fprintf(stderr,
				    "nvctl: invalid device index '%s'\n",
				    optarg);
				return (NVCTL_EXIT_USAGE);
			}
			opts.device_idx = (int)dev_u32;
			break;
		case 'j':
			opts.json = 1;
			break;
		case 'q':
			opts.quiet = 1;
			break;
		case 'v':
			opts.verbose++;
			break;
		case 'h':
		default:
			usage();
			return (NVCTL_EXIT_USAGE);
		}
	}

	argc -= optind;
	argv += optind;
	verb = (argc > 0) ? argv[0] : "status";

	if (strcmp(verb, "status") != 0 && strcmp(verb, "list") != 0 &&
	    strcmp(verb, "info") != 0 && strcmp(verb, "temp") != 0 &&
	    strcmp(verb, "power") != 0 && strcmp(verb, "persist") != 0 &&
	    strcmp(verb, "probe") != 0) {
		fprintf(stderr, "nvctl: unknown command '%s'\n", verb);
		usage();
		return (NVCTL_EXIT_USAGE);
	}
	if (strcmp(verb, "power") != 0 && argc > 1) {
		reject_extra_arg(verb, argv[1]);
		usage();
		return (NVCTL_EXIT_USAGE);
	}

	if (nv_ctx_open(&ctx, opts.verbose) != 0)
		return (NVCTL_EXIT_ERR);

	if (ctx.gpu_count == 0) {
		fprintf(stderr, "nvctl: no attached NVIDIA GPUs found\n");
		nv_ctx_close(&ctx);
		return (NVCTL_EXIT_ERR);
	}

	if (opts.device_idx >= ctx.gpu_count) {
		fprintf(stderr, "nvctl: invalid device index %d (found %d)\n",
		    opts.device_idx, ctx.gpu_count);
		nv_ctx_close(&ctx);
		return (NVCTL_EXIT_USAGE);
	}

	if (strcmp(verb, "status") == 0)
		rc = cmd_status(&ctx, &opts);
	else if (strcmp(verb, "list") == 0)
		rc = cmd_list(&ctx, &opts);
	else if (strcmp(verb, "info") == 0)
		rc = cmd_info(&ctx, &opts);
	else if (strcmp(verb, "temp") == 0)
		rc = cmd_temp(&ctx, &opts);
	else if (strcmp(verb, "power") == 0)
		rc = cmd_power(&ctx, &opts, argc, argv);
	else if (strcmp(verb, "persist") == 0)
		rc = cmd_persist(&ctx, &opts);
	else
		rc = cmd_probe(&ctx, &opts);

	nv_ctx_close(&ctx);
	return (rc);
}
