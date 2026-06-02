/*=============================================================================
	AudioSubsystem.cpp: Unreal audio interface object.
	Copyright 1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Brandon Reinhart.
	Based on the UGalaxyAudioSubsystem interface.
=============================================================================*/

/*------------------------------------------------------------------------------------
	Audio includes.
------------------------------------------------------------------------------------*/

#include "AudioPrivate.h"

#if PLATFORM_ANDROID
#ifndef UT99_ANDROID_AUDIO_FRAME_TRACE
#define UT99_ANDROID_AUDIO_FRAME_TRACE 0
#endif
#endif

/*------------------------------------------------------------------------------------
	UGenericAudioSubsystem.
------------------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UGenericAudioSubsystem);

static UBOOL IsKnownAudioActorPointer( AActor* Actor )
{
	if( !Actor )
		return 0;
#if PLATFORM_ANDROID
	QWORD Addr = (QWORD)Actor;
	return Addr != (QWORD)-1 && (Addr & 0x7)==0;
#else
	for( TObjectIterator<AActor> It; It; ++It )
		if( *It==Actor )
			return 1;
	return 0;
#endif
}

static UBOOL IsKnownAudioSoundPointer( USound* Sound )
{
	if( !Sound )
		return 0;
#if PLATFORM_ANDROID
	QWORD Addr = (QWORD)Sound;
	return Addr != (QWORD)-1 && (Addr & 0x7)==0;
#else
	for( TObjectIterator<USound> It; It; ++It )
		if( *It==Sound )
			return 1;
	return 0;
#endif
}

static UBOOL IsKnownAudioLevelPointer( ULevel* Level )
{
	if( !Level )
		return 0;
#if PLATFORM_ANDROID
	QWORD Addr = (QWORD)Level;
	return Addr != (QWORD)-1 && (Addr & 0x7)==0;
#else
	for( TObjectIterator<ULevel> It; It; ++It )
		if( *It==Level )
			return 1;
	return 0;
#endif
}

FLOAT UGenericAudioSubsystem::SoundPriority( UViewport* InViewport, FVector Location, FLOAT Volume, FLOAT Radius )
{
	guard(UGenericAudioSubsystem::SoundPriority);
	if( !InViewport || !IsKnownAudioActorPointer(InViewport->Actor) || Radius <= 1.f )
	{
		debugf( NAME_Init, TEXT("UT99_ANDROID_V213_AUDIO_BAD_PRIORITY_INPUT viewport=0x%08x actor=0x%08x radius=%f"),
			(DWORD)(QWORD)InViewport,
			InViewport ? (DWORD)(QWORD)InViewport->Actor : 0,
			Radius );
		return 0.f;
	}
	AActor* Listener = InViewport->Actor;
	APlayerPawn* PlayerListener = Listener->IsA(APlayerPawn::StaticClass()) ? (APlayerPawn*)Listener : NULL;
	if( PlayerListener && PlayerListener->ViewTarget )
	{
		if( IsKnownAudioActorPointer(PlayerListener->ViewTarget) )
			Listener = PlayerListener->ViewTarget;
		else
			debugf( NAME_Init, TEXT("UT99_ANDROID_V213_AUDIO_BAD_VIEWTARGET actor=0x%08x viewtarget=0x%08x"),
				(DWORD)(QWORD)InViewport->Actor,
				(DWORD)(QWORD)PlayerListener->ViewTarget );
	}
	return Volume * (1.0 - (Location - Listener->Location).Size()/Radius);
	unguard;
}

UGenericAudioSubsystem::UGenericAudioSubsystem()
{
	guard(UGenericAudioSubsystem::UGenericAudioSubsystem);

	MusicFade			= 1.0;
	CurrentCDTrack		= 255;
	LastTime			= appSeconds();
	
	unguard;
}

void UGenericAudioSubsystem::StaticConstructor()
{
	guard(UGenericAudioSubsystem::StaticConstructor);

	UEnum* OutputRates = new( GetClass(), TEXT("OutputRates") )UEnum( NULL );
		new( OutputRates->Names )FName( TEXT("8000Hz" ) );
		new( OutputRates->Names )FName( TEXT("11025Hz") );
		new( OutputRates->Names )FName( TEXT("16000Hz") );
		new( OutputRates->Names )FName( TEXT("22050Hz") );
		new( OutputRates->Names )FName( TEXT("32000Hz") );
		new( OutputRates->Names )FName( TEXT("44100Hz") );
		new( OutputRates->Names )FName( TEXT("48000Hz") );
	new(GetClass(),TEXT("UseFilter"),       RF_Public)UBoolProperty  (CPP_PROPERTY(UseFilter      ), TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("UseSurround"),     RF_Public)UBoolProperty  (CPP_PROPERTY(UseSurround    ), TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("UseStereo"),       RF_Public)UBoolProperty  (CPP_PROPERTY(UseStereo      ), TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("UseCDMusic"),      RF_Public)UBoolProperty  (CPP_PROPERTY(UseCDMusic     ), TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("UseDigitalMusic"), RF_Public)UBoolProperty  (CPP_PROPERTY(UseDigitalMusic), TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("ReverseStereo"),   RF_Public)UBoolProperty  (CPP_PROPERTY(ReverseStereo  ), TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("Latency"),         RF_Public)UIntProperty   (CPP_PROPERTY(Latency        ), TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("OutputRate"),      RF_Public)UByteProperty  (CPP_PROPERTY(OutputRate     ), TEXT("Audio"), CPF_Config, OutputRates );
	new(GetClass(),TEXT("Channels"), 		RF_Public)UIntProperty   (CPP_PROPERTY(Channels), TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("MusicVolume"),     RF_Public)UByteProperty  (CPP_PROPERTY(MusicVolume    ), TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("SoundVolume"),     RF_Public)UByteProperty  (CPP_PROPERTY(SoundVolume    ), TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("AmbientFactor"),   RF_Public)UFloatProperty (CPP_PROPERTY(AmbientFactor  ), TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("DopplerSpeed"),    RF_Public)UFloatProperty (CPP_PROPERTY(DopplerSpeed   ), TEXT("Audio"), CPF_Config );

	unguard;
}

/*------------------------------------------------------------------------------------
	UObject Interface.
------------------------------------------------------------------------------------*/

