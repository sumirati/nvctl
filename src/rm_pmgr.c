#include <stdio.h>
#include <string.h>

#include "nvctl.h"

#define PMGR_PWR_LIMIT_BOUNDS_CMD   0x2080261CU
#define PMGR_PWR_LIMIT_BOUNDS_SZ    136U
#define PMGR_PWR_LIMIT_GET_CMD      0x2080261FU
#define PMGR_PWR_LIMIT_GET_SZ       8U
#define PMGR_PWR_LIMIT_SET_CMD      0x2080261EU
#define PMGR_PWR_LIMIT_SET_SZ       12U

struct probe_target {
	const char	*name;
	uint32_t	 cmd;
	uint32_t	 size;
};

static const struct probe_target k_targets[] = {
	{ "pwr_pol_info",   0x20802618U, 5520U },
	{ "pwr_pol_ctrl",   0x2080261AU, 2124U },
	{ "pwr_pol_bounds", 0x2080261CU,  136U },
	{ "pwr_lim_get",    0x2080261FU,    8U },
	{ "perf_pstate",    0x20802068U,    4U },
	{ "bus_pci_info",   0x20801801U,   16U },
	{ "fan_2702",       0x20802702U,    4U },
	{ "fan_2703",       0x20802703U,   16U },
	{ "fan_2709",       0x20802709U,    8U },
	{ "fan_270a",       0x2080270AU,   16U },
	{ "fan_270c",       0x2080270CU,   32U }
};

struct rm_info_item {
	uint32_t	index;
	uint32_t	data;
};

struct rm_info_list_params {
	uint32_t	list_size;
	uint32_t	pad;
	uint64_t	list_ptr;
};

int
nv_power_read(struct nvctl_ctx *ctx, int idx, struct nvctl_power *out,
    int verbose)
{
	uint8_t bbuf[PMGR_PWR_LIMIT_BOUNDS_SZ];
	uint32_t gbuf[PMGR_PWR_LIMIT_GET_SZ / 4];
	uint32_t st_b;
	uint32_t st_g;

	memset(out, 0, sizeof(*out));
	if (idx < 0 || idx >= ctx->gpu_count)
		return (-1);

	memset(bbuf, 0, sizeof(bbuf));
	st_b = nv_rm_control(ctx, ctx->gpus[idx].h_sub,
	    PMGR_PWR_LIMIT_BOUNDS_CMD, bbuf, sizeof(bbuf));

	memset(gbuf, 0, sizeof(gbuf));
	st_g = nv_rm_control(ctx, ctx->gpus[idx].h_sub,
	    PMGR_PWR_LIMIT_GET_CMD, gbuf, sizeof(gbuf));

	if (st_b != NV_OK || st_g != NV_OK) {
		out->rm_status = (st_b != NV_OK) ? st_b : st_g;
		return (-1);
	}

	memcpy(&out->min_mw, bbuf + 0x0C, sizeof(out->min_mw));
	memcpy(&out->default_mw, bbuf + 0x10, sizeof(out->default_mw));
	memcpy(&out->max_mw, bbuf + 0x14, sizeof(out->max_mw));
	out->current_mw = gbuf[1];
	out->valid = 1;
	out->rm_status = NV_OK;

	if (verbose >= 1)
		fprintf(stderr,
		    "nvctl: gpu%d power cap min=%uW cur=%uW def=%uW max=%uW\n",
		    idx, out->min_mw / 1000U, out->current_mw / 1000U,
		    out->default_mw / 1000U, out->max_mw / 1000U);
	return (0);
}

