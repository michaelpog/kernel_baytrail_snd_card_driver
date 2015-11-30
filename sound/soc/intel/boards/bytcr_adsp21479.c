/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */



/*
 *  byt_cr_dpcm_rt5651.c - ASoc Machine driver for Intel Byt CR platform
 *
 *  Copyright (C) 2014 Intel Corp
 *  Author: Subhransu S. Prusty <subhransu.s.prusty@intel.com>
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
#include <linux/spi/spi.h>
#include <linux/wait.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <linux/spi/spi.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/err.h>
#include "bytcr_adsp21479.h"


#include "../atom/sst-atom-controls.h"
//#include "../../sound/sound/soc/intel/atom/sst-atom-controls.h"

#define LOW_SPEED_SPIDEV_SPI_BUS 0
#define LOW_SPEED_SPIDEV_SPI_CS 0
#define LOW_SPEED_SPIDEV_MAX_CLK_HZ 100000


static struct spi_board_info cal_spi_board_info  = {  
        .modalias       = SOUND_CARD_NAME,
        .bus_num        = LOW_SPEED_SPIDEV_SPI_BUS,
        .chip_select    = LOW_SPEED_SPIDEV_SPI_CS,
        .max_speed_hz   = LOW_SPEED_SPIDEV_MAX_CLK_HZ,
};

static  struct snd_adsp21479_chip chip;



static struct spi_device *spi = NULL;


static unsigned int calculate_crc32(const u32 *data,unsigned int len)
{
    unsigned int i;
    unsigned int sum;
    
    sum=0;
    for(i=0;i<len;i++)
    {
        sum+=data[i];
    
    }
    
    return (~sum+1);
}

static void populate_word(u8 * word_buffer,u8 MSB, u8 SMSB, u8 SLSB, u8 LSB)
{
    
    word_buffer[0] = LSB;
    word_buffer[1] = SLSB;
    word_buffer[2] = SMSB;
    word_buffer[3] = MSB;
    pr_info("poplulating word with %x %x %x %x which turns into %x\n",MSB,SMSB,SLSB,LSB,(*(unsigned int *) word_buffer));

}

int float_to_int(unsigned int float_number)
{
    if(float_number==0)
    {
        return 0;
    }
    unsigned int mask = 0x80000000;
    int is_negative = (float_number & mask)?1:0;
    mask  =0x7fffffff;
    unsigned int exponent = float_number & mask;
    exponent >>=23;
    exponent-=127;
    unsigned int mantisa = 1<<exponent;
    mask  = 0x007fffff;
    unsigned int fraction = float_number & mask;
    fraction<<=8;

    unsigned int mirror_fraction=0;
    mask = 0x80000000;
    int i=0;
    int result =0 ;
    for(i=0;i<32;i++)
    {       
        mirror_fraction |= ((fraction & mask)?1:0)<<i;
        mask>>=1;
    }
    mask = 1;
    for(i=0;i<24;i++)
    {
        unsigned int fraction_factor = mask & mirror_fraction;
        if(fraction_factor!=0)
        {
           result+= mantisa/fraction_factor; 
        }
        mask<<=1;
       
    }
    result+=mantisa;
    result = (-1)*is_negative * result;
    return result;
		 
}

static void swap_bits_and_bytes(u8 *data, int length)
{
    int i;
    pr_info("used to be ");
    for(i=0;i<length;i++)
    {
       pr_info("%hu ",data[i]); 
    }
    pr_info("\n"); 
    
  /*  for(i=0;i<length/2;i++)
    {
       u8 temp = data[length-i-1];
       data[length-i-1] = bit_reverse_table_256[data[i]];
       data[i]=bit_reverse_table_256[temp];
    }*/
    for(i=0;i<length;i++)
    {       
       
       data[i]=bit_reverse_table_256[data[i]];
    }
    
    pr_info("now is ");
    for(i=0;i<length;i++)
    {
       pr_info("%hu ",data[i]); 
    }
    pr_info("\n"); 
}

