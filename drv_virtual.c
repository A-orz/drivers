/*
* File: drv_virtual.c
*
* COPYRIGHT (C) 2012-2019, Shanghai Real-Thread Technology Co., Ltd
*/

#include "dfs.h"
#include "dfs_posix.h"

#define DBG_TAG "drv_virtual"
#define DBG_LVL DBG_LOG
#define DBG_COLOR
#include <rtdbg.h>

#define TX_DMA_FIFO_SIZE (2048)

struct tina_sound
{
    struct rt_audio_device device;
    struct rt_audio_configure replay_config;
    int volume;
    rt_uint8_t *tx_fifo;
    int fd;
    struct rt_thread thread;
    int endflag;
};

static rt_err_t getcaps(struct rt_audio_device *audio, struct rt_audio_caps *caps)
{
    rt_err_t ret = RT_EOK;
    struct tina_sound *sound = RT_NULL;

    RT_ASSERT(audio != RT_NULL);
    sound = (struct tina_sound *)audio->parent.user_data; (void)sound;

    switch(caps->main_type)
    {
    case AUDIO_TYPE_QUERY:
    {
        switch (caps->sub_type)
        {
        case AUDIO_TYPE_QUERY:
            caps->udata.mask = AUDIO_TYPE_OUTPUT | AUDIO_TYPE_MIXER;
            break;

        default:
            ret = -RT_ERROR;
            break;
        }

        break;
    }

    case AUDIO_TYPE_OUTPUT:
    {
        switch(caps->sub_type)
        {
        case AUDIO_DSP_PARAM:
            caps->udata.config.channels   = sound->replay_config.channels;
            caps->udata.config.samplebits = sound->replay_config.samplebits;
            caps->udata.config.samplerate = sound->replay_config.samplerate;
            break;

        default:
            ret = -RT_ERROR;
            break;
        }

        break;
    }

    case AUDIO_TYPE_MIXER:
    {
        switch (caps->sub_type)
        {
        case AUDIO_MIXER_QUERY:
            caps->udata.mask = AUDIO_MIXER_VOLUME | AUDIO_MIXER_LINE;
            break;

        case AUDIO_MIXER_VOLUME:
            caps->udata.value = sound->volume;
            break;

        case AUDIO_MIXER_LINE:
            break;

        default:
            ret = -RT_ERROR;
            break;
        }

        break;
    }

    default:
        ret = -RT_ERROR;
        break;
    }

    return ret;
}

static rt_err_t configure(struct rt_audio_device *audio, struct rt_audio_caps *caps)
{
    rt_err_t ret = RT_EOK;
    struct tina_sound *sound = RT_NULL;

    RT_ASSERT(audio != RT_NULL);
    sound = (struct tina_sound *)audio->parent.user_data; (void)sound;

    switch(caps->main_type)
    {
    case AUDIO_TYPE_MIXER:
    {
        switch(caps->sub_type)
        {
        case AUDIO_MIXER_VOLUME:
        {
            int volume = caps->udata.value;
            sound->volume = volume;
            break;
        }

        default:
            ret = -RT_ERROR;
            break;
        }

        break;
    }

    case AUDIO_TYPE_OUTPUT:
    {
        switch(caps->sub_type)
        {
        case AUDIO_DSP_PARAM:
        {
            int samplerate;

            samplerate = caps->udata.config.samplerate;
            sound->replay_config.samplerate = samplerate;
            LOG_I("set samplerate = %d", samplerate);
            break;
        }

        case AUDIO_DSP_SAMPLERATE:
        {
            int samplerate;

            samplerate = caps->udata.config.samplerate;
            sound->replay_config.samplerate = samplerate;
            LOG_I("set samplerate = %d", samplerate);
            break;
        }

        case AUDIO_DSP_CHANNELS:
        {
            break;
        }

        default:
            break;
        }

        break;
    }

    default:
        break;
    }

    return ret;
}

