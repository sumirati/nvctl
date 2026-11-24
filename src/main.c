#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nvctl.h"

static void
usage(void)
{
	fprintf(stderr,
	    "usage: nvctl [-jqv] [-d index] [status | list | temp | power | fan | probe]\n");
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
			    "\"sensors\":%u,\"stride\":%u,\"status\":\"0x%X\"}",
			    first ? "" : ",", i, th.valid ? "true" : "false",
			    th.core_temp_c, th.sensor_count, th.instr_stride,
			    th.rm_status);
			first = 0;
		} else if (o->quiet) {
			if (th.valid)
				printf("%d\n", th.core_temp_c);
			else
				printf("ERR\n");
		} else {
			if (th.valid)
				printf("gpu%d: %d C (sensors=%u, stride=%u)\n",
				    i, th.core_temp_c, th.sensor_count,
				    th.instr_stride);
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
			printf("  thermal 0x20800501  size=%u  temp=%d C (OK)\n",
			    th.instr_stride, th.core_temp_c);
		else
			printf("  thermal 0x20800501  failed (status=0x%X)\n",
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

	if (strcmp(verb, "list") == 0)
		rc = cmd_list(&ctx, &opts);
	else if (strcmp(verb, "temp") == 0 || strcmp(verb, "status") == 0)
		rc = cmd_temp(&ctx, &opts);
	else if (strcmp(verb, "probe") == 0)
		rc = cmd_probe(&ctx, &opts);
	else if (strcmp(verb, "power") == 0 || strcmp(verb, "fan") == 0) {
		fprintf(stderr,
		    "nvctl: '%s' awaits verified PMGR struct sizes — run 'nvctl probe' first\n",
		    verb);
		rc = NVCTL_EXIT_UNSUP;
	} else {
		usage();
		rc = NVCTL_EXIT_USAGE;
	}

	nv_ctx_close(&ctx);
	return (rc);
}
