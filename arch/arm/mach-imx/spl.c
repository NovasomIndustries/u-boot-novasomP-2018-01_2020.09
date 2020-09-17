/*
 * Copyright (C) 2014 Gateworks Corporation
 * Copyright (C) 2011-2012 Freescale Semiconductor, Inc.
 *
 * Author: Tim Harvey <tharvey@gateworks.com>
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */

#include <common.h>
#include <asm/io.h>
#include <asm/arch/imx-regs.h>
#include <asm/arch/sys_proto.h>
#include <asm/spl.h>
#include <spl.h>
#include <asm/mach-imx/hab.h>
#include <asm/mach-imx/boot_mode.h>
#include <g_dnl.h>

DECLARE_GLOBAL_DATA_PTR;

#if defined(CONFIG_MX6)
/* determine boot device from SRC_SBMR1 (BOOT_CFG[4:1]) or SRC_GPR9 register */
u32 spl_boot_device(void)
{
	unsigned int bmode = readl(&src_base->sbmr2);
	u32 reg = imx6_src_get_boot_mode();

	debug("bmode 0x%x\n", (reg & 0x000000FF) >> 4);
	/*
	 * Check for BMODE if serial downloader is enabled
	 * BOOT_MODE - see IMX6DQRM Table 8-1
	 */
	if (((bmode >> 24) & 0x03) == 0x01) /* Serial Downloader */
		return BOOT_DEVICE_BOARD;

	/*
	 * The above method does not detect that the boot ROM used
	 * serial downloader in case the boot ROM decided to use the
	 * serial downloader as a fall back (primary boot source failed).
	 *
	 * Infer that the boot ROM used the USB serial downloader by
	 * checking whether the USB PHY is currently active... This
	 * assumes that SPL did not (yet) initialize the USB PHY...
	 */
	if (is_usbotg_phy_active())
		return BOOT_DEVICE_BOARD;

	/* BOOT_CFG1[7:4] - see IMX6DQRM Table 8-8 */
	switch ((reg & IMX6_BMODE_MASK) >> IMX6_BMODE_SHIFT) {
	 /* EIM: See 8.5.1, Table 8-9 */
	case IMX6_BMODE_EMI:
		/* BOOT_CFG1[3]: NOR/OneNAND Selection */
		switch ((reg & IMX6_BMODE_EMI_MASK) >> IMX6_BMODE_EMI_SHIFT) {
		case IMX6_BMODE_ONENAND:
#ifdef CONFIG_SPL_ONENAND_SUPPORT
			return BOOT_DEVICE_ONENAND;
#endif
			break;
		case IMX6_BMODE_NOR:
#ifdef CONFIG_SPL_NOR_SUPPORT
			return BOOT_DEVICE_NOR;
#endif
			break;
		}
		break;
	/* Reserved: Used to force Serial Downloader */
	case IMX6_BMODE_RESERVED:
		return BOOT_DEVICE_BOARD;
	/* SATA: See 8.5.4, Table 8-20 */
#if !defined(CONFIG_MX6UL) && !defined(CONFIG_MX6ULL)
	case IMX6_BMODE_SATA:
#ifdef CONFIG_SPL_SATA_SUPPORT
		return BOOT_DEVICE_SATA;
#endif
		break;
#endif
	/* Serial ROM: See 8.5.5.1, Table 8-22 */
	case IMX6_BMODE_SERIAL_ROM:
		/* BOOT_CFG4[2:0] */
		switch ((reg & IMX6_BMODE_SERIAL_ROM_MASK) >>
			IMX6_BMODE_SERIAL_ROM_SHIFT) {
		case IMX6_BMODE_ECSPI1:
		case IMX6_BMODE_ECSPI2:
		case IMX6_BMODE_ECSPI3:
		case IMX6_BMODE_ECSPI4:
		case IMX6_BMODE_ECSPI5:
#ifdef CONFIG_SPL_SPI_FLASH_SUPPORT
			return BOOT_DEVICE_SPI;
#endif
		case IMX6_BMODE_I2C1:
		case IMX6_BMODE_I2C2:
		case IMX6_BMODE_I2C3:
#ifdef CONFIG_SPL_I2C_SUPPORT
			return BOOT_DEVICE_I2C;
#endif
		}
		break;
	/* SD/eSD: 8.5.3, Table 8-15  */
	case IMX6_BMODE_SD:
	case IMX6_BMODE_ESD:
#ifdef CONFIG_SPL_MMC_SUPPORT
		return BOOT_DEVICE_MMC1;
#endif
		break;
	/* MMC/eMMC: 8.5.3 */
	case IMX6_BMODE_MMC:
	case IMX6_BMODE_EMMC:
#ifdef CONFIG_SPL_MMC_SUPPORT
		return BOOT_DEVICE_MMC1;
#endif
		break;
	/* NAND Flash: 8.5.2, Table 8-10 */
	case IMX6_BMODE_NAND_MIN ... IMX6_BMODE_NAND_MAX:
#ifdef CONFIG_SPL_NAND_SUPPORT
		return BOOT_DEVICE_NAND;
#endif
		break;
	}
#if defined(CONFIG_SPL_DFU_SUPPORT)
	return BOOT_DEVICE_DFU;
#endif
	return BOOT_DEVICE_NONE;
}