void UGenericAudioSubsystem::PostEditChange()
{
	guard(UGenericAudioSubsystem::PostEditChange);

	// Validate configurable variables.
	OutputRate      = Clamp(OutputRate,(BYTE)0,(BYTE)6);
	Latency         = Clamp<INT>(Latency,10,250);
	Channels 		= Clamp<INT>(Channels,0,MAX_EFFECTS_CHANNELS);
	DopplerSpeed    = Clamp(DopplerSpeed,1.f,100000.f);
	AmbientFactor   = Clamp(AmbientFactor,0.f,10.f);
#if PLATFORM_ANDROID
	if( AmbientFactor <= 0.f )
	{
		debugf( NAME_Init, TEXT("UT99_ANDROID_V293_AUDIO_AMBIENT_FACTOR_FIX old=%f"), AmbientFactor );
		AmbientFactor = 1.f;
	}
#endif
	SetVolumes();

	unguard;
}

void UGenericAudioSubsystem::Destroy()
{
	guard(UGenericAudioSubsystem::Destroy);
	if( Initialized )
	{
		// Unhook.
		USound::Audio = NULL;
		UMusic::Audio = NULL;

		// Shut down viewport.
		SetViewport( NULL );

		// Stop CD.
		if( UseCDMusic && CurrentCDTrack != 255 )
			StopCDAudio();

		// Shutdown soundsystem.
		safecall(AudioShutdown());

		debugf( NAME_Exit, TEXT("Generic audio subsystem shut down.") );
	}
	Super::Destroy();
	unguard;
}

void UGenericAudioSubsystem::ShutdownAfterError()
{
	guard(UGenericAudioSubsystem::ShutdownAfterError);

	// Unhook.
	USound::Audio = NULL;
	UMusic::Audio = NULL;

	// Safely shut down.
	debugf( NAME_Exit, TEXT("UGenericAudioSubsystem::ShutdownAfterError") );
	safecall(AudioStopOutput());
	if( Viewport )
		safecall(AudioShutdown());
	Super::ShutdownAfterError();
	unguard;
}

/*------------------------------------------------------------------------------------
	UAudioSubsystem Interface.
------------------------------------------------------------------------------------*/

UBOOL UGenericAudioSubsystem::Init()
{
	guard(UGenericAudioSubsystem::Init);

#if PLATFORM_ANDROID
	if( OutputRate < 3 || !UseStereo || Channels < 8 || Latency < 20 || !UseDigitalMusic )
	{
		debugf( NAME_Init, TEXT("UT99_ANDROID_V211_AUDIO_DEFAULT_FIX oldRate=%i oldStereo=%i oldChannels=%i oldLatency=%i oldDigital=%i"),
			OutputRate,
			UseStereo,
			Channels,
			Latency,
			UseDigitalMusic );
		OutputRate = Max<BYTE>( OutputRate, 3 );
		UseStereo = 1;
		Channels  = Max<INT>( Channels, 8 );
		Latency   = Max<INT>( Latency, 20 );
#if defined(HAVE_LIBXMP)
		UseDigitalMusic = 1;
#endif
	}
	if( AmbientFactor <= 0.f )
	{
		debugf( NAME_Init, TEXT("UT99_ANDROID_V293_AUDIO_AMBIENT_FACTOR_FIX old=%f"), AmbientFactor );
		AmbientFactor = 1.f;
	}
#endif

	// Initialize Unreal Audio library.
	guard(InitAudio);
	OutputMode = AUDIO_16BIT;
	if( UseFilter )
		OutputMode |= AUDIO_COSINE;
	if( UseStereo )
		OutputMode |= AUDIO_STEREO;
	OutputMode |= AUDIO_2DAUDIO;
	INT Rates[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000};
	INT Rate = Rates[OutputRate];
#if PLATFORM_ANDROID
	debugf( NAME_Init, TEXT("UT99_ANDROID_V211_AUDIO_CONFIG rate=%i rateIndex=%i stereo=%i channels=%i latency=%i mode=0x%08x"),
		Rate,
		OutputRate,
		UseStereo,
		Channels,
		Latency,
		OutputMode );
#endif
	if (AudioInit( Rate, OutputMode, Latency ) == 0)
		return false;
	unguard;

	// Allocate voices.
	guard(AllocateVoices);
	verify(AllocateVoices(Channels));
	unguard;
	
	// Initialized!
	USound::Audio = this;
	UMusic::Audio = this;
	Initialized = 1;

	debugf( NAME_Init, TEXT("Generic audio subsystem initialized.") );
	return 1;
	unguard;
}

