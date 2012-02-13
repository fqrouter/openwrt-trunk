/*
 *  Ralink RT3662/RT3883 SoC register definitions
 *
 *  Copyright (C) 2011-2012 Gabor Juhos <juhosg@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#ifndef _RT3883_REGS_H_
#define _RT3883_REGS_H_

#include <linux/bitops.h>

#define RT3883_SDRAM_BASE	0x00000000
#define RT3883_SYSC_BASE	0x10000000
#define RT3883_TIMER_BASE	0x10000100
#define RT3883_INTC_BASE	0x10000200
#define RT3883_MEMC_BASE	0x10000300
#define RT3883_UART0_BASE	0x10000500
#define RT3883_PIO_BASE		0x10000600
#define RT3883_FSCC_BASE	0x10000700
#define RT3883_NANDC_BASE	0x10000810
#define RT3883_I2C_BASE		0x10000900
#define RT3883_I2S_BASE		0x10000a00
#define RT3883_SPI_BASE		0x10000b00
#define RT3883_UART1_BASE	0x10000c00
#define RT3883_PCM_BASE		0x10002000
#define RT3883_GDMA_BASE	0x10002800
#define RT3883_CODEC1_BASE	0x10003000
#define RT3883_CODEC2_BASE	0x10003800
#define RT3883_FE_BASE		0x10100000
#define RT3883_ROM_BASE		0x10118000
#define RT3883_USBDEV_BASE	0x10112000
#define RT3883_PCI_BASE		0x10140000
#define RT3883_WLAN_BASE	0x10180000
#define RT3883_USBHOST_BASE	0x101c0000
#define RT3883_BOOT_BASE	0x1c000000
#define RT3883_SRAM_BASE	0x1e000000
#define RT3883_PCIMEM_BASE	0x20000000

#define RT3883_EHCI_BASE	(RT3883_USBHOST_BASE)
#define RT3883_OHCI_BASE	(RT3883_USBHOST_BASE + 0x1000)

#define RT3883_SYSC_SIZE	0x100
#define RT3883_TIMER_SIZE	0x100
#define RT3883_INTC_SIZE	0x100
#define RT3883_MEMC_SIZE	0x100
#define RT3883_UART0_SIZE	0x100
#define RT3883_UART1_SIZE	0x100
#define RT3883_PIO_SIZE		0x100
#define RT3883_FSCC_SIZE	0x100
#define RT3883_NANDC_SIZE	0x0f0
#define RT3883_I2C_SIZE		0x100
#define RT3883_I2S_SIZE		0x100
#define RT3883_SPI_SIZE		0x100
#define RT3883_PCM_SIZE		0x800
#define RT3883_GDMA_SIZE	0x800
#define RT3883_CODEC1_SIZE	0x800
#define RT3883_CODEC2_SIZE	0x800
#define RT3883_FE_SIZE		0x10000
#define RT3883_ROM_SIZE		0x4000
#define RT3883_USBDEV_SIZE	0x4000
#define RT3883_PCI_SIZE		0x40000
#define RT3883_WLAN_SIZE	0x40000
#define RT3883_USBHOST_SIZE	0x40000
#define RT3883_BOOT_SIZE	(32 * 1024 * 1024)
#define RT3883_SRAM_SIZE	(32 * 1024 * 1024)

/* SYSC registers */
#define RT3883_SYSC_REG_CHIPID0_3	0x00	/* Chip ID 0 */
#define RT3883_SYSC_REG_CHIPID4_7	0x04	/* Chip ID 1 */
#define RT3883_SYSC_REG_REVID		0x0c	/* Chip Revision Identification */
#define RT3883_SYSC_REG_SYSCFG0		0x10	/* System Configuration 0 */
#define RT3883_SYSC_REG_SYSCFG1		0x14	/* System Configuration 1 */
#define RT3883_SYSC_REG_CLKCFG0		0x2c	/* Clock Configuration 0 */
#define RT3883_SYSC_REG_CLKCFG1		0x30	/* Clock Configuration 1 */
#define RT3883_SYSC_REG_RSTCTRL		0x34	/* Reset Control*/
#define RT3883_SYSC_REG_RSTSTAT		0x38	/* Reset Status*/
#define RT3883_SYSC_REG_USB_PS		0x5c	/* USB Power saving control */
#define RT3883_SYSC_REG_GPIO_MODE	0x60	/* GPIO Purpose Select */
#define RT3883_SYSC_REG_PMU		0x88
#define RT3883_SYSC_REG_PMU1		0x8c