static void setup_transfer_message_single_message(struct spi_transfer *message_transfer,
        u8 * sent_buffer,
        u8 * receive_buffer)
{
    memset(message_transfer,0,sizeof(struct spi_transfer));
    message_transfer->len = WORD_LENGTH_BYTES;
    message_transfer->cs_change = 0;
    message_transfer->bits_per_word = BITS_PER_BYTE;
    message_transfer->speed_hz = SPEED_HZ;
    message_transfer->tx_buf = sent_buffer;
    message_transfer->rx_buf = receive_buffer;
}

static void setup_transfer_message_for_send_data(struct snd_adsp21479_chip *chip,struct spi_transfer message_transfer [])
{
    pr_info("setup_transfer_message_for_send_data for 4 words\n");
    int i=0;
    for(i=0;i<SPI_MESSAGE_MAX_NUMBER_OF_WORDS;i++)
    {
        setup_transfer_message_single_message(&message_transfer[i],
                &(chip->spi_send_buffer[i*WORD_LENGTH_BYTES]),
                &(chip->spi_receive_buffer[i*WORD_LENGTH_BYTES]));      
    }
}



static void analyze_read_data_and_update_status(SPI_RECEIVE_STATE *state,u32 * data,int is_write,
        long * read_value)
{
    int expected_length;
    int actual_length;
    int status;
    switch(*state)
    {
        case WAITING_FOR_SYNC:

            if(*data==SPI_SYNC_WORD)
            {
               pr_info("sync word read, moving to state waiting for length and status\n");
               *state = WAITING_FOR_LENGTH_AND_STATUS; 
            }
            break;
        case WAITING_FOR_LENGTH_AND_STATUS:
            //This is not a bug, when we write data we expect to read just acknowledgment,
            //But when we read data we expect the acknowledgment and value
            expected_length = is_write?SPI_READ_MESSAGE_LENGTH: SPI_WRITE_MESSAGE_LENGTH;
            actual_length = (*data & 0xffff0000u)>>16;
            status = *data & 0xffffu;
            if(actual_length == expected_length && status==0)
            {  
                if(status!=0)
                {
                    pr_info("status is not 0, something webt wrong\n");
                }
                *state = is_write?WAITING_FOR_CRC:WAITING_FOR_RESULT_VALUE;
                pr_info("length and status is correct moving to state %s\n",(is_write?"waiting for crc":"waiting for result"));
            }
            
            break;
        case WAITING_FOR_RESULT_VALUE:
             *read_value = float_to_int(*data);
             *state = WAITING_FOR_CRC;
             pr_info("read value %ld moving to state waiting for crc\n",*read_value);
             break;
        case WAITING_FOR_CRC:
          // if(*data ==0)
          //{
               pr_info("crc is read and it's %ld, we're done\n",*read_value);
               *state = DONE;
          // }
           break;
        
    }
}

static int snd_adsp21479_spi_receive_message(struct snd_adsp21479_chip *chip,
        int is_write,
        long * read_value,int words_already_read)
{
    int i;
    int err;
    SPI_RECEIVE_STATE state = WAITING_FOR_SYNC;
    for(i=0;i<words_already_read;i++)
    {
       
        swap_bits_and_bytes(&(chip->spi_receive_buffer[i*WORD_LENGTH_BYTES]),WORD_LENGTH_BYTES);
        u32 * data = (u32 *)&(chip->spi_receive_buffer[i*WORD_LENGTH_BYTES]); 
        pr_info("read word %x \n",*data);
        analyze_read_data_and_update_status(&state,data,is_write,read_value);
        if(state == DONE)
        {     
            
            return 0;
        }
            
    }
    
    
   
    for(i=0;i<10;i++)
    {
        struct spi_message message;
        struct spi_transfer message_transfer;  
        spi_message_init(&message);
        setup_transfer_message_single_message(&message_transfer,chip->spi_send_buffer,chip->spi_receive_buffer);
        memset(chip->spi_send_buffer,0,WORD_LENGTH_BYTES);
        memset(chip->spi_receive_buffer,0,WORD_LENGTH_BYTES);
        spi_message_add_tail(&message_transfer,&message);
        err= spi_sync(spi,&message);
        if(err<0)
        {
            pr_err("spi_sync failed");
            return err;
        }
       
        swap_bits_and_bytes(chip->spi_receive_buffer,WORD_LENGTH_BYTES);
        u32 * data =(u32 *) chip->spi_receive_buffer; 
        pr_info("read word %x \n",*data);
        analyze_read_data_and_update_status(&state,data,is_write,read_value);
        
        if(state == DONE)
        {
            return 0;
        }
    
    }
      
    return -1;
}