int
nv_power_set(struct nvctl_ctx *ctx, int idx, uint32_t watts,
    uint32_t *out_status, int verbose)
{
	struct nvctl_power cur;
	uint32_t sbuf[PMGR_PWR_LIMIT_SET_SZ / 4];
	uint32_t target_mw;
	uint32_t st;

	if (out_status != NULL)
		*out_status = NV_ERR_INVALID_ARGUMENT;
	if (nv_power_read(ctx, idx, &cur, 0) != 0)
		return (-1);

	target_mw = watts * 1000U;
	if (target_mw < cur.min_mw || target_mw > cur.max_mw) {
		fprintf(stderr,
		    "nvctl: %u W out of hardware bounds (%u..%u W)\n",
		    watts, cur.min_mw / 1000U, cur.max_mw / 1000U);
		return (-2);
	}

	memset(sbuf, 0, sizeof(sbuf));
	sbuf[0] = 0;          /* policy / domain index */
	sbuf[1] = target_mw;  /* limit in mW */
	sbuf[2] = 0;          /* flags */

	st = nv_rm_control(ctx, ctx->gpus[idx].h_sub,
	    PMGR_PWR_LIMIT_SET_CMD, sbuf, sizeof(sbuf));
	if (out_status != NULL)
		*out_status = st;
	if (verbose >= 1)
		fprintf(stderr,
		    "nvctl: gpu%d power set 0x%08X (%u mW) -> status=0x%X\n",
		    idx, PMGR_PWR_LIMIT_SET_CMD, target_mw, st);

	return (st == NV_OK) ? 0 : -1;
}

void
nv_pmgr_probe(struct nvctl_ctx *ctx, int idx, int verbose)
{
	static uint8_t buf[6144];
	struct rm_info_item fb_items[3];
	struct rm_info_item bus_items[3];
	struct rm_info_list_params lp;
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
		printf("  %-14s 0x%08X (%4u B) -> 0x%02X%s\n",
		    k_targets[i].name, k_targets[i].cmd, k_targets[i].size,
		    st, (st == NV_OK) ? " (OK)" : "");

		if (verbose >= 2 && st == NV_OK) {
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

	/* Probe FB_GET_INFO (0x20801301): ram_size(0), ram_type(2), bus_width(5) */
	memset(fb_items, 0, sizeof(fb_items));
	fb_items[0].index = 0U;
	fb_items[1].index = 2U;
	fb_items[2].index = 5U;
	memset(&lp, 0, sizeof(lp));
	lp.list_size = 3U;
	lp.list_ptr = (uint64_t)(uintptr_t)fb_items;
	st = nv_rm_control(ctx, ctx->gpus[idx].h_sub, 0x20801301U,
	    &lp, sizeof(lp));
	printf("  %-14s 0x%08X (%4zu B) -> 0x%02X%s\n",
	    "fb_get_info", 0x20801301U, sizeof(lp), st,
	    (st == NV_OK) ? " (OK)" : "");
	if (verbose >= 2 && st == NV_OK)
		printf("    items: ram_kb=%u ram_type=%u bus_width=%u\n",
		    fb_items[0].data, fb_items[1].data, fb_items[2].data);

	/* Probe BUS_GET_INFO (0x20801802): link_caps(5), link_ctrl(6), gen_info(30) */
	memset(bus_items, 0, sizeof(bus_items));
	bus_items[0].index = 5U;
	bus_items[1].index = 6U;
	bus_items[2].index = 30U;
	memset(&lp, 0, sizeof(lp));
	lp.list_size = 3U;
	lp.list_ptr = (uint64_t)(uintptr_t)bus_items;
	st = nv_rm_control(ctx, ctx->gpus[idx].h_sub, 0x20801802U,
	    &lp, sizeof(lp));
	printf("  %-14s 0x%08X (%4zu B) -> 0x%02X%s\n",
	    "bus_get_info", 0x20801802U, sizeof(lp), st,
	    (st == NV_OK) ? " (OK)" : "");
	if (verbose >= 2 && st == NV_OK)
		printf("    items: link_caps=0x%X link_ctrl=0x%X gen_info=0x%X\n",
		    bus_items[0].data, bus_items[1].data, bus_items[2].data);
}
