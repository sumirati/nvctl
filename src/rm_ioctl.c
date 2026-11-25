#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include "nvctl.h"

#define NV_IOC(cmd, type) _IOWR(NV_IOCTL_MAGIC, (cmd), type)
#define NV_ESC_CHECK_VERSION_STR  0xD2

struct nv_ioctl_rm_api_version {
	uint32_t	cmd;
	uint32_t	reply;
	char		version_string[64];
};

struct nv_os00_params {
	uint32_t	h_root;
	uint32_t	h_parent;
	uint32_t	h_obj;
	uint32_t	status;
};

struct nv_os21_params {
	uint32_t	h_root;
	uint32_t	h_parent;
	uint32_t	h_new;
	uint32_t	h_class;
	void		*parms;
	uint32_t	status;
};

struct nv_os54_params {
	uint32_t	h_client;
	uint32_t	h_obj;
	uint32_t	cmd;
	uint32_t	flags;
	void		*params;
	uint32_t	params_size;
	uint32_t	status;
};

struct nv0080_alloc_params {
	uint32_t	device_id;
	uint32_t	h_client_share;
	uint32_t	h_target_client;
	uint32_t	h_target_device;
	uint32_t	flags;
	uint64_t	va_space_size;
	uint64_t	va_start_internal;
	uint64_t	va_limit_internal;
	uint32_t	va_mode;
};

struct nv2080_alloc_params {
	uint32_t	subdevice_id;
};

struct nv0000_gpu_attach_params {
	uint32_t	gpu_ids[NVCTL_MAX_GPUS];
	uint32_t	failed_id;
};

struct nv0000_gpu_attached_params {
	uint32_t	gpu_ids[NVCTL_MAX_GPUS];
};

struct nv0000_gpu_id_info_params {
	uint32_t	gpu_id;
	uint32_t	gpu_flags;
	uint32_t	device_instance;
	uint32_t	subdevice_instance;
	void		*sz_name;
	uint32_t	sli_status;
	uint32_t	board_id;
	uint32_t	gpu_instance;
	int32_t		numa_id;
};

static void
sysctl_read_str(const char *name, char *buf, size_t sz, const char *fallback)
{
	size_t len;

	len = sz - 1;
	if (sysctlbyname(name, buf, &len, NULL, 0) == 0 && len > 0) {
		buf[len] = '\0';
		while (len > 0 && (buf[len - 1] == '\0' ||
		    buf[len - 1] == '\n' || buf[len - 1] == ' '))
			buf[--len] = '\0';
		if (len > 0)
			return;
	}
	snprintf(buf, sz, "%s", fallback);
}

static int
sysctl_read_int(const char *name, int fallback)
{
	int val;
	size_t len;

	len = sizeof(val);
	if (sysctlbyname(name, &val, &len, NULL, 0) == 0 && len == sizeof(val))
		return (val);
	return (fallback);
}

static uint32_t
rm_alloc(int fd, uint32_t root, uint32_t parent, uint32_t obj,
    uint32_t cls, void *parms, uint32_t *out_handle, int verbose)
{
	struct nv_os21_params p;

	memset(&p, 0, sizeof(p));
	p.h_root = root;
	p.h_parent = parent;
	p.h_new = obj;
	p.h_class = cls;
	p.parms = parms;

	if (ioctl(fd, NV_IOC(NV_ESC_RM_ALLOC, struct nv_os21_params), &p) < 0) {
		if (verbose >= 2)
			fprintf(stderr,
			    "[vv] ioctl(NV_ESC_RM_ALLOC, cls=0x%X): %s\n",
			    cls, strerror(errno));
		return (0xFFFFFFFFU);
	}
	if (out_handle != NULL)
		*out_handle = p.h_new;
	return (p.status);
}

static uint32_t
rm_free(int fd, uint32_t root, uint32_t parent, uint32_t obj)
{
	struct nv_os00_params p;

	memset(&p, 0, sizeof(p));
	p.h_root = root;
	p.h_parent = parent;
	p.h_obj = obj;

	if (ioctl(fd, NV_IOC(NV_ESC_RM_FREE, struct nv_os00_params), &p) < 0)
		return (0xFFFFFFFFU);
	return (p.status);
}

