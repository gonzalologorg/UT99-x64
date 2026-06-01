/*=============================================================================
	AudioLibrary.cpp: Unreal audio interface object.
	Copyright 1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Brandon Reinhart.
=============================================================================*/

/*------------------------------------------------------------------------------------
	Audio includes.
------------------------------------------------------------------------------------*/

#include "AudioPrivate.h"
#if PLATFORM_ANDROID && defined(HAVE_LIBXMP)
#include "xmp.h"
#endif

/*------------------------------------------------------------------------------------
	Globals
------------------------------------------------------------------------------------*/

// Raw Buffer
void* 	AudioBuffer=NULL;
void*	MixBuffer=NULL;
INT   	BufferSize=0;
INT		AudioFormat;
INT		AudioRate;

// State
INT   	AudioInitialized=0;
INT		AudioPaused=0;

// Audio
INT		MusicVoices;
INT		SampleVoices;
Voice 	Voices[AUDIO_TOTALVOICES];

// Volume
INT		SampleVolume;

// Threads
AudioThread		MixingThread;
AudioMutex		Mutex;

#if PLATFORM_ANDROID && defined(HAVE_LIBXMP)
static xmp_context GMusicContext = NULL;
static UBOOL GMusicPlaying = 0;
#endif

/*------------------------------------------------------------------------------------
	Library control.
------------------------------------------------------------------------------------*/

// Start the audio system.
UBOOL AudioInit( DWORD Rate, INT OutputMode, INT Latency )
{
	if (AudioInitialized)
		appErrorf( TEXT("Soundsystem already initialized!") );
	
	// Open the audio subsystem.
	if (OpenAudio( Rate, OutputMode, Latency ) == 0)
		return 0;

	// Initialize defaults.
	SampleVoices = 0;
	MusicVoices = 0;
	for (INT i=0; i<AUDIO_TOTALVOICES; i++)
		Voices[i].State = VOICE_DISABLED;

	// Create the thread sync mutex.
	CreateAudioMutex(&Mutex);

	// Create the mixing thread.
	AudioPaused = 1;
	CreateAudioThread(&MixingThread, DoSound);
	
	AudioInitialized = 1;
	return 1;
}

// Restart the audio system.
UBOOL AudioReinit( DWORD Rate, INT OutputMode, INT Latency )
{
	AudioInitialized = 0;
	INT Result = ReopenAudioDevice( Rate, OutputMode, Latency );
	AudioInitialized = 1;

	return Result;
}

// Stop the audio system.
UBOOL AudioShutdown()
{
	AudioInitialized = 0;
	StopMusicModule();
	DestroyAudioThread(&MixingThread);	
	CloseAudio();
	DestroyAudioMutex(&Mutex);
	return 1;
}

// Allocate memory.
UBOOL AllocateVoices( INT RequestedChannels )
{
	CheckAudioLib(0);

	ALock;
	if (RequestedChannels + MusicVoices <= AUDIO_TOTALVOICES)
	{
		SampleVoices = RequestedChannels;
		for (INT i=0; i<RequestedChannels; i++)
			Voices[i].State |= VOICE_ENABLED;
		AUnlock;
		return 1;
	}
	AUnlock;
	return 0;
}

/*------------------------------------------------------------------------------------
	Master control.
------------------------------------------------------------------------------------*/

// Start [Unpause] all playing audio.
UBOOL AudioStartOutput( DWORD Rate, INT OutputMode, INT Latency )
{
	CheckAudioLib(0);

	ALock;	
	AudioReinit( Rate, OutputMode, Latency );
	AudioPaused = 0;
	AUnlock;
	return 1;
}

// Stop [Pause] all playing audio.
UBOOL AudioStopOutput()
{
	ALock;
	AudioPaused = 1;
	for (INT i=0; i<AUDIO_TOTALVOICES; i++)
	{
		if (Voices[i].State & VOICE_ACTIVE)
		{
			Voices[i].PlayPosition = 0;
			Voices[i].State &= ~VOICE_ACTIVE;
		}
	}
	AUnlock;
	return 1;
}

/*------------------------------------------------------------------------------------
	Sample control.
------------------------------------------------------------------------------------*/

