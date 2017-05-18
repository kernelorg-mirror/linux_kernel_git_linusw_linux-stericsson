/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>,
 *         Roger Nilsson <roger.xr.nilsson@stericsson.com>
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <asm/page.h>

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/slab.h>
#include <linux/platform_data/dma-ste-dma40.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "ux500_msp_i2s.h"
#include "ux500_pcm.h"

#define UX500_PLATFORM_PERIODS_BYTES_MIN	128
#define UX500_PLATFORM_PERIODS_BYTES_MAX	(64 * PAGE_SIZE)
#define UX500_PLATFORM_PERIODS_MIN		2
#define UX500_PLATFORM_PERIODS_MAX		48
#define UX500_PLATFORM_BUFFER_BYTES_MAX		(2048 * PAGE_SIZE)

static const struct snd_pcm_hardware ux500_pcm_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_RESUME |
		SNDRV_PCM_INFO_PAUSE,
	.buffer_bytes_max = UX500_PLATFORM_BUFFER_BYTES_MAX,
	.period_bytes_min = UX500_PLATFORM_PERIODS_BYTES_MIN,
	.period_bytes_max = UX500_PLATFORM_PERIODS_BYTES_MAX,
	.periods_min = UX500_PLATFORM_PERIODS_MIN,
	.periods_max = UX500_PLATFORM_PERIODS_MAX,
};

static const struct snd_dmaengine_pcm_config ux500_dmaengine_of_pcm_config = {
	.pcm_hardware = &ux500_pcm_hw,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.prealloc_buffer_size = UX500_PLATFORM_BUFFER_BYTES_MAX,
};

int ux500_pcm_register_platform(struct platform_device *pdev)
{
	const struct snd_dmaengine_pcm_config *pcm_config;
	int ret;

	pcm_config = &ux500_dmaengine_of_pcm_config;

	ret = snd_dmaengine_pcm_register(&pdev->dev, pcm_config,
					 SND_DMAENGINE_PCM_FLAG_COMPAT);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"%s: ERROR: Failed to register platform '%s' (%d)!\n",
			__func__, pdev->name, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ux500_pcm_register_platform);

int ux500_pcm_unregister_platform(struct platform_device *pdev)
{
	snd_dmaengine_pcm_unregister(&pdev->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(ux500_pcm_unregister_platform);

MODULE_AUTHOR("Ola Lilja");
MODULE_AUTHOR("Roger Nilsson");
MODULE_DESCRIPTION("ASoC UX500 driver");
MODULE_LICENSE("GPL v2");