void UGenericAudioSubsystem::SetViewport( UViewport* InViewport )
{
	guard(UGenericAudioSubsystem::SetViewport);

	// Stop playing sounds.
	for( INT i=0; i<Channels; i++ )
		StopSound( i );

	// Remember the viewport.
	if( Viewport != InViewport )
	{
		if( Viewport )
		{
			// Unregister everything.
			for( TObjectIterator<UMusic> MusicIt; MusicIt; ++MusicIt )
				if( MusicIt->Handle )
					UnregisterMusic( *MusicIt );

			// Shut down.
			safecall(AudioStopOutput());
		}
		Viewport = InViewport;
		if( Viewport )
		{
#if PLATFORM_ANDROID
			debugf( NAME_Init, TEXT("UT99_ANDROID_V238_AUDIO_VIEWPORT_STATE useDigital=%i useCD=%i actor=%s actorSong=%s actorSection=%i transition=%i levelSong=%s levelSection=%i levelCd=%i"),
				UseDigitalMusic,
				UseCDMusic,
				Viewport->Actor ? Viewport->Actor->GetFullName() : TEXT("None"),
				Viewport->Actor && Viewport->Actor->Song ? Viewport->Actor->Song->GetFullName() : TEXT("None"),
				Viewport->Actor ? Viewport->Actor->SongSection : -1,
				Viewport->Actor ? Viewport->Actor->Transition : -1,
				Viewport->Actor && Viewport->Actor->Level && Viewport->Actor->Level->Song ? Viewport->Actor->Level->Song->GetFullName() : TEXT("None"),
				Viewport->Actor && Viewport->Actor->Level ? Viewport->Actor->Level->SongSection : -1,
				Viewport->Actor && Viewport->Actor->Level ? Viewport->Actor->Level->CdTrack : -1 );
#endif
			// Determine startup parameters.
			if( Viewport->Actor->Song && Viewport->Actor->Transition==MTRAN_None )
			{
				Viewport->Actor->Transition = MTRAN_Instant;
#if PLATFORM_ANDROID
				debugf( NAME_Init, TEXT("UT99_ANDROID_V236_AUDIO_MUSIC_REQUEST song=%s transition=instant note=generic_music_path_not_implemented"),
					Viewport->Actor->Song->GetFullName() );
#endif
			}

			// Start sound output.
			guard(AudioStartOutput);
			/*
			if (OutputRate == 0)
				OutputRate = 1;
			else if (OutputRate == 2)
				OutputRate = 3;
			else if ((OutputRate == 4) || (OutputRate == 6))
				OutputRate = 5;
			*/
			INT Rates[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000};
			INT Rate = Rates[OutputRate];
			INT Result = AudioStartOutput( Rate, OutputMode, Latency );
			if (Result == 0)
			{
				// Initialization failed.
				debugf( NAME_Init, TEXT("Failed to initialize audio subsystem.") );
				Viewport = NULL;
				return;
			}
			unguard;
			SetVolumes();			
		}
	}
	unguard;
}

UViewport* UGenericAudioSubsystem::GetViewport()
{
	guard(UGenericAudioSubsystem::GetViewport);
	return Viewport;
	unguard;
}

void UGenericAudioSubsystem::RegisterSound( USound* Sound )
{
	guard(UGenericAudioSubsystem::RegisterSound);
	checkSlow(Sound);
	if( !Sound->Handle )
	{
		// Set the handle to avoid reentrance.
		Sound->Handle = (void*)-1;

		// Load the data.
		Sound->Data.Load();
		debugf( NAME_DevSound, TEXT("Register sound: %s (%i)"), Sound->GetPathName(), Sound->Data.Num() );
		check(Sound->Data.Num()>0);

		// Register the sound.
		guard(LoadSample);
		MemChunk SoundChunk;
		SoundChunk.Data			= &Sound->Data(0);
		SoundChunk.DataLength	= Sound->Data.Num();
		SoundChunk.Position		= 0;
		Sound->Handle = LoadSample( &SoundChunk, Sound->GetFullName() );
		if( !Sound->Handle )
			appErrorf( TEXT("Invalid sound format in %s"), Sound->GetFullName() );
#if PLATFORM_ANDROID
		Sample* LoadedSample = (Sample*)Sound->Handle;
		if( LoadedSample && LoadedSample->SamplesPerSec <= 0 )
		{
			debugf( NAME_Init, TEXT("UT99_ANDROID_V294_AUDIO_SAMPLE_RATE_FIX stage=register sound=%s oldRate=%i audioRate=%i length=%i type=0x%04x"),
				Sound->GetFullName(),
				LoadedSample->SamplesPerSec,
				AudioRate,
				LoadedSample->Length,
				LoadedSample->Type );
			LoadedSample->SamplesPerSec = AudioRate ? AudioRate : 22050;
		}
#endif
		unguardf(( TEXT("(%i)"), Sound->Data.Num() ));

		// Unload the data.
		Sound->Data.Unload();
	}
	unguard;
}

void UGenericAudioSubsystem::RegisterMusic( UMusic* Music )
{
	guard(UGenericAudioSubsystem::RegisterMusic);
	if( !Music || !UseDigitalMusic )
		return;
	if( CurrentMusic == Music && IsMusicModulePlaying() )
		return;
	Music->Data.Load();
	INT MusicSize = Music->Data.Num();
	debugf( NAME_Init, TEXT("UT99_ANDROID_V237_MUSIC_REGISTER name=%s size=%i section=%i useDigital=%i"),
		Music->GetFullName(),
		MusicSize,
		Viewport && Viewport->Actor ? Viewport->Actor->SongSection : -1,
		UseDigitalMusic );
	if( MusicSize > 0 && LoadMusicModule( &Music->Data(0), MusicSize, Viewport && Viewport->Actor ? Viewport->Actor->SongSection : 0, Music->GetFullName() ) )
	{
		Music->Handle = (void*)1;
		CurrentMusic = Music;
	}
	Music->Data.Unload();
	unguard;
}

void UGenericAudioSubsystem::UnregisterSound( USound* Sound )
{
	guard(UGenericAudioSubsystem::UnregisterSound);
	check(Sound);
	if( Sound->Handle )
	{
		debugf( NAME_DevSound, TEXT("Unregister sound: %s"), Sound->GetFullName() );

		// Stop this sound.
		for( INT i=0; i<Channels; i++ )
			if ( PlayingSounds[i].Sound == Sound )
				StopSound( i );

		// Unload this sound.
		safecall( UnloadSample( (Sample*) Sound->Handle ) );
	}
	unguard;
}

void UGenericAudioSubsystem::UnregisterMusic( UMusic* Music )
{
	guard(UGenericAudioSubsystem::UnregisterMusic);
	if( Music && Music == CurrentMusic )
	{
		StopMusicModule();
		Music->Handle = NULL;
		CurrentMusic = NULL;
	}
	unguard;
}

