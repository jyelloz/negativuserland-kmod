/*
 * ASoC driver for NUL BBB Reference System
 *
 * Author:      Jordan Yelloz, <jordan@yelloz.me>
 * Based on:    davinci-evm.c by Vladimir Barinov, <vbarinov@embeddedalley.com>
 * Copyright:   (C) 2016 Jordan Yelloz
 *              (C) 2007 MontaVista Software, Inc., <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <sound/core.h>
#include <sound/soc.h>

#define PREFIX "nulbbb,"

struct snd_soc_card_drvdata_nul_bbb {
	struct clk *mclk;
	unsigned sysclk;
};

static int startup(struct snd_pcm_substream *const substream)
{
	struct snd_soc_pcm_runtime *const rtd = substream->private_data;
	struct snd_soc_card *const soc_card = rtd->card;
	struct snd_soc_card_drvdata_nul_bbb *const drvdata =
		snd_soc_card_get_drvdata(soc_card);

	if (drvdata->mclk)
		return clk_prepare_enable(drvdata->mclk);

	return 0;
}

static void shutdown(struct snd_pcm_substream *const substream)
{
	struct snd_soc_pcm_runtime *const rtd = substream->private_data;
	struct snd_soc_card *const soc_card = rtd->card;
	struct snd_soc_card_drvdata_nul_bbb *const drvdata =
		snd_soc_card_get_drvdata(soc_card);

	if (drvdata->mclk)
		clk_disable_unprepare(drvdata->mclk);
}

static int hw_params(struct snd_pcm_substream *const substream,
			 struct snd_pcm_hw_params *const params)
{
	struct snd_soc_pcm_runtime *const rtd = substream->private_data;
	struct snd_soc_dai *const cpu_dai = rtd->cpu_dai;
	struct snd_soc_card *const soc_card = rtd->card;
	unsigned const sysclk = ((struct snd_soc_card_drvdata_nul_bbb *)
			   snd_soc_card_get_drvdata(soc_card))->sysclk;

	/* set the CPU system clock */
	/* SND_SOC_CLOCK_IN means get clock from Y4 (24.576 MHz) */
	/* SND_SOC_CLOCK_OUT means emit 24.000 MHz clock */
	return snd_soc_dai_set_sysclk(cpu_dai, 0, sysclk, SND_SOC_CLOCK_IN);

}

static const struct snd_soc_ops ops = {
	.startup	= startup,
	.shutdown	= shutdown,
	.hw_params	= hw_params,
};

static struct snd_soc_dai_link dai_nul_bbb_purple = {
	.name		= "NUL BBB",
	.stream_name	= "Playback",
	.ops		= &ops,
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS |
		   SND_SOC_DAIFMT_NB_NF,
};

static struct snd_soc_dai_link dai_nul_bbb_yellow = {
	.name		= "NUL BBB",
	.stream_name	= "Playback",
	.ops		= &ops,
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS |
		   SND_SOC_DAIFMT_NB_IF,
};

static const struct of_device_id nul_bbb_dt_ids[] = {
	{
		.compatible = PREFIX "audio",
		.data = (void *) &dai_nul_bbb_purple,
	},
	{
		.compatible = PREFIX "audio-purple",
		.data = (void *) &dai_nul_bbb_purple,
	},
	{
		.compatible = PREFIX "audio-yellow",
		.data = (void *) &dai_nul_bbb_yellow,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nul_bbb_dt_ids);

static struct snd_soc_card nul_bbb_soc_card = {
	.owner = THIS_MODULE,
	.num_links = 1,
};

static int nul_bbb_probe(struct platform_device *const pdev)
{
	struct device *const dev = &pdev->dev;
	struct device_node *const np = dev->of_node;

	const struct of_device_id *const match =
		of_match_device(of_match_ptr(nul_bbb_dt_ids), dev);

	struct snd_soc_dai_link *const dai =
		(struct snd_soc_dai_link *) match->data;

	struct device_node *const mcasp = of_parse_phandle(np,
		PREFIX "mcasp-controller", 0);

	const int num_codecs = of_property_count_elems_of_size(np,
		PREFIX "audio-codec", sizeof(phandle));

	struct snd_soc_dai_link_component *const codecs = devm_kzalloc(
		dev,
		num_codecs * sizeof(struct snd_soc_dai_link_component),
		GFP_KERNEL
	);

	const char **const codec_names = devm_kzalloc(dev,
		num_codecs * sizeof(char *),
		GFP_KERNEL);

	struct snd_soc_card_drvdata_nul_bbb *drvdata = NULL;
	struct clk *mclk;
	int ret;
	int i;

	if (!codecs || !codec_names)
		return -ENOMEM;

	if (!mcasp)
		return -EINVAL;

	dev_dbg(dev, "trying to parse phandles for %d codecs\n", num_codecs);

	ret = of_property_read_string_array(np, PREFIX "codec-name",
		codec_names, num_codecs);
	if (ret < 0) {
		dev_err(dev, "failed to parse property `nulbbb,codec-name'\n");
		return ret;
	}

	for (i = 0; i < num_codecs; i++) {

		struct snd_soc_dai_link_component *const codec = &codecs[i];
		struct device_node *const codec_of_node = of_parse_phandle(np,
			PREFIX "audio-codec", i);

		if (!codec_of_node) {
			dev_err(dev, "failed parsing codec #%d\n", i);
			return -EINVAL;
		}

		codec->of_node = codec_of_node;
		codec->dai_name = codec_names[i];

		dev_dbg(dev, "successfully parsed codec #%d (%s)\n", i,
			codec->dai_name);
	}

	dai->cpu_of_node = mcasp;
	dai->platform_of_node = mcasp;
	dai->codecs = codecs;
	dai->num_codecs = num_codecs;

	nul_bbb_soc_card.dev = dev;
	nul_bbb_soc_card.dai_link = dai;

	ret = snd_soc_of_parse_card_name(&nul_bbb_soc_card, PREFIX "model");
	if (ret)
		return ret;

	mclk = devm_clk_get(dev, "mclk");
	if (PTR_ERR(mclk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(mclk)) {
		dev_dbg(dev, "mclk not found.\n");
		mclk = NULL;
	}

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->mclk = mclk;

	ret = of_property_read_u32(np, PREFIX "codec-clock-rate", &drvdata->sysclk);

	snd_soc_card_set_drvdata(&nul_bbb_soc_card, drvdata);
	ret = devm_snd_soc_register_card(dev, &nul_bbb_soc_card);

	if (ret)
		dev_err(dev, "snd_soc_register_card failed (%d)\n", ret);

	return ret;
}

static struct platform_driver nul_bbb_driver = {
	.probe		= nul_bbb_probe,
	.driver		= {
		.name	= "nul_bbb",
		.pm	= &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(nul_bbb_dt_ids),
	},
};

static int __init nul_bbb_init(void)
{

	if (of_have_populated_dt())
		return platform_driver_register(&nul_bbb_driver);

	return -EINVAL;

}

static void __exit nul_bbb_exit(void)
{
	if (of_have_populated_dt()) {
		platform_driver_unregister(&nul_bbb_driver);
		return;
	}
}

module_init(nul_bbb_init);
module_exit(nul_bbb_exit);

MODULE_AUTHOR("Jordan Yelloz <jordan@yelloz.me>");
MODULE_DESCRIPTION("NUL BBB ASoC driver");
MODULE_LICENSE("GPL v2");
