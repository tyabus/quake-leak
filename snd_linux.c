#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <linux/soundcard.h>
#include <stdio.h>
#include "quakedef.h"

int audio_fd;
dma_t the_shm;
int snd_inited;

static int tryrates[] = { 44100, 22050, 11025, 8000 };

extern int desired_speed;
extern int desired_bits;
extern int desired_num_channels;

qboolean SNDDMA_Init(void)
{

	int rc;
    int fmt;
	int tmp;
    int i;
    char *s;
	struct audio_buf_info info;
	int caps;

	snd_inited = 0;

// open /dev/dsp, confirm capability to mmap, and get size of dma buffer

    audio_fd = open("/dev/dsp", O_RDWR);
    if (audio_fd < 0)
	{
		perror("/dev/dsp");
        Con_Printf("Could not open /dev/dsp\n");
		return 0;
	}

    rc = ioctl(audio_fd, SNDCTL_DSP_RESET, 0);
    if (rc < 0)
	{
		perror("/dev/dsp");
		Con_Printf("Could not reset /dev/dsp\n");
		close(audio_fd);
		return 0;
	}

	if (ioctl(audio_fd, SNDCTL_DSP_GETCAPS, &caps)==-1)
	{
		perror("/dev/dsp");
        Con_Printf("Sound driver too old\n");
		close(audio_fd);
		return 0;
	}

	if (!(caps & DSP_CAP_TRIGGER)
		|| !(caps & DSP_CAP_MMAP))
	{
		Con_Printf("Sorry but your soundcard can't do this\n");
		close(audio_fd);
//		return dmasim_Init();
		return 0;
	}

    if (ioctl(audio_fd, SNDCTL_DSP_GETOSPACE, &info)==-1)
    {   
        perror("GETOSPACE");
		Con_Printf("Um, can't do GETOSPACE?\n");
		close(audio_fd);
		return 0;
    }
    
	shm = &the_shm;
    shm->splitbuffer = 0;

// set sample bits & speed

    s = getenv("QUAKE_SOUND_SAMPLEBITS");
    if (s) shm->samplebits = atoi(s);
    else
    {
        ioctl(audio_fd, SNDCTL_DSP_GETFMTS, &fmt);
        if (fmt & AFMT_S16_LE) shm->samplebits = 16;
        else if (fmt & AFMT_U8) shm->samplebits = 8;
    }

		s = getenv("QUAKE_SOUND_SPEED");
		if (s) shm->speed = atoi(s);
		else
		{
			shm->speed = desired_speed;
			if (ioctl(audio_fd, SNDCTL_DSP_SPEED, &shm->speed))
			{
					for (i=0 ; i<sizeof(tryrates)/4 ; i++)
							if (!ioctl(audio_fd, SNDCTL_DSP_SPEED, &tryrates[i])) break;
					shm->speed = tryrates[i];
			}
		}

    s = getenv("QUAKE_SOUND_CHANNELS");
    if (s) shm->channels = atoi(s);
    else shm->channels = 2;
    
	shm->samples = info.fragstotal * info.fragsize / (shm->samplebits/8);
	shm->submission_chunk = 1;

// memory map the dma buffer

	shm->buffer = (unsigned char *) mmap(NULL, info.fragstotal
		* info.fragsize, PROT_WRITE, MAP_FILE|MAP_SHARED, audio_fd, 0);
	if (!shm->buffer)
	{
		perror("/dev/dsp");
		Con_Printf("Could not mmap /dev/dsp\n");
		close(audio_fd);
		return 0;
	}

    rc = ioctl(audio_fd, SOUND_PCM_WRITE_CHANNELS, &shm->channels);
    if (rc < 0)
    {
		perror("/dev/dsp");
        Con_Printf("Could not set /dev/dsp to stereo=%d", shm->channels);
		close(audio_fd);
        return 0;
    }

    rc = ioctl(audio_fd, SNDCTL_DSP_SPEED, &shm->speed);
    if (rc < 0)
    {
		perror("/dev/dsp");
        Con_Printf("Could not set /dev/dsp speed to %d", shm->speed);
		close(audio_fd);
        return 0;
    }

    if (shm->samplebits == 16)
    {
        rc = AFMT_S16_LE;
        rc = ioctl(audio_fd, SNDCTL_DSP_SETFMT, &rc);
        if (rc < 0)
		{
			perror("/dev/dsp");
			Con_Printf("Could not support 16-bit data.  Try 8-bit.\n");
			close(audio_fd);
			return 0;
		}
    }
    else if (shm->samplebits == 8)
    {
        rc = AFMT_U8;
        rc = ioctl(audio_fd, SNDCTL_DSP_SETFMT, &rc);
        if (rc < 0)
		{
			perror("/dev/dsp");
			Con_Printf("Could not support 8-bit data.\n");
			close(audio_fd);
			return 0;
		}
    }
	else
	{
		perror("/dev/dsp");
		Con_Printf("%d-bit sound not supported.", shm->samplebits);
		close(audio_fd);
		return 0;
	}

// toggle the trigger & start her up

    tmp = 0;
    ioctl(audio_fd, SNDCTL_DSP_SETTRIGGER, &tmp);
    tmp = PCM_ENABLE_OUTPUT;
    ioctl(audio_fd, SNDCTL_DSP_SETTRIGGER, &tmp);

	shm->samplepos = 0;

	snd_inited = 1;
	return 1;

}

int SNDDMA_GetDMAPos(void)
{

	struct count_info count;

	if (!snd_inited) return 0;

	if (ioctl(audio_fd, SNDCTL_DSP_GETOPTR, &count)==-1)
	{
		perror("/dev/dsp");
		Con_Printf("Uh, sound dead.\n");
		close(audio_fd);
		snd_inited = 0;
		return 0;
	}
//	shm->samplepos = (count.bytes / (shm->samplebits / 8)) & (shm->samples-1);
//	fprintf(stderr, "%d    \r", count.ptr);
	shm->samplepos = count.ptr / (shm->samplebits / 8);

	return shm->samplepos;

}

void SNDDMA_Shutdown(void)
{
	if (snd_inited)
	{
		close(audio_fd);
		snd_inited = 0;
	}
}