UBOOL UGenericAudioSubsystem::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UGenericAudioSubsystem::Exec);
	const TCHAR* Str = Cmd;
	if( ParseCommand(&Str,TEXT("ASTAT")) )
	{
		if( ParseCommand(&Str,TEXT("Audio")) )
		{
			AudioStats ^= 1;
			return 1;
		}
		if( ParseCommand(&Str,TEXT("Detail")) )
		{
			DetailStats ^= 1;
			return 1;
		}
	}
	return 0;
	
	unguard;
}

UBOOL UGenericAudioSubsystem::PlaySound
(
	AActor*	Actor,
	INT		Id,
	USound*	Sound,
	FVector	Location,
	FLOAT	Volume,
	FLOAT	Radius,
	FLOAT	Pitch
)
{
	guard(UGenericAudioSubsystem::PlaySound);
	static INT PlaySoundTraceCount = 0;
	if( PlaySoundTraceCount++ < 16 )
		debugf( NAME_Init, TEXT("UT99_ANDROID_V214_AUDIO_PLAYSOUND_ENTRY viewport=0x%08x viewportActor=0x%08x actor=0x%08x id=%i sound=0x%08x radius=%f pitch=%f"),
			(DWORD)(QWORD)Viewport,
			Viewport ? (DWORD)(QWORD)Viewport->Actor : 0,
			(DWORD)(QWORD)Actor,
			Id,
			(DWORD)(QWORD)Sound,
			Radius,
			Pitch );
	if( !Viewport || !IsKnownAudioActorPointer(Viewport->Actor) || !Sound || Sound==(USound*)-1 )
		return 0;
	if( Actor && !IsKnownAudioActorPointer(Actor) )
	{
		debugf( NAME_Init, TEXT("UT99_ANDROID_V213_AUDIO_PLAYSOUND_BAD_ACTOR actor=0x%08x id=%i sound=0x%08x"),
			(DWORD)(QWORD)Actor,
			Id,
			(DWORD)(QWORD)Sound );
		Actor = NULL;
	}
	if( !IsKnownAudioSoundPointer(Sound) )
	{
		debugf( NAME_Init, TEXT("UT99_ANDROID_V213_AUDIO_PLAYSOUND_BAD_SOUND actor=0x%08x id=%i sound=0x%08x"),
			(DWORD)(QWORD)Actor,
			Id,
			(DWORD)(QWORD)Sound );
		return 0;
	}
	if( Radius <= 1.f )
		Radius = 1600.f;
	if( Pitch <= 0.01f )
	{
		debugf( NAME_Init, TEXT("UT99_ANDROID_V217_AUDIO_PITCH_FIX id=%i oldPitch=%f actor=0x%08x sound=0x%08x"),
			Id,
			Pitch,
			(DWORD)(QWORD)Actor,
			(DWORD)(QWORD)Sound );
		Pitch = 1.0f;
	}

	// Allocate a new slot if requested.
	if( (Id&14)==2*SLOT_None )
		Id = 16 * --FreeSlot;

	// Compute priority.
	FLOAT Priority = SoundPriority( Viewport, Location, Volume, Radius );

	// If already playing, stop it.
	INT   Index        = -1;
	FLOAT BestPriority = Priority;
	for( INT i=0; i<Channels; i++ )
	{
		FPlayingSound& Playing = PlayingSounds[i];
		if( (Playing.Id&~1)==(Id&~1) )
		{
			// Skip if not interruptable.
			if( Id&1 )
				return 0;

			// Stop the sound.
			Index = i;
			break;
		}
		else if( Playing.Priority<=BestPriority )
		{
			Index = i;
			BestPriority = Playing.Priority;
		}
	}

	// If no sound, or its priority is overruled, stop it.
	if( Index==-1 )
		return 0;

	// Put the sound on the play-list.
	StopSound( Index );
	if( Sound!=(USound*)-1 )
	{
		PlayingSounds[Index] = FPlayingSound( Actor, Id, Sound, Location, Volume, Radius, Pitch, Priority );
#if PLATFORM_ANDROID
		static INT AndroidAudioAcceptLogs = 0;
		if( AndroidAudioAcceptLogs < 32 || (AndroidAudioAcceptLogs % 120) == 0 )
			debugf( NAME_Init, TEXT("UT99_ANDROID_V236_AUDIO_ACCEPT count=%i index=%i id=%i sound=%s actor=%s priority=%f volume=%f radius=%f pitch=%f"),
				AndroidAudioAcceptLogs,
				Index,
				Id,
				Sound->GetFullName(),
				Actor ? Actor->GetFullName() : TEXT("None"),
				Priority,
				Volume,
				Radius,
				Pitch );
		AndroidAudioAcceptLogs++;
#endif
	}
	return 1;

	unguard;
}

void UGenericAudioSubsystem::NoteDestroy( AActor* Actor )
{
	guard(UGenericAudioSubsystem::NoteDestroy);
	check(Actor);
	check(Actor->IsValid());

	// Stop referencing actor.
	for( INT i=0; i<Channels; i++ )
	{
		if( PlayingSounds[i].Actor==Actor )
		{
			if( (PlayingSounds[i].Id&14)==SLOT_Ambient*2 )
			{
				// Stop ambient sound when actor dies.
				StopSound( i );
			}
			else
			{
				// Unbind regular sounds from actors.
				PlayingSounds[i].Actor = NULL;
			}
		}
	}

	unguard;
}

void UGenericAudioSubsystem::RenderAudioGeometry( FSceneNode* Frame )
{
	guard(UGenericAudioSubsystem::RenderAudioGeometry);

	unguard;
}