#define RT3883_REVID_VER_ID_MASK	0x0f
#define RT3883_REVID_VER_ID_SHIFT	8
#define RT3883_REVID_ECO_ID_MASK	0x0f

#define RT3883_SYSCFG0_DRAM_TYPE_DDR2	BIT(17)
#define RT3883_SYSCFG0_CPUCLK_SHIFT	8
#define RT3883_SYSCFG0_CPUCLK_MASK	0x3
#define RT3883_SYSCFG0_CPUCLK_250	0x0
#define RT3883_SYSCFG0_CPUCLK_384	0x1
#define RT3883_SYSCFG0_CPUCLK_480	0x2
#define RT3883_SYSCFG0_CPUCLK_500	0x3

#define RT3883_SYSCFG1_USB0_HOST_MODE	BIT(10)
#define RT3883_SYSCFG1_GPIO2_AS_WDT_OUT	BIT(2)

#define RT3883_CLKCFG1_UPHY1_CLK_EN	BIT(20)
#define RT3883_CLKCFG1_UPHY0_CLK_EN	BIT(18)

#define RT3883_GPIO_MODE_I2C		BIT(0)
#define RT3883_GPIO_MODE_SPI		BIT(1)
#define RT3883_GPIO_MODE_UART0_SHIFT	2
#define RT3883_GPIO_MODE_UART0_MASK	0x7
#define RT3883_GPIO_MODE_UART0(x)	((x) << RT3883_GPIO_MODE_UART0_SHIFT)
#define RT3883_GPIO_MODE_UARTF		0x0
#define RT3883_GPIO_MODE_PCM_UARTF	0x1
#define RT3883_GPIO_MODE_PCM_I2S	0x2
#define RT3883_GPIO_MODE_I2S_UARTF	0x3
#define RT3883_GPIO_MODE_PCM_GPIO	0x4
#define RT3883_GPIO_MODE_GPIO_UARTF	0x5
#define RT3883_GPIO_MODE_GPIO_I2S	0x6
#define RT3883_GPIO_MODE_GPIO		0x7
#define RT3883_GPIO_MODE_UART1		BIT(5)
#define RT3883_GPIO_MODE_JTAG		BIT(6)
#define RT3883_GPIO_MODE_MDIO		BIT(7)
#define RT3883_GPIO_MODE_GE1		BIT(9)
#define RT3883_GPIO_MODE_GE2		BIT(10)
#define RT3883_GPIO_MODE_PCI_SHIFT	11
#define RT3883_GPIO_MODE_PCI_MASK	0x7
#define RT3883_GPIO_MODE_PCI(_x)	((_x) << RT3883_GPIO_MODE_PCI_SHIFT)
#define RT3883_GPIO_MODE_PCI_DEV	0
#define RT3883_GPIO_MODE_PCI_HOST2	1
#define RT3883_GPIO_MODE_PCI_HOST1	2
#define RT3883_GPIO_MODE_PCI_FNC	3
#define RT3883_GPIO_MODE_PCI_GPIO	7
#define RT3883_GPIO_MODE_LNA_A_SHIFT	16
#define RT3883_GPIO_MODE_LNA_A_MASK	0x3
#define RT3883_GPIO_MODE_LNA_A(_x)	((_x) << RT3883_GPIO_MODE_LNA_A_SHIFT)
#define RT3883_GPIO_MODE_LNA_A_GPIO	0x3
#define RT3883_GPIO_MODE_LNA_G_SHIFT	18
#define RT3883_GPIO_MODE_LNA_G_MASK	0x3
#define RT3883_GPIO_MODE_LNA_G(_x)	((_x) << RT3883_GPIO_MODE_LNA_G_SHIFT)
#define RT3883_GPIO_MODE_LNA_G_GPIO	0x3

