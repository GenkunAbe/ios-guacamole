/*
 * Bring-up report from the board's live FDT.
 *
 * Built for the first boot of a new board: with no payload in the ramdisk
 * (or when the payload fails to start) the shim prints what ABL actually
 * handed it - memory banks, reserved-memory carve-outs, the GIC/timer/UFS/
 * USB/SPMI/RPMh/TLMM/GCC MMIO addresses, the CPU MPIDRs and /chosen - next
 * to the values the shim was built with (shim/boards/<board>.h). Run it
 * before trusting any *(verify)* number in tools/devices.py.
 *
 * Parsing is a straight walk of the structure block (see fdt.c); with the
 * MMU off all loads are Device memory, so everything stays 4-byte aligned.
 * The report is bounded by the console's row count: it degrades to
 * "truncated" instead of running off the panel.
 */
#include <stdint.h>
#include "console.h"
#include "probe.h"

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9

#define MAX_DEPTH	10
#define NAME_MAX	28
#define NAME_SHOW	22
#define RSVD_MAX	16
#define CPU_MAX		16
#define KEEP_ROWS	14	/* leave room for the UFS probe after the report */

/* Captured before the console clears ABL's splash. */
static uint32_t fb_px0;
static unsigned fb_lit;		/* non-black samples of 256 */
static unsigned fb_distinct;	/* distinct values in those samples */

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static int seq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static int starts(const char *s, const char *p)
{
	while (*p && *s == *p)
		s++, p++;
	return *p == 0;
}

/* Copy the node name, dropping the @unit-address. */
static void leaf_name(const char *name, char *out, unsigned max)
{
	unsigned i = 0;
	while (name[i] && name[i] != '@' && i + 1 < max) {
		out[i] = name[i];
		i++;
	}
	out[i] = 0;
}

static char *ph(char *p, uint64_t v, unsigned digits)
{
	static const char hex[] = "0123456789ABCDEF";
	for (unsigned i = 0; i < digits; i++)
		p[i] = hex[(v >> (4 * (digits - 1 - i))) & 0xf];
	return p + digits;
}

static char *ps(char *p, const char *s, unsigned max)
{
	unsigned i = 0;
	while (s[i] && i < max)
		*p++ = s[i++];
	if (s[i]) {
		*p++ = '~';
	}
	return p;
}

/* ---- framebuffer sanity, before the splash is overwritten ---- */

void
probe_fb_capture(void)
{
	unsigned total = (unsigned)(FB_H * (FB_STRIDE / 4));
	uint32_t seen[8];
	unsigned nseen = 0, lit = 0;

	fb_px0 = con_fb[0];
	for (unsigned i = 0; i < 256; i++) {
		uint32_t v = con_fb[(uint64_t)i * total / 256];
		if (v & 0x00ffffffu) {
			lit++;
		}
		unsigned j;
		for (j = 0; j < nseen; j++) {
			if (seen[j] == v) {
				break;
			}
		}
		if (j == nseen && nseen < 8) {
			seen[nseen++] = v;
		}
	}
	fb_lit = lit;
	fb_distinct = nseen;
}

/* ---- live-FDT report ---- */

/*
 * Nodes the report looks at, matched by leaf name. Kept as an enum + string
 * comparisons (no pointer tables: static pointers would create ABS64
 * relocations, and the shim must run wherever ABL puts it).
 */
enum {
	IT_NONE = 0,
	IT_GIC,
	IT_TMR,
	IT_SPMI,
	IT_UFS,
	IT_UFSPHY,
	IT_USB,
	IT_DWC3,
	IT_HSPHY,
	IT_QMPPHY,
	IT_EUD,
	IT_TLMM,
	IT_GCC,
	IT_RSC,
	IT_IMEM,
	IT_PSHOLD,
};