// Load a sound image from memory.
Sample* LoadSample( MemChunk* LoadChunk, const TCHAR* SampleName )
{
	CheckAudioLib(NULL);

	if (LoadChunk == NULL)
		return NULL;

	Sample* lSample = (Sample*) appMalloc( sizeof(Sample), SampleName );
	if (lSample == NULL)
		return NULL;

	appMemset( lSample, 0, sizeof(Sample) );
	BYTE Type[4];
	LoadChunk->Position = 8;
	ReadMem(Type, 1, 4, LoadChunk);
	SeekMem(LoadChunk, 0, MEM_SEEK_ABS);
	if (!appMemcmp( Type, "WAVE", 4 ))
		return LoadWAV(lSample, LoadChunk);	

	return NULL;
}

// Unload a sound image from memory.
UBOOL UnloadSample( Sample* UnloadSample )
{
	CheckAudioLib(0);

	ALock;
	if (UnloadSample)
	{
		// Set length to zero stopping the sound.
		UnloadSample->Length = 0;

		if (UnloadSample->Data)
		{
			appFree(UnloadSample->Data);
			UnloadSample->Data = NULL;
		}
		appFree(UnloadSample);
	}
	AUnlock;

	return 1;
}

// Add sample to set of playing sounds.
Voice* StartSample( INT VoiceNum, Sample* Sample, INT Freq, INT Volume, INT Panning )
{
	CheckAudioLib(NULL);

	if (SampleVoices == 0)
		return NULL;
	if (Sample == NULL)
		return NULL;

	ALock;
	INT Layers;
	if (Sample->Type & SAMPLE_STEREO)
	{
		Layers = 2;
		VoiceNum = VOICE_AUTO;
	} else
		Layers = 1;
	Voice* CurrentVoice = NULL;
	Voice* StereoVoice = NULL;
	for (INT i=0; i<Layers; i++)
	{
		if (VoiceNum == VOICE_AUTO)
			CurrentVoice = AcquireVoice();
		else
			CurrentVoice = &Voices[VoiceNum-1];
		if (CurrentVoice)
		{
			CurrentVoice->State &= ~VOICE_ACTIVE;
			if (Sample->Data)
			{
				CurrentVoice->State |= VOICE_ACTIVE;
				CurrentVoice->State &= ~VOICE_FINISHED;
				CurrentVoice->pSample = Sample;
				if (Layers > 1)
					CurrentVoice->Panning = (i&1) ? AUDIO_MINPAN : AUDIO_MAXPAN;
				else
					CurrentVoice->Panning = Panning;
				CurrentVoice->BasePanning = Panning;
				CurrentVoice->StartTime = CurrentVoice->LastTime = appSeconds();
				CurrentVoice->StereoVoice = StereoVoice;
				CurrentVoice->PlayPosition = 0;
				CurrentVoice->Volume = Volume;
				StereoVoice = CurrentVoice;
			}
		}
	}
	AUnlock;

	return CurrentVoice;
}

// Stop a playing sample.
UBOOL StopSample( Voice* StopVoice )
{
	ALock;
	if (StopVoice)
	{
		StopVoice->State &= ~VOICE_ACTIVE;
		if (StopVoice->StereoVoice)
			StopVoice->StereoVoice->State &= ~VOICE_ACTIVE;
	}
	AUnlock;
	return 1;
}

// Check status of a sample.
UBOOL SampleFinished( Voice* Sample )
{
	return Sample->State & VOICE_FINISHED;
}

// Change a sample.
void UpdateSample( Voice* InVoice, INT Freq, INT Volume, INT Panning )
{
	ALock;
	InVoice->Volume = Volume;
	InVoice->Panning = InVoice->Panning + ((Panning - InVoice->Panning)/2);
	InVoice->BasePanning = InVoice->Panning;
	AUnlock;
}

/*------------------------------------------------------------------------------------
	Voice control.
------------------------------------------------------------------------------------*/