#define RT3883_RSTCTRL_PCIE_PCI_PDM	BIT(27)
#define RT3883_RSTCTRL_FLASH		BIT(26)
#define RT3883_RSTCTRL_UDEV		BIT(25)
#define RT3883_RSTCTRL_PCI		BIT(24)
#define RT3883_RSTCTRL_PCIE		BIT(23)
#define RT3883_RSTCTRL_UHST		BIT(22)
#define RT3883_RSTCTRL_FE		BIT(21)
#define RT3883_RSTCTRL_WLAN		BIT(20)
#define RT3883_RSTCTRL_UART1		BIT(29)
#define RT3883_RSTCTRL_SPI		BIT(18)
#define RT3883_RSTCTRL_I2S		BIT(17)
#define RT3883_RSTCTRL_I2C		BIT(16)
#define RT3883_RSTCTRL_NAND		BIT(15)
#define RT3883_RSTCTRL_DMA		BIT(14)
#define RT3883_RSTCTRL_PIO		BIT(13)
#define RT3883_RSTCTRL_UART		BIT(12)
#define RT3883_RSTCTRL_PCM		BIT(11)
#define RT3883_RSTCTRL_MC		BIT(10)
#define RT3883_RSTCTRL_INTC		BIT(9)
#define RT3883_RSTCTRL_TIMER		BIT(8)
#define RT3883_RSTCTRL_SYS		BIT(0)

#define RT3883_INTC_INT_SYSCTL	BIT(0)
#define RT3883_INTC_INT_TIMER0	BIT(1)
#define RT3883_INTC_INT_TIMER1	BIT(2)
#define RT3883_INTC_INT_IA	BIT(3)
#define RT3883_INTC_INT_PCM	BIT(4)
#define RT3883_INTC_INT_UART0	BIT(5)
#define RT3883_INTC_INT_PIO	BIT(6)
#define RT3883_INTC_INT_DMA	BIT(7)
#define RT3883_INTC_INT_NAND	BIT(8)
#define RT3883_INTC_INT_PERFC	BIT(9)
#define RT3883_INTC_INT_I2S	BIT(10)
#define RT3883_INTC_INT_UART1	BIT(12)
#define RT3883_INTC_INT_UHST	BIT(18)
#define RT3883_INTC_INT_UDEV	BIT(19)

/* FLASH/SRAM/Codec Controller registers */
#define RT3883_FSCC_REG_FLASH_CFG0	0x00
#define RT3883_FSCC_REG_FLASH_CFG1	0x04
#define RT3883_FSCC_REG_CODEC_CFG0	0x40
#define RT3883_FSCC_REG_CODEC_CFG1	0x44

#define RT3883_FLASH_CFG_WIDTH_SHIFT	26
#define RT3883_FLASH_CFG_WIDTH_MASK	0x3
#define RT3883_FLASH_CFG_WIDTH_8BIT	0x0
#define RT3883_FLASH_CFG_WIDTH_16BIT	0x1
#define RT3883_FLASH_CFG_WIDTH_32BIT	0x2


/* UART registers */
#define RT3883_UART_REG_RX	0
#define RT3883_UART_REG_TX	1
#define RT3883_UART_REG_IER	2
#define RT3883_UART_REG_IIR	3
#define RT3883_UART_REG_FCR	4
#define RT3883_UART_REG_LCR	5
#define RT3883_UART_REG_MCR	6
#define RT3883_UART_REG_LSR	7

#endif /* _RT3883_REGS_H_ */
