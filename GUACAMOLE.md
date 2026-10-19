# guacamole: the sweet port on the OnePlus 7 Pro (SM8150)

The same plan as sweet, one board over: **Apple's XNU boots from the stock
bootloader via `fastboot boot`, reuses ABL's continuous-splash framebuffer,
runs the unmodified iPhone X iOS 16.7.16 userland, and keeps its HFS+ root
inside the `userdata` partition.** Nothing about the userland, the ADT
identity (`D22AP` / `iPhone10,3`) or the root image changes.

Status: **port scaffold, hardware-audited; no kernel has been built or booted
on a OnePlus 7 Pro yet.** The parameterization (ADT, shim, kernel machine
config) is in place, a **probe boot image is built** (`shim/out/shim-boot.img`)
and every *(verify)* value below was checked on a real GM1910 with
`tools/fdt-audit.py` against the phone's own live FDT (see "Audited on the
phone") - 20/20 match after the memory-layout fix.

## Where the device lives in the tree

| Layer | Parameterized by | sweet | guacamole |
| --- | --- | --- | --- |
| ADT (memory, GIC, CPUs, fb, UFS) | `tools/devices.py`, `tools/mkadt.py --device` | `--device sweet` (default) | `--device guacamole` |
| Shim (fb, GIC, reset, UFS probe) | `shim/boards/*.h`, `BOARD=... shim/build.sh` | default | `BOARD=guacamole` |
| Kernel (board config, machine config) | `MACHINE_CONFIG=GUACAMOLE` (CI input `machine_config`) | `SWEET` | `GUACAMOLE` |
| Kernel per-device constants | `pexpert/pexpert/arm64/sweet_dev.h` | block 2 | block 1 |
| Framebuffer presentation | `SweetFramebuffer.cpp` (boot_args) + `/chosen display-scale` | 1080x2400 cropped to 1125x2436 | 1440x3120 letterboxed to 1125x2436 |

The iPhone X logical panel (1125x2436, 375x812 pt @3x) is presented on
**both** boards: a smaller physical panel crops it, a larger one letterboxes
it (black bars). `sweetfb_native=1` still switches to the raw panel size.

## Board facts (guacamole)

Sources: the phone's **own live FDT** (audited 2026-10-19, see below),
LineageOS `android_kernel_oneplus_sm8150` (branch lineage-22.1, same values
on 20/21/22.2/23.2), `arch/arm64/boot/dts/18821/`; mainline
`sm8150-oneplus-guacamole.dts` (pmOS tree, kernel 6.17). Project id 18821 =
OnePlus 7 Pro; **guacamoleb (OnePlus 7) is a different project (18857) with a
different touch IC (Synaptics)** - do not mix the DTS directories.

## Audited on the phone (2026-10-19)

A retail GM1910 (OOS 12, build `GM1910_11_H.40`, slot `_b`, **bootloader
unlocked**, Magisk root) was connected and its trees pulled read-only:

| Pulled | Where |
| --- | --- |
| live FDT (`/sys/firmware/fdt`, ABL-patched + overlays, 749 KB) | `dtb/guacamole-live.dtb` (gitignored) |
| stock `boot_b` (96 MB), extracted DTB container (12 MB) | `backup/boot_b.img`, `dtb/boot_unpacked/` (gitignored) |
| `dtbo_b` (24 MB; carries the panel + S3SY761 touch overlay) | `backup/dtbo_b.img` (gitignored) |

`tools/fdt-audit.py dtb/guacamole-live.dtb --device guacamole` reports
**20/20 match** against `tools/devices.py` + the driver constants. Findings
that changed the scaffold:

- **Memory is not what the DTS suggested.** `/memory` has three banks:
  `0x80000000+0x3bb00000`, `0xc0000000+0xc0000000` and
  `0x180000000+0x100000000`; the `0x100000000..0x180000000` window (2 GiB) is
  OnePlus's *mem-offline* region and `0xbbb00000..0xc0000000` (69 MiB) is a
  hole - neither is in `/memory`. The port now puts the kernel in the upper
  bank (`0x180000000`, 4 GiB) instead of sweet's stale `0x100000000`.
