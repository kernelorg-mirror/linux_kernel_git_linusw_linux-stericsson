/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl022.h>
#include <linux/platform_data/dma-ste-dma40.h>
#include <linux/platform_data/hsi-ux500.h>
#include <linux/mfd/dbx500-prcmu.h>

#include "setup.h"
#include "irqs.h"
#include "db8500-regs.h"
#include "devices-db8500.h"
#include "ste-dma40-db8500.h"

static struct resource dma40_resources[] = {
	[0] = {
		.start = U8500_DMA_BASE,
		.end   = U8500_DMA_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
		.name  = "base",
	},
	[1] = {
		.start = U8500_DMA_LCPA_BASE,
		.end   = U8500_DMA_LCPA_BASE + 2 * SZ_1K - 1,
		.flags = IORESOURCE_MEM,
		.name  = "lcpa",
	},
	[2] = {
		.start = IRQ_DB8500_DMA,
		.end   = IRQ_DB8500_DMA,
		.flags = IORESOURCE_IRQ,
	}
};

struct stedma40_platform_data dma40_plat_data = {
	.disabled_channels = {-1},
};

struct platform_device u8500_dma40_device = {
	.dev = {
		.platform_data = &dma40_plat_data,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.name = "dma40",
	.id = 0,
	.num_resources = ARRAY_SIZE(dma40_resources),
	.resource = dma40_resources
};

struct resource keypad_resources[] = {
	[0] = {
		.start = U8500_SKE_BASE,
		.end = U8500_SKE_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_DB8500_KB,
		.end = IRQ_DB8500_KB,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device u8500_ske_keypad_device = {
	.name = "nmk-ske-keypad",
	.id = -1,
	.num_resources = ARRAY_SIZE(keypad_resources),
	.resource = keypad_resources,
};

struct prcmu_pdata db8500_prcmu_pdata = {
	.ab_platdata	= &ab8500_platdata,
	.ab_irq		= IRQ_DB8500_AB8500,
	.irq_base	= IRQ_PRCMU_BASE,
	.version_offset	= DB8500_PRCMU_FW_VERSION_OFFSET,
	.legacy_offset	= DB8500_PRCMU_LEGACY_OFFSET,
};

static struct resource db8500_prcmu_res[] = {
	{
		.name  = "prcmu",
		.start = U8500_PRCMU_BASE,
		.end   = U8500_PRCMU_BASE + SZ_8K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.name  = "prcmu-tcdm",
		.start = U8500_PRCMU_TCDM_BASE,
		.end   = U8500_PRCMU_TCDM_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.name  = "irq",
		.start = IRQ_DB8500_PRCMU1,
		.end   = IRQ_DB8500_PRCMU1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name  = "prcmu-tcpm",
		.start = U8500_PRCMU_TCPM_BASE,
		.end   = U8500_PRCMU_TCPM_BASE + SZ_32K - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device db8500_prcmu_device = {
	.name			= "db8500-prcmu",
	.resource		= db8500_prcmu_res,
	.num_resources		= ARRAY_SIZE(db8500_prcmu_res),
	.dev = {
		.platform_data = &db8500_prcmu_pdata,
	},
};

/*
 * HSI
 */
#define HSIR_OVERRUN(num) {			    \
	.start  = IRQ_DB8500_HSIR_CH##num##_OVRRUN, \
	.end    = IRQ_DB8500_HSIR_CH##num##_OVRRUN, \
	.flags  = IORESOURCE_IRQ,		    \
	.name   = "hsi_rx_overrun_ch"#num	    \
}

#define STE_HSI_PORT0_TX_CHANNEL_CFG(n) { \
       .dir = STEDMA40_MEM_TO_PERIPH, \
       .high_priority = false, \
       .mode = STEDMA40_MODE_LOGICAL, \
       .mode_opt = STEDMA40_LCHAN_SRC_LOG_DST_LOG, \
       .src_dev_type = STEDMA40_DEV_SRC_MEMORY, \
       .dst_dev_type = n,\
       .src_info.big_endian = false,\
       .src_info.data_width = STEDMA40_WORD_WIDTH,\
       .dst_info.big_endian = false,\
       .dst_info.data_width = STEDMA40_WORD_WIDTH,\
},

#define STE_HSI_PORT0_RX_CHANNEL_CFG(n) { \
       .dir = STEDMA40_PERIPH_TO_MEM, \
       .high_priority = false, \
       .mode = STEDMA40_MODE_LOGICAL, \
       .mode_opt = STEDMA40_LCHAN_SRC_LOG_DST_LOG, \
       .src_dev_type = n,\
       .dst_dev_type = STEDMA40_DEV_DST_MEMORY, \
       .src_info.big_endian = false,\
       .src_info.data_width = STEDMA40_WORD_WIDTH,\
       .dst_info.big_endian = false,\
       .dst_info.data_width = STEDMA40_WORD_WIDTH,\
},

static struct resource u8500_hsi_resources[] = {
       {
	       .start  = U8500_HSIR_BASE,
	       .end    = U8500_HSIR_BASE + SZ_4K - 1,
	       .flags  = IORESOURCE_MEM,
	       .name   = "hsi_rx_base"
       },
       {
	       .start  = U8500_HSIT_BASE,
	       .end    = U8500_HSIT_BASE + SZ_4K - 1,
	       .flags  = IORESOURCE_MEM,
	       .name   = "hsi_tx_base"
       },
       {
	       .start  = IRQ_DB8500_HSIRD0,
	       .end    = IRQ_DB8500_HSIRD0,
	       .flags  = IORESOURCE_IRQ,
	       .name   = "hsi_rx_irq0"
       },
       {
	       .start  = IRQ_DB8500_HSITD0,
	       .end    = IRQ_DB8500_HSITD0,
	       .flags  = IORESOURCE_IRQ,
	       .name   = "hsi_tx_irq0"
       },
       {
	       .start  = IRQ_DB8500_HSIR_EXCEP,
	       .end    = IRQ_DB8500_HSIR_EXCEP,
	       .flags  = IORESOURCE_IRQ,
	       .name   = "hsi_rx_excep0"
       },
       HSIR_OVERRUN(0),
       HSIR_OVERRUN(1),
       HSIR_OVERRUN(2),
       HSIR_OVERRUN(3),
       HSIR_OVERRUN(4),
       HSIR_OVERRUN(5),
       HSIR_OVERRUN(6),
       HSIR_OVERRUN(7),
};

#ifdef CONFIG_STE_DMA40
static struct stedma40_chan_cfg ste_hsi_port0_dma_tx_cfg[] = {
       STE_HSI_PORT0_TX_CHANNEL_CFG(DB8500_DMA_DEV20_SLIM0_CH0_TX_HSI_TX_CH0)
       STE_HSI_PORT0_TX_CHANNEL_CFG(DB8500_DMA_DEV21_SLIM0_CH1_TX_HSI_TX_CH1)
       STE_HSI_PORT0_TX_CHANNEL_CFG(DB8500_DMA_DEV22_SLIM0_CH2_TX_HSI_TX_CH2)
       STE_HSI_PORT0_TX_CHANNEL_CFG(DB8500_DMA_DEV23_SLIM0_CH3_TX_HSI_TX_CH3)
};

static struct stedma40_chan_cfg ste_hsi_port0_dma_rx_cfg[] = {
       STE_HSI_PORT0_RX_CHANNEL_CFG(DB8500_DMA_DEV20_SLIM0_CH0_RX_HSI_RX_CH0)
       STE_HSI_PORT0_RX_CHANNEL_CFG(DB8500_DMA_DEV21_SLIM0_CH1_RX_HSI_RX_CH1)
       STE_HSI_PORT0_RX_CHANNEL_CFG(DB8500_DMA_DEV22_SLIM0_CH2_RX_HSI_RX_CH2)
       STE_HSI_PORT0_RX_CHANNEL_CFG(DB8500_DMA_DEV23_SLIM0_CH3_RX_HSI_RX_CH3)
};
#endif

static struct ste_hsi_port_cfg ste_hsi_port0_cfg = {
#ifdef CONFIG_STE_DMA40
       .dma_filter = stedma40_filter,
       .dma_tx_cfg = ste_hsi_port0_dma_tx_cfg,
       .dma_rx_cfg = ste_hsi_port0_dma_rx_cfg
#endif
};

struct ste_hsi_platform_data u8500_hsi_platform_data = {
       .num_ports = 1,
       .use_dma = 1,
       .port_cfg = &ste_hsi_port0_cfg,
};

struct platform_device u8500_hsi_device = {
       .dev = {
		.platform_data = &u8500_hsi_platform_data,
       },
       .name = "ste_hsi",
       .id = 0,
       .resource = u8500_hsi_resources,
       .num_resources = ARRAY_SIZE(u8500_hsi_resources)
};