static unsigned
interest_kind(const char *leaf)
{
	const char *m = leaf;

	if (starts(m, "qcom,")) {
		m += 5;
	}
	if (seq(m, "interrupt-controller")) {
		return IT_GIC;
	}
	if (seq(m, "timer")) {
		return IT_TMR;
	}
	if (seq(m, "spmi")) {
		return IT_SPMI;
	}
	if (seq(m, "ufshc")) {
		return IT_UFS;
	}
	if (seq(m, "ufsphy")) {
		return IT_UFSPHY;
	}
	if (seq(m, "ssusb")) {
		return IT_USB;
	}
	if (seq(m, "dwc3")) {
		return IT_DWC3;
	}
	if (seq(m, "hsphy") || seq(m, "qusb2phy")) {
		return IT_HSPHY;
	}
	if (seq(m, "ssphy")) {
		return IT_QMPPHY;
	}
	if (seq(m, "eud")) {
		return IT_EUD;
	}
	if (seq(m, "pinctrl")) {
		return IT_TLMM;
	}
	if (seq(m, "clock-controller")) {
		return IT_GCC;
	}
	if (seq(m, "mailbox")) {
		return IT_RSC;
	}
	if (seq(m, "msm-imem")) {
		return IT_IMEM;
	}
	if (seq(m, "restart")) {
		return IT_PSHOLD;
	}
	return IT_NONE;
}

/* 2D char array (not a pointer table): no ABS64 relocations. */
static const char interest_labels[][8] = {
	"", "GIC", "TMR", "SPMI", "UFS", "UFSPHY", "USB", "DWC3", "HSPHY",
	"QMPPHY", "EUD", "TLMM", "GCC", "RSC", "IMEM", "PSHOLD",
};

static const char *
interest_label(unsigned it)
{
	if (it < sizeof(interest_labels) / sizeof(interest_labels[0])) {
		return interest_labels[it];
	}
	return 0;
}

/* Skip instances that only share a leaf name (PDC vs GIC, other mailboxes). */
static int
interest_printable(unsigned it, uint64_t base)
{
	switch (it) {
	case IT_GIC: return base >= 0x17000000 && base < 0x18000000;
	case IT_TLMM: return base >= 0x3000000 && base < 0x4000000;
	case IT_GCC: return base >= 0x100000 && base < 0x200000;
	case IT_RSC: return base >= 0x18200000 && base < 0x18300000;
	default: return 1;
	}
}

struct walk {
	uintptr_t strtab;
	unsigned depth, nodes;
	unsigned ac, sc;			/* root cell sizes */
	char stack[MAX_DEPTH][NAME_MAX];	/* leaf names by depth */
	unsigned kind[MAX_DEPTH];
	int reg_done[MAX_DEPTH];
	uint64_t initrd_lo, initrd_hi;
	int have_lo, have_hi;
	unsigned rsvd_shown, rsvd_total;
	unsigned cpu_count;
	uint32_t cpu_regs[CPU_MAX];
};

#define K_NONE		0
#define K_MEM		1
#define K_RSVD		2
#define K_RSVD_CHILD	3
#define K_CPUS		4
#define K_CPU		5
#define K_CHOSEN	6
#define K_INTEREST	0x100	/* + interest_kind() */

static int room(void)
{
	return (int)con_row() < (int)con_rows() - KEEP_ROWS;
}

static void
print_reg(const char *label, const char *name, uint64_t base, uint64_t size)
{
	char buf[64], *p = buf;
	int wide = con_cols() >= 54;	/* guacamole 58, sweet 43 */

	if (!room()) {
		return;
	}
	if (name && name[0]) {
		p = ps(p, name, wide ? NAME_SHOW : 10);
		*p++ = ' ';
	}
	p = ph(p, base, 12);
	if (size) {
		*p++ = '+';
		p = ph(p, size, wide ? 12 : 8);
	}
	*p = 0;
	con_str(label, buf);
}

/* First entry of a reg property, honouring the root cell sizes. */
static void
decode_reg(struct walk *w, uintptr_t val, unsigned len, uint64_t *base, uint64_t *size)
{
	*base = *size = 0;
	if (w->ac == 2 && len >= 16) {
		*base = ((uint64_t)be32(val) << 32) | be32(val + 4);
		*size = ((uint64_t)be32(val + 8) << 32) | be32(val + 12);
	} else if (len >= 8) {
		*base = be32(val);
		*size = be32(val + 4);
	} else if (len >= 4) {
		*base = be32(val);
	}
}