- **TLMM is `0x03000000`** (`pinctrl@03000000`, +0xdc2000), not mainline's
  `0x3100000`.
- **RPMh command DB is `0xc3f0000c`** (live node `qcom,cmd-db@c3f000c`), the
  same value sweet uses - RPMh votes are fully specified now.
- **Touch** lives under `soc/i2c@0xc80000/Samsung_18821@48` with
  `compatible = "sec-s6sy761"`, `reg = 0x48`, `irq-gpio` tlmm 122 - the node
  comes from the **dtbo overlay**, not the base DTB (the base has neither the
  panel nor the touch node).
- Everything else (splash 0x9C000000+36 MiB, GIC 0x17a00000/0x17a60000,
  timer, UFS 0x1d84000 + PHY 0x1d87000, SPMI 0xc440000, usb0 a600000 +
  hsphy 88e2000 + QMP 88e8000, GCC 0x100000, IMEM 0x146bf000, PS_HOLD
  0xc264000, CPU MPIDRs 0x0..0x700) matched the scaffold as built.

## Board facts table

| Area | Value | Confidence |
| --- | --- | --- |
| Panel | Samsung **S6E3HC2**, 1440x3120 DSC cmd mode, 4 lanes, TE tlmm 8; 60 Hz and 90 Hz timing sets; reset tlmm 6 (alt 78), AVDD "eldo" via tlmm 130; backlight PM8150L WLED (SID 5) | exact DTS |
| Splash | reserved-memory **`cont_splash_region` 0x9C000000 + 0x02400000 (36 MiB)**; mainline drives the same range as `simple-framebuffer`, 1440x3120, stride 5760, `a8r8g8b8`. SDE looks it up **by node name**; there is no "cont-splash-enabled" property on SM8150 - enabled state is read from the DSI controller at runtime | exact DTS + mainline |
| Touch | **Samsung S6SY761** (`sec-s6sy761`), I2C addr **0x48** on **qupv3 SE17** (`i2c@0xc80000`, GIC SPI 373); IRQ tlmm 122, reset tlmm 54, 1.8 V I/O switch tlmm 119; vdd = **pm8150_l17** (RPMh `ldoa17`, 3.008 V) | exact DTS |
| Keys | power = PM8150 PON (SID 0); **volume up = pm8150_gpios 6**, **volume down = pm8150_gpios 7** (both `GPIO_ACTIVE_LOW`); alert slider = tri-state on tlmm 27/134/125 | exact DTS |
| SPMI | arbiter `qcom,spmi@c440000` (regs 0xc440000/0xc600000/0xe600000/0xe700000/0xc40a000) - same as sweet | exact DTS |
| PMIC SIDs | PM8150 = 0, PM8150B = 2 (charger/FG/PD PHY), PM8150L = 4 (LCDB/WLED), PM8009 = 0xa | exact DTS |
| Display rails | vddio = pm8150_l14 (`ldoa14`); lab/ibb = PM8150L `lcdb@ec00` (`lcdb_ldo`/`lcdb_ncp`) | exact DTS |
| USB | **usb0** = `ssusb@a600000` / `dwc3@a600000`; HS PHY `hsphy@88e2000`; QMP `ssphy@88e8000` (vdd pm8150_l18); `eud@88e0000`; extcon = PM8150B PD PHY (SID 2). `usb1` disabled | exact DTS |
| Charger / battery | smb5 on SID 2 + `fg-gen4`, OPlus `bq27541` fuel gauge on se8 I2C, `stm8s` fast-charge MCU, `smb1390` charge pump | exact DTS |
| SoC-level | GIC `0x17a00000`/`0x17a60000` (GIC-600, PPI 9 maintenance), CNTFRQ 19.2 MHz, VTIMER PPI 3 = INTID 19, UFS `ufshc@1d84000` (2 lanes, HS-G4), RPMh apps RSC `0x18220000` + TCS `0x18220d00` + cmd-db `0xc3f0000c`, MPIDR `0x0..0x700`, PSCI `smc`, PS_HOLD `0xc264000`, IMEM `146bf000` | exact DTS + **live FDT** |

## What the scaffold already does