static int snd_adsp21479_spi_send_message(struct snd_adsp21479_chip *chip,
        int is_write,
        int command_id,
        long * value )
{
    struct spi_message message;
    struct spi_transfer message_transfer[SPI_MESSAGE_MAX_NUMBER_OF_WORDS];
    spi_message_init(&message);
    int current_transfer = 0;
    int err =-ENODEV;
    int i=0;
    
    setup_transfer_message_for_send_data(chip,message_transfer);
    
    populate_word(&chip->spi_send_buffer[current_transfer*WORD_LENGTH_BYTES],
                (u8)((SPI_SYNC_WORD & 0xFF000000u) >>24),
                (u8)((SPI_SYNC_WORD & 0xFF0000u) >>16),
                (u8)((SPI_SYNC_WORD & 0xFF00u) >>8),
                (u8)(SPI_SYNC_WORD & 0xFFu));
   
    current_transfer++;
    
    populate_word(&chip->spi_send_buffer[current_transfer*WORD_LENGTH_BYTES],
                (u8)(!is_write?1<<7:0),
                (u8)(is_write?SPI_WRITE_MESSAGE_LENGTH:SPI_READ_MESSAGE_LENGTH),
                (u8)((command_id &0xFF00)>>8),
                (u8)((command_id &0xFF)));
      
    current_transfer++;
    
    if(is_write)
    {
        u32 volume_level_db = integer_volume_to_float_db[*value+100];
        pr_info("we are writing so we are adding the value %ld which is 0x%x as a float\n",*value,volume_level_db);
              
        populate_word(&chip->spi_send_buffer[current_transfer*WORD_LENGTH_BYTES],
                (u8) ((volume_level_db & 0xFF000000u) >>24),
                (u8) ((volume_level_db & 0xFF0000u) >>16),
                (u8) ((volume_level_db & 0xFF00u)>>8),
                (u8) (volume_level_db & 0xFFu));

        current_transfer++;
    
    }
    
    u32* data_for_crc = (u32*)(&chip->spi_send_buffer[WORD_LENGTH_BYTES]);
    u32 data_length_for_crc = is_write?SPI_WRITE_MESSAGE_LENGTH:SPI_READ_MESSAGE_LENGTH;
    data_length_for_crc--;
    
    u32 crc = calculate_crc32(data_for_crc,data_length_for_crc);
    pr_info("crc is %x\n",crc);
    
    populate_word(&chip->spi_send_buffer[current_transfer*WORD_LENGTH_BYTES],
                (u8) ((crc & 0xFF000000u) >>24),
                (u8) ((crc & 0xFF0000u) >>16),
                (u8) ((crc & 0xFF00u)>>8),
                (u8) (crc & 0xFFu));
    
    int number_of_words = is_write ? SPI_WRITE_NUMBER_OF_WORDS : SPI_READ_NUMBER_OF_WORDS;
    pr_info("number of words %d\n",number_of_words);
    for(i=0;i<number_of_words;i++)
    {
        pr_info("swaping bits and bytes and adding transfer to message to word %d\n",i);
        swap_bits_and_bytes(&(chip->spi_send_buffer[i*WORD_LENGTH_BYTES]),WORD_LENGTH_BYTES);
        pr_info("adding trnsfer to message");
        spi_message_add_tail(&message_transfer[i],&message);
    }
    pr_info("calling spi_sync\n");
    
    err= spi_sync(spi,&message);
    if(err<0)
    {
        pr_err("spi_sync failed\n");
        return err;
    }
    else
    {
        pr_info("message sent, reading response\n");
    }
    
    return snd_adsp21479_spi_receive_message(chip,is_write,value,number_of_words);

   
}