static void
on_prop(struct walk *w, const char *pname, uintptr_t val, unsigned len)
{
	unsigned d = w->depth;
	unsigned k = w->kind[d];

	if (d == 1 && seq(pname, "#address-cells") && len == 4) {
		w->ac = be32(val);
		return;
	}
	if (d == 1 && seq(pname, "#size-cells") && len == 4) {
		w->sc = be32(val);
		return;
	}
	if (len < 4) {
		return;
	}
	if (d == 1 && (seq(pname, "model") || seq(pname, "compatible"))) {
		if (room()) {
			char buf[64], *p = buf;
			p = ps(p, (const char *)val, con_cols() >= 54 ? 44 : 34);
			*p = 0;
			con_str(seq(pname, "model") ? "ROOT" : "COMPAT", buf);
		}
		return;
	}
	if (k == K_CHOSEN) {
		if (seq(pname, "linux,initrd-start")) {
			w->initrd_lo = len == 8 ? ((uint64_t)be32(val) << 32) | be32(val + 4) : be32(val);
			w->have_lo = 1;
		} else if (seq(pname, "linux,initrd-end")) {
			w->initrd_hi = len == 8 ? ((uint64_t)be32(val) << 32) | be32(val + 4) : be32(val);
			w->have_hi = 1;
		}
		return;
	}
	if (!seq(pname, "reg") || w->reg_done[d]) {
		return;
	}

	switch (k) {
	case K_MEM: {
		uintptr_t p = val;
		unsigned left = len, step = w->ac == 2 ? 16 : 8;
		while (left >= step && room()) {
			uint64_t base, size;
			decode_reg(w, p, left, &base, &size);
			if (!base && !size) {
				break;
			}
			print_reg("MEM", "", base, size);
			p += step;
			left -= step;
		}
		w->reg_done[d] = 1;
		return;
	}
	case K_RSVD_CHILD: {
		uint64_t base, size;
		decode_reg(w, val, len, &base, &size);
		w->rsvd_total++;
		if (w->rsvd_shown < RSVD_MAX) {
			w->rsvd_shown++;
			print_reg("RSVD", w->stack[d], base, size);
		}
		w->reg_done[d] = 1;
		return;
	}
	case K_CPU: {
		/* Downstream cpu reg = <0x0 MPIDR>; mainline = <MPIDR> */
		uint32_t reg = w->ac == 2 && len >= 8 ? be32(val + 4) : be32(val);
		if (w->cpu_count < CPU_MAX) {
			w->cpu_regs[w->cpu_count] = reg;
		}
		w->cpu_count++;
		w->reg_done[d] = 1;
		return;
	}
	case K_INTEREST ... K_INTEREST + 0xff: {
		unsigned it = k - K_INTEREST;
		uint64_t base, size;
		decode_reg(w, val, len, &base, &size);
		if (interest_printable(it, base)) {
			print_reg(interest_label(it), w->stack[d], base, size);
		}
		w->reg_done[d] = 1;
		return;
	}
	default:
		return;
	}
}

static void
on_node(struct walk *w, const char *name)
{
	unsigned d = w->depth;
	char leaf[NAME_MAX];

	leaf_name(name, leaf, sizeof(leaf));
	for (unsigned i = 0; i < sizeof(w->stack[d]); i++) {
		w->stack[d][i] = leaf[i];
		if (!leaf[i]) {
			break;
		}
	}
	w->kind[d] = K_NONE;
	w->reg_done[d] = 0;

	if (d == 2) {
		if (seq(leaf, "memory")) {
			w->kind[d] = K_MEM;
		} else if (seq(leaf, "reserved-memory")) {
			w->kind[d] = K_RSVD;
		} else if (seq(leaf, "cpus")) {
			w->kind[d] = K_CPUS;
		} else if (seq(leaf, "chosen")) {
			w->kind[d] = K_CHOSEN;
		}
		return;
	}
	if (d == 3 && seq(w->stack[2], "reserved-memory")) {
		w->kind[d] = K_RSVD_CHILD;
		return;
	}
	if (d == 3 && seq(w->stack[2], "cpus") && seq(leaf, "cpu")) {
		w->kind[d] = K_CPU;
		return;
	}
	unsigned it = interest_kind(leaf);
	if (it != IT_NONE) {
		w->kind[d] = K_INTEREST + it;
	}
}