- `tools/mkadt.py --device guacamole --highmem` builds the SM8150 ADT
  (memory high bank, GIC, 8 Kryo 485 CPU nodes, UFS node, 1440x3120 fbcon).
  sweet output is byte-identical to before the refactor (verified).
  RAM size follows the SKU: the default `0xfcc00000` fits the 6/8/12 GB
  variants (all carve-outs are below 4 GiB); use
  `--dram-size 0x180000000` (8 GB) or `0x280000000` (12 GB) to hand the
  kernel the whole high bank.
- `BOARD=guacamole shim/build.sh` builds the shim with the 0x9C000000 /
  1440x3120 / stride 5760 framebuffer; the sweet shim binary is unchanged
  besides the board banner. Reset goes through SM8150's IMEM
  (`restart_reason` 0x146bf65c - sweet's IMEM is 0x146aa000) and PS_HOLD
  0xc264000.
- Kernel patch `0082-sweet-guacamole-board.patch` adds the `GUACAMOLE`
  machine config, `GUACAMOLE.h`, `sweet_dev.h` and the device-aware early
  framebuffer/HID/framebuffer-presentation changes; touch and USB compile as
  stubs (`SWEET_DEV_TOUCH 0`, `SWEET_DEV_USB 0`), keys poll the power key
  only (`SWEET_DEV_KEYS_VOLUME 0`).
- Full CI-order replay (staging + 85 patches + 0082) applies cleanly with
  `MACHINE_CONFIG=GUACAMOLE` (86 patches; the same 4 dxb version-specific
  patches are skipped as for sweet).

## Bring-up sequence

Prerequisites on the OnePlus side: unlocked bootloader, Linux PC, a GitHub
account (CI builds run on free public macOS runners), OrangeFox or another
recovery with `adb root` for log pulls.

0. **Back up** `persist`, `modemst1/2`, `fsg`, `fsc`, `boot`, `dtbo`,
   `vbmeta` (same as sweet; `backup/` is gitignored).
1. **Probe boot: run the shim *without* a payload first.** This is the
   "does it boot at all" step, and it audits every value from the live FDT on
   the phone itself. The image is already built here
   (`shim/out/shim-boot.img`, made with the pulled stock DTB); to rebuild:
   ```sh
   python3 tools/unpack_bootv2.py backup/boot.img dtb/boot_unpacked   # stock DTB
   BOARD=guacamole DTB=dtb/boot_unpacked/dtb shim/build.sh            # no payload
   fastboot boot shim/out/shim-boot.img                               # needs fastboot mode
   ```
   Expected on the panel: `BOARD guacamole`, then the report - GIC
   `0x17a00000`, TMR `0x17c20000`, SPMI `0xc440000`, UFS `0x1d84000` +
   `ufsphy_mem`, USB `a600000`/`88e2000`/`88e8000`, TLMM `0x3000000`, GCC
   `0x100000`, RSC `0x18220000`, IMEM/PSHOLD, **MEM three banks**, **RSVD
   `cont_splash_region 00009C000000+00002400000`**, `/chosen` initrd, the
   eight CPU MPIDRs and the pre-console framebuffer pixel stats. It ends with
   the read-only UFS probe and reboots after ~3 minutes (hold Power +
   Volume-Down for fastboot). If these lines appear at all, the ABL splash
   handover works and item 2 can follow.
   Without a phone the same report renders on a workstation:
   ```sh
   python3 tools/mkfdt-test.py /tmp/guac.dtb
   (cd shim && BOARD=guacamole DTB=/tmp/guac.dtb ./build.sh)
   ./shim/out/host_test /tmp/guac.dtb      # -> shim/out/probe.ppm
   python3 tools/fdt-audit.py dtb/guacamole-live.dtb --device guacamole
   ```
2. **Build the kernel**: GitHub Actions -> `xnu` workflow with
   `machine_config=GUACAMOLE`; artifact `kernel.development.guacamole`.
3. **Build the ADT + payload + boot image**:
   ```sh
   python3 tools/mkadt.py shim/out/guacamole.adt --device guacamole --highmem \
       [--dram-size 0x180000000]     # 8 GB SKU; matches the report's MEM line
   python3 tools/mkpayload.py xnu/out/kernel.development.guacamole shim/out/guacamole.adt \
       shim/out/payload-guac.bin --cmdline 'debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetfb_noconsole=1 sweetfb_hz=30'
   BOARD=guacamole DTB=dtb/boot_unpacked/dtb shim/build.sh ../shim/out/payload-guac.bin
   ```
   Start with `cpus=1`. The iOS root image is the same iPhone X build as on
   sweet (`tools/mkiosroot.sh`, or reuse an existing `ios16-root*.simg`).
4. **Boot it** (`fastboot boot shim/out/shim-boot.img`): with a payload the
   shim prints `LOADER xnu jumping` and the kernel console takes over the
   panel; without one it falls back to the report above.
5. **Logs**: the persistent console ring in `userdata` works once the UFS
   driver is up (`tools/sweet-log-pull.sh` from recovery). No serial port.
6. **Then**, in order: UFS root + HFS+ (verify UFS 3.0 link), power key via
   PON, volume keys (identify the PM8150 GPIO APID, see below), RPMh, the
   S6SY761 touch driver, USB, live log over USB.

## Next work items

1. **Volume keys**: the GPIO-based key path assumes the *first* arbiter APID
   with peripheral id 0x4c1 is the volume-up GPIO (that happened to hold on
   sweet). On guacamole both volume keys are PMIC GPIOs (PM8150 6 and 7), so
   the driver should scan the 0x4c1 APIDs and log their status/configuration,
   with a boot-arg to select the APID (the same diagnose-on-hardware pattern
   used for the GT9896 on sweet). `SWEET_DEV_KEYS_VOLUME` gates this today.
2. **Touch**: new driver for Samsung S6SY761 over the GENI I2C SE17. The
   protocol has an open reference (mainline `s6sy761` input driver); the
   bring-up needs the same three pieces as sweet: GENI SE bring-up (ABL leaves
   I2C firmware loaded), optional RPMh vote on `ldoa17` (3.008 V), and
   reporting through the existing `sweet_hid_touch`/SweetHIDTouch path.
   The device also needs `tlmm 119` (1.8 V I/O switch) and reset `tlmm 54`
   states checked against the PMIC/GPIO defaults firmware leaves behind.
3. **USB**: the DWC3 core (0xa600000), the HS PHY (0x88e2000), the QMP PHY
   (0x88e8000) and the QSCRATCH wrapper (0xa6f8800) are all confirmed; only
   the SM8150 GCC GDSC/CBCR offsets remain (Linux `gcc-sm8150.c`) before
   `SWEET_DEV_USB` can be flipped back on.
4. **Battery**: smb5 is on SID 2 with a different register map than sweet's
   PM6150 charger; the SweetBattery service will need a device branch.
5. **RPMh**: RSC/DRV-2 (0x18220000) and TCS (0x18220d00) are confirmed; the
   command-DB address (sweet: 0xc3f0000c) still needs the live-FDT check
   before touch power can be voted on. The touch rail is `ldoa17`.

## Risks

- **Splash framebuffer**: the reserved-memory region is audited on the phone
  (0x9C000000 + 36 MiB, matching the mainline port's simple-framebuffer), so
  the address is right; whether OnePlus's ABL keeps the panel lit across
  `fastboot boot` is the one thing only the probe boot can prove. If it does
  not, a real DSI/DSC panel driver becomes necessary (much larger job; the
  panel init sequence is in the dtbo's `dsi-panel-samsung_oneplus_dsc`).
- **Memory holes**: the live `/memory` already excludes the mem-offline
  window and the 69 MiB hole; the port's upper-bank choice sits entirely
  inside a listed bank. Check the report's MEM lines against
  `tools/devices.py` on any other unit/SKU.
- **SMP**: MPIDRs (0x0..0x700) and PSCI (`smc`) are confirmed, but CPU_ON
  behaviour of the three clusters on production firmware is not; start with
  `cpus=1` and add cores one at a time.
- **UFS 3.0**: the register block is shared, but the link is ABL's to set up;
  same "don't touch the PHY" approach as sweet.
- **Boot artifacts**: keep `research/ipsw-x/d22ap.adt` (iPhone X `/product`)
  from the sweet machine; without it mkadt omits `/product`. Device dumps
  (`dtb/`, `backup/`) stay out of git.