uint32_t
nv_rm_control(struct nvctl_ctx *ctx, uint32_t h_obj, uint32_t cmd,
    void *params, uint32_t params_size)
{
	struct nv_os54_params p;

	memset(&p, 0, sizeof(p));
	p.h_client = ctx->h_client;
	p.h_obj = h_obj;
	p.cmd = cmd;
	p.params = params;
	p.params_size = params_size;

	if (ioctl(ctx->ctl_fd, NV_IOC(NV_ESC_RM_CONTROL, struct nv_os54_params),
	    &p) < 0)
		return (0xFFFFFFFFU);
	return (p.status);
}

int
nv_ctx_open(struct nvctl_ctx *ctx, int verbose)
{
	struct nv0000_gpu_attach_params att;
	struct nv0000_gpu_attached_params ids;
	struct nv0000_gpu_id_info_params info;
	struct nv0080_alloc_params devp;
	struct nv2080_alloc_params subp;
	char devpath[32];
	uint32_t st;
	int fd;
	int i;

	memset(ctx, 0, sizeof(*ctx));
	ctx->ctl_fd = -1;
	sysctl_read_str("hw.nvidia.version", ctx->driver_version,
	    sizeof(ctx->driver_version), "unknown");
	for (i = 0; i < NVCTL_MAX_GPUS; i++) {
		ctx->dev_fds[i] = -1;
		ctx->dev_units[i] = -1;
		ctx->gpus[i].dev_fd = -1;
		ctx->gpus[i].dev_unit = -1;
		ctx->gpus[i].irq = -1;
	}

	ctx->ctl_fd = open("/dev/nvidiactl", O_RDWR);
	if (ctx->ctl_fd < 0) {
		fprintf(stderr, "nvctl: open(/dev/nvidiactl): %s\n",
		    strerror(errno));
		return (-1);
	}

	/*
	 * Legacy branches (e.g. 390.xx) require NV_ESC_CHECK_VERSION_STR
	 * handshake on /dev/nvidiactl before accepting NV_ESC_RM_ALLOC.
	 * Pass the short version string parsed from hw.nvidia.version with
	 * NV_RM_API_VERSION_CMD_RELAXED ('1').
	 */
	{
		struct nv_ioctl_rm_api_version ver;
		const char *p;
		const char *start;
		size_t n;

		memset(&ver, 0, sizeof(ver));
		ver.cmd = '1'; /* NV_RM_API_VERSION_CMD_RELAXED */
		for (p = ctx->driver_version; *p != '\0'; ) {
			while (*p == ' ' || *p == '\t')
				p++;
			start = p;
			while (*p != '\0' && *p != ' ' && *p != '\t')
				p++;
			if (start[0] >= '0' && start[0] <= '9' &&
			    memchr(start, '.', (size_t)(p - start)) != NULL) {
				n = (size_t)(p - start);
				if (n >= sizeof(ver.version_string))
					n = sizeof(ver.version_string) - 1;
				memcpy(ver.version_string, start, n);
				ver.version_string[n] = '\0';
				break;
			}
		}
		if (ioctl(ctx->ctl_fd,
		    NV_IOC(NV_ESC_CHECK_VERSION_STR,
		    struct nv_ioctl_rm_api_version), &ver) == 0) {
			if (verbose)
				fprintf(stderr,
				    "[v] CHECK_VERSION_STR ver='%s' reply=%u\n",
				    ver.version_string, ver.reply);
		} else if (verbose) {
			fprintf(stderr,
			    "[v] CHECK_VERSION_STR ioctl failed: %s\n",
			    strerror(errno));
		}
	}

	/*
	 * On FreeBSD, /dev/nvidia<N> follows the vgapci<N> unit number
	 * (e.g. /dev/nvidia2 when two non-NVIDIA GPUs occupy vgapci0..1).
	 * Open all present /dev/nvidia* nodes before GPU_ATTACH_IDS.
	 */
	for (i = 0; i < NVCTL_MAX_GPUS; i++) {
		snprintf(devpath, sizeof(devpath), "/dev/nvidia%d", i);
		fd = open(devpath, O_RDWR);
		if (fd >= 0) {
			ctx->dev_fds[ctx->dev_fd_count] = fd;
			ctx->dev_units[ctx->dev_fd_count] = i;
			ctx->dev_fd_count++;
			if (verbose >= 2)
				fprintf(stderr, "[vv] opened %s\n", devpath);
		}
	}

	{
		uint32_t root_out = 0;

		st = rm_alloc(ctx->ctl_fd, 0, 0, 0, NV01_ROOT_USER, &root_out,
		    &ctx->h_client, verbose);
		if (st == NV_OK && ctx->h_client == 0 && root_out != 0)
			ctx->h_client = root_out;
	}
	if (st != NV_OK) {
		fprintf(stderr, "nvctl: NV01_ROOT_USER alloc failed (0x%X)\n",
		    st);
		goto fail;
	}
	if (verbose)
		fprintf(stderr, "[v] hClient=0x%08X\n", ctx->h_client);

	memset(&att, 0, sizeof(att));
	att.gpu_ids[0] = 0xFFFFFFFFU;
	st = nv_rm_control(ctx, ctx->h_client, NV0000_CTRL_CMD_GPU_ATTACH_IDS,
	    &att, sizeof(att));
	if (verbose)
		fprintf(stderr, "[v] GPU_ATTACH_IDS status=0x%X\n", st);

	memset(&ids, 0, sizeof(ids));
	st = nv_rm_control(ctx, ctx->h_client,
	    NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS, &ids, sizeof(ids));
	if (st != NV_OK) {
		fprintf(stderr,
		    "nvctl: GPU_GET_ATTACHED_IDS failed (0x%X)\n", st);
		goto fail;
	}

	for (i = 0; i < NVCTL_MAX_GPUS; i++) {
		struct nvctl_gpu *g;

		if (ids.gpu_ids[i] == 0xFFFFFFFFU || ids.gpu_ids[i] == 0)
			break;

		g = &ctx->gpus[ctx->gpu_count];
		g->gpu_id = ids.gpu_ids[i];
		g->device_instance = (uint32_t)ctx->gpu_count;
		g->subdevice_instance = 0;
		g->dev_unit = (ctx->gpu_count < ctx->dev_fd_count) ?
		    ctx->dev_units[ctx->gpu_count] : -1;
		if (g->dev_unit >= 0) {
			char sname[64];

			snprintf(sname, sizeof(sname),
			    "hw.nvidia.gpus.%d.model", g->dev_unit);
			sysctl_read_str(sname, g->model, sizeof(g->model),
			    "unknown");
			snprintf(sname, sizeof(sname),
			    "hw.nvidia.gpus.%d.uuid", g->dev_unit);
			sysctl_read_str(sname, g->uuid, sizeof(g->uuid),
			    "unknown");
			snprintf(sname, sizeof(sname),
			    "hw.nvidia.gpus.%d.vbios", g->dev_unit);
			sysctl_read_str(sname, g->vbios, sizeof(g->vbios),
			    "unknown");
			snprintf(sname, sizeof(sname),
			    "hw.nvidia.gpus.%d.type", g->dev_unit);
			sysctl_read_str(sname, g->bus_type, sizeof(g->bus_type),
			    "unknown");
			snprintf(sname, sizeof(sname),
			    "hw.nvidia.gpus.%d.irq", g->dev_unit);
			g->irq = sysctl_read_int(sname, -1);
		} else {
			snprintf(g->model, sizeof(g->model), "unknown");
			snprintf(g->uuid, sizeof(g->uuid), "unknown");
			snprintf(g->vbios, sizeof(g->vbios), "unknown");
			snprintf(g->bus_type, sizeof(g->bus_type), "unknown");
			g->irq = -1;
		}

		memset(&info, 0, sizeof(info));
		info.gpu_id = g->gpu_id;
		st = nv_rm_control(ctx, ctx->h_client,
		    NV0000_CTRL_CMD_GPU_GET_ID_INFO, &info, sizeof(info));
		if (st == NV_OK) {
			g->device_instance = info.device_instance;
			g->subdevice_instance = info.subdevice_instance;
		} else if (verbose) {
			fprintf(stderr,
			    "[v] GPU_GET_ID_INFO(0x%08X) status=0x%X "
			    "(fallback inst=%u)\n",
			    g->gpu_id, st, g->device_instance);
		}

		g->h_dev = 0xBEEF0100U + (uint32_t)ctx->gpu_count;
		g->h_sub = 0xBEEF0200U + (uint32_t)ctx->gpu_count;

		memset(&devp, 0, sizeof(devp));
		devp.device_id = g->device_instance;
		st = rm_alloc(ctx->ctl_fd, ctx->h_client, ctx->h_client,
		    g->h_dev, NV01_DEVICE_0, &devp, NULL, verbose);
		if (st != NV_OK && ctx->dev_fd_count > 0) {
			/* 390.xx allocates NV01_DEVICE_0 on /dev/nvidia<N>. */
			int dfd = (ctx->gpu_count < ctx->dev_fd_count) ?
			    ctx->dev_fds[ctx->gpu_count] : ctx->dev_fds[0];
			devp.h_client_share = ctx->h_client;
			st = rm_alloc(dfd, ctx->h_client, ctx->h_client,
			    g->h_dev, NV01_DEVICE_0, &devp, NULL, verbose);
		}
		if (st != NV_OK) {
			if (verbose)
				fprintf(stderr,
				    "[v] NV01_DEVICE_0(%u) failed (0x%X)\n",
				    g->device_instance, st);
			g->h_dev = 0;
			g->h_sub = 0;
			continue;
		}

		memset(&subp, 0, sizeof(subp));
		subp.subdevice_id = g->subdevice_instance;
		st = rm_alloc(ctx->ctl_fd, ctx->h_client, g->h_dev,
		    g->h_sub, NV20_SUBDEVICE_0, &subp, NULL, verbose);
		if (st != NV_OK) {
			if (verbose)
				fprintf(stderr,
				    "[v] NV20_SUBDEVICE_0(%u) failed (0x%X)\n",
				    g->subdevice_instance, st);
			(void)rm_free(ctx->ctl_fd, ctx->h_client,
			    ctx->h_client, g->h_dev);
			g->h_dev = 0;
			g->h_sub = 0;
			continue;
		}

		ctx->gpu_count++;
	}

	return (0);

fail:
	nv_ctx_close(ctx);
	return (-1);
}