#elif defined(CONFIG_MX7)
/* Translate iMX7 boot device to the SPL boot device enumeration */
u32 spl_boot_device(void)
{
	enum boot_device boot_device_spl = get_boot_device();

	switch (boot_device_spl) {
	case SD1_BOOT:
	case MMC1_BOOT:
		return BOOT_DEVICE_MMC1;
	case SD2_BOOT:
	case MMC2_BOOT:
		return BOOT_DEVICE_MMC2;
	case SPI_NOR_BOOT:
		return BOOT_DEVICE_SPI;
	default:
		return BOOT_DEVICE_NONE;
	}
}
#endif /* CONFIG_MX6 || CONFIG_MX7 */

#ifdef CONFIG_SPL_USB_GADGET_SUPPORT
int g_dnl_bind_fixup(struct usb_device_descriptor *dev, const char *name)
{
	put_unaligned(CONFIG_USB_GADGET_PRODUCT_NUM + 0xfff, &dev->idProduct);

	return 0;
}
#endif

#if defined(CONFIG_SPL_MMC_SUPPORT)
/* called from spl_mmc to see type of boot mode for storage (RAW or FAT) */
u32 spl_boot_mode(const u32 boot_device)
{
	switch (spl_boot_device()) {
	/* for MMC return either RAW or FAT mode */
	case BOOT_DEVICE_MMC1:
	case BOOT_DEVICE_MMC2:
#if defined(CONFIG_SPL_FAT_SUPPORT)
		return MMCSD_MODE_FS;
#elif defined(CONFIG_SUPPORT_EMMC_BOOT)
		return MMCSD_MODE_EMMCBOOT;
#else
		return MMCSD_MODE_RAW;
#endif
		break;
	default:
		puts("spl: ERROR:  unsupported device\n");
		hang();
	}
}
#endif

#if defined(CONFIG_SECURE_BOOT)

__weak void __noreturn jump_to_image_no_args(struct spl_image_info *spl_image)
{
	typedef void __noreturn (*image_entry_noargs_t)(void);

	image_entry_noargs_t image_entry =
		(image_entry_noargs_t)(unsigned long)spl_image->entry_point;

	debug("image entry point: 0x%lX\n", spl_image->entry_point);

	/* HAB looks for the CSF at the end of the authenticated data therefore,
	 * we need to subtract the size of the CSF from the actual filesize */
	if (authenticate_image(spl_image->load_addr,
			       spl_image->size - CONFIG_CSF_SIZE)) {
		image_entry();
	} else {
		puts("spl: ERROR:  image authentication unsuccessful\n");
		hang();
	}
}

#endif

#if defined(CONFIG_MX6) && defined(CONFIG_SPL_OS_BOOT)
int dram_init_banksize(void)
{
	gd->bd->bi_dram[0].start = CONFIG_SYS_SDRAM_BASE;
	gd->bd->bi_dram[0].size = imx_ddr_size();

	return 0;
}
#endif
