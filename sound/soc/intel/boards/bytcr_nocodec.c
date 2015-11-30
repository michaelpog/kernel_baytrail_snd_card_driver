/*
 *  bytcr_nocodec.c - ASoc Machine driver for MinnowBoard
 *  to make I2S signals observable on the Low-Speed connector. Audio codec
 *  is not managed by ASoC/DAPM
 *
 *  Copyright (C) 2015 Intel Corp
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include "../sound/sound/soc/intel/atom/sst-atom-controls.h"


static const struct snd_soc_dapm_route byt_nocodec_audio_map[] = {
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx"},
	{"codec_in1", NULL, "ssp2 Rx"},
};


static int byt_nocodec_init(struct snd_soc_pcm_runtime *runtime)
{
	return 0;
}

static const struct snd_soc_pcm_stream byt_nocodec_dai_params = {
	.formats = SNDRV_PCM_FMTBIT_S24_LE,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
};

static int byt_nocodec_codec_fixup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);

	/* The DSP will covert the FE rate to 48k, stereo, 24bits */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP2 to 24-bit */
	params_set_format(params, SNDRV_PCM_FORMAT_S24_LE);
	return 0;
}

static unsigned int rates_48000[] = {
	48000,
};

static struct snd_pcm_hw_constraint_list constraints_48000 = {
	.count = ARRAY_SIZE(rates_48000),
	.list  = rates_48000,
};

static int byt_nocodec_aif1_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE,
			&constraints_48000);
}

static struct snd_soc_ops byt_nocodec_aif1_ops = {
	.startup = byt_nocodec_aif1_startup,
};

static struct snd_soc_dai_link byt_nocodec_dais[] = {
	[MERR_DPCM_AUDIO] = {
		.name = "Audio Port",
		.stream_name = "Audio",
		.cpu_dai_name = "media-cpu-dai",
		.codec_dai_name = "snd-soc-nocodec-dai",
		.codec_name = "snd-soc-nocodec",
		.platform_name = "sst-mfld-platform",
		.ignore_suspend = 1,
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &byt_nocodec_aif1_ops,
	},
	[MERR_DPCM_DEEP_BUFFER] = {
		.name = "Deep-Buffer Audio Port",
		.stream_name = "Deep-Buffer Audio",
		.cpu_dai_name = "deepbuffer-cpu-dai",
		.codec_dai_name = "snd-soc-nocodec-dai",
		.codec_name = "snd-soc-nocodec",
		.platform_name = "sst-mfld-platform",
		.ignore_suspend = 1,
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.ops = &byt_nocodec_aif1_ops,
	},
	[MERR_DPCM_COMPR] = {
		.name = "Compressed Port",
		.stream_name = "Compress",
		.cpu_dai_name = "compress-cpu-dai",
		.codec_dai_name = "snd-soc-nocodec-dai",
		.codec_name = "snd-soc-nocodec",
		.platform_name = "sst-mfld-platform",
	},
	/* CODEC<->CODEC link */
	/* back ends */
	{
		.name = "LowSpeed Connector",
		.be_id = 1,
		.cpu_dai_name = "ssp2-port",
		.platform_name = "sst-mfld-platform",
		.no_pcm = 1,
		.codec_dai_name = "snd-soc-dummy",
		.codec_name = "snd-soc-dummy-dai",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS,
		.be_hw_params_fixup = byt_nocodec_codec_fixup,
		.ignore_suspend = 1,
		.nonatomic = true,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.init = byt_nocodec_init,
	},
};

/* SoC card */
static struct snd_soc_card byt_nocodec_card = {
	.name = "bytcr-nocodec",
	.owner = THIS_MODULE,
	.dai_link = byt_nocodec_dais,
	.num_links = ARRAY_SIZE(byt_nocodec_dais),
	.dapm_routes = byt_nocodec_audio_map,
	.num_dapm_routes = ARRAY_SIZE(byt_nocodec_audio_map),
	.fully_routed = true,
};

static int snd_byt_nocodec_mc_probe(struct platform_device *pdev)
{
	int ret_val = 0;

	/* register the soc card */
	byt_nocodec_card.dev = &pdev->dev;

	ret_val = devm_snd_soc_register_card(&pdev->dev, &byt_nocodec_card);

	if (ret_val) {
		dev_err(&pdev->dev, "devm_snd_soc_register_card failed %d\n",
			ret_val);
		return ret_val;
	}
	platform_set_drvdata(pdev, &byt_nocodec_card);
	return ret_val;
}

static struct platform_driver snd_byt_nocodec_mc_driver = {
	.driver = {
		.name = "bytcr_nocodec",
		.pm = &snd_soc_pm_ops,
	},
	.probe = snd_byt_nocodec_mc_probe,
};

module_platform_driver(snd_byt_nocodec_mc_driver);

MODULE_DESCRIPTION("ASoC Intel(R) Baytrail CR Nocodec Machine driver");
MODULE_AUTHOR("Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bytcr_nocodec");


