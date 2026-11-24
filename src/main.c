#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nvctl.h"

static void
usage(void)
{
	fprintf(stderr,
	    "usage: nvctl [-jqv] [-d index] "
	    "[status | list | temp | power [get | set watts] | probe]\n");
}

static int
cmd_list(struct nvctl_ctx *ctx, const struct nvctl_opts *o)
{
	int i;
	int first;

	first = 1;
	if (o->json)
		printf("[");

	for (i = 0; i < ctx->gpu_count; i++) {
		if (o->device_idx >= 0 && o->device_idx != i)
			continue;
		if (o->json) {
			printf("%s{\"index\":%d,\"gpu_id\":\"0x%08X\","
			    "\"device_instance\":%u}",
			    first ? "" : ",", i, ctx->gpus[i].gpu_id,
			    ctx->gpus[i].device_instance);
			first = 0;
		} else {
			printf("gpu%d: gpuId=0x%08X dev=%u subdev=%u\n",
			    i, ctx->gpus[i].gpu_id,
			    ctx->gpus[i].device_instance,
			    ctx->gpus[i].subdevice_instance);
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
	int target;
	int i;
	int first;
	int rc;

	if (argc >= 2 && strcmp(argv[1], "set") == 0) {
		if (argc < 3) {
			usage();
			return (NVCTL_EXIT_USAGE);
		}
		target = (o->device_idx >= 0) ? o->device_idx : 0;
		if (ctx->gpu_count > 1 && o->device_idx < 0) {
			fprintf(stderr,
			    "nvctl: -d <index> required with multiple GPUs\n");
			return (NVCTL_EXIT_USAGE);
		}
		watts = (uint32_t)strtoul(argv[2], NULL, 10);
		rc = nv_power_set(ctx, target, watts, &st, o->verbose);
		if (rc == -2)
			return (NVCTL_EXIT_USAGE);
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
		(void)nv_power_read(ctx, target, &pw, 0);
		printf("gpu%d: power cap set to %u W\n", target,
		    pw.current_mw / 1000U);
		return (NVCTL_EXIT_OK);
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
	int ch;
	int rc;

	opts.device_idx = -1;
	opts.json = 0;
	opts.quiet = 0;
	opts.verbose = 0;

	while ((ch = getopt(argc, argv, "d:jqvh")) != -1) {
		switch (ch) {
		case 'd':
			opts.device_idx = atoi(optarg);
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
	else if (strcmp(verb, "temp") == 0)
		rc = cmd_temp(&ctx, &opts);
	else if (strcmp(verb, "power") == 0)
		rc = cmd_power(&ctx, &opts, argc, argv);
	else if (strcmp(verb, "probe") == 0)
		rc = cmd_probe(&ctx, &opts);
	else {
		usage();
		rc = NVCTL_EXIT_USAGE;
	}

	nv_ctx_close(&ctx);
	return (rc);
}
