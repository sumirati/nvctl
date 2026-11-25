#ifndef NVCTL_H
#define NVCTL_H

#include <stdint.h>
#include <stddef.h>

#define NVCTL_MAX_GPUS       32

#define NVCTL_EXIT_OK        0
#define NVCTL_EXIT_ERR       1
#define NVCTL_EXIT_USAGE     2
#define NVCTL_EXIT_UNSUP     3
#define NVCTL_EXIT_CLAMPED   4
#define NVCTL_EXIT_RANGE     5

#define NV_IOCTL_MAGIC       'F'
#define NV_ESC_RM_FREE       0x29
#define NV_ESC_RM_CONTROL    0x2A
#define NV_ESC_RM_ALLOC      0x2B

#define NV01_ROOT_USER       0x00000041U
#define NV01_DEVICE_0        0x00000080U
#define NV20_SUBDEVICE_0     0x00002080U

#define NV0000_CTRL_CMD_GPU_GET_ATTACHED_IDS        0x00000201U
#define NV0000_CTRL_CMD_GPU_GET_ID_INFO             0x00000202U
#define NV0000_CTRL_CMD_GPU_ATTACH_IDS              0x00000215U

#define NV_OK                        0x00000000U
#define NV_ERR_INSUFFICIENT_PERM     0x0000001BU
#define NV_ERR_INVALID_ARGUMENT      0x0000001FU
#define NV_ERR_INVALID_PARAM_STRUCT  0x0000003AU
#define NV_ERR_NOT_SUPPORTED         0x00000056U

struct nvctl_opts {
	int	device_idx;
	int	json;
	int	quiet;
	int	verbose;
};

struct nvctl_gpu {
	uint32_t	gpu_id;
	uint32_t	device_instance;
	uint32_t	subdevice_instance;
	uint32_t	h_dev;
	uint32_t	h_sub;
	int		dev_fd;
	int		dev_unit;
	int		irq;
	char		model[96];
	char		uuid[64];
	char		vbios[32];
	char		bus_type[16];
};

struct nvctl_ctx {
	int			ctl_fd;
	int			dev_fds[NVCTL_MAX_GPUS];
	int			dev_units[NVCTL_MAX_GPUS];
	int			dev_fd_count;
	uint32_t		h_client;
	int			gpu_count;
	char			driver_version[128];
	struct nvctl_gpu	gpus[NVCTL_MAX_GPUS];
};

struct nvctl_thermal {
	int		valid;
	uint32_t	rm_status;
	uint32_t	instr_stride;
	uint32_t	sensor_count;
	int32_t		core_temp_c;
	uint32_t	slowdown_c;
	uint32_t	shutdown_c;
};

struct nvctl_power {
	int		valid;
	uint32_t	rm_status;
	uint32_t	min_mw;
	uint32_t	default_mw;
	uint32_t	current_mw;
	uint32_t	max_mw;
};

struct nvctl_hwinfo {
	int		pci_valid;
	uint16_t	pci_vendor;
	uint16_t	pci_device;
	uint16_t	pci_subvendor;
	uint16_t	pci_subdevice;
	uint8_t		pci_rev;
	int		fb_valid;
	uint32_t	vram_total_mb;
	uint32_t	vram_free_mb;
	uint32_t	vram_bus_width;
	uint32_t	vram_type;
	uint32_t	fb_partitions;
	uint32_t	l2_cache_kb;
	int		pcie_valid;
	uint32_t	pcie_gen;
	uint32_t	pcie_width;
	uint32_t	pcie_max_gen;
	uint32_t	pcie_max_width;
	int		gr_valid;
	uint32_t	sm_version;
	uint32_t	gpc_count;
	uint32_t	sm_count;
	uint32_t	cuda_cores;
	uint32_t	max_warps_per_sm;
	int		clk_valid;
	uint32_t	core_clk_khz;
	uint32_t	mem_clk_khz;
	int		pstate;
};

int	 nv_ctx_open(struct nvctl_ctx *ctx, int verbose);
void	 nv_ctx_close(struct nvctl_ctx *ctx);
uint32_t nv_rm_control(struct nvctl_ctx *ctx, uint32_t h_obj, uint32_t cmd,
	    void *params, uint32_t params_size);

int	 nv_thermal_read(struct nvctl_ctx *ctx, int idx,
	    struct nvctl_thermal *out, int verbose);
int	 nv_power_read(struct nvctl_ctx *ctx, int idx,
	    struct nvctl_power *out, int verbose);
int	 nv_power_set(struct nvctl_ctx *ctx, int idx, uint32_t watts,
	    uint32_t *out_status, int verbose);
void	 nv_hwinfo_read(struct nvctl_ctx *ctx, int idx,
	    struct nvctl_hwinfo *out);
const char *nv_ram_type_str(uint32_t ram_type);
double	 nv_pcie_gbs(uint32_t gen, uint32_t width);
double	 nv_mem_gbs(uint32_t mem_clk_khz, uint32_t bus_width_bits,
	    uint32_t ram_type);
void	 nv_pmgr_probe(struct nvctl_ctx *ctx, int idx, int verbose);

int	 nv_compat_smi(int argc, char *argv[]);
int	 nv_compat_persistenced(int argc, char *argv[]);

#endif /* NVCTL_H */