static int snd_adsp21479_set_volume(VOLUME_LEVEL which_volume,struct snd_adsp21479_chip *chip,long *new_volume)
{
    switch(which_volume)
    {
        case BOTH:
            pr_info("calling snd_adsp21479_spi_send_message to set both volumes to  %ld\n",*new_volume);
            return snd_adsp21479_spi_send_message(chip,1,VOLUME_CMD_GROUP,new_volume);  
            break;
        case VOLUME1:           
            return snd_adsp21479_spi_send_message(chip,1,VOLUME1_SET_LEVEL,new_volume);   
            break;
        case VOLUME2:
            return snd_adsp21479_spi_send_message(chip,1,VOLUME2_SET_LEVEL,new_volume);  
            break;
                                    
    }
   
    return -ENODEV;
}



static int snd_adsp21479_get_volume(VOLUME_LEVEL which_volume,struct snd_adsp21479_chip *chip,long* volume)
{
    switch(which_volume)
    {
        case BOTH:
            return snd_adsp21479_spi_send_message(chip,0,VOLUME_CMD_GROUP,volume);   
            break;
        case VOLUME1:
            return snd_adsp21479_spi_send_message(chip,0,VOLUME1_SET_LEVEL,volume);    
            break;
        case VOLUME2:
            return snd_adsp21479_spi_send_message(chip,0,VOLUME2_SET_LEVEL,volume); 
            break;
                                    
    }
   
    return -ENODEV;
    
}

static struct snd_adsp21479_chip * get_chip_from_kcontrol(struct snd_kcontrol *kcontrol)
{
   
    return &chip;
}

static int snd_adsp21479_stereo_info(struct snd_kcontrol *kcontrol,
        struct snd_ctl_elem_info * uinfo)
{
    pr_info("getting info\n");
    struct snd_adsp21479_chip * chip = get_chip_from_kcontrol(kcontrol);
    if(chip==NULL)
    {
	pr_err("chip is null, can't get info\n");
	
	return -1;
    }
    else
    {
        pr_info("card address %p\n",chip->card);   
     
    }
    spin_lock(&chip->lock);
    uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
    uinfo->count = 2; 
    uinfo->value.integer.min = -100;
    uinfo->value.integer.max = 0;
    spin_unlock(&chip->lock);
    return 0;

}

static int snd_adsp21479_stereo_get(struct snd_kcontrol *kcontrol,
        struct snd_ctl_elem_value *ucontrol)
{
    pr_info("getting\n");
    struct snd_adsp21479_chip * chip = get_chip_from_kcontrol(kcontrol);
    int err;
    if(chip==NULL)
    {
       pr_err("chip is null cannot change volume\n");
       return  -ENODEV;;
    }
    
    if(spi==NULL)
    {
        pr_err("spi is null\n");
        return -ENODEV;
    }
    
    
    spin_lock(&chip->lock);
    err = snd_adsp21479_get_volume(BOTH,chip,&chip->current_volume);
    if(err<0)
    {
        pr_info("error occured while readin g the volume\n");
        spin_unlock(&chip->lock);
        return err;  
    }
    ucontrol->value.integer.value[0] = chip->current_volume;
    ucontrol->value.integer.value[1] = chip->current_volume;
    spin_unlock(&chip->lock);
    
    return 0;
}

static int snd_adsp21479_stereo_put(struct snd_kcontrol *kcontrol,
        struct snd_ctl_elem_value *ucontrol)
{
    pr_info("putting\n");
    struct snd_adsp21479_chip * chip = get_chip_from_kcontrol(kcontrol);
    if(chip==NULL)
    {
       pr_err("chip is null cannot change volume\n");
       return  -ENODEV;;
    }
    
    if(spi==NULL)
    {
        pr_err("spi is null\n");
        return -ENODEV;
    }
    
    int changed = 0;
    spin_lock(&chip->lock);
    if(chip->current_volume != ucontrol->value.integer.value[0])
    {
        pr_info("changing volume from %ld to %ld \n",chip->current_volume,ucontrol->value.integer.value[0]);
        changed =1;
        snd_adsp21479_set_volume(BOTH,chip,&ucontrol->value.integer.value[0]);
        chip->current_volume = ucontrol->value.integer.value[0];
    }
    spin_unlock(&chip->lock);
    
    return changed;

}