// Acquire an active voice.
Voice* AcquireVoice()
{
	ALock;
	Voice* OpenVoice = NULL;
	INT FoundVoice = 0, OldestVoice = -1;
	DOUBLE LastTime = 0xfffffffe;
	for (INT i=0; i<SampleVoices && !FoundVoice; i++)
	{
		if (Voices[i].State & VOICE_ENABLED)
		{
			if ((~Voices[i].State) & VOICE_ACTIVE)
			{
				OpenVoice = &Voices[i];
				FoundVoice = 1;
			}
			else if (Voices[i].StartTime < LastTime)
			{
				LastTime = Voices[i].StartTime;
				OldestVoice = i;
			}
		}
	}
	if (OldestVoice > -1)
		OpenVoice = &Voices[OldestVoice];
	AUnlock;
	
	return OpenVoice;
}

// Get some info on a voice.
void GetVoiceStats( VoiceStats* InStats, Voice* InVoice )
{
	ALock;

	// % complete.
	if (InVoice->PlayPosition == 0)
		InStats->Completion = 0;
	else if (InVoice->pSample != NULL)
	{
		FLOAT SoundSamples = InVoice->pSample->Length;
		FLOAT Position = InVoice->PlayPosition;
		InStats->Completion = Position / SoundSamples;
	}

	// Completion string.
	FString CompletionString;
	INT NumStars = (INT) (InStats->Completion * 10);
	INT i;
	for (i=0; i<NumStars; i++)
		CompletionString = CompletionString + FString::Printf( TEXT("*") );
	for (i=NumStars; i<10; i++)
		CompletionString = CompletionString + FString::Printf( TEXT("-") );
	InStats->CompletionString = CompletionString;
	
	AUnlock;
}

/*------------------------------------------------------------------------------------
	Volume control.
------------------------------------------------------------------------------------*/

UBOOL SetSampleVolume( FLOAT Volume )
{
	ALock;
	SampleVolume = (INT) Volume;
	AUnlock;
	return 1;
}

UBOOL SetMusicVolume( FLOAT Volume )
{
	return 1;
}

