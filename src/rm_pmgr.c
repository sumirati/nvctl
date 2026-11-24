#include <stdio.h>
#include <string.h>

#include "nvctl.h"

struct probe_target {
	const char	*name;
	uint32_t	 cmd;
	uint32_t	 size;
};

static const struct probe_target k_targets[] = {
	{ "pwr_pol_info",   0x20802618U, 5520U },
	{ "pwr_pol_ctrl",   0x2080261AU, 2124U },
	{ "pwr_pol_stat",   0x2080261CU,  136U },
	{ "pwr_261f",       0x2080261FU,    8U },
	{ "pwr_2620",       0x20802620U,   56U },
	{ "fan_2702",       0x20802702U,    4U },
	{ "fan_2703",       0x20802703U,   16U },
	{ "fan_2709",       0x20802709U,    8U },
	{ "fan_270a",       0x2080270AU,   16U },
	{ "fan_270c",       0x2080270CU,   32U }
};

void
nv_pmgr_probe(struct nvctl_ctx *ctx, int idx, int verbose)
{
	static uint8_t buf[6144];
	uint32_t st;
	uint32_t val;
	uint32_t off;
	int printed;
	size_t i;

	if (idx < 0 || idx >= ctx->gpu_count)
		return;

	for (i = 0; i < sizeof(k_targets) / sizeof(k_targets[0]); i++) {
		memset(buf, 0, k_targets[i].size);
		st = nv_rm_control(ctx, ctx->gpus[idx].h_sub,
		    k_targets[i].cmd, buf, k_targets[i].size);
		printf("  %-13s 0x%08X (%4u B) -> 0x%02X%s\n",
		    k_targets[i].name, k_targets[i].cmd, k_targets[i].size,
		    st, (st == NV_OK) ? " (OK)" : "");

		if (verbose >= 1 && st == NV_OK) {
			printed = 0;
			printf("    words:");
			for (off = 0; off + 4 <= k_targets[i].size &&
			    printed < 16; off += 4) {
				memcpy(&val, buf + off, sizeof(val));
				if (val != 0) {
					printf(" [0x%X]=%u(0x%X)",
					    off, val, val);
					printed++;
				}
			}
			printf("\n");
		}
	}
}
