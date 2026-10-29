#pragma once
/* Meizu 16s Pro (m1973, SM8150 / SD855+). Values from the phone's own stock
 * trees: the FDT inside the Flyme 8.1.5.3A update.zip boot.img and its dtbo
 * overlays (root model "M1973(MTP),meizu"); mirrors tools/devices.py and
 * pexpert/pexpert/arm64/sweet_dev.h. See M1973.md. */

#define BOARD_NAME "m1973"

/* ABL continuous-splash framebuffer: stock-DTS reserved-memory
 * "cont_splash_region" = <0x0 0x9c000000 0x0 0x0240000> (36 MiB), same
 * range as guacamole. Panel: Samsung SOFEF00M AMOLED, 1080x2232
 * (FHD+), DSI cmd mode, 4 lanes. */
#define FB_BASE		0x9c000000UL
#define FB_W		1080
#define FB_H		2232
#define FB_STRIDE	(FB_W * 4)

/* GICv3: stock DTB arm,gic-v3 reg = <0x17a00000 0x10000 0x17a60000
 * 0x100000>, redistributor-stride 0x20000. */
#define GICD_BASE	0x17a00000UL
#define GICR_BASE	0x17a60000UL
#define GICR_SIZE	0x100000UL
#define GICR_PE_SIZE	0x20000UL
#define VTIMER_INTID	19	/* GIC_PPI 3 numbering */
#define PTIMER_INTID	18

/* SoC-level reset registers: stock DTB qcom,msm-imem@146bf000, so
 * restart_reason@65c is at 0x146bf65c (same as guacamole); PS_HOLD
 * restart@c264000 (qcom,pshold). */
#define IMEM_RESTART_REASON	0x146bf65cUL
#define PSHOLD			0x0c264000UL

/* Internal UFS host (stock DTB ufshc@1d84000, 2 lanes). */
#define UFS_BASE		0x01d84000UL

/* Skip the read-only UFS register probe, as on guacamole: reading the
 * host registers while ABL has it gated stalls the bus. */
#define PROBE_UFS		0

/* Reset-surviving scratch word (DRAM, 0x18000000 into the assumed upper
 * kernel bank, as on guacamole - VERIFY on the probe report's MEM line). */
#define MARK_PA		0x198000000UL

/* TLMM (pinctrl@03000000 in the stock DTB); pin stride 0x1000. */
#define BOARD_TLMM_PA		0x03000000UL