UBOOL LoadMusicModule( void* Data, INT Size, INT Section, const TCHAR* MusicName )
{
#if PLATFORM_ANDROID && defined(HAVE_LIBXMP)
	ALock;
	StopMusicModule();
	if( !Data || Size <= 0 )
	{
		debugf( NAME_Init, TEXT("UT99_ANDROID_V237_MUSIC_LOAD_SKIP name=%s size=%i"), MusicName ? MusicName : TEXT("None"), Size );
		AUnlock;
		return 0;
	}
	BYTE* Bytes = (BYTE*)Data;
	debugf( NAME_Init, TEXT("UT99_ANDROID_V239_MUSIC_LOAD_ENTER name=%s size=%i head=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x"),
		MusicName ? MusicName : TEXT("None"),
		Size,
		Size > 0 ? Bytes[0] : 0,
		Size > 1 ? Bytes[1] : 0,
		Size > 2 ? Bytes[2] : 0,
		Size > 3 ? Bytes[3] : 0,
		Size > 4 ? Bytes[4] : 0,
		Size > 5 ? Bytes[5] : 0,
		Size > 6 ? Bytes[6] : 0,
		Size > 7 ? Bytes[7] : 0,
		Size > 8 ? Bytes[8] : 0,
		Size > 9 ? Bytes[9] : 0,
		Size > 10 ? Bytes[10] : 0,
		Size > 11 ? Bytes[11] : 0,
		Size > 12 ? Bytes[12] : 0,
		Size > 13 ? Bytes[13] : 0,
		Size > 14 ? Bytes[14] : 0,
		Size > 15 ? Bytes[15] : 0 );
	GMusicContext = xmp_create_context();
	if( !GMusicContext )
	{
		debugf( NAME_Init, TEXT("UT99_ANDROID_V237_MUSIC_CREATE_FAILED name=%s"), MusicName ? MusicName : TEXT("None") );
		AUnlock;
		return 0;
	}
	debugf( NAME_Init, TEXT("UT99_ANDROID_V239_MUSIC_CONTEXT_OK name=%s ctx=%p"), MusicName ? MusicName : TEXT("None"), GMusicContext );
	debugf( NAME_Init, TEXT("UT99_ANDROID_V239_MUSIC_LOAD_BEGIN name=%s size=%i"), MusicName ? MusicName : TEXT("None"), Size );
	INT LoadResult = xmp_load_module_from_memory( GMusicContext, Data, Size );
	debugf( NAME_Init, TEXT("UT99_ANDROID_V239_MUSIC_LOAD_DONE name=%s result=%i"), MusicName ? MusicName : TEXT("None"), LoadResult );
	if( LoadResult != 0 )
	{
		debugf( NAME_Init, TEXT("UT99_ANDROID_V237_MUSIC_LOAD_FAILED name=%s size=%i result=%i"), MusicName ? MusicName : TEXT("None"), Size, LoadResult );
		xmp_free_context( GMusicContext );
		GMusicContext = NULL;
		AUnlock;
		return 0;
	}
	debugf( NAME_Init, TEXT("UT99_ANDROID_V239_MUSIC_START_BEGIN name=%s rate=%i"), MusicName ? MusicName : TEXT("None"), AudioRate ? AudioRate : 22050 );
	INT StartResult = xmp_start_player( GMusicContext, AudioRate ? AudioRate : 22050, 0 );
	debugf( NAME_Init, TEXT("UT99_ANDROID_V239_MUSIC_START_DONE name=%s result=%i"), MusicName ? MusicName : TEXT("None"), StartResult );
	if( StartResult != 0 )
	{
		debugf( NAME_Init, TEXT("UT99_ANDROID_V237_MUSIC_START_FAILED name=%s rate=%i result=%i"), MusicName ? MusicName : TEXT("None"), AudioRate, StartResult );
		xmp_release_module( GMusicContext );
		xmp_free_context( GMusicContext );
		GMusicContext = NULL;
		AUnlock;
		return 0;
	}
	if( Section != 255 )
		xmp_set_position( GMusicContext, Section );
	GMusicPlaying = 1;
	debugf( NAME_Init, TEXT("UT99_ANDROID_V237_MUSIC_STARTED name=%s size=%i section=%i rate=%i"), MusicName ? MusicName : TEXT("None"), Size, Section, AudioRate );
	AUnlock;
	return 1;
#else
	debugf( NAME_Init, TEXT("UT99_ANDROID_V237_MUSIC_UNAVAILABLE name=%s size=%i"), MusicName ? MusicName : TEXT("None"), Size );
	return 0;
#endif
}

UBOOL StopMusicModule()
{
#if PLATFORM_ANDROID && defined(HAVE_LIBXMP)
	if( GMusicContext )
	{
		xmp_end_player( GMusicContext );
		xmp_release_module( GMusicContext );
		xmp_free_context( GMusicContext );
		GMusicContext = NULL;
	}
	GMusicPlaying = 0;
#endif
	return 1;
}

UBOOL MixMusicBuffer( void* Buffer, INT Size )
{
#if PLATFORM_ANDROID && defined(HAVE_LIBXMP)
	if( !GMusicContext || !GMusicPlaying || !Buffer || Size <= 0 )
		return 0;
	INT Result = xmp_play_buffer( GMusicContext, Buffer, Size, 1 );
	if( Result != 0 )
	{
		static INT AndroidMusicMixLogs = 0;
		if( AndroidMusicMixLogs < 16 || (AndroidMusicMixLogs % 120) == 0 )
			debugf( NAME_Init, TEXT("UT99_ANDROID_V237_MUSIC_MIX_RESULT count=%i result=%i"), AndroidMusicMixLogs, Result );
		AndroidMusicMixLogs++;
	}
	return 1;
#else
	return 0;
#endif
}

UBOOL IsMusicModulePlaying()
{
#if PLATFORM_ANDROID && defined(HAVE_LIBXMP)
	return GMusicPlaying;
#else
	return 0;
#endif
}

UBOOL SetCDAudioVolume( FLOAT Volume )
{
	return 1;
}

/*------------------------------------------------------------------------------------
	CD Audio control.
------------------------------------------------------------------------------------*/

UBOOL StartCDAudio( INT Track )
{
	return 1;
}

UBOOL StopCDAudio()
{
	return 1;
}
