/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja (ola.o.lilja@stericsson.com)
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <asm/mach-types.h>

#include <linux/module.h>
#include <linux/io.h>
#include <linux/spi/spi.h>
#include <linux/of.h>

#include <sound/soc.h>
#include <sound/initval.h>

#include "ux500_pcm.h"
#include "ux500_msp_dai.h"
#include "mop500_ab8500.h"

/**
 * struct ux500_sound_data - state container for the sound card
 * @card: the card we're providing data for
 * @dai_link: the digital audio interface link
 */
struct ux500_sound_data {
	struct snd_soc_card *card;
	/* This array expands downward to fit the links */
	struct snd_soc_dai_link dai_link[];
};

static int mop500_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *np, *codec, *cpu;
	struct snd_soc_card *card;
	struct snd_soc_dai_link *link;
	struct ux500_sound_data *data;
	int ret, num_links;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;
	card->dev = dev;

	ret = snd_soc_of_parse_card_name(card, "stericsson,card-name");
	if (ret)
		return ret;

	/* Uses static routes for now */
#if 0
	ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
	if (ret)
		return ret;
#endif

	/* Populate DAI links */
	num_links = of_get_child_count(node);

	/* Allocate the private data and the DAI link array */
	data = devm_kzalloc(dev, sizeof(*data) + sizeof(*link) * num_links,
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->card = card;

	card->dai_link  = &data->dai_link[0];
	card->num_links = num_links;
	link = data->dai_link;

	for_each_child_of_node(node, np) {
		cpu = of_get_child_by_name(np, "cpu");
		codec = of_get_child_by_name(np, "codec");

		if (!cpu || !codec) {
			dev_err(dev, "Can't find cpu/codec DT node\n");
			return -EINVAL;
		}

		link->cpu_of_node = of_parse_phandle(cpu, "sound-dai", 0);
		if (!link->cpu_of_node) {
			dev_err(card->dev, "error getting cpu phandle\n");
			return -EINVAL;
		}

		ret = snd_soc_of_get_dai_name(cpu, &link->cpu_dai_name);
		if (ret) {
			dev_err(card->dev, "error getting cpu dai name\n");
			return ret;
		}

		ret = snd_soc_of_get_dai_link_codecs(dev, codec, link);
		if (ret < 0) {
			dev_err(card->dev, "error getting codec dai link\n");
			return ret;
		}

		link->platform_of_node = link->cpu_of_node;
		ret = of_property_read_string(np, "link-name", &link->name);
		if (ret) {
			dev_err(card->dev, "error getting codec DAI link name\n");
			return ret;
		}

		link->stream_name = link->name;
		// link->init = ux500_sound_dai_init;
		// link->ops = &ux500_sound_ops;
		dev_info(dev, "parsed link \"%s\"\n", link->name);
		link++;
	}

	platform_set_drvdata(pdev, data);
	snd_soc_card_set_drvdata(card, data);

	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id snd_soc_mop500_match[] = {
	{ .compatible = "stericsson,snd-soc-mop500", },
	{},
};
MODULE_DEVICE_TABLE(of, snd_soc_mop500_match);

static struct platform_driver snd_soc_mop500_driver = {
	.driver = {
		.name = "snd-soc-mop500",
		.of_match_table = snd_soc_mop500_match,
	},
	.probe = mop500_probe,
};
module_platform_driver(snd_soc_mop500_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC MOP500 board driver");
MODULE_AUTHOR("Ola Lilja");