void UGenericAudioSubsystem::Update( FPointRegion Region, FCoords& Coords )
{
	guard(UGenericAudioSubsystem::Update);
	if( !Viewport || !IsKnownAudioActorPointer(Viewport->Actor) )
		return;
#if PLATFORM_ANDROID
	static INT AndroidAudioUpdateLogs = 0;
	INT AndroidAudioActiveIds = 0;
	INT AndroidAudioActiveChannels = 0;
	INT AndroidAudioStarted = 0;
	INT AndroidAmbientActors = 0;
	INT AndroidAmbientValid = 0;
	INT AndroidAmbientInRange = 0;
	INT AndroidAmbientAlready = 0;
	INT AndroidAmbientRequested = 0;
	INT AndroidAmbientAccepted = 0;
	INT AndroidAmbientNull = 0;
	INT AndroidAmbientBadActor = 0;
	INT AndroidAmbientBadSound = 0;
	INT AndroidAmbientOutOfRange = 0;
	INT AndroidAmbientZeroVolume = 0;
#endif
	
	// Lock to sync sound.
#if PLATFORM_ANDROID
	if( !ATryLock )
	{
		static INT AndroidAudioTryLockSkips = 0;
#if UT99_ANDROID_AUDIO_FRAME_TRACE
		if( AndroidAudioTryLockSkips < 16 || (AndroidAudioTryLockSkips % 120) == 0 )
			debugf( NAME_Init, TEXT("UT99_ANDROID_V254_AUDIO_UPDATE_TRYLOCK_SKIP count=%i"), AndroidAudioTryLockSkips );
#endif
		AndroidAudioTryLockSkips++;
		return;
	}
#else
	ALock;
#endif
	
	// Time passes...
	DOUBLE DeltaTime = appSeconds() - LastTime;
	LastTime += DeltaTime;
	DeltaTime = Clamp<DOUBLE>( DeltaTime, 0.0, 1.0 );

	AActor* ViewActor = (Viewport->Actor->ViewTarget && IsKnownAudioActorPointer(Viewport->Actor->ViewTarget)) ? Viewport->Actor->ViewTarget : Viewport->Actor;
	ULevel* Level = Viewport->Actor->GetLevel();
	ALevelInfo* LevelInfo = Viewport->Actor->Level;
	if( !IsKnownAudioActorPointer(ViewActor) || !IsKnownAudioLevelPointer(Level) || !IsKnownAudioActorPointer(LevelInfo) )
	{
		AUnlock;
		return;
	}

#if PLATFORM_ANDROID
#if UT99_ANDROID_AUDIO_FRAME_TRACE
	static INT AndroidMusicStateLogs = 0;
	if( AndroidMusicStateLogs < 32 || (AndroidMusicStateLogs % 120) == 0 )
		debugf( NAME_Init, TEXT("UT99_ANDROID_V238_AUDIO_MUSIC_STATE count=%i useDigital=%i actorSong=%s actorSection=%i transition=%i levelSong=%s levelSection=%i levelCd=%i current=%s"),
			AndroidMusicStateLogs,
			UseDigitalMusic,
			Viewport->Actor->Song ? Viewport->Actor->Song->GetFullName() : TEXT("None"),
			Viewport->Actor->SongSection,
			Viewport->Actor->Transition,
			LevelInfo->Song ? LevelInfo->Song->GetFullName() : TEXT("None"),
			LevelInfo->SongSection,
			LevelInfo->CdTrack,
			CurrentMusic ? CurrentMusic->GetFullName() : TEXT("None") );
	AndroidMusicStateLogs++;
#endif
	if( UseDigitalMusic && !Viewport->Actor->Song && LevelInfo->Song )
	{
		Viewport->Actor->Song = LevelInfo->Song;
		Viewport->Actor->SongSection = LevelInfo->SongSection;
		Viewport->Actor->CdTrack = LevelInfo->CdTrack;
		Viewport->Actor->Transition = MTRAN_Fade;
		debugf( NAME_Init, TEXT("UT99_ANDROID_V237_MUSIC_RESTORE_FROM_LEVEL song=%s section=%i cd=%i"),
			LevelInfo->Song ? LevelInfo->Song->GetFullName() : TEXT("None"),
			LevelInfo->SongSection,
			LevelInfo->CdTrack );
	}
	if( UseDigitalMusic && Viewport->Actor->Song && (Viewport->Actor->Transition != MTRAN_None || Viewport->Actor->Song != CurrentMusic) )
	{
		debugf( NAME_Init, TEXT("UT99_ANDROID_V237_MUSIC_TRANSITION song=%s section=%i transition=%i current=%s"),
			Viewport->Actor->Song->GetFullName(),
			Viewport->Actor->SongSection,
			Viewport->Actor->Transition,
			CurrentMusic ? CurrentMusic->GetFullName() : TEXT("None") );
		RegisterMusic( Viewport->Actor->Song );
		Viewport->Actor->Transition = MTRAN_None;
	}
#endif

	// See if any new ambient sounds need to be started.
	UBOOL Realtime = Viewport->IsRealtime() && LevelInfo->Pauser==TEXT("");
	if( Realtime )
	{
		guard(StartAmbience);
		for( INT i=0; i<Level->Actors.Num(); i++ )
		{
			AActor* Actor = Level->Actors(i);
			if( Actor && !IsKnownAudioActorPointer(Actor) )
			{
				debugf( NAME_Init, TEXT("UT99_ANDROID_V212_AUDIO_SKIP_BAD_ACTOR index=%i actor=0x%08x"), i, (DWORD)(QWORD)Actor );
#if PLATFORM_ANDROID
				AndroidAmbientBadActor++;
#endif
				continue;
			}
#if PLATFORM_ANDROID
			if( !Actor )
			{
				AndroidAmbientNull++;
				continue;
			}
			if( Actor->AmbientSound )
				AndroidAmbientActors++;
			else
				continue;
			const UBOOL bKnownAmbient = IsKnownAudioSoundPointer(Actor->AmbientSound);
			if( bKnownAmbient )
				AndroidAmbientValid++;
			else
			{
				AndroidAmbientBadSound++;
				continue;
			}
			const FLOAT AmbientRadius = Actor->WorldSoundRadius();
			const FLOAT AmbientVolume = AmbientFactor*Actor->SoundVolume/255.0;
			if( AmbientVolume <= 0.f )
				AndroidAmbientZeroVolume++;
			if( FDistSquared(ViewActor->Location,Actor->Location) > Square(AmbientRadius) )
			{
				AndroidAmbientOutOfRange++;
				continue;
			}
			AndroidAmbientInRange++;
			INT Id = Actor->GetIndex()*16+SLOT_Ambient*2;
			INT j;
			for( j=0; j<Channels; j++ )
				if( PlayingSounds[j].Id==Id )
					break;
			if( j==Channels )
			{
				AndroidAmbientRequested++;
				if( PlaySound( Actor, Id, Actor->AmbientSound, Actor->Location, AmbientVolume, AmbientRadius, Actor->SoundPitch/64.0 ) )
					AndroidAmbientAccepted++;
			}
			else
				AndroidAmbientAlready++;
#else
			if
			(	Actor
			&&	IsKnownAudioSoundPointer(Actor->AmbientSound)
			&&	FDistSquared(ViewActor->Location,Actor->Location)<=Square(Actor->WorldSoundRadius()) )
			{
				INT Id = Actor->GetIndex()*16+SLOT_Ambient*2;
				INT j;
				for( j=0; j<Channels; j++ )
					if( PlayingSounds[j].Id==Id )
						break;
				if( j==Channels )
					PlaySound( Actor, Id, Actor->AmbientSound, Actor->Location, AmbientFactor*Actor->SoundVolume/255.0, Actor->WorldSoundRadius(), Actor->SoundPitch/64.0 );
			}
#endif
		}
		unguard;
	}

	// Update all playing ambient sounds.
	guard(UpdateAmbience);
	for( INT i=0; i<Channels; i++ )
	{
		FPlayingSound& Playing = PlayingSounds[i];
		if( (Playing.Id&14)==SLOT_Ambient*2 )
		{
			if( !IsKnownAudioActorPointer(Playing.Actor) || !IsKnownAudioSoundPointer(Playing.Sound) )
			{
				debugf( NAME_Init, TEXT("UT99_ANDROID_V212_AUDIO_STOP_BAD_AMBIENT channel=%i actor=0x%08x sound=0x%08x"), i, (DWORD)(QWORD)Playing.Actor, (DWORD)(QWORD)Playing.Sound );
				StopSound( i );
				continue;
			}
			if
			(	FDistSquared(ViewActor->Location,Playing.Actor->Location)>Square(Playing.Actor->WorldSoundRadius())
			||	Playing.Actor->AmbientSound!=Playing.Sound 
			||  !Realtime )
			{
				// Ambient sound went out of range.
				StopSound( i );
			}
			else
			{
				// Update basic sound properties.
				FLOAT Brightness = 2.0 * (AmbientFactor*Playing.Actor->SoundVolume/255.0);
				if( Playing.Actor->LightType!=LT_None )
				{
					FPlane Color;
					Brightness *= Playing.Actor->LightBrightness/255.0;
//					Viewport->GetOuterUClient()->Engine->Render->GlobalLighting( (Viewport->Actor->ShowFlags & SHOW_PlayerCtrl)!=0, Playing.Actor, Brightness, Color );
				}
				Playing.Volume = Brightness;
				Playing.Radius = Playing.Actor->WorldSoundRadius();
				Playing.Pitch  = Playing.Actor->SoundPitch/64.0;
			}
		}
	}
	unguard;

	// Update all active sounds.
	guard(UpdateSounds);
	for( INT Index=0; Index<Channels; Index++ )
	{
		FPlayingSound& Playing = PlayingSounds[Index];
		if( Playing.Actor && !IsKnownAudioActorPointer(Playing.Actor) )
		{
			debugf( NAME_Init, TEXT("UT99_ANDROID_V212_AUDIO_CLEAR_BAD_PLAYING channel=%i actor=0x%08x"), Index, (DWORD)(QWORD)Playing.Actor );
			Playing.Actor = NULL;
		}
		if( PlayingSounds[Index].Id==0 )
		{
			// Sound is not playing.
			continue;
		}
#if PLATFORM_ANDROID
		AndroidAudioActiveIds++;
		if( Playing.Channel )
			AndroidAudioActiveChannels++;
#endif
		if( Playing.Channel && SampleFinished(Playing.Channel) )
		{
			// Sound is finished.
			StopSound( Index );
		}
		else
		{
			if( !IsKnownAudioSoundPointer(Playing.Sound) )
			{
				debugf( NAME_Init, TEXT("UT99_ANDROID_V212_AUDIO_STOP_BAD_SOUND channel=%i sound=0x%08x"), Index, (DWORD)(QWORD)Playing.Sound );
				StopSound( Index );
				continue;
			}

			// Update positioning from actor, if available.
			if( Playing.Actor )
				Playing.Location = Playing.Actor->Location;void UpdateSample( Voice* InVoice, INT Freq, INT Volume, INT Panning );

			// Update the priority.
			Playing.Priority = SoundPriority( Viewport, Playing.Location, Playing.Volume, Playing.Radius );

			// Compute the spatialization.
			FVector Location = Playing.Location.TransformPointBy( Coords );
			FLOAT   PanAngle = appAtan2(Location.X, Abs(Location.Z));

			// Despatialize sounds when you get real close to them.
			FLOAT CenterDist  = 0.1*Playing.Radius;
			FLOAT Size        = Location.Size();
			if( Location.SizeSquared() < Square(CenterDist) )
				PanAngle *= Size / CenterDist;

			// Compute panning and volume.
			INT     SoundPan      = Clamp<INT>( (INT)(AUDIO_MAXPAN/2 + PanAngle*AUDIO_MAXPAN*7/8/PI), 0, AUDIO_MAXPAN );
			FLOAT   Attenuation = Clamp(1.0-Size/Playing.Radius,0.0,1.0);
			INT     SoundVolume   = Clamp<INT>( (INT)(AUDIO_MAXVOLUME * Playing.Volume * Attenuation * EFFECT_FACTOR), 0, AUDIO_MAXVOLUME );
			if( ReverseStereo )
				SoundPan = AUDIO_MAXPAN - SoundPan;
			if( Location.Z<0.0 && UseSurround )
				SoundPan = AUDIO_MIDPAN | AUDIO_SURPAN;

			// Compute doppler shifting (doesn't account for player's velocity).
			FLOAT Doppler=1.0;
			if( Playing.Actor )
			{
				FLOAT V = (Playing.Actor->Velocity/*-ViewActor->Velocity*/) | (Playing.Actor->Location - ViewActor->Location).SafeNormal();
				Doppler = Clamp( 1.0 - V/DopplerSpeed, 0.5, 2.0 );
			}

			// Update the sound.
			Sample* Sample = GetSound(Playing.Sound);
#if PLATFORM_ANDROID
			if( Playing.Pitch <= 0.01f )
			{
				debugf( NAME_Init, TEXT("UT99_ANDROID_V295_AUDIO_PLAYBACK_FIX stage=pitch id=%i sound=%s oldPitch=%f"),
					Playing.Id,
					Playing.Sound ? Playing.Sound->GetFullName() : TEXT("None"),
					Playing.Pitch );
				Playing.Pitch = 1.f;
			}
			if( Sample && Sample->SamplesPerSec <= 0 )
			{
				debugf( NAME_Init, TEXT("UT99_ANDROID_V294_AUDIO_SAMPLE_RATE_FIX stage=update id=%i sound=%s oldRate=%i audioRate=%i length=%i type=0x%04x"),
					Playing.Id,
					Playing.Sound ? Playing.Sound->GetFullName() : TEXT("None"),
					Sample->SamplesPerSec,
					AudioRate,
					Sample->Length,
					Sample->Type );
				Sample->SamplesPerSec = AudioRate ? AudioRate : 22050;
			}
			INT AndroidPlaybackFreq = Sample ? (INT)(Sample->SamplesPerSec * Playing.Pitch * Doppler) : 0;
			if( Sample && AndroidPlaybackFreq <= 0 )
			{
				debugf( NAME_Init, TEXT("UT99_ANDROID_V295_AUDIO_PLAYBACK_FIX stage=freq id=%i sound=%s rate=%i pitch=%f doppler=%f audioRate=%i length=%i type=0x%04x"),
					Playing.Id,
					Playing.Sound ? Playing.Sound->GetFullName() : TEXT("None"),
					Sample->SamplesPerSec,
					Playing.Pitch,
					Doppler,
					AudioRate,
					Sample->Length,
					Sample->Type );
				if( Sample->SamplesPerSec <= 0 )
					Sample->SamplesPerSec = AudioRate ? AudioRate : 22050;
				AndroidPlaybackFreq = Sample->SamplesPerSec > 0 ? Sample->SamplesPerSec : 22050;
			}
			if( Sample && (Playing.Id&14)==SLOT_Ambient*2 && !(Sample->Type & SAMPLE_LOOPED) )
			{
				debugf( NAME_Init, TEXT("UT99_ANDROID_V297_AUDIO_FORCE_AMBIENT_LOOP id=%i sound=%s type=0x%04x length=%i loop=%i,%i"),
					Playing.Id,
					Playing.Sound ? Playing.Sound->GetFullName() : TEXT("None"),
					Sample->Type,
					Sample->Length,
					Sample->LoopStart,
					Sample->LoopEnd );
				Sample->Type |= SAMPLE_LOOPED;
				if( Sample->LoopEnd <= Sample->LoopStart || Sample->LoopEnd > Sample->Length )
				{
					Sample->LoopStart = 0;
					Sample->LoopEnd = Sample->Length;
				}
			}
#endif
			FVector Z(0,0,0);
			FVector L(Location.X/400.0,Location.Y/400.0,Location.Z/400.0);

			if( Playing.Channel )
			{
				// Update an existing sound.
				guard(UpdateSample);
				UpdateSample
				( 
					Playing.Channel,
#if PLATFORM_ANDROID
					AndroidPlaybackFreq,
#else
					(INT) (Sample->SamplesPerSec * Playing.Pitch * Doppler),
#endif
					SoundVolume,
					SoundPan
				);
				Playing.Channel->BasePanning = SoundPan;
				unguard;
			}
			else
			{
				// Start this new sound.
				guard(StartSample);
				if( !Playing.Channel ) 
				{
					Playing.Channel = StartSample
						( Index+1, Sample, 
#if PLATFORM_ANDROID
						  AndroidPlaybackFreq,
#else
						  (INT) (Sample->SamplesPerSec * Playing.Pitch * Doppler), 
#endif
						  SoundVolume, SoundPan );
#if PLATFORM_ANDROID
					AndroidAudioStarted++;
					static INT AndroidAudioStartLogs = 0;
					if( AndroidAudioStartLogs < 32 || (AndroidAudioStartLogs % 120) == 0 )
						debugf( NAME_Init, TEXT("UT99_ANDROID_V236_AUDIO_START_SAMPLE count=%i index=%i id=%i sample=%p freq=%i volume=%i pan=%i channel=%p sound=%s"),
							AndroidAudioStartLogs,
							Index,
							Playing.Id,
							Sample,
							AndroidPlaybackFreq,
							SoundVolume,
							SoundPan,
							Playing.Channel,
							Playing.Sound ? Playing.Sound->GetFullName() : TEXT("None") );
					AndroidAudioStartLogs++;
#endif
				}
				check(Playing.Channel);
				unguard;
			}
		}
	}
	unguard;
#if PLATFORM_ANDROID
	if( AndroidAudioUpdateLogs < 32 || (AndroidAudioUpdateLogs % 120) == 0 )
		debugf( NAME_Init, TEXT("UT99_ANDROID_V293_AUDIO_AMBIENT_SCAN count=%i realtime=%i actors=%i valid=%i inRange=%i requested=%i accepted=%i already=%i null=%i badActor=%i badSound=%i outRange=%i zeroVol=%i factor=%f viewActor=%s levelActors=%i"),
			AndroidAudioUpdateLogs,
			Realtime,
			AndroidAmbientActors,
			AndroidAmbientValid,
			AndroidAmbientInRange,
			AndroidAmbientRequested,
			AndroidAmbientAccepted,
			AndroidAmbientAlready,
			AndroidAmbientNull,
			AndroidAmbientBadActor,
			AndroidAmbientBadSound,
			AndroidAmbientOutOfRange,
			AndroidAmbientZeroVolume,
			AmbientFactor,
			ViewActor ? ViewActor->GetFullName() : TEXT("None"),
			Level ? Level->Actors.Num() : -1 );
#if UT99_ANDROID_AUDIO_FRAME_TRACE
	if( AndroidAudioUpdateLogs < 32 || (AndroidAudioUpdateLogs % 120) == 0 )
		debugf( NAME_Init, TEXT("UT99_ANDROID_V236_AUDIO_UPDATE count=%i realtime=%i activeIds=%i activeChannels=%i started=%i viewActor=%s levelActors=%i"),
			AndroidAudioUpdateLogs,
			Realtime,
			AndroidAudioActiveIds,
			AndroidAudioActiveChannels,
			AndroidAudioStarted,
			ViewActor ? ViewActor->GetFullName() : TEXT("None"),
			Level ? Level->Actors.Num() : -1 );
	AndroidAudioUpdateLogs++;
#endif
#endif

	// Unlock.
	AUnlock;
	unguard;
}

