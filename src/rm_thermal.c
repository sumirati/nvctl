#include <stdio.h>
#include <string.h>

#include "nvctl.h"

#define NV2080_CTRL_CMD_THERMAL_0501   0x20800501U
#define NV2080_THERM_0501_SIZE         1036U

int
nv_thermal_read(struct nvctl_ctx *ctx, int idx, struct nvctl_thermal *out,
    int verbose)
{
	uint32_t words[NV2080_THERM_0501_SIZE / 4];
	uint32_t masks[3];
	uint32_t st;
	size_t m;
	size_t w;
	int printed;

	memset(out, 0, sizeof(*out));
	if (idx < 0 || idx >= ctx->gpu_count)
		return (-1);

	masks[0] = 0xFFFFFFFFU;
	masks[1] = 0x00000001U;
	masks[2] = 0x00000003U;

	st = NV_ERR_INVALID_ARGUMENT;
	for (m = 0; m < 3; m++) {
		memset(words, 0, sizeof(words));
		words[0] = masks[m];
		st = nv_rm_control(ctx, ctx->gpus[idx].h_sub,
		    NV2080_CTRL_CMD_THERMAL_0501, words, sizeof(words));
		if (verbose >= 2)
			fprintf(stderr,
			    "[vv] 0x20800501 mask=0x%X -> status=0x%X\n",
			    masks[m], st);
		if (st == NV_OK) {
			if (verbose >= 2) {
				printed = 0;
				fprintf(stderr, "[vv] 0x20800501 words:");
				for (w = 0; w < NV2080_THERM_0501_SIZE / 4 &&
				    printed < 24; w++) {
					if (words[w] != 0) {
						fprintf(stderr,
						    " [%zu]=0x%X(%d)",
						    w, words[w],
						    (int32_t)words[w]);
						printed++;
					}
				}
				fprintf(stderr, "\n");
			} else if (verbose == 1) {
				fprintf(stderr,
				    "nvctl: gpu%d thermal 0x20800501 OK "
				    "(%d C, slowdown %u C, shutdown %u C)\n",
				    idx, (int32_t)words[1],
				    (words[13] >> 8) & 0xFFU,
				    (words[13] >> 16) & 0xFFU);
			}
			out->valid = 1;
			out->rm_status = st;
			out->instr_stride = NV2080_THERM_0501_SIZE;
			out->sensor_count = 1;
			out->core_temp_c = (int32_t)words[1];
			return (0);
		}
	}

	out->rm_status = st;
	return (-1);
}
