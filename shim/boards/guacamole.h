#pragma once
/* OnePlus 7 Pro (guacamole, SM8150). Values from the LineageOS
 * oneplus_sm8150 DTS (arch/arm64/boot/dts/18821/) and the mainline
 * sm8150-oneplus-guacamole.dts; confirm against the stock DTB
 * (see GUACAMOLE.md). Mirrors tools/devices.py. */

#define BOARD_NAME "guacamole"

/* ABL continuous-splash framebuffer: DTS reserved-memory
 * "cont_splash_region" = <0x0 0x9c000000 0x0 0x02400000> (36 MiB);
 * mainline maps the same range as a simple-framebuffer, 1440x3120
 * a8r8g8b8 stride 5760. Panel: Samsung S6E3HC2, DSC cmd mode. */
#define FB_BASE		0x9c000000UL
#define FB_W		1440
#define FB_H		3120
#define FB_STRIDE	(FB_W * 4)

/* GICv3; verify the SM8150 bases and PPI numbering against the DTB. */
#define GICD_BASE	0x17a00000UL
#define GICR_BASE	0x17a60000UL
#define GICR_SIZE	0x100000UL
#define GICR_PE_SIZE	0x20000UL
#define VTIMER_INTID	19	/* GIC_PPI 3 numbering, as on SM7150 */
#define PTIMER_INTID	18

/* SoC-level reset registers: IMEM is imem@146bf000 on SM8150, so
 * restart_reason@65c is at 0x146bf65c (sweet's IMEM is 0x146aa000); PS_HOLD
 * is the same 0x0c264000 (restart@c264000). */
#define IMEM_RESTART_REASON	0x146bf65cUL
#define PSHOLD			0x0c264000UL

/* Internal UFS host (ufshc@1d84000, 2 lanes, HS-G4); verify base/size
 * against the DTB. */
#define UFS_BASE		0x01d84000UL

/* Skip the read-only UFS register probe on guacamole: reading the host
 * registers while ABL has it gated stalls the bus and the watchdog resets
 * the phone (observed 2026-10-19). The xnu driver reports UFS state later. */
#define PROBE_UFS		0