void UGenericAudioSubsystem::PostRender( FSceneNode* Frame )
{
	guard(UGenericAudioSubsystem::PostRender);
	Frame->Viewport->Canvas->Color = FColor(255,255,255);
	if( AudioStats )
	{
		Frame->Viewport->Canvas->CurX=0;
		Frame->Viewport->Canvas->CurY=16;
		Frame->Viewport->Canvas->WrappedPrintf
		(
			Frame->Viewport->Canvas->SmallFont,
			0, TEXT("GenericAudioSubsystem Statistics")
		);
		for (INT i=0; i<Channels; i++)
		{
			if (PlayingSounds[i].Channel)
			{
				INT Factor;
				if (DetailStats)
					Factor = 16;
				else
					Factor = 8;
					
				// Current Sound.
				Frame->Viewport->Canvas->CurX=10;
				Frame->Viewport->Canvas->CurY=24 + Factor*i;
				Frame->Viewport->Canvas->WrappedPrintf
				( Frame->Viewport->Canvas->SmallFont, 0, TEXT("Channel %2i: %s"),
					i, PlayingSounds[i].Sound->GetFullName() );

				if (DetailStats)
				{
					// Play meter.
					VoiceStats CurrentStats;
					GetVoiceStats( &CurrentStats, PlayingSounds[i].Channel );
					Frame->Viewport->Canvas->CurX=10;
					Frame->Viewport->Canvas->CurY=32 + Factor*i;
					Frame->Viewport->Canvas->WrappedPrintf
					( Frame->Viewport->Canvas->SmallFont, 0, TEXT("  [%s] %05.1f\% Vol: %05.2f"),
						*CurrentStats.CompletionString, CurrentStats.Completion*100, PlayingSounds[i].Volume );
				}
			} else {
				INT Factor;
				if (DetailStats)
					Factor = 16;
				else
					Factor = 8;
					
				Frame->Viewport->Canvas->CurX=10;
				Frame->Viewport->Canvas->CurY=24 + Factor*i;
				if (i >= 10)
					Frame->Viewport->Canvas->WrappedPrintf
					( Frame->Viewport->Canvas->SmallFont, 0, TEXT("Channel %i:  None"),
						i );
				else
					Frame->Viewport->Canvas->WrappedPrintf
					( Frame->Viewport->Canvas->SmallFont, 0, TEXT("Channel %i: None"),
						i );

				if (DetailStats)
				{
					// Play meter.
					Frame->Viewport->Canvas->CurX=10;
					Frame->Viewport->Canvas->CurY=32 + Factor*i;
					Frame->Viewport->Canvas->WrappedPrintf
					( Frame->Viewport->Canvas->SmallFont, 0, TEXT("  [----------]") );
				}
			}
		}
	}
	unguard;
}

/*------------------------------------------------------------------------------------
	Internals.
------------------------------------------------------------------------------------*/

void UGenericAudioSubsystem::SetVolumes()
{
	guard(UGenericAudioSubsystem::SetVolumes);

	// Normalize the volumes.
	FLOAT NormSoundVolume = SoundVolume/255.0;
	FLOAT NormMusicVolume = Clamp(MusicVolume/255.0,0.0,1.0);

	// Set music and effects volumes.
	verify( SetSampleVolume( 127*NormSoundVolume ) );
	if( UseDigitalMusic )
		verify( SetMusicVolume( 127*NormMusicVolume*Max(MusicFade,0.f) ) );
	if( UseCDMusic )
		SetCDAudioVolume( 127*NormMusicVolume*Max(MusicFade,0.f) );

	unguard;
}

void UGenericAudioSubsystem::StopSound( INT Index )
{
	guard(UGenericAudioSubsystem::StopSound);
	FPlayingSound& Playing = PlayingSounds[Index];

	if( Playing.Channel )
	{
		guard(StopSample);
		StopSample( Playing.Channel );
		unguard;
	}
	PlayingSounds[Index] = FPlayingSound();

	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