void
nv_ctx_close(struct nvctl_ctx *ctx)
{
	int i;

	if (ctx->ctl_fd < 0)
		return;

	for (i = 0; i < ctx->gpu_count; i++) {
		if (ctx->gpus[i].h_sub != 0)
			(void)rm_free(ctx->ctl_fd, ctx->h_client,
			    ctx->gpus[i].h_dev, ctx->gpus[i].h_sub);
		if (ctx->gpus[i].h_dev != 0)
			(void)rm_free(ctx->ctl_fd, ctx->h_client,
			    ctx->h_client, ctx->gpus[i].h_dev);
		if (ctx->gpus[i].dev_fd >= 0) {
			close(ctx->gpus[i].dev_fd);
			ctx->gpus[i].dev_fd = -1;
		}
	}

	if (ctx->h_client != 0) {
		(void)rm_free(ctx->ctl_fd, ctx->h_client, ctx->h_client,
		    ctx->h_client);
		ctx->h_client = 0;
	}

	for (i = 0; i < ctx->dev_fd_count; i++) {
		if (ctx->dev_fds[i] >= 0) {
			close(ctx->dev_fds[i]);
			ctx->dev_fds[i] = -1;
		}
	}
	ctx->dev_fd_count = 0;

	close(ctx->ctl_fd);
	ctx->ctl_fd = -1;
}
