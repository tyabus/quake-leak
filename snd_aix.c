
#include "fcntl.h"
#include "quakedef.h"

#include <UMSAudioDevice.h>

extern int desired_speed;
extern int desired_bits;

static UMSAudioDevice audio_device;
static UMSAudioTypes_Buffer output_buffer;
static Environment *audio_env;

static int oosp;
static int osp;

static dma_t the_shm;

static int rawsound;

qboolean SNDDMA_Init(void)
{
	int size;
	UMSAudioDeviceMClass audio_device_class;
	char *alias;
	long flags;
	UMSAudioDeviceMClass_ErrorCode audio_device_class_error;
	char *error_string;
	char *audio_formats_alias;
	char *audio_inputs_alias;
	char *audio_outputs_alias;
	long left_gain;
	long right_gain;
	char *s;
	int actual_dma_size;
	int latency;

	size = 32*1024;
	shm = &the_shm;

	shm->channels = 2;
	s = getenv("QUAKE_SOUND_CHANNELS");
	if (s) shm->channels = Q_atoi(s);
	shm->speed = desired_speed;
	s = getenv("QUAKE_SOUND_SPEED");
	if (s) shm->speed = Q_atoi(s);
	shm->samplebits = desired_bits;
	s = getenv("QUAKE_SOUND_SAMPLEBITS");
	if (s) shm->samplebits = Q_atoi(s);
	shm->submission_chunk = 1;
	shm->soundalive = 1;
	shm->samplepos = 0;

	audio_env = somGetGlobalEnvironment();

	audio_device_class = UMSAudioDeviceNewClass(UMSAudioDevice_MajorVersion,
		UMSAudioDevice_MinorVersion);

	if (audio_device_class == NULL)
	{
		Sys_Warn("sound driver: Can't create AudioDeviceMClass metaclass\n");
		return 0;
	}

    alias = "Audio";
    flags = UMSAudioDevice_BlockingIO;
//    flags = O_NDELAY;

	audio_device = UMSAudioDeviceMClass_make_by_alias(audio_device_class,
			audio_env, alias, "PLAY", flags, &audio_device_class_error,
			&error_string, &audio_formats_alias, &audio_inputs_alias,
			&audio_outputs_alias);

    if (audio_device == NULL)
	{
		Sys_Warn(stderr, "sound driver : Can't create audio device object\n");
		return 0;
	}

	UMSAudioDevice_set_sample_rate(audio_device, audio_env, shm->speed, (long*)&shm->speed);
	UMSAudioDevice_set_bits_per_sample(audio_device, audio_env, shm->samplebits);
	UMSAudioDevice_set_number_of_channels(audio_device, audio_env, shm->channels);

    UMSAudioDevice_set_audio_format_type(audio_device, audio_env, "PCM");
    UMSAudioDevice_set_byte_order(audio_device, audio_env, "MSB");
	if (shm->samplebits == 16)
		UMSAudioDevice_set_number_format(audio_device, audio_env,
			"TWOS_COMPLEMENT");
	else
		UMSAudioDevice_set_number_format(audio_device, audio_env,
			"UNSIGNED");
	UMSAudioDevice_set_volume(audio_device, audio_env, 100);
	UMSAudioDevice_set_balance(audio_device, audio_env, 0);

	UMSAudioDevice_set_time_format(audio_device, audio_env, UMSAudioTypes_Samples);

	left_gain = right_gain = 100;
	UMSAudioDevice_enable_output(audio_device, audio_env, "INTERNAL_SPEAKER",
		&left_gain, &right_gain);
	left_gain = right_gain = 100;
	UMSAudioDevice_enable_output(audio_device, audio_env, "LINE_OUT",
		&left_gain, &right_gain);

	UMSAudioDevice_set_DMA_buffer_size(audio_device, audio_env, 256, &actual_dma_size);
	Con_Printf("Actual sound DMA size=%d\n", actual_dma_size);

	UMSAudioDevice_initialize(audio_device, audio_env);
	UMSAudioDevice_start(audio_device, audio_env);

	UMSAudioDevice_set_audio_buffer_size(audio_device, audio_env, 256*1024, &actual_dma_size);
	Con_Printf("Actual sound buffer size=%d\n", actual_dma_size);

	s = getenv("QUAKE_SOUND_SAMPLES");
	if (s) shm->samples = Q_atoi(s);
	else shm->samples = 1<<Q_log2(shm->speed * shm->channels / 10);

	size = shm->samples * shm->samplebits / 8;
	shm->buffer = (unsigned char *) malloc (size);
	memset(shm->buffer, 0, size);

	oosp = 0;
	osp = 0;
	shm->samplepos = 0;

//	rawsound = Sys_FileOpenWrite("quake_sound.raw");

	return 1;

}