static struct snd_kcontrol_new volume_controls[] = {
    {
    .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
    .name = "PCM Playback Volume",
    .index = 0,
    .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
    .info = snd_adsp21479_stereo_info,
    .get = snd_adsp21479_stereo_get,
    .put = snd_adsp21479_stereo_put
    },
};




static const struct snd_soc_dapm_route byt_adsp21479_audio_map[] = {
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx"},
	{"codec_in1", NULL, "ssp2 Rx"},
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


static int byt_nocodec_init(struct snd_soc_pcm_runtime *runtime)
{
	return 0;
}


static struct snd_soc_dai_link byt_adsp21479_dais[] = {
	[MERR_DPCM_AUDIO] = {
		.name = "Audio Port",
		.stream_name = "Audio",
		.cpu_dai_name = "media-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
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
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
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
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
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
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
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
static struct snd_soc_card byt_adsp21479_card = {
	.name = SOUND_CARD_NAME,
	.owner = THIS_MODULE,
	.dai_link = byt_adsp21479_dais,
	.num_links = ARRAY_SIZE(byt_adsp21479_dais),
        .dapm_routes = byt_adsp21479_audio_map,
	.num_dapm_routes = ARRAY_SIZE(byt_adsp21479_audio_map),
        .controls = volume_controls,
        .num_controls=ARRAY_SIZE(volume_controls),
	.fully_routed = true,
};


static int register_spi_device(void)
{
    struct spi_master *master=NULL;
    master = spi_busnum_to_master(LOW_SPEED_SPIDEV_SPI_BUS);
    pr_info("master=%p\n", master);
    if (!master)
    {
         pr_err("spi_busnum_to_master failed");  
         return -ENODEV;
    }
    
    spi = spi_new_device(master, &cal_spi_board_info);
    pr_info("spi device =%p\n", spi);
    if (!spi)
    {
         pr_err("spi gister_driver failed\n"); 
         return -ENODEV;
    }
    
    return 0;

}

static int snd_byt_adsp221479_mc_remove(struct platform_device *pdev)
{
    pr_info("snd_byt_adsp221479_mc_remove\n");
    if(spi)
    {
	pr_info("unregistering spi device\n");
    	spi_unregister_device(spi);
    }
    else
    {
	pr_info("can't unregister spi device, spi is null\n");
    }
    int return_value = snd_soc_unregister_card(&byt_adsp21479_card);
    if(return_value <0)
    {
	pr_info("failed to unregister sound card error: %d\n",return_value);
	return return_value;
    }
    
 
    
  
    return 0;
}

static int snd_byt_adsp21479_mc_probe(struct platform_device *pdev)
{
    int ret_val = 0;
    pr_info("snd_byt_adsp21479_mc_probe\n");
    /* register the soc card */
    byt_adsp21479_card.dev = &pdev->dev;
    register_spi_device();
    pr_info("card address %p\n",&byt_adsp21479_card);   
   
    //chip = kmalloc(sizeof(struct snd_adsp21479_chip),GFP_KERNEL);
    chip.card = &byt_adsp21479_card;
    spin_lock_init(&(chip.lock));  
    chip.current_volume = 0;

   
    //ret_val = devm_snd_soc_register_card(&pdev->dev, &byt_adsp21479_card);
    ret_val = snd_soc_register_card(&byt_adsp21479_card);
    if (ret_val) {
            dev_err(&pdev->dev, "devm_snd_soc_register_card failed %d\n",
                    ret_val);
            return ret_val;
    }
    platform_set_drvdata(pdev, &byt_adsp21479_card);
    pr_info("snd_byt_adsp21479_mc_probe end\n");
    return ret_val;
}



static struct platform_driver snd_byt_adsp21479_mc_driver = {
	.driver = {
		.name = SOUND_CARD_MACHINE_DEVICE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = snd_byt_adsp21479_mc_probe,
        .remove = snd_byt_adsp221479_mc_remove
};

module_platform_driver(snd_byt_adsp21479_mc_driver);

MODULE_DESCRIPTION("ASoC Intel(R) Baytrail ADSP -21479 EZ-Board Machine driver");
MODULE_AUTHOR("Michael Pogrebinsky <michael.pogrebinsky@daqri.com");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bytcr_adsp21479");