void
probe_report(uintptr_t dtb)
{
	static struct walk w;	/* .bss: no memset in a freestanding image */
	uint64_t *z = (uint64_t *)&w;
	char buf[64], *p;

	for (unsigned i = 0; i < sizeof(w) / 8; i++) {
		z[i] = 0;
	}
	con_str("PROBE", "live FDT report");

	if (!dtb || be32(dtb) != FDT_MAGIC) {
		con_str("DTB", "no device tree");
		return;
	}
	p = buf;
	p = ps(p, "size=", 5);
	p = ph(p, be32(dtb + 4), 6);
	p = ps(p, " ver=", 6);
	p = ph(p, be32(dtb + 20), 2);
	*p = 0;
	con_str("DTB", buf);

	w.strtab = dtb + be32(dtb + 12);
	w.ac = 2;
	w.sc = 2;

	uintptr_t q = dtb + be32(dtb + 8);
	for (;;) {
		uint32_t tok = be32(q);
		q += 4;
		if (tok == FDT_BEGIN_NODE) {
			w.depth++;
			w.nodes++;
			if (w.depth < MAX_DEPTH) {
				on_node(&w, (const char *)q);
			}
			while (*(volatile char *)q) {
				q++;
			}
			q = align4(q + 1);
		} else if (tok == FDT_END_NODE) {
			if (w.depth) {
				w.depth--;
			}
		} else if (tok == FDT_PROP) {
			uint32_t len = be32(q), nameoff = be32(q + 4);
			uintptr_t val = q + 8;
			if (w.depth > 0 && w.depth < MAX_DEPTH) {
				on_prop(&w, (const char *)(w.strtab + nameoff), val, len);
			}
			q = align4(val + len);
		} else if (tok == FDT_NOP) {
			continue;
		} else {
			break;
		}
	}

	if (w.have_lo && w.have_hi) {
		p = buf;
		p = ph(p, w.initrd_lo, 10);
		p = ps(p, "+", 2);
		p = ph(p, w.initrd_hi - w.initrd_lo, 8);
		*p = 0;
		con_str("CHOSEN", buf);
	}
	if (w.cpu_count) {
		p = buf;
		p = ps(p, "n=", 3);
		p = ph(p, w.cpu_count, 2);
		for (unsigned i = 0; i < w.cpu_count && i < CPU_MAX; i++) {
			*p++ = i ? ',' : ' ';
			p = ph(p, w.cpu_regs[i], 3);
		}
		if (w.cpu_count > CPU_MAX) {
			p = ps(p, " ...", 8);
		}
		*p = 0;
		con_str("CPUS", buf);
	}
	if (w.rsvd_total > w.rsvd_shown) {
		p = buf;
		p = ps(p, "shown ", 8);
		p = ph(p, w.rsvd_shown, 2);
		p = ps(p, " of ", 6);
		p = ph(p, w.rsvd_total, 2);
		*p = 0;
		con_str("RSVD", buf);
	}
	con_hex2("CELLS", "ac=", w.ac, "sc=", w.sc);
	p = buf;
	p = ps(p, "px0=", 6);
	p = ph(p, fb_px0, 8);
	p = ps(p, " lit=", 7);
	p = ph(p, fb_lit, 2);
	p = ps(p, "/256 col=", 10);
	p = ph(p, fb_distinct, 2);
	*p = 0;
	con_str("FB", buf);

	if (!room()) {
		con_str("PROBE", "truncated (no rows left)");
	}
}