// return the current sample position (in mono samples read)
// inside the recirculating dma buffer
int SNDDMA_GetDMAPos(void)
{

	long samplepairs_en_route;
	long samplepairs_written;
	int submit_size;
	int len;
	int submit_pos;
	int i;
	unsigned test;

// get current samples in queue
	UMSAudioDevice_write_buff_used(audio_device, audio_env, &samplepairs_en_route);

// prime the dma buffer if empty
	if (!samplepairs_en_route)
	{
//		Con_Printf("empty!\n");
		output_buffer._buffer = shm->buffer;
		output_buffer._length = shm->samples * (shm->samplebits/8);
		output_buffer._maximum = output_buffer._length;
		UMSAudioDevice_write(audio_device, audio_env, &output_buffer,
			shm->samples / shm->channels, &samplepairs_written);
		oosp = 0;
		osp = 0;
		shm->samplepos = 0;
		return;
	}

// calculate new samplepos
	shm->samplepos = (oosp - (samplepairs_en_route * shm->channels)) & (shm->samples-1);
// calculate number of samples to submit
	submit_size = (osp - oosp) & (shm->samples-1);
// s is the position where samples will be submitted from
	submit_pos = oosp;

	if (submit_size)
	{
		output_buffer._buffer = shm->buffer + submit_pos * (shm->samplebits/8);
	// only if the submission chunk wraps around the virtual dma buffer
		if (submit_pos + submit_size > shm->samples)
		{
			len = shm->samples - submit_pos;
			output_buffer._length = len * (shm->samplebits/8);
			output_buffer._maximum = output_buffer._length;
/*
			for (i=0 ; i<len ; i++)
			{
				test = output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8];
				test <<= 8;
				test |= output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8+1];
				test <<= 8;
				test |= output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8+2];
				test <<= 8;
				test |= output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8+3];
				if (test == 0xffff0000)
					Con_Printf("element %d is bogus! (w)\n", i);
			}
*/
			UMSAudioDevice_write(audio_device, audio_env, &output_buffer,
				len / shm->channels, &samplepairs_written);
/*
			if (samplepairs_written != len / shm->channels)
				Con_Printf("ARG! %d of %d written (w)\n", samplepairs_written,
					len / shm->channels);
			Sys_FileWrite(rawsound, output_buffer._buffer, output_buffer._length);
			for (i=0 ; i<len ; i++)
			{
				output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8] = 0xff;
				output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8+1] = 0xff;
				output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8+2] = 0;
				output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8+3] = 0;
			}
*/
			output_buffer._buffer = shm->buffer;
			submit_size -= len;
		}

		output_buffer._length = submit_size * (shm->samplebits/8);
		output_buffer._maximum = output_buffer._length;
	// only if the submission chunk wraps around the virtual dma buffer
/*
		for (i=0 ; i<submit_size ; i++)
		{
			test = output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8];
			test <<= 8;
			test |= output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8+1];
			test <<= 8;
			test |= output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8+2];
			test <<= 8;
			test |= output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8+3];
			if (test == 0xffff0000)
				Con_Printf("element %d is bogus!\n", i);
		}
*/
		UMSAudioDevice_write(audio_device, audio_env, &output_buffer,
			submit_size / shm->channels, &samplepairs_written);
/*
		if (samplepairs_written != submit_size / shm->channels)
			Con_Printf("ARG! %d of %d written\n", samplepairs_written,
				submit_size / shm->channels);
		Sys_FileWrite(rawsound, output_buffer._buffer, output_buffer._length);
		for (i=0 ; i<submit_size ; i++)
		{
			output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8] = 0xff;
			output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8+1] = 0xff;
			output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8+2] = 0;
			output_buffer._buffer[(i&(shm->samples-1))*shm->samplebits/8+3] = 0;
		}
*/
	}

// update the old old samplepos
	oosp = osp;
// update the old samplepos
	osp = shm->samplepos;

	return shm->samplepos;

}

// return the current sample position (in mono samples read)
// inside the recirculating dma buffer
int SNDDMA_GetDMAPos_debug(void)
{

	long samplepairs_en_route;
	long samplepairs_written;
	int submit_size;
	int len;
	int s;
	int rc;
	double t1, t2;

	Con_Printf("Sound update called at %fs\n", Sys_FloatTime());

// get current samples in queue
	t1 = Sys_FloatTime();
	rc = UMSAudioDevice_write_buff_used(audio_device, audio_env, &samplepairs_en_route);
	t1 = Sys_FloatTime() - t1;
	if (rc == UMSAudioDevice_UnderRun)
		Con_Printf("underrun!\n", rc);

// submit_size is both the number of samples to advance the samplepos as well as
// the number of samples which will be submitted
	submit_size = shm->samples - samplepairs_en_route * shm->channels;
	if (submit_size <= 0) return shm->samplepos;

// s is the position where samples will be submitted from
	s = (shm->samplepos + shm->samples) & (shm->samples - 1);
	output_buffer._buffer = shm->buffer + s * (shm->samplebits/8);

// only if the submission chunk wraps around the virtual dma buffer
	if (s + submit_size > shm->samples)
	{
		len = shm->samples - s;
		output_buffer._length = len * (shm->samplebits/8);
		output_buffer._maximum = len * (shm->samplebits/8);
		t2 = Sys_FloatTime();
		UMSAudioDevice_write(audio_device, audio_env, &output_buffer,
			len / shm->channels, &samplepairs_written);
		t2 = Sys_FloatTime() - t2;
		Con_Printf("wraparound write:%f (%d)\n", t2, len / shm->channels);
		output_buffer._buffer = shm->buffer;
		submit_size -= len;
	}

	output_buffer._length = submit_size * (shm->samplebits/8);
	output_buffer._maximum = submit_size * (shm->samplebits/8);
	t2 = Sys_FloatTime();
// only if the submission chunk wraps around the virtual dma buffer
	UMSAudioDevice_write(audio_device, audio_env, &output_buffer,
		submit_size / shm->channels, &samplepairs_written);
	t2 = Sys_FloatTime() - t2;

	Con_Printf("wu:%f (%d) write:%f (%d)\n", t1, samplepairs_en_route,
		t2, submit_size / shm->channels);

// update the samplepos
	shm->samplepos = (shm->samplepos + submit_size) & (shm->samples-1);

	return shm->samplepos;

}

void SNDDMA_Shutdown(void)
{
	UMSAudioDevice_play_remaining_data(audio_device, audio_env, TRUE);
	UMSAudioDevice_stop(audio_device, audio_env);
	UMSAudioDevice_close(audio_device, audio_env);
	free(shm->buffer);
}