static void virtualplay(void *p)
{
    struct tina_sound *sound = (struct tina_sound *)p; (void)sound;

    while(1)
    {
        /* tick = TX_DMA_FIFO_SIZE/2 * 1000ms / 44100 / 4 鈮� 5.8 */
        rt_thread_mdelay(6);
        rt_audio_tx_complete(&sound->device);

        if(sound->endflag == 1)
        {
            break;
        }
    }
}

static int thread_stack[1024] = {0};

static rt_err_t init(struct rt_audio_device *audio)
{
    struct tina_sound *sound = RT_NULL;

    RT_ASSERT(audio != RT_NULL);
    sound = (struct tina_sound *)audio->parent.user_data; (void)sound;

    LOG_I("sound init");

    return RT_EOK;
}

static rt_err_t start(struct rt_audio_device *audio, int stream)
{
    struct tina_sound *sound = RT_NULL;
    rt_err_t ret = RT_EOK;

    RT_ASSERT(audio != RT_NULL);
    sound = (struct tina_sound *)audio->parent.user_data; (void)sound;

    LOG_I("sound start");

    ret = rt_thread_init(&sound->thread, "virtual", virtualplay, sound, &thread_stack, sizeof(thread_stack), 1, 10);
    if(ret != RT_EOK)
    {
        LOG_E("virtual play thread init failed");
        return (-RT_ERROR);
    }
    rt_thread_startup(&sound->thread);

    sound->endflag = 0;

    sound->fd = open("/tmp/virtual.pcm", O_CREAT | O_RDWR, 0666);

    return RT_EOK;
}

static rt_err_t stop(struct rt_audio_device *audio, int stream)
{
    struct tina_sound *sound = RT_NULL;

    RT_ASSERT(audio != RT_NULL);
    sound = (struct tina_sound *)audio->parent.user_data; (void)sound;

    LOG_I("sound stop");  

    sound->endflag = 1;

    close(sound->fd);
    sound->fd = -1;

    return RT_EOK;
}

rt_size_t transmit(struct rt_audio_device *audio, const void *wb, void *rb, rt_size_t size)
{
    struct tina_sound *sound = RT_NULL;

    RT_ASSERT(audio != RT_NULL);
    sound = (struct tina_sound *)audio->parent.user_data; (void)sound;

    return write(sound->fd, wb, size);
}

static void buffer_info(struct rt_audio_device *audio, struct rt_audio_buf_info *info)
{
    struct tina_sound *sound = RT_NULL;

    RT_ASSERT(audio != RT_NULL);
    sound = (struct tina_sound *)audio->parent.user_data;

    /**
     *               TX_FIFO
     * +----------------+----------------+
     * |     block1     |     block2     |
     * +----------------+----------------+
     *  \  block_size  /
     */
    info->buffer      = sound->tx_fifo;
    info->total_size  = TX_DMA_FIFO_SIZE;
    info->block_size  = TX_DMA_FIFO_SIZE / 2;
    info->block_count = 2;
}

static struct rt_audio_ops ops =
{
    .getcaps     = getcaps,
    .configure   = configure,
    .init        = init,
    .start       = start,
    .stop        = stop,
    .transmit    = transmit,
    .buffer_info = buffer_info,
};

static int rt_hw_sound_init(void)
{
    rt_uint8_t *tx_fifo = RT_NULL;
    static struct tina_sound sound = {0};

    tx_fifo = rt_calloc(1, TX_DMA_FIFO_SIZE);
    if(tx_fifo == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    sound.tx_fifo = tx_fifo;

    {
        sound.replay_config.samplerate = 44100;
        sound.replay_config.channels   = 2;
        sound.replay_config.samplebits = 16;
        sound.volume                   = 60;
        sound.fd                       = -1;
        sound.endflag                  = 0;
    }


    sound.device.ops = &ops;
    rt_audio_register(&sound.device, "sound0", RT_DEVICE_FLAG_WRONLY, &sound);

    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_hw_sound_init);