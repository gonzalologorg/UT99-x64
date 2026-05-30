/*=============================================================================
	UnGame.cpp: Unreal game engine.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRender.h"
#include "UnNet.h"

/*-----------------------------------------------------------------------------
	Object class implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UGameEngine);

static void MigrateNativePropertyDefaults( UClass* Class, UProperty* Property, INT OldOffset, INT NewOffset )
{
	guard(MigrateNativePropertyDefaults);
	if( !Class || !Property || OldOffset==NewOffset )
		return;
	INT Size = Property->GetSize();
	for( TObjectIterator<UClass> It; It; ++It )
	{
		UClass* TestClass = *It;
		if( !TestClass->IsChildOf( Class ) || !TestClass->Defaults.Num() )
			continue;
		if( OldOffset<0 || NewOffset<0 || OldOffset+Size>TestClass->Defaults.Num() || NewOffset+Size>TestClass->Defaults.Num() )
			continue;
		appMemcpy( &TestClass->Defaults(NewOffset), &TestClass->Defaults(OldOffset), Size );
		appMemzero( &TestClass->Defaults(OldOffset), Size );
		debugf( NAME_Log, TEXT("UT99_ANDROID_V180_DEFAULT_OFFSET_MIGRATE class=%s property=%s old=%i native=%i size=%i"),
			TestClass->GetFullName(),
			Property->GetFullName(),
			OldOffset,
			NewOffset,
			Size );
	}
	unguard;
}

static void MigrateNativeDefaultBytes( UClass* Class, const TCHAR* Label, INT OldOffset, INT NewOffset, INT Size )
{
	guard(MigrateNativeDefaultBytes);
	if( !Class || OldOffset==NewOffset )
		return;
	for( TObjectIterator<UClass> It; It; ++It )
	{
		UClass* TestClass = *It;
		if( !TestClass->IsChildOf( Class ) || !TestClass->Defaults.Num() )
			continue;
		if( OldOffset<0 || NewOffset<0 || OldOffset+Size>TestClass->Defaults.Num() || NewOffset+Size>TestClass->Defaults.Num() )
			continue;
		appMemcpy( &TestClass->Defaults(NewOffset), &TestClass->Defaults(OldOffset), Size );
		appMemzero( &TestClass->Defaults(OldOffset), Size );
		debugf( NAME_Log, TEXT("UT99_ANDROID_V181_DEFAULT_BLOCK_MIGRATE class=%s block=%s old=%i native=%i size=%i"),
			TestClass->GetFullName(),
			Label,
			OldOffset,
			NewOffset,
			Size );
	}
	unguard;
}

static UProperty* FindNativeProperty( UClass* Class, const TCHAR* Name )
{
	guard(FindNativeProperty);
	if( !Class )
		return NULL;
	for( TFieldIterator<UProperty> It(Class); It; ++It )
		if( appStricmp( It->GetName(), Name )==0 )
			return *It;
	return NULL;
	unguard;
}

static void FixupNativeBoolBlockOffset( UClass* Class, const TCHAR* Label, const TCHAR** Names, INT Count, INT Offset );

static void FixupNativePropertyOffset( UClass* Class, const TCHAR* Name, INT Offset )
{
	guard(FixupNativePropertyOffset);
	if( !Class )
		return;
	for( TFieldIterator<UProperty> It(Class); It; ++It )
	{
		if( appStricmp( It->GetName(), Name )==0 )
		{
			if( It->Offset != Offset )
			{
				debugf( NAME_Warning, TEXT("UT99_ANDROID_V177_NATIVE_OFFSET_FIX class=%s property=%s script=%i native=%i"), Class->GetFullName(), Name, It->Offset, Offset );
				MigrateNativePropertyDefaults( Class, *It, It->Offset, Offset );
				It->Offset = Offset;
			}
			return;
		}
	}
	debugf( NAME_Warning, TEXT("UT99_ANDROID_V177_NATIVE_OFFSET_MISSING class=%s property=%s native=%i"), Class->GetFullName(), Name, Offset );
	unguard;
}

static void FixupNativeStructMemberOffset( UStruct* Struct, const TCHAR* Name, INT Offset )
{
	guard(FixupNativeStructMemberOffset);
	if( !Struct )
		return;
	for( TFieldIterator<UProperty> It(Struct); It; ++It )
	{
		if( appStricmp( It->GetName(), Name )==0 )
		{
			if( It->Offset != Offset )
			{
				debugf( NAME_Warning, TEXT("UT99_ANDROID_V204_STRUCT_OFFSET_FIX struct=%s property=%s script=%i native=%i"),
					Struct->GetFullName(),
					Name,
					It->Offset,
					Offset );
				It->Offset = Offset;
			}
			return;
		}
	}
	debugf( NAME_Warning, TEXT("UT99_ANDROID_V204_STRUCT_OFFSET_MISSING struct=%s property=%s native=%i"),
		Struct->GetFullName(),
		Name,
		Offset );
	unguard;
}

static void FixupPointRegionStruct()
{
	guard(FixupPointRegionStruct);
	UProperty* RegionProperty = FindNativeProperty( AActor::StaticClass(), TEXT("Region") );
	UStructProperty* StructProperty = Cast<UStructProperty>( RegionProperty );
	UStruct* PointRegionStruct = StructProperty ? StructProperty->Struct : NULL;
	if( !PointRegionStruct )
		return;
	if( PointRegionStruct->GetPropertiesSize() != sizeof(FPointRegion) )
	{
		debugf( NAME_Warning, TEXT("UT99_ANDROID_V204_STRUCT_SIZE_FIX struct=%s script=%i native=%i"),
			PointRegionStruct->GetFullName(),
			PointRegionStruct->GetPropertiesSize(),
			sizeof(FPointRegion) );
		PointRegionStruct->SetPropertiesSize( sizeof(FPointRegion) );
	}
	FixupNativeStructMemberOffset( PointRegionStruct, TEXT("Zone"),       STRUCT_OFFSET(FPointRegion,Zone) );
	FixupNativeStructMemberOffset( PointRegionStruct, TEXT("iLeaf"),      STRUCT_OFFSET(FPointRegion,iLeaf) );
	FixupNativeStructMemberOffset( PointRegionStruct, TEXT("ZoneNumber"), STRUCT_OFFSET(FPointRegion,ZoneNumber) );
	for( TObjectIterator<UStructProperty> It; It; ++It )
	{
		if( It->Struct == PointRegionStruct && It->ElementSize != sizeof(FPointRegion) )
		{
			debugf( NAME_Warning, TEXT("UT99_ANDROID_V204_STRUCT_PROPERTY_SIZE_FIX property=%s script=%i native=%i"),
				It->GetFullName(),
				It->ElementSize,
				sizeof(FPointRegion) );
			It->ElementSize = sizeof(FPointRegion);
		}
	}
	unguard;
}

static void FixupBitmapTextureOffsets()
{
	guard(FixupBitmapTextureOffsets);
	FixupNativePropertyOffset( UBitmap::StaticClass(), TEXT("Format"), STRUCT_OFFSET(UBitmap,Format) );
	FixupNativePropertyOffset( UBitmap::StaticClass(), TEXT("Palette"), STRUCT_OFFSET(UBitmap,Palette) );
	FixupNativePropertyOffset( UBitmap::StaticClass(), TEXT("UBits"), STRUCT_OFFSET(UBitmap,UBits) );
	FixupNativePropertyOffset( UBitmap::StaticClass(), TEXT("VBits"), STRUCT_OFFSET(UBitmap,VBits) );
	FixupNativePropertyOffset( UBitmap::StaticClass(), TEXT("USize"), STRUCT_OFFSET(UBitmap,USize) );
	FixupNativePropertyOffset( UBitmap::StaticClass(), TEXT("VSize"), STRUCT_OFFSET(UBitmap,VSize) );
	FixupNativePropertyOffset( UBitmap::StaticClass(), TEXT("UClamp"), STRUCT_OFFSET(UBitmap,UClamp) );
	FixupNativePropertyOffset( UBitmap::StaticClass(), TEXT("VClamp"), STRUCT_OFFSET(UBitmap,VClamp) );
	FixupNativePropertyOffset( UBitmap::StaticClass(), TEXT("MipZero"), STRUCT_OFFSET(UBitmap,MipZero) );

	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("BumpMap"), STRUCT_OFFSET(UTexture,BumpMap) );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("DetailTexture"), STRUCT_OFFSET(UTexture,DetailTexture) );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("MacroTexture"), STRUCT_OFFSET(UTexture,MacroTexture) );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("Diffuse"), STRUCT_OFFSET(UTexture,Diffuse) );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("Specular"), STRUCT_OFFSET(UTexture,Specular) );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("Alpha"), STRUCT_OFFSET(UTexture,Alpha) );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("Scale"), STRUCT_OFFSET(UTexture,Scale) );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("Friction"), STRUCT_OFFSET(UTexture,Friction) );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("MipMult"), STRUCT_OFFSET(UTexture,MipMult) );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("FootstepSound"), STRUCT_OFFSET(UTexture,FootstepSound) );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("HitSound"), STRUCT_OFFSET(UTexture,HitSound) );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("PolyFlags"), STRUCT_OFFSET(UTexture,PolyFlags) );
	const INT TextureBoolOffset = STRUCT_OFFSET(UTexture,LODSet) - sizeof(BITFIELD);
	static const TCHAR* TextureBools[] =
	{
		TEXT("bHighColorQuality"), TEXT("bHighTextureQuality"), TEXT("bRealtime"),
		TEXT("bParametric"), TEXT("bRealtimeChanged"), TEXT("bHasComp")
	};
	FixupNativeBoolBlockOffset( UTexture::StaticClass(), TEXT("TextureFlags"), TextureBools, ARRAY_COUNT(TextureBools), TextureBoolOffset );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("LODSet"), STRUCT_OFFSET(UTexture,LODSet) );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("AnimNext"), STRUCT_OFFSET(UTexture,AnimNext) );
	FixupNativePropertyOffset( UTexture::StaticClass(), TEXT("AnimCur"), STRUCT_OFFSET(UTexture,AnimCur) );
	unguard;
}

static void FixupPlayerOffsets()
{
	guard(FixupPlayerOffsets);
	FixupNativePropertyOffset( UPlayer::StaticClass(), TEXT("Actor"), STRUCT_OFFSET(UPlayer,Actor) );
	FixupNativePropertyOffset( UPlayer::StaticClass(), TEXT("Console"), STRUCT_OFFSET(UPlayer,Console) );
	FixupNativePropertyOffset( UPlayer::StaticClass(), TEXT("WindowsMouseX"), STRUCT_OFFSET(UPlayer,WindowsMouseX) );
	FixupNativePropertyOffset( UPlayer::StaticClass(), TEXT("WindowsMouseY"), STRUCT_OFFSET(UPlayer,WindowsMouseY) );
	FixupNativePropertyOffset( UPlayer::StaticClass(), TEXT("CurrentNetSpeed"), STRUCT_OFFSET(UPlayer,CurrentNetSpeed) );
	FixupNativePropertyOffset( UPlayer::StaticClass(), TEXT("ConfiguredInternetSpeed"), STRUCT_OFFSET(UPlayer,ConfiguredInternetSpeed) );
	FixupNativePropertyOffset( UPlayer::StaticClass(), TEXT("ConfiguredLanSpeed"), STRUCT_OFFSET(UPlayer,ConfiguredLanSpeed) );
	FixupNativePropertyOffset( UPlayer::StaticClass(), TEXT("SelectedCursor"), STRUCT_OFFSET(UPlayer,SelectedCursor) );
	const INT BoolOffset = STRUCT_OFFSET(UPlayer,WindowsMouseX) - sizeof(BITFIELD);
	static const TCHAR* PlayerBools[] =
	{
		TEXT("bWindowsMouseAvailable"), TEXT("bShowWindowsMouse"), TEXT("bSuspendPrecaching")
	};
	FixupNativeBoolBlockOffset( UPlayer::StaticClass(), TEXT("PlayerMouse"), PlayerBools, ARRAY_COUNT(PlayerBools), BoolOffset );
	unguard;
}

static void FixupNativeBoolBlockOffset( UClass* Class, const TCHAR* Label, const TCHAR** Names, INT Count, INT Offset )
{
	guard(FixupNativeBoolBlockOffset);
	if( !Class || !Names || Count<=0 )
		return;

	UProperty* First = FindNativeProperty( Class, Names[0] );
	if( First && First->Offset != Offset )
		MigrateNativeDefaultBytes( Class, Label, First->Offset, Offset, sizeof(BITFIELD) );

	for( INT i=0; i<Count; i++ )
	{
		UProperty* Property = FindNativeProperty( Class, Names[i] );
		if( !Property )
		{
			debugf( NAME_Warning, TEXT("UT99_ANDROID_V181_BOOL_OFFSET_MISSING class=%s block=%s property=%s native=%i"),
				Class->GetFullName(),
				Label,
				Names[i],
				Offset );
			continue;
		}
		if( Property->Offset != Offset )
		{
			debugf( NAME_Warning, TEXT("UT99_ANDROID_V181_BOOL_OFFSET_FIX class=%s block=%s property=%s script=%i native=%i"),
				Class->GetFullName(),
				Label,
				Names[i],
				Property->Offset,
				Offset );
			Property->Offset = Offset;
		}
	}
	unguard;
}

static void FixupCriticalNativeOffsets()
{
#if defined(PLATFORM_64BIT)
	guard(FixupCriticalNativeOffsets);
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Level"), STRUCT_OFFSET(AActor,Level) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("XLevel"), STRUCT_OFFSET(AActor,XLevel) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Owner"), STRUCT_OFFSET(AActor,Owner) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Instigator"), STRUCT_OFFSET(AActor,Instigator) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Base"), STRUCT_OFFSET(AActor,Base) );
	FixupBitmapTextureOffsets();
	FixupPlayerOffsets();
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Region"), STRUCT_OFFSET(AActor,Region) );
	FixupPointRegionStruct();
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("FootRegion"), STRUCT_OFFSET(APawn,FootRegion) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("HeadRegion"), STRUCT_OFFSET(APawn,HeadRegion) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("MoveTarget"), STRUCT_OFFSET(APawn,MoveTarget) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("FaceTarget"), STRUCT_OFFSET(APawn,FaceTarget) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Enemy"), STRUCT_OFFSET(APawn,Enemy) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Weapon"), STRUCT_OFFSET(APawn,Weapon) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("PendingWeapon"), STRUCT_OFFSET(APawn,PendingWeapon) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SelectedItem"), STRUCT_OFFSET(APawn,SelectedItem) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("PlayerReplicationInfo"), STRUCT_OFFSET(APawn,PlayerReplicationInfo) );
	static const TCHAR* ActorCoreBools[] =
	{
		TEXT("bStatic"), TEXT("bHidden"), TEXT("bNoDelete"), TEXT("bAnimFinished"), TEXT("bAnimLoop"), TEXT("bAnimNotify"),
		TEXT("bAnimByOwner"), TEXT("bDeleteMe"), TEXT("bAssimilated"), TEXT("bTicked"), TEXT("bLightChanged"),
		TEXT("bDynamicLight"), TEXT("bTimerLoop"), TEXT("bCanTeleport"), TEXT("bIsSecretGoal"), TEXT("bIsKillGoal"),
		TEXT("bIsItemGoal"), TEXT("bCollideWhenPlacing"), TEXT("bTravel"), TEXT("bMovable"), TEXT("bHighDetail"),
		TEXT("bStasis"), TEXT("bForceStasis"), TEXT("bIsPawn"), TEXT("bNetTemporary"), TEXT("bNetOptional"),
		TEXT("bReplicateInstigator"), TEXT("bTrailerSameRotation"), TEXT("bTrailerPrePivot"), TEXT("bClientAnim"), TEXT("bSimFall")
	};
	static const TCHAR* ActorEditorBools[] =
	{
		TEXT("bHiddenEd"), TEXT("bDirectional"), TEXT("bSelected"), TEXT("bMemorized"), TEXT("bHighlighted"),
		TEXT("bEdLocked"), TEXT("bEdShouldSnap"), TEXT("bEdSnap"), TEXT("bTempEditor"), TEXT("bDifficulty0"),
		TEXT("bDifficulty1"), TEXT("bDifficulty2"), TEXT("bDifficulty3"), TEXT("bSinglePlayer"), TEXT("bNet"), TEXT("bNetSpecial")
	};
	static const TCHAR* ActorRenderBools[] =
	{
		TEXT("bUnlit"), TEXT("bNoSmooth"), TEXT("bParticles"), TEXT("bRandomFrame"), TEXT("bMeshEnviroMap"), TEXT("bMeshCurvy")
	};
	static const TCHAR* ActorRelevancyBools[] =
	{
		TEXT("bShadowCast"), TEXT("bOwnerNoSee"), TEXT("bOnlyOwnerSee"), TEXT("bIsMover"), TEXT("bAlwaysRelevant"),
		TEXT("bAlwaysTick"), TEXT("bHurtEntry"), TEXT("bGameRelevant"), TEXT("bCarriedItem"), TEXT("bForcePhysicsUpdate")
	};
	static const TCHAR* ActorCollisionBools[] =
	{
		TEXT("bCollideActors"), TEXT("bCollideWorld"), TEXT("bBlockActors"), TEXT("bBlockPlayers"), TEXT("bProjTarget")
	};
	static const TCHAR* ActorLightingBools[] =
	{
		TEXT("bSpecialLit"), TEXT("bActorShadows"), TEXT("bCorona"), TEXT("bLensFlare"), TEXT("bBounce"),
		TEXT("bFixedRotationDir"), TEXT("bRotateToDesired"), TEXT("bInterpolating"), TEXT("bJustTeleported")
	};
	FixupNativeBoolBlockOffset( AActor::StaticClass(), TEXT("ActorCore"), ActorCoreBools, ARRAY_COUNT(ActorCoreBools), STRUCT_OFFSET(AActor,Physics)-sizeof(BITFIELD) );
	FixupNativeBoolBlockOffset( AActor::StaticClass(), TEXT("ActorEditor"), ActorEditorBools, ARRAY_COUNT(ActorEditorBools), STRUCT_OFFSET(AActor,OddsOfAppearing)-sizeof(BITFIELD) );
	FixupNativeBoolBlockOffset( AActor::StaticClass(), TEXT("ActorRender"), ActorRenderBools, ARRAY_COUNT(ActorRenderBools), STRUCT_OFFSET(AActor,VisibilityRadius)-sizeof(BITFIELD) );
	FixupNativeBoolBlockOffset( AActor::StaticClass(), TEXT("ActorRelevancy"), ActorRelevancyBools, ARRAY_COUNT(ActorRelevancyBools), STRUCT_OFFSET(AActor,MultiSkins)-sizeof(BITFIELD) );
	FixupNativeBoolBlockOffset( AActor::StaticClass(), TEXT("ActorCollision"), ActorCollisionBools, ARRAY_COUNT(ActorCollisionBools), STRUCT_OFFSET(AActor,LightType)-sizeof(BITFIELD) );
	FixupNativeBoolBlockOffset( AActor::StaticClass(), TEXT("ActorLighting"), ActorLightingBools, ARRAY_COUNT(ActorLightingBools), STRUCT_OFFSET(AActor,DodgeDir)-sizeof(BITFIELD) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("TimeDilation"), STRUCT_OFFSET(ALevelInfo,TimeDilation) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("TimeSeconds"), STRUCT_OFFSET(ALevelInfo,TimeSeconds) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Year"), STRUCT_OFFSET(ALevelInfo,Year) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Month"), STRUCT_OFFSET(ALevelInfo,Month) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Day"), STRUCT_OFFSET(ALevelInfo,Day) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("DayOfWeek"), STRUCT_OFFSET(ALevelInfo,DayOfWeek) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Hour"), STRUCT_OFFSET(ALevelInfo,Hour) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Minute"), STRUCT_OFFSET(ALevelInfo,Minute) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Second"), STRUCT_OFFSET(ALevelInfo,Second) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Millisecond"), STRUCT_OFFSET(ALevelInfo,Millisecond) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Title"), STRUCT_OFFSET(ALevelInfo,Title) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Author"), STRUCT_OFFSET(ALevelInfo,Author) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("IdealPlayerCount"), STRUCT_OFFSET(ALevelInfo,IdealPlayerCount) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("RecommendedEnemies"), STRUCT_OFFSET(ALevelInfo,RecommendedEnemies) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("RecommendedTeammates"), STRUCT_OFFSET(ALevelInfo,RecommendedTeammates) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("LevelEnterText"), STRUCT_OFFSET(ALevelInfo,LevelEnterText) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("LocalizedPkg"), STRUCT_OFFSET(ALevelInfo,LocalizedPkg) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Pauser"), STRUCT_OFFSET(ALevelInfo,Pauser) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Summary"), STRUCT_OFFSET(ALevelInfo,Summary) );
	static const TCHAR* LevelInfoPlayBools[] =
	{
		TEXT("bLonePlayer"), TEXT("bBegunPlay"), TEXT("bPlayersOnly"), TEXT("bHighDetailMode"), TEXT("bDropDetail"),
		TEXT("bAggressiveLOD"), TEXT("bStartup"), TEXT("bHumansOnly"), TEXT("bNoCheating"), TEXT("bAllowFOV")
	};
	FixupNativeBoolBlockOffset( ALevelInfo::StaticClass(), TEXT("LevelInfoPlay"), LevelInfoPlayBools, ARRAY_COUNT(LevelInfoPlayBools), STRUCT_OFFSET(ALevelInfo,Song)-sizeof(BITFIELD) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Song"), STRUCT_OFFSET(ALevelInfo,Song) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("SongSection"), STRUCT_OFFSET(ALevelInfo,SongSection) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("CdTrack"), STRUCT_OFFSET(ALevelInfo,CdTrack) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("PlayerDoppler"), STRUCT_OFFSET(ALevelInfo,PlayerDoppler) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Brightness"), STRUCT_OFFSET(ALevelInfo,Brightness) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Screenshot"), STRUCT_OFFSET(ALevelInfo,Screenshot) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("DefaultTexture"), STRUCT_OFFSET(ALevelInfo,DefaultTexture) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("HubStackLevel"), STRUCT_OFFSET(ALevelInfo,HubStackLevel) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("LevelAction"), STRUCT_OFFSET(ALevelInfo,LevelAction) );
	static const TCHAR* LevelInfoRenderBools[] = { TEXT("bNeverPrecache") };
	FixupNativeBoolBlockOffset( ALevelInfo::StaticClass(), TEXT("LevelInfoRender"), LevelInfoRenderBools, ARRAY_COUNT(LevelInfoRenderBools), STRUCT_OFFSET(ALevelInfo,NetMode)-sizeof(BITFIELD) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("Game"), STRUCT_OFFSET(ALevelInfo,Game) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("NetMode"), STRUCT_OFFSET(ALevelInfo,NetMode) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("ComputerName"), STRUCT_OFFSET(ALevelInfo,ComputerName) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("EngineVersion"), STRUCT_OFFSET(ALevelInfo,EngineVersion) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("MinNetVersion"), STRUCT_OFFSET(ALevelInfo,MinNetVersion) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("DefaultGameType"), STRUCT_OFFSET(ALevelInfo,DefaultGameType) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("NavigationPointList"), STRUCT_OFFSET(ALevelInfo,NavigationPointList) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("PawnList"), STRUCT_OFFSET(ALevelInfo,PawnList) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("NextURL"), STRUCT_OFFSET(ALevelInfo,NextURL) );
	static const TCHAR* LevelInfoServerBools[] = { TEXT("bNextItems") };
	FixupNativeBoolBlockOffset( ALevelInfo::StaticClass(), TEXT("LevelInfoServer"), LevelInfoServerBools, ARRAY_COUNT(LevelInfoServerBools), STRUCT_OFFSET(ALevelInfo,NextSwitchCountdown)-sizeof(BITFIELD) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("NextSwitchCountdown"), STRUCT_OFFSET(ALevelInfo,NextSwitchCountdown) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("AvgAITime"), STRUCT_OFFSET(ALevelInfo,AvgAITime) );
	static const TCHAR* LevelInfoPhysicsBools[] = { TEXT("bCheckWalkSurfaces") };
	FixupNativeBoolBlockOffset( ALevelInfo::StaticClass(), TEXT("LevelInfoPhysics"), LevelInfoPhysicsBools, ARRAY_COUNT(LevelInfoPhysicsBools), STRUCT_OFFSET(ALevelInfo,SpawnNotify)-sizeof(BITFIELD) );
	FixupNativePropertyOffset( ALevelInfo::StaticClass(), TEXT("SpawnNotify"), STRUCT_OFFSET(ALevelInfo,SpawnNotify) );
	unguard;
#endif
}

/*-----------------------------------------------------------------------------
	cleanup!!
-----------------------------------------------------------------------------*/

void UGameEngine::PaintProgress()
{
	guard(PaintProgress);

	FVector LoadFog(0,.1,.25);
	FVector LoadScale(.2,.2,.2);
	UViewport* Viewport=Client->Viewports(0);
	Exchange(Viewport->Actor->FlashFog,LoadFog);
	Exchange(Viewport->Actor->FlashScale,LoadScale);
	Draw( Viewport );
	Exchange(Viewport->Actor->FlashFog,LoadFog);
	Exchange(Viewport->Actor->FlashScale,LoadScale);

	unguard;
}

INT UGameEngine::ChallengeResponse( INT Challenge )
{
	guard(UGameEngine::ChallengeResponse);
	return (Challenge*237) ^ (0x93fe92Ce) ^ (Challenge>>16) ^ (Challenge<<16);
	unguard;
}

void UGameEngine::UpdateConnectingMessage()
{
	guard(UGameEngine::UpdateConnectingMessage);
	if( GPendingLevel && Client && Client->Viewports.Num() )
	{
		APlayerPawn* Actor = Client->Viewports(0)->Actor;
		if( Actor->ProgressTimeOut<Actor->Level->TimeSeconds )
		{
			TCHAR Msg1[256], Msg2[256];
			if( GPendingLevel->DemoRecDriver )
			{
				appSprintf( Msg1, TEXT("") );
				appSprintf( Msg2, *GPendingLevel->URL.Map );
			}
			else
			{
				appSprintf( Msg1, LocalizeProgress("ConnectingText") );
				appSprintf( Msg2, LocalizeProgress("ConnectingURL"), *GPendingLevel->URL.Host, *GPendingLevel->URL.Map );
			}
			SetProgress( Msg1, Msg2, 60.0 );
		}
	}
	unguard;
}
void UGameEngine::BuildServerMasterMap( UNetDriver* NetDriver, ULevel* InLevel )
{
	guard(UGameEngine::BuildServerMasterMap);
	check(NetDriver);
	check(InLevel);
	BeginLoad();
	{
		// Init LinkerMap.
		check(InLevel->GetLinker());
		NetDriver->MasterMap->AddLinker( InLevel->GetLinker() );

		// Load server-required packages.
		for( INT i=0; i<ServerPackages.Num(); i++ )
		{
			debugf( TEXT("Server Package: %s"), *ServerPackages(i) );
			ULinkerLoad* Linker = GetPackageLinker( NULL, *ServerPackages(i), LOAD_NoFail, NULL, NULL );
			if( NetDriver->MasterMap->AddLinker( Linker )==INDEX_NONE )
				debugf( TEXT("   (server-side only)") );
		}

		// Add GameInfo's package to map.
		check(InLevel->GetLevelInfo());
		check(InLevel->GetLevelInfo()->Game);
		check(InLevel->GetLevelInfo()->Game->GetClass()->GetLinker());
		NetDriver->MasterMap->AddLinker( InLevel->GetLevelInfo()->Game->GetClass()->GetLinker() );

		// Precompute linker info.
		NetDriver->MasterMap->Compute();
	}
	EndLoad();
	unguard;
}

/*-----------------------------------------------------------------------------
	Game init and exit.
-----------------------------------------------------------------------------*/

//
// Construct the game engine.
//
UGameEngine::UGameEngine()
: LastURL(TEXT(""))
, ServerActors( E_NoInit )
, ServerPackages( E_NoInit )
{}

//
// Class creator.
//
void UGameEngine::StaticConstructor()
{
	guard(UGameEngine::StaticConstructor);

	UArrayProperty* A = new(GetClass(),TEXT("ServerActors"),RF_Public)UArrayProperty( CPP_PROPERTY(ServerActors), TEXT("Settings"), CPF_Config );
	A->Inner = new(A,TEXT("StrProperty0"),RF_Public)UStrProperty;

	UArrayProperty* B = new(GetClass(),TEXT("ServerPackages"),RF_Public)UArrayProperty( CPP_PROPERTY(ServerPackages), TEXT("Settings"), CPF_Config );
	B->Inner = new(B,TEXT("StrProperty0"),RF_Public)UStrProperty;

	unguard;
}

//
// Initialize the game engine.
//
void UGameEngine::Init()
{
	guard(UGameEngine::Init);
	check(sizeof(*this)==GetClass()->GetPropertiesSize());
	debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE UGameEngine::Init begin GIsClient=%i"), GIsClient );

	// Call base.
	UEngine::Init();

	// Init variables.
	GLevel = NULL;

	// Delete temporary files in cache.
	appCleanFileCache();

	// If not a dedicated server.
	if( GIsClient )
	{	
		// Init client.
		UClass* ClientClass = StaticLoadClass( UClient::StaticClass(), NULL, TEXT("ini:Engine.Engine.ViewportManager"), NULL, LOAD_NoFail, NULL );
		Client = ConstructObject<UClient>( ClientClass );
		Client->Init( this );
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE client initialized class=%s"), ClientClass ? ClientClass->GetName() : TEXT("None") );

		// Init rendering.
		UClass* RenderClass = StaticLoadClass( URenderBase::StaticClass(), NULL, TEXT("ini:Engine.Engine.Render"), NULL, LOAD_NoFail, NULL );
		Render = ConstructObject<URenderBase>( RenderClass );
		Render->Init( this );
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE render initialized class=%s"), RenderClass ? RenderClass->GetName() : TEXT("None") );
	}

	// Load the entry level.
	FString Error;
	if( Client )
	{
		if( !LoadMap( FURL(TEXT("Entry")), NULL, NULL, Error ) )
			appErrorf( LocalizeError("FailedBrowse"), TEXT("Entry"), *Error );
		Exchange( GLevel, GEntry );
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE entry map loaded GEntry=%i"), GEntry != NULL );
#ifdef PLATFORM_LOW_MEMORY
#if defined(PLATFORM_64BIT)
		debugf( NAME_Init, TEXT("UT99_ANDROID_V150_ENTRY_GC_SKIP skipping low-memory Entry GC on 64-bit") );
#else
		// Purge unused objects and flush caches.
		Flush(1);
		UObject::CollectGarbage( RF_Native );
#endif
#endif
	}

	// Create default URL.
	FURL DefaultURL;
	DefaultURL.LoadURLConfig( TEXT("DefaultPlayer"), TEXT("User") );

	// Enter initial world.
	TCHAR Parm[4096]=TEXT("");
	const TCHAR* Tmp = appCmdLine();
	if
	(	!ParseToken( Tmp, Parm, ARRAY_COUNT(Parm), 0 )
	||	(appStricmp(Parm,TEXT("SERVER"))==0 && !ParseToken( Tmp, Parm, ARRAY_COUNT(Parm), 0 ))
	||	Parm[0]=='-' )
		appStrcpy( Parm, *FURL::DefaultLocalMap );
	FURL URL( &DefaultURL, Parm, TRAVEL_Partial );
	if( !URL.Valid )
		appErrorf( LocalizeError("InvalidUrl"), Parm );
	UBOOL Success = Browse( URL, NULL, Error );
	debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE initial Browse success=%i url=%s error=%s"), Success, Parm, *Error );

	// If waiting for a network connection, go into the starting level.
	if( !Success && Error==TEXT("") && appStricmp( Parm, *FURL::DefaultLocalMap )!=0 )
	{
		Success = Browse( FURL(&DefaultURL,*FURL::DefaultLocalMap,TRAVEL_Partial), NULL, Error );
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE fallback Browse success=%i error=%s"), Success, *Error );
	}

	// Handle failure.
	if( !Success )
		appErrorf( LocalizeError("FailedBrowse"), Parm, *Error );

	// Open initial Viewport.
	if( Client )
	{
		// Init input.!!Temporary
		UInput::StaticInitInput();

		// Create viewport.
		UViewport* Viewport = Client->NewViewport( NAME_None );
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE NewViewport viewport=%i"), Viewport != NULL );

		// Create console.
		UClass* ConsoleClass = StaticLoadClass( UConsole::StaticClass(), NULL, TEXT("ini:Engine.Engine.Console"), NULL, LOAD_NoFail, NULL );
		UConsole::FixupNativeClassSize( ConsoleClass );
		Viewport->Console = ConstructObject<UConsole>( ConsoleClass );
		Viewport->Console->_Init( Viewport );
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE console initialized class=%s"), ConsoleClass ? ConsoleClass->GetName() : TEXT("None") );

		// Spawn play actor.
		FString Error;
		if( !GLevel->SpawnPlayActor( Viewport, ROLE_SimulatedProxy, URL, Error ) )
			appErrorf( TEXT("%s"), *Error );
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE play actor spawned Actor=%i"), Viewport->Actor != NULL );
		Viewport->Input->Init( Viewport );
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE input initialized opening window") );
		Viewport->OpenWindow( 0, 0, (INT) INDEX_NONE, (INT) INDEX_NONE, (INT) INDEX_NONE, (INT) INDEX_NONE );
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE OpenWindow returned Size=%ix%i RenDev=%i"), Viewport->SizeX, Viewport->SizeY, Viewport->RenDev != NULL );
		GLevel->DetailChange( Viewport->RenDev->HighDetailActors );
		InitAudio();
		if( Audio )
			Audio->SetViewport( Viewport );
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE initial viewport ready Audio=%i"), Audio != NULL );
	}
	debugf( NAME_Init, TEXT("Game engine initialized") );

	unguard;
}

//
// Pre exit.
//
void UGameEngine::Exit()
{
	guard(UGameEngine::Exit);
	Super::Exit();

	// Exit net.
	if( GLevel->NetDriver )
	{
		delete GLevel->NetDriver;
		GLevel->NetDriver = NULL;
	}

	unguard;
}

//
// Game exit.
//
void UGameEngine::Destroy()
{
	guard(UGameEngine::Destroy);

	// Game exit.
	if( GPendingLevel )
		CancelPending();
	GLevel = NULL;
	debugf( NAME_Exit, TEXT("Game engine shut down") );

	Super::Destroy();
	unguard;
}

//
// Progress text.
//
void UGameEngine::SetProgress( const TCHAR* Str1, const TCHAR* Str2, FLOAT Seconds )
{
	guard(UGameEngine::SetProgress);
	if( Client && Client->Viewports.Num() )
	{
		APlayerPawn* Actor = Client->Viewports(0)->Actor;
		if( Seconds==-1.0 )
		{
			// Upgrade message.
			Actor->eventShowUpgradeMenu();
		}
		Actor->ProgressMessage[0] = Str1;
		Actor->ProgressColor[0].R = 255;
		Actor->ProgressColor[0].G = 255;
		Actor->ProgressColor[0].B = 255;

		Actor->ProgressMessage[1] = Str2;
		Actor->ProgressColor[1].R = 255;
		Actor->ProgressColor[1].G = 255;
		Actor->ProgressColor[1].B = 255;

		Actor->ProgressTimeOut    = Actor->Level->TimeSeconds + Seconds;
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	Command line executor.
-----------------------------------------------------------------------------*/

//
// This always going to be the last exec handler in the chain. It
// handles passing the command to all other global handlers.
//
UBOOL UGameEngine::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UGameEngine::Exec);
	const TCHAR* Str=Cmd;
	if( ParseCommand( &Str, TEXT("OPEN") ) )
	{
		FString Error;
		if( Client && Client->Viewports.Num() )
			SetClientTravel( Client->Viewports(0), Str, 0, TRAVEL_Partial );
		else
		if( !Browse( FURL(&LastURL,Str,TRAVEL_Partial), NULL, Error ) && Error!=TEXT("") )
			Ar.Logf( TEXT("Open failed: %s"), *Error );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("START") ) )
	{
		FString Error;
		if( Client && Client->Viewports.Num() )
			SetClientTravel( Client->Viewports(0), Str, 0, TRAVEL_Absolute );
		else
		if( !Browse( FURL(&LastURL,Str,TRAVEL_Absolute), NULL, Error ) && Error!=TEXT("") )
			Ar.Logf( TEXT("Start failed: %s"), *Error );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("SERVERTRAVEL") ) && (GIsServer && !GIsClient) )
	{
		GLevel->GetLevelInfo()->eventServerTravel(Str,0);
		return 1;
	}
	else if( (GIsServer && !GIsClient) && ParseCommand( &Str, TEXT("SAY") ) )
	{
		GLevel->GetLevelInfo()->eventBroadcastMessage(Str,1,NAME_None);
		return 1;
	}
	else if( ParseCommand(&Str, TEXT("DISCONNECT")) )
	{
		FString Error;
		if( Client && Client->Viewports.Num() )
			SetClientTravel( Client->Viewports(0), TEXT("?failed"), 0, TRAVEL_Absolute );
		else
		if( !Browse( FURL(&LastURL,TEXT("?failed"),TRAVEL_Absolute), NULL, Error ) && Error!=TEXT("") )
			Ar.Logf( TEXT("Disconnect failed: %s"), *Error );
		return 1;
	}
	else if( ParseCommand(&Str, TEXT("RECONNECT")) )
	{
		FString Error;
		if( Client && Client->Viewports.Num() )
			SetClientTravel( Client->Viewports(0), *LastURL.String(), 0, TRAVEL_Absolute );
		else
		if( !Browse( FURL(LastURL), NULL, Error ) && Error!=TEXT("") )
			Ar.Logf( TEXT("Reconnect failed: %s"), *Error );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("GETCURRENTTICKRATE") ) )
	{
		Ar.Logf( TEXT("%f"), CurrentTickRate );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("GETMAXTICKRATE") ) )
	{
		Ar.Logf( TEXT("%f"), GetMaxTickRate() );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("GSPYLITE") ) )
	{
		FString Error;
		appLaunchURL( TEXT("GSpyLite.exe"), TEXT(""), &Error );
		return 1;
	}
	else if( ParseCommand(&Str,TEXT("SAVEGAME")) )
	{
		if( appIsDigit(Str[0]) )
			SaveGame( appAtoi(Str) );
		return 1;
	}
	else if( ParseCommand( &Cmd, TEXT("CANCEL") ) )
	{
		if( GPendingLevel )
			SetProgress( LocalizeProgress("CancelledConnect"), TEXT(""), 2.0 );
		else
			SetProgress( TEXT(""), TEXT(""), 0.0 );
		CancelPending();
		return 1;
	}
	else if( GLevel && GLevel->Exec( Cmd, Ar ) )
	{
		return 1;
	}
	else if( GLevel && GLevel->GetLevelInfo()->Game && GLevel->GetLevelInfo()->Game->ScriptConsoleExec(Cmd,Ar,NULL) )
	{
		return 1;
	}
	else if( UEngine::Exec( Cmd, Ar ) )
	{
		return 1;
	}
	else return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	Serialization.
-----------------------------------------------------------------------------*/

//
// Serializer.
//
void UGameEngine::Serialize( FArchive& Ar )
{
	guard(UGameEngine::Serialize);
	Super::Serialize( Ar );

	Ar << GLevel << GEntry << GPendingLevel;

	unguardobj;
}

/*-----------------------------------------------------------------------------
	Game entering.
-----------------------------------------------------------------------------*/

//
// Cancel pending level.
//
void UGameEngine::CancelPending()
{
	guard(UGameEngine::CancelPending);
	if( GPendingLevel )
	{
		delete GPendingLevel;
		GPendingLevel = NULL;
	}
	unguard;
}

//
// Match Viewports to actors.
//
static void MatchViewportsToActors( UClient* Client, ULevel* Level, const FURL& URL )
{
	guard(MatchViewportsToActors);
	for( INT i=0; i<Client->Viewports.Num(); i++ )
	{
		FString Error;
		UViewport* Viewport = Client->Viewports(i);
		debugf( NAME_Log, TEXT("Spawning new actor for Viewport %s"), Viewport->GetName() );
		if( !Level->SpawnPlayActor( Viewport, ROLE_SimulatedProxy, URL, Error ) )
			appErrorf( TEXT("%s"), *Error );
	}
	unguardf(( TEXT("(%s)"), *Level->URL.Map ));
}

//
// Browse to a specified URL, relative to the current one.
//
UBOOL UGameEngine::Browse( FURL URL, const TMap<FString,FString>* TravelInfo, FString& Error )
{
	guard(UGameEngine::Browse);
	Error = TEXT("");
	const TCHAR* Option;

	// Convert .unreal link files.
	const TCHAR* LinkStr = TEXT(".unreal");//!!
	if( appStrstr(*URL.Map,LinkStr)-*URL.Map==appStrlen(*URL.Map)-appStrlen(LinkStr) )
	{
		debugf( TEXT("Link: %s"), *URL.Map );
		FString NewUrlString;
		if( GConfig->GetString( TEXT("Link")/*!!*/, TEXT("Server"), NewUrlString, *URL.Map ) )
		{
			// Go to link.
			URL = FURL( NULL, *NewUrlString, TRAVEL_Absolute );//!!
		}
		else
		{
			// Invalid link.
			guard(InvalidLink);
			Error = FString::Printf( LocalizeError("InvalidLink"), *URL.Map );
			unguard;
			return 0;
		}
	}

	// Crack the URL.
	debugf( TEXT("Browse: %s"), *URL.String() );

	// Handle it.
	if( !URL.Valid )
	{
		// Unknown URL.
		guard(UnknownURL);
		Error = FString::Printf( LocalizeError("InvalidUrl"), *URL.String() );
		unguard;
		return 0;
	}
	else if( URL.HasOption(TEXT("failed")) || URL.HasOption(TEXT("entry")) )
	{
		// Handle failure URL.
		guard(FailedURL);
		debugf( NAME_Log, LocalizeError("AbortToEntry") );
		if( GLevel && GLevel!=GEntry )
		{
			if( GLevel->BrushTracker )
			{
				delete GLevel->BrushTracker;
				GLevel->BrushTracker = NULL;
			}
			ResetLoaders( GLevel->GetOuter(), 1, 0 );
		}
		NotifyLevelChange();
		GLevel = GEntry;
		GLevel->GetLevelInfo()->LevelAction = LEVACT_None;
		check(Client && Client->Viewports.Num());
		MatchViewportsToActors( Client, GLevel, URL );
		if( Audio )
			Audio->SetViewport( Audio->GetViewport() );
		//CollectGarbage( RF_Native ); // Causes texture corruption unless you flush.
		if( URL.HasOption(TEXT("failed")) )
		{
			if( !GPendingLevel )
				SetProgress( LocalizeError("ConnectionFailed"), TEXT(""), 6.0 );
		}
		unguard;
		return 1;
	}
	else if( URL.HasOption(TEXT("pop")) )
	{
		// Pop the hub.
		guard(PopURL);
		if( GLevel && GLevel->GetLevelInfo()->HubStackLevel>0 )
		{
			TCHAR Filename[256], SavedPortal[256];
			appSprintf( Filename, TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, GLevel->GetLevelInfo()->HubStackLevel-1 );
			appStrcpy( SavedPortal, *URL.Portal );
			URL = FURL( &URL, Filename, TRAVEL_Partial );
			URL.Portal = SavedPortal;
		}
		else return 0;
		unguard;
	}
	else if( URL.HasOption(TEXT("restart")) )
	{
		// Handle restarting.
		guard(RestartURL);
		URL = LastURL;
		unguard;
	}
	else if( (Option=URL.GetOption(TEXT("load="),NULL))!=NULL )
	{
		// Handle loadgame.
		guard(LoadURL);
		FString Error, Temp=FString::Printf( TEXT("%s") PATH_SEPARATOR TEXT("Save%i.usa?load"), *GSys->SavePath, appAtoi(Option) );
		if( LoadMap(FURL(&LastURL,*Temp,TRAVEL_Partial),NULL,NULL,Error) )
		{
			// Copy the hub stack.
			INT i;
			for( i=0; i<GLevel->GetLevelInfo()->HubStackLevel; i++ )
			{
				TCHAR Src[256], Dest[256];//!!
				appSprintf( Src, TEXT("%s") PATH_SEPARATOR TEXT("Save%i%i.usa"), *GSys->SavePath, appAtoi(Option), i );
				appSprintf( Dest, TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, i );
				GFileManager->Copy( Src, Dest );
			}
			while( 1 )
			{
				Temp = FString::Printf( TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, i++ );
				if( GFileManager->FileSize(*Temp)<=0 )
					break;
				GFileManager->Delete( *Temp );
			}
			LastURL = GLevel->URL;
			return 1;
		}
		else return 0;
		unguard;
	}

	// Handle normal URL's.
	if( URL.IsLocalInternal() )
	{
		// Local map file.
		guard(LocalMapURL);
		return LoadMap( URL, NULL, TravelInfo, Error )!=NULL;
		unguard;
	}
	else if( URL.IsInternal() && GIsClient )
	{
		// Network URL.
		guard(NetworkURL);
		if( GPendingLevel )
			CancelPending();
		GPendingLevel = new UNetPendingLevel( this, URL );
		if( !GPendingLevel->NetDriver )
		{
			SetProgress( TEXT("Networking Failed"), *GPendingLevel->Error, 6.0 );
			delete GPendingLevel;
			GPendingLevel = NULL;
		}
		return 0;
		unguard;
	}
	else if( URL.IsInternal() )
	{
		// Invalid.
		guard(InvalidURL);
		Error = LocalizeError("ServerOpen");
		unguard;
		return 0;
	}
	else
	{
		// External URL.
		guard(ExternalURL);
		appLaunchURL( *URL.String(), TEXT(""), &Error );
		unguard;
		return 0;
	}
	unguard;
}

//
// Notify that level is changing
//
void UGameEngine::NotifyLevelChange()
{
	guard(UGameEngine::NotifyLevelChange);
	if( Client && Client->Viewports.Num() && Client->Viewports(0)->Console )
		Client->Viewports(0)->Console->eventNotifyLevelChange();
	unguard;	
}

//
// Load a map.
//
ULevel* UGameEngine::LoadMap( const FURL& URL, UPendingLevel* Pending, const TMap<FString,FString>* TravelInfo, FString& Error )
{
	guard(UGameEngine::LoadMap);
	Error = TEXT("");
	debugf( NAME_Log, TEXT("LoadMap: %s"), *URL.String() );
	GInitRunaway();

	// Remember current level's stack level.
	INT SavedHubStackLevel = GLevel ? GLevel->GetLevelInfo()->HubStackLevel : 0;

	// Display loading screen.
	guard(LoadingScreen);
	if( Client && Client->Viewports.Num() && GLevel )
	{
		GLevel->GetLevelInfo()->LevelAction = LEVACT_Loading;
		GLevel->GetLevelInfo()->Pauser = TEXT("");
		APlayerPawn* PP = Client->Viewports(0)->Actor;
		if( PP )
			PP->bShowMenu = 0;
		PaintProgress();
		if( Audio )
			Audio->SetViewport( Audio->GetViewport() );
		GLevel->GetLevelInfo()->LevelAction = LEVACT_None;
	}
	unguard;

	// Get network package map.
	UPackageMap* PackageMap = NULL;
	if( Pending )
		PackageMap = Pending->GetDriver()->ServerConnection->PackageMap;

	// Verify that we can load all packages we need.
	UObject* MapParent = NULL;
	guard(VerifyPackages);
	try
	{
		BeginLoad();
		if( Pending )
		{
			// Verify that we can load everything needed for client in this network level.
			INT i;
			for( i=0; i<PackageMap->List.Num(); i++ )
				PackageMap->List(i).Linker = GetPackageLinker
				(
					PackageMap->List(i).Parent,
					NULL,
					LOAD_Verify | LOAD_Throw | LOAD_NoWarn | LOAD_NoVerify,
					NULL,
					&PackageMap->List(i).Guid
				);
			for( i=0; i<PackageMap->List.Num(); i++ )
				VerifyLinker( PackageMap->List(i).Linker );
			if( PackageMap->List.Num() )
				MapParent = PackageMap->List(0).Parent;
		}
		LoadObject<ULevel>( MapParent, TEXT("MyLevel"), *URL.Map, LOAD_Verify | LOAD_Throw | LOAD_NoWarn, NULL );
		EndLoad();

#if DEMOVERSION
		// If we area demo, prevent third party maps from being loaded.
		if( !Pending || !Pending->DemoRecDriver )
		{
			FString FileName(FString(TEXT("../Maps/"))+URL.Map);
			if( FileName.Right(4).Caps() != TEXT(".UNR"))
				FileName = FileName + TEXT(".unr");
			INT FileSize = GFileManager->FileSize( *FileName );
			debugf(TEXT("Looking for file: %s %d"), *FileName, FileSize);
			if( //FileSize != 0 &&
				( FileName.Caps() != TEXT("../MAPS/DM-TURBINEDEMO.UNR")	|| FileSize != 2135105 ) &&
				( FileName.Caps() != TEXT("../MAPS/DM-PHOBOSDEMO.UNR")	|| FileSize != 1618994 ) &&
				( FileName.Caps() != TEXT("../MAPS/DM-MORPHEUSDEMO.UNR")|| FileSize != 1193759 ) &&
				( FileName.Caps() != TEXT("../MAPS/DM-TEMPESTDEMO.UNR")	|| FileSize != 2152238 ) &&
				( FileName.Caps() != TEXT("../MAPS/CTF-CORETDEMO.UNR")	|| FileSize != 3498978 ) &&
				( FileName.Caps() != TEXT("../MAPS/DOM-SESMARDEMO.UNR")	|| FileSize != 2155658 ) &&
				( FileName.Caps() != TEXT("../MAPS/ENTRY.UNR")			|| FileSize != 34822 ) &&
				( FileName.Caps() != TEXT("../MAPS/UT-LOGO-MAP.UNR")	|| FileSize != 34884 ) )
			{
				Error = TEXT("Sorry, only the retail version of UT can load third party maps.");
				SetProgress( LocalizeError(TEXT("UrlFailed"),TEXT("Core")), *Error, 6.0 );
				return NULL;
			}
		}
#endif
	}
	catch( TCHAR* CatchError )
	{
		// Safely failed loading.
		EndLoad();
		Error = CatchError;
		SetProgress( LocalizeError(TEXT("UrlFailed"),TEXT("Core")), CatchError, 6.0 );
		return NULL;
	}
	unguard;

	// Notify of the level change, before we dissociate Viewport actors
	guard(NotifyLevelChange);
	if( GLevel )
		NotifyLevelChange();
	unguard;

	// Dissociate Viewport actors.
	guard(DissociateViewports);
	if( Client )
	{
		for( INT i=0; i<Client->Viewports.Num(); i++ )
		{
			APlayerPawn* Actor          = Client->Viewports(i)->Actor;
			ULevel*      Level          = Actor->GetLevel();
			Actor->Player               = NULL;
			Client->Viewports(i)->Actor = NULL;
			Level->DestroyActor( Actor );
		}
	}
	unguard;

	// Clean up game state.
	guard(ExitLevel);
	if( GLevel )
	{
		// Shut down.
		ResetLoaders( GLevel->GetOuter(), 1, 0 );
		if( GLevel->BrushTracker )
		{
			delete GLevel->BrushTracker;
			GLevel->BrushTracker = NULL;
		}
		if( GLevel->NetDriver )
		{
			delete GLevel->NetDriver;
			GLevel->NetDriver = NULL;
		}
		if( GLevel->DemoRecDriver )
		{
			delete GLevel->DemoRecDriver;
			GLevel->DemoRecDriver = NULL;
		}
		if( URL.HasOption(TEXT("push")) )
		{
			// Save the current level minus players actors.
			GLevel->CleanupDestroyed( 1 );
			TCHAR Filename[256];
			appSprintf( Filename, TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, SavedHubStackLevel );
			SavePackage( GLevel->GetOuter(), GLevel, 0, Filename, GLog );
		}
		GLevel = NULL;
	}
	unguard;

	// Load the level and all objects under it, using the proper Guid.
	guard(LoadLevel);
	GLevel = LoadObject<ULevel>( MapParent, TEXT("MyLevel"), *URL.Map, LOAD_NoFail, NULL );
	unguard;

	// If pending network level.
	if( Pending )
	{
		// If playing this network level alone, ditch the pending level.
		if( Pending && Pending->LonePlayer )
			Pending = NULL;

		// Setup network package info.
		PackageMap->Compute();
		for( INT i=0; i<PackageMap->List.Num(); i++ )
			if( PackageMap->List(i).LocalGeneration!=PackageMap->List(i).RemoteGeneration )
				Pending->NetDriver->ServerConnection->Logf( TEXT("HAVE GUID=%s GEN=%i"), PackageMap->List(i).Guid.String(), PackageMap->List(i).LocalGeneration );
	}

	// Verify classes.
	guard(VerifyClasses);
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Owner"), STRUCT_OFFSET(AActor,Owner) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("TimerCounter"), STRUCT_OFFSET(AActor,TimerCounter) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Level"), STRUCT_OFFSET(AActor,Level) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("XLevel"), STRUCT_OFFSET(AActor,XLevel) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Tag"), STRUCT_OFFSET(AActor,Tag) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Event"), STRUCT_OFFSET(AActor,Event) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Target"), STRUCT_OFFSET(AActor,Target) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Instigator"), STRUCT_OFFSET(AActor,Instigator) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Inventory"), STRUCT_OFFSET(AActor,Inventory) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Base"), STRUCT_OFFSET(AActor,Base) );
	FixupPlayerOffsets();
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Region"), STRUCT_OFFSET(AActor,Region) );
	FixupPointRegionStruct();
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("FootRegion"), STRUCT_OFFSET(APawn,FootRegion) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("HeadRegion"), STRUCT_OFFSET(APawn,HeadRegion) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("MoveTarget"), STRUCT_OFFSET(APawn,MoveTarget) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("FaceTarget"), STRUCT_OFFSET(APawn,FaceTarget) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Enemy"), STRUCT_OFFSET(APawn,Enemy) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Weapon"), STRUCT_OFFSET(APawn,Weapon) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("PendingWeapon"), STRUCT_OFFSET(APawn,PendingWeapon) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SelectedItem"), STRUCT_OFFSET(APawn,SelectedItem) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("PlayerReplicationInfo"), STRUCT_OFFSET(APawn,PlayerReplicationInfo) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Location"), STRUCT_OFFSET(AActor,Location) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Rotation"), STRUCT_OFFSET(AActor,Rotation) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("DrawType"), STRUCT_OFFSET(AActor,DrawType) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Style"), STRUCT_OFFSET(AActor,Style) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Sprite"), STRUCT_OFFSET(AActor,Sprite) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Texture"), STRUCT_OFFSET(AActor,Texture) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Skin"), STRUCT_OFFSET(AActor,Skin) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Mesh"), STRUCT_OFFSET(AActor,Mesh) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Brush"), STRUCT_OFFSET(AActor,Brush) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("DrawScale"), STRUCT_OFFSET(AActor,DrawScale) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("PrePivot"), STRUCT_OFFSET(AActor,PrePivot) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("MultiSkins"), STRUCT_OFFSET(AActor,MultiSkins) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("AmbientSound"), STRUCT_OFFSET(AActor,AmbientSound) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("CollisionRadius"), STRUCT_OFFSET(AActor,CollisionRadius) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("CollisionHeight"), STRUCT_OFFSET(AActor,CollisionHeight) );
	VERIFY_CLASS_OFFSET( A, PlayerPawn,  Player        );
	VERIFY_CLASS_OFFSET( A, PlayerPawn,  MaxStepHeight );
	FixupCriticalNativeOffsets();
	unguard;

	// Get LevelInfo.
	check(GLevel);
	ALevelInfo* Info = GLevel->GetLevelInfo();
	Info->ComputerName = appComputerName();

	// Handle pushing.
	guard(ProcessHubStack);
	Info->HubStackLevel
	=	URL.HasOption(TEXT("load")) ? Info->HubStackLevel
	:	URL.HasOption(TEXT("push")) ? SavedHubStackLevel+1
	:	URL.HasOption(TEXT("pop" )) ? Max<INT>(SavedHubStackLevel-1,0)
	:	URL.HasOption(TEXT("peer")) ? SavedHubStackLevel
	:	                              0;
	unguard;

	// Handle pending level.
	guard(ActivatePending);
	if( Pending )
	{
		check(Pending==GPendingLevel);

		// Hook network driver up to level.
		GLevel->NetDriver = Pending->NetDriver;
		if( GLevel->NetDriver )
			GLevel->NetDriver->Notify = GLevel;

		// Hook demo playback driver to level
		GLevel->DemoRecDriver = Pending->DemoRecDriver;
		if( GLevel->DemoRecDriver )
			GLevel->DemoRecDriver->Notify = GLevel;

		// Setup level.
		GLevel->GetLevelInfo()->NetMode = NM_Client;
	}
	else check(!GLevel->NetDriver);
	unguard;

	// Set level info.
	guard(InitLevel);
	if( !URL.GetOption(TEXT("load"),NULL) )
		GLevel->URL = URL;
	Info->EngineVersion = FString::Printf( TEXT("%i"), ENGINE_VERSION );
	Info->MinNetVersion = FString::Printf( TEXT("%i"), ENGINE_MIN_NET_VERSION );
	GLevel->Engine = this;
	if( TravelInfo )
		GLevel->TravelInfo = *TravelInfo;
	unguard;

	// Purge unused objects and flush caches.
	guard(Cleanup);
	if( appStricmp(GLevel->GetOuter()->GetName(),TEXT("Entry"))!=0 )
	{
		Flush(0);
		{for( TObjectIterator<AActor> It; It; ++It )
			if( It->IsIn(GLevel->GetOuter()) )
				It->SetFlags( RF_EliminateObject );}
		{for( INT i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->ClearFlags( RF_EliminateObject );}
#if defined(PLATFORM_64BIT)
		debugf( NAME_Log, TEXT("UT99_ANDROID_V163_LOADMAP_GC_SKIP skipping cleanup GC on 64-bit map=%s"), GLevel->GetOuter()->GetName() );
#else
		CollectGarbage( RF_Native );
#endif
	}
	unguard;

	// Init collision.
	GLevel->SetActorCollision( 1 );

	// Setup zone distance table for sound damping. Fast enough: Approx 3 msec.
	guard(SetupZoneTable);
	QWORD OldConvConn[64];
	QWORD ConvConn[64];
	INT i, j;
	for( i=0; i<64; i++ )
	{
		for( j=0; j<64; j++ )
		{
			OldConvConn[i] = GLevel->Model->Zones[i].Connectivity;
			if( i == j )
				GLevel->ZoneDist[i][j] = 0;
			else
				GLevel->ZoneDist[i][j] = 255;
		}
	}
	for( i=1; i<64; i++ )
	{
		for( INT j=0; j<64; j++ )
			for( INT k=0; k<64; k++ )
				if( (GLevel->ZoneDist[j][k] > i) && ((OldConvConn[j] & ((QWORD)1 << k)) != 0) )
					GLevel->ZoneDist[j][k] = i;
		for( j=0; j<64; j++ )
			ConvConn[j] = 0;
		for( j=0; j<64; j++ )
			for( INT k=0; k<64; k++ )
				if( (OldConvConn[j] & ((QWORD)1 << k)) != 0 )
					ConvConn[j] = ConvConn[j] | OldConvConn[k];
		for( j=0; j<64; j++ )
			OldConvConn[j] = ConvConn[j];
	}
	unguard;

	// Update the LevelInfo's time.
	GLevel->UpdateTime(Info);

	// Init the game info.
	TCHAR Options[1024]=TEXT("");
	TCHAR GameClassName[256]=TEXT("");
	FString Error=TEXT("");
	guard(InitGameInfo);
	for( INT i=0; i<URL.Op.Num(); i++ )
	{
		appStrcat( Options, TEXT("?") );
		appStrcat( Options, *URL.Op(i) );
		Parse( *URL.Op(i), TEXT("GAME="), GameClassName, ARRAY_COUNT(GameClassName) );
	}
	if( GLevel->IsServer() && !Info->Game )
	{
		// Get the GameInfo class.
		UClass* GameClass=NULL;
		if( !GameClassName[0] )
		{
			GameClass=Info->DefaultGameType;
			if( !GameClass )
				GameClass = StaticLoadClass( AGameInfo::StaticClass(), NULL, Client ? TEXT("ini:Engine.Engine.DefaultGame") : TEXT("ini:Engine.Engine.DefaultServerGame"), NULL, LOAD_NoFail, PackageMap );
		}
		else GameClass = StaticLoadClass( AGameInfo::StaticClass(), NULL, GameClassName, NULL, LOAD_NoFail, PackageMap );

		// Spawn the GameInfo.
		debugf( NAME_Log, TEXT("Game class is '%s'"), GameClass->GetName() );
		Info->Game = (AGameInfo*)GLevel->SpawnActor( GameClass );
		check(Info->Game!=NULL);
	}
	unguard;

	// Listen for clients.
	guard(Listen);
	if( !Client || URL.HasOption(TEXT("Listen")) )
	{
		if( GPendingLevel )
		{
			guard(CancelPendingForListen);
			check(!Pending);
			delete GPendingLevel;
			GPendingLevel = NULL;
			unguard;
		}
		FString Error;
		if( !GLevel->Listen( Error ) )
			appErrorf( LocalizeError("ServerListen"), *Error );
	}
	unguard;

	// Init detail.
	Info->bHighDetailMode = 1;
	if
	(	Client
	&&	Client->Viewports.Num()
	&&	Client->Viewports(0)->RenDev
	&&	!Client->Viewports(0)->RenDev->HighDetailActors )
		Info->bHighDetailMode = 0;

	// Init level gameplay info.
	guard(BeginPlay);
	GLevel->iFirstDynamicActor = 0;
	if( !Info->bBegunPlay )
	{
		// Lock the level.
		debugf( NAME_Log, TEXT("Bringing %s up for play (%i)..."), GLevel->GetFullName(), appRound(GetMaxTickRate()) );
		GLevel->TimeSeconds = 0;
		GLevel->GetLevelInfo()->TimeSeconds = 0;

		// Init touching actors.
		for( INT i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				for( INT j=0; j<ARRAY_COUNT(GLevel->Actors(i)->Touching); j++ )
					GLevel->Actors(i)->Touching[j] = NULL;

		// Kill off actors that aren't interesting to the client.
		INT i;
		if( !GLevel->IsServer() )
		{
			for( i=0; i<GLevel->Actors.Num(); i++ )
			{
				AActor* Actor = GLevel->Actors(i);
				if( Actor )
				{
					if( Actor->bStatic || Actor->bNoDelete )
						Exchange( Actor->Role, Actor->RemoteRole );
					else
						GLevel->DestroyActor( Actor );
				}
			}
		}

		// Init scripting.
		for( i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->InitExecution();

		// Enable actor script calls.
		Info->bBegunPlay = 1;
		Info->bStartup = 1;

		// Init the game.
#if defined(PLATFORM_64BIT)
		if( Info->Game && appStricmp(GLevel->GetOuter()->GetName(),TEXT("Entry"))==0 )
		{
			debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE skipping Entry eventInitGame on 64-bit game=%s"), Info->Game->GetFullName() );
		}
		else
#endif
		if( Info->Game )
		{
			debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE eventInitGame begin game=%s options=%s"), Info->Game->GetFullName(), Options );
			Info->Game->eventInitGame( Options, Error );
			debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE eventInitGame done game=%s error=%s"), Info->Game->GetFullName(), *Error );
		}

		UBOOL bSkipEntryScriptStartup = 0;
#if defined(PLATFORM_64BIT)
		bSkipEntryScriptStartup = appStricmp(GLevel->GetOuter()->GetName(),TEXT("Entry"))==0;
#endif
		if( bSkipEntryScriptStartup )
		{
			debugf( NAME_Log, TEXT("UT99_ANDROID_V148_ENTRY_STARTUP_SKIP skipping Entry actor script startup events on 64-bit actors=%i"), GLevel->Actors.Num() );
		}
		else
		{
			// Send PreBeginPlay.
#if defined(PLATFORM_64BIT)
			debugf( NAME_Log, TEXT("UT99_ANDROID_V167_PREBEGINPLAY_LOOP_SKIP skipping PreBeginPlay loop on 64-bit map=%s actors=%i"), GLevel->GetOuter()->GetName(), GLevel->Actors.Num() );
#else
			debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE PreBeginPlay loop begin actors=%i"), GLevel->Actors.Num() );
			for( i=0; i<GLevel->Actors.Num(); i++ )
				if( GLevel->Actors(i) )
				{
					GLevel->Actors(i)->eventPreBeginPlay();
				}
			debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE PreBeginPlay loop done") );
#endif

			// Set BeginPlay.
#if defined(PLATFORM_64BIT)
			debugf( NAME_Log, TEXT("UT99_ANDROID_V168_BEGINPLAY_LOOP_SKIP skipping BeginPlay loop on 64-bit map=%s actors=%i"), GLevel->GetOuter()->GetName(), GLevel->Actors.Num() );
#else
			debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE BeginPlay loop begin") );
			for( i=0; i<GLevel->Actors.Num(); i++ )
				if( GLevel->Actors(i) )
					GLevel->Actors(i)->eventBeginPlay();
			debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE BeginPlay loop done") );
#endif
		}

		// Set zones.
		debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE SetActorZone loop begin") );
		for( i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->SetActorZone( GLevel->Actors(i), 1, 1 );
		debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE SetActorZone loop done") );

		if( !bSkipEntryScriptStartup )
		{
#if defined(PLATFORM_64BIT)
			debugf( NAME_Log, TEXT("UT99_ANDROID_V169_POSTSTARTUP_LOOP_SKIP skipping PostBeginPlay/SetInitialState on 64-bit map=%s actors=%i"), GLevel->GetOuter()->GetName(), GLevel->Actors.Num() );
#else
			// Post begin play.
			debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE PostBeginPlay loop begin") );
			for( i=0; i<GLevel->Actors.Num(); i++ )
				if( GLevel->Actors(i) )
					GLevel->Actors(i)->eventPostBeginPlay();
			debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE PostBeginPlay loop done") );

			// Begin scripting.
			debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE SetInitialState loop begin") );
			for( i=0; i<GLevel->Actors.Num(); i++ )
				if( GLevel->Actors(i) )
					GLevel->Actors(i)->eventSetInitialState();
			debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE SetInitialState loop done") );
#endif
		}

		// Find bases
		debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE FindBases loop begin") );
		for( i=0; i<GLevel->Actors.Num(); i++ )
		{
			if( GLevel->Actors(i) ) 
			{
				if ( GLevel->Actors(i)->AttachTag != NAME_None )
				{
					//find actor to attach self onto
					for( INT j=0; j<GLevel->Actors.Num(); j++ )
					{
						if( GLevel->Actors(j) && (GLevel->Actors(j)->Tag == GLevel->Actors(i)->AttachTag) )
						{
							GLevel->Actors(i)->SetBase(GLevel->Actors(j), 0);
							break;
						}
					}
				}
				else if( !GLevel->Actors(i)->Base && GLevel->Actors(i)->bCollideWorld 
				 && (GLevel->Actors(i)->IsA(ADecoration::StaticClass()) || GLevel->Actors(i)->IsA(AInventory::StaticClass()) || GLevel->Actors(i)->IsA(APawn::StaticClass())) 
				 &&	((GLevel->Actors(i)->Physics == PHYS_None) || (GLevel->Actors(i)->Physics == PHYS_Rotating)) )
				{
					 GLevel->Actors(i)->FindBase();
					 if ( GLevel->Actors(i)->Base == Info )
						 GLevel->Actors(i)->SetBase(NULL, 0);
				}
			}
		}
		debugf( NAME_Log, TEXT("UT99_ANDROID_V145_BEGINPLAY_TRACE FindBases loop done") );
		Info->bStartup = 0;
	}
	else GLevel->TimeSeconds = GLevel->GetLevelInfo()->TimeSeconds;
	unguard;

	// Rearrange actors: static first, then others.
	guard(Rearrange);
	TArray<AActor*> Actors;
	Actors.AddItem(GLevel->Actors(0));
	Actors.AddItem(GLevel->Actors(1));
	INT i;
	for( i=2; i<GLevel->Actors.Num(); i++ )
		if( GLevel->Actors(i) && GLevel->Actors(i)->bStatic )
			Actors.AddItem( GLevel->Actors(i) );
	GLevel->iFirstDynamicActor=Actors.Num();
	for( i=2; i<GLevel->Actors.Num(); i++ )
		if( GLevel->Actors(i) && !GLevel->Actors(i)->bStatic )
			Actors.AddItem( GLevel->Actors(i) );
	GLevel->Actors.Empty();
	GLevel->Actors.Add( Actors.Num() );
	for( i=0; i<Actors.Num(); i++ )
		GLevel->Actors(i) = Actors(i);
	unguard;

	// Cleanup profiling.
#if DO_GUARD_SLOW
	guard(CleanupProfiling);
	for( TObjectIterator<UFunction> It; It; ++It )
		It->Calls = It->Cycles=0;
	GTicks=1;
	unguard;
#endif

	// Client init.
	guard(ClientInit);
	if( Client )
	{
		debugf( NAME_Log, TEXT("UT99_ANDROID_V162_LOADMAP_TRACE ClientInit begin map=%s viewports=%i server=%i"), GLevel->GetOuter()->GetName(), Client->Viewports.Num(), GLevel->IsServer() );
		// Match Viewports to actors.
		MatchViewportsToActors( Client, GLevel->IsServer() ? GLevel : GEntry, URL );
		debugf( NAME_Log, TEXT("UT99_ANDROID_V162_LOADMAP_TRACE MatchViewports done map=%s viewports=%i actor=%s"), GLevel->GetOuter()->GetName(), Client->Viewports.Num(), (Client->Viewports.Num() && Client->Viewports(0)->Actor) ? Client->Viewports(0)->Actor->GetFullName() : TEXT("None") );

		// Init brush tracker.
		if( appStricmp(GLevel->GetOuter()->GetName(),TEXT("Entry"))!=0 )//!!
		{
			debugf( NAME_Log, TEXT("UT99_ANDROID_V162_LOADMAP_TRACE BrushTracker begin map=%s"), GLevel->GetOuter()->GetName() );
			GLevel->BrushTracker = GNewBrushTracker( GLevel );
			debugf( NAME_Log, TEXT("UT99_ANDROID_V162_LOADMAP_TRACE BrushTracker done tracker=%i"), GLevel->BrushTracker != NULL );
		}

		// Set up audio.
		if( Audio )
		{
			debugf( NAME_Log, TEXT("UT99_ANDROID_V162_LOADMAP_TRACE Audio SetViewport begin viewport=%i"), Client->Viewports.Num() );
			Audio->SetViewport( Audio->GetViewport() );
			debugf( NAME_Log, TEXT("UT99_ANDROID_V162_LOADMAP_TRACE Audio SetViewport done") );
		}

		// Reset viewports.
		for( INT i=0; i<Client->Viewports.Num(); i++ )
		{
			UViewport* Viewport = Client->Viewports(i);
			debugf( NAME_Log, TEXT("UT99_ANDROID_V162_LOADMAP_TRACE ResetViewport index=%i actor=%s rendev=%s size=%ix%i"), i, Viewport->Actor ? Viewport->Actor->GetFullName() : TEXT("None"), Viewport->RenDev ? Viewport->RenDev->GetClass()->GetName() : TEXT("None"), Viewport->SizeX, Viewport->SizeY );
			Viewport->Input->ResetInput();
			if( Viewport->RenDev )
				Viewport->RenDev->Flush(1);
		}
		debugf( NAME_Log, TEXT("UT99_ANDROID_V162_LOADMAP_TRACE ClientInit done map=%s"), GLevel->GetOuter()->GetName() );
	}
	unguard;

	// Init detail.
	debugf( NAME_Log, TEXT("UT99_ANDROID_V162_LOADMAP_TRACE DetailChange begin high=%i map=%s"), Info->bHighDetailMode, GLevel->GetOuter()->GetName() );
	GLevel->DetailChange( Info->bHighDetailMode );
	debugf( NAME_Log, TEXT("UT99_ANDROID_V162_LOADMAP_TRACE DetailChange done map=%s"), GLevel->GetOuter()->GetName() );

	// Remember the URL.
	guard(RememberURL);
	LastURL = URL;
	unguard;

	// Remember DefaultPlayer options.
	if( GIsClient )
	{
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Name" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Team" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Class"), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Skin" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Face" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Voice" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("OverrideClass" ), TEXT("User") );
	}

	// Successfully started local level.
	debugf( NAME_Log, TEXT("UT99_ANDROID_V162_LOADMAP_TRACE LoadMap success map=%s actors=%i viewports=%i"), GLevel->GetOuter()->GetName(), GLevel->Actors.Num(), Client ? Client->Viewports.Num() : 0 );
	return GLevel;
	unguard;
}

/*-----------------------------------------------------------------------------
	Game Viewport functions.
-----------------------------------------------------------------------------*/

//
// Draw a global view.
//
void UGameEngine::Draw( UViewport* Viewport, UBOOL Blit, BYTE* HitData, INT* HitSize )
{
	guard(UGameEngine::Draw);
	static INT DrawTraceCount = 0;

	// If not up and running yet, don't draw.
	if( !GIsRunning )
	{
		if( DrawTraceCount < 5 )
		{
			debugf( NAME_Log, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE Draw skipped GIsRunning=0 count=%i"), DrawTraceCount );
			DrawTraceCount++;
		}
		return;
	}
	UpdateConnectingMessage();

	// Get view location.
	AActor*      ViewActor    = Viewport->Actor;
	FVector      ViewLocation = ViewActor->Location;
	FRotator     ViewRotation = ViewActor->Rotation;
	Viewport->Actor->eventPlayerCalcView( ViewActor, ViewLocation, ViewRotation );
	check(ViewActor);

	// Precaching message.
	BYTE SavedAction = ViewActor->Level->LevelAction;
	if( Viewport->RenDev->PrecacheOnFlip && !Viewport->bSuspendPrecaching )
		ViewActor->Level->LevelAction = LEVACT_Precaching;

	// See if viewer is inside world.
	DWORD LockFlags=0;
	FCheckResult Hit;
	if( !GLevel->Model->PointCheck(Hit,NULL,ViewLocation,FVector(0,0,0),0) )
		LockFlags |= LOCKR_ClearScreen;

#if defined(LEGEND) //MWP
	if( Viewport->Actor->IsA( APlayerPawn::StaticClass() ) )
	{
		// call the PlayerPawn Render Control Interface (RCI) to assess clear-screen operations
		if( Viewport->Actor->ClearScreen() )
		{
			LockFlags |= LOCKR_ClearScreen;
		}

		// call the PlayerPawn Render Control Interface (RCI) to assess lighting recomputation
		//
		// WARNING: RecomputeLighting() should *not* return false regularly, or rendering 
		//          performance will be severly compromised
		if( Viewport->Actor->RecomputeLighting() )
		{
			guard(RecomputeLighting);
			Flush();
			unguard;
		}
	}
#endif

	// Lock the Viewport.
	check(Render);
	FPlane FlashScale = Client->ScreenFlashes ? 0.5*Viewport->Actor->FlashScale : FVector(0.5,0.5,0.5);
	FPlane FlashFog   = Client->ScreenFlashes ? Viewport->Actor->FlashFog : FVector(0,0,0);
	FlashScale.X = Clamp( FlashScale.X, 0.f, 1.f );
	FlashScale.Y = Clamp( FlashScale.Y, 0.f, 1.f );
	FlashScale.Z = Clamp( FlashScale.Z, 0.f, 1.f );
	FlashFog.X   = Clamp( FlashFog.X  , 0.f, 1.f );
	FlashFog.Y   = Clamp( FlashFog.Y  , 0.f, 1.f );
	FlashFog.Z   = Clamp( FlashFog.Z  , 0.f, 1.f );
	if( DrawTraceCount < 5 )
		debugf( NAME_Log, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE Draw begin count=%i Size=%ix%i Blit=%i LockFlags=0x%08X Actor=%s"), DrawTraceCount, Viewport->SizeX, Viewport->SizeY, Blit, LockFlags, ViewActor ? ViewActor->GetName() : TEXT("None") );
	if( Viewport->Lock(FlashScale,FlashFog,FPlane(0,0,0,0),LockFlags,HitData,HitSize) )
	{
		// Setup rendering coords.
		FSceneNode* Frame = Render->CreateMasterFrame( Viewport, ViewLocation, ViewRotation, NULL );

		// Update level audio.
		if( Audio )
		{
			clock(GLevel->AudioTickCycles);
			Audio->Update( ViewActor->Region, Frame->Coords );
			unclock(GLevel->AudioTickCycles);
		}

		// Render.
		Render->PreRender( Frame );
		Viewport->Canvas->Render = Render;
		if( Viewport->Console )
			Viewport->Console->PreRender( Frame );
		Viewport->Canvas->Update( Frame );
		Viewport->Actor->eventPreRender( Viewport->Canvas );
#if defined(LEGEND) //MWP
		INT SaveXB = Frame->XB, SaveYB = Frame->YB, SaveX = Frame->X, SaveY = Frame->Y;
		Frame->XB += Viewport->Canvas->OrgX;
		Frame->YB += Viewport->Canvas->OrgY;
		Frame->X = Viewport->Canvas->ClipX;
		Frame->Y = Viewport->Canvas->ClipY;
		Frame->ComputeRenderSize();
#endif
		if( Frame->X>0 && Frame->Y>0 && (!Viewport->Console || Viewport->Console->GetDrawWorld()) )
			Render->DrawWorld( Frame );
#if defined(LEGEND) //MWP
		Frame->XB = SaveXB, Frame->YB = SaveYB, Frame->X = SaveX, Frame->Y = SaveY;
		Frame->ComputeRenderSize();
#endif
		Viewport->RenDev->EndFlash();
		Viewport->Actor->eventPostRender( Viewport->Canvas );
		if( Viewport->Console )
		{
			Viewport->Console->PostRender( Frame );
			Viewport->Console->eventPostRender( Viewport->Canvas );
		}
		if( Audio )
			Audio->PostRender( Frame );

#if 0
/* BEGIN BETA VERSION */
		if(GLevel && GLevel->GetLevelInfo() && GLevel->GetLevelInfo()->Game && FString(GLevel->GetLevelInfo()->Game->GetClass()->GetName()) == FString(TEXT("UTIntro")))
		{
			if ( ((AGameInfo*) AGameInfo::StaticClass()->GetDefaultObject())->DemoBuild == 0 )
			{
				// "BETA VERSION" XOR'd with BetaDecoder
				static TCHAR BetaCypher[] = { 67, 4, 50, 41, 108, 125, 82, 27, 46, 55, 121, 25 };
				static TCHAR BetaDecoder[] = { 1, 65, 102, 104, 76, 43, 23, 73, 125, 126, 54, 87, 33, 78, 0 };
				static TCHAR BetaDecoded[] = TEXT("            "); // gets replaced with "BETA VERSION"

				for(INT i=0; BetaDecoded[i]; i++)
						BetaDecoded[i] = BetaCypher[i] ^ BetaDecoder[i];
			
				Frame->Viewport->Canvas->Color = FColor(255,255,255);
				Frame->Viewport->Canvas->CurX=0;
				Frame->Viewport->Canvas->CurY=0;
				Frame->Viewport->Canvas->WrappedPrintf( Frame->Viewport->Canvas->SmallFont, 0, BetaDecoded );
				Frame->Viewport->Canvas->CurX=Frame->Viewport->Canvas->ClipX - 72;
				Frame->Viewport->Canvas->CurY=0;
				Frame->Viewport->Canvas->WrappedPrintf( Frame->Viewport->Canvas->SmallFont, 0, BetaDecoded );
				Frame->Viewport->Canvas->CurX=0;
				Frame->Viewport->Canvas->CurY=Frame->Viewport->Canvas->ClipY - 10;
				Frame->Viewport->Canvas->WrappedPrintf( Frame->Viewport->Canvas->SmallFont, 0, BetaDecoded );
				Frame->Viewport->Canvas->CurX=Frame->Viewport->Canvas->ClipX - 72;
				Frame->Viewport->Canvas->CurY=Frame->Viewport->Canvas->ClipY - 10;
				Frame->Viewport->Canvas->WrappedPrintf( Frame->Viewport->Canvas->SmallFont, 0, BetaDecoded );
			}
		}
/* END BETA VERSION */
#endif

		Viewport->Canvas->Render = 0;
		Render->PostRender( Frame );
		Viewport->Unlock( Blit );
		Render->FinishMasterFrame();
		if( DrawTraceCount < 5 )
			debugf( NAME_Log, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE Draw finished count=%i"), DrawTraceCount );
	}
	else if( DrawTraceCount < 5 )
	{
		debugf( NAME_Log, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE Draw lock failed count=%i"), DrawTraceCount );
	}
	if( DrawTraceCount < 5 )
		DrawTraceCount++;
	ViewActor->Level->LevelAction = SavedAction;

	// Precache now if desired.
	if( Viewport->RenDev->PrecacheOnFlip && !Viewport->bSuspendPrecaching )
	{
		Viewport->RenDev->PrecacheOnFlip = 0;
		if ( !ViewActor->Level->bNeverPrecache )
			Render->Precache( Viewport );
	}

	unguard;
}

void ExportTravel( FOutputDevice& Out, AActor* Actor )
{
	guard(ExportTravel);
	debugf( TEXT("Exporting travelling actor of class %s"), Actor->GetClass()->GetPathName() );//!!xyzzy
	check(Actor);
	if( !Actor->bTravel )
		return;
	Out.Logf( TEXT("Class=%s Name=%s\r\n{\r\n"), Actor->GetClass()->GetPathName(), Actor->GetName() );
	for( TFieldIterator<UProperty> It(Actor->GetClass()); It; ++It )
	{
		for( INT Index=0; Index<It->ArrayDim; Index++ )
		{
			TCHAR Value[1024];
			if
			(	(It->PropertyFlags & CPF_Travel)
			&&	It->ExportText( Index, Value, (BYTE*)Actor, &Actor->GetClass()->Defaults(0), 0 ) )
			{
				Out.Log( It->GetName() );
				if( It->ArrayDim!=1 )
					Out.Logf( TEXT("[%i]"), Index );
				Out.Log( TEXT("=") );
				UObjectProperty* Ref = Cast<UObjectProperty>( *It );
				if( Ref && Ref->PropertyClass->IsChildOf(AActor::StaticClass()) )
				{
					UObject* Obj = *(UObject**)( (BYTE*)Actor + It->Offset + Index*It->ElementSize );
					Out.Logf( TEXT("%s\r\n"), Obj ? Obj->GetName() : TEXT("None") );
				}
				Out.Logf( TEXT("%s\r\n"), Value );
			}
		}
	}
	Out.Logf( TEXT("}\r\n") );
	unguard;
}

//
// Jumping viewport.
//
void UGameEngine::SetClientTravel( UPlayer* Player, const TCHAR* NextURL, UBOOL bItems, ETravelType TravelType )
{
	guard(UGameEngine::SetClientTravel);
	check(Player);

	UViewport* Viewport    = CastChecked<UViewport>( Player );
	Viewport->TravelURL    = NextURL;
	Viewport->TravelType   = TravelType;
	Viewport->bTravelItems = bItems;

	unguard;
}

/*-----------------------------------------------------------------------------
	Tick.
-----------------------------------------------------------------------------*/

//
// Get tick rate limitor.
//
FLOAT UGameEngine::GetMaxTickRate()
{
	guard(UGameEngine::GetMaxTickRate);
	static UBOOL LanPlay = ParseParam(appCmdLine(),TEXT("lanplay"));
	if( GLevel && GLevel->NetDriver && !GIsClient )
		return Clamp<INT>( LanPlay ? GLevel->NetDriver->LanServerMaxTickRate : GLevel->NetDriver->NetServerMaxTickRate, 10, 120 );
	else if( GLevel && GLevel->NetDriver && GLevel->NetDriver->ServerConnection )
		return GLevel->NetDriver->ServerConnection->CurrentNetSpeed/64;
	else if( GLevel && GLevel->DemoRecDriver && !GLevel->DemoRecDriver->ServerConnection )
		return Clamp<INT>( LanPlay ? GLevel->NetDriver->LanServerMaxTickRate : GLevel->DemoRecDriver->NetServerMaxTickRate, 10, 120 );
	else
		return 0;
	unguard;
}

//
// Update everything.
//
void UGameEngine::Tick( FLOAT DeltaSeconds )
{
	guard(UGameEngine::Tick);
	INT LocalTickCycles=0;
	clock(LocalTickCycles);

	// If all viewports closed, time to exit.
	if( Client && Client->Viewports.Num()==0 )
	{
		debugf( TEXT("All Windows Closed") );
		appRequestExit( 0 );
		return;
	}

	// If game is paused, release the cursor.
	static UBOOL WasPaused=1;
	if
	(	Client
	&&	Client->Viewports.Num()==1
	&&	GLevel
	&&	!Client->Viewports(0)->IsFullscreen() )
	{
		UBOOL IsPaused
		=	GLevel->GetLevelInfo()->Pauser!=TEXT("")
		||	Client->Viewports(0)->Actor->bShowMenu
		||	Client->Viewports(0)->bShowWindowsMouse;
		if( IsPaused && !WasPaused )
			Client->Viewports(0)->SetMouseCapture( 0, 0, 0 );
		else if( WasPaused && !IsPaused && Client->CaptureMouse )
			Client->Viewports(0)->SetMouseCapture( 1, 1, 1 );
		WasPaused = IsPaused;
	}
	else WasPaused=0;

	// Update subsystems.
	UObject::StaticTick();				
	GCache.Tick();

	// Update the level.
	guard(TickLevel);
	GameCycles=0;
	clock(GameCycles);
	if( GLevel )
	{
		// Decide whether to drop high detail because of frame rate
		if ( Client )
		{
			GLevel->GetLevelInfo()->bDropDetail = (DeltaSeconds > 1.f/Clamp(Client->MinDesiredFrameRate,1.f,100.f));
			GLevel->GetLevelInfo()->bAggressiveLOD = (DeltaSeconds > 1.f/Clamp(Client->MinDesiredFrameRate - 5.f,1.f,100.f));;
		}
		// tick the level
		GLevel->Tick( LEVELTICK_All, DeltaSeconds );
	}
	if( GEntry && GEntry!=GLevel )
		GEntry->Tick( LEVELTICK_All, DeltaSeconds );
	if( Client && Client->Viewports.Num() && Client->Viewports(0)->Actor->GetLevel()!=GLevel )
		Client->Viewports(0)->Actor->GetLevel()->Tick( LEVELTICK_All, DeltaSeconds );
	unclock(GameCycles);
	unguard;

	// Handle server travelling.
	guard(ServerTravel);
	if( GLevel && GLevel->GetLevelInfo()->NextURL!=TEXT("") )
	{
		if( (GLevel->GetLevelInfo()->NextSwitchCountdown-=DeltaSeconds) <= 0.0 )
		{
			// Travel to new level, and exit.
			TMap<FString,FString> TravelInfo;
			if( GLevel->GetLevelInfo()->NextURL==TEXT("?RESTART") )
			{
				TravelInfo = GLevel->TravelInfo;
			}
			else if( GLevel->GetLevelInfo()->bNextItems )
			{
				TravelInfo = GLevel->TravelInfo;
				for( INT i=0; i<GLevel->Actors.Num(); i++ )
				{
					APlayerPawn* P = Cast<APlayerPawn>( GLevel->Actors(i) );
					if( P && P->Player )
					{
						// Export items and self.
						FStringOutputDevice PlayerTravelInfo;
						ExportTravel( PlayerTravelInfo, P );
						for( AActor* Inv=P->Inventory; Inv; Inv=Inv->Inventory )
							ExportTravel( PlayerTravelInfo, Inv );
						TravelInfo.Set( *P->PlayerReplicationInfo->PlayerName, *PlayerTravelInfo );

						// Prevent local ClientTravel from taking place, since it will happen automatically.
						if( Cast<UViewport>( P->Player ) )
							Cast<UViewport>( P->Player )->TravelURL = TEXT("");
					}
				}
			}
			debugf( TEXT("Server switch level: %s"), *GLevel->GetLevelInfo()->NextURL );
			FString Error;
			Browse( FURL(&LastURL,*GLevel->GetLevelInfo()->NextURL,TRAVEL_Relative), &TravelInfo, Error );
			GLevel->GetLevelInfo()->NextURL = TEXT("");
			return;
		}
	}
	unguard;

	// Handle client travelling.
	guard(ClientTravel);
	if( Client && Client->Viewports.Num() && Client->Viewports(0)->TravelURL!=TEXT("") )
	{
		// Travel to new level, and exit.
		UViewport* Viewport = Client->Viewports( 0 );
		TMap<FString,FString> TravelInfo;

		// Export items.
		if( appStricmp(*Viewport->TravelURL,TEXT("?RESTART"))==0 )
		{
			TravelInfo = GLevel->TravelInfo;
		}
		else if( Viewport->bTravelItems )
		{
			debugf( TEXT("Export travel for: %s"), *Viewport->Actor->PlayerReplicationInfo->PlayerName );
			FStringOutputDevice PlayerTravelInfo;
			ExportTravel( PlayerTravelInfo, Viewport->Actor );
			for( AActor* Inv=Viewport->Actor->Inventory; Inv; Inv=Inv->Inventory )
				ExportTravel( PlayerTravelInfo, Inv );
			TravelInfo.Set( *Viewport->Actor->PlayerReplicationInfo->PlayerName, *PlayerTravelInfo );
		}
		FString Error;
		Browse( FURL(&LastURL,*Viewport->TravelURL,Viewport->TravelType), &TravelInfo, Error );
		Viewport->TravelURL=TEXT("");

		return;
	}
	unguard;

	// Update the pending level.
	guard(TickPending);
	if( GPendingLevel )
	{
		GPendingLevel->Tick( DeltaSeconds );
		if( GPendingLevel->Error!=TEXT("") )
		{
			// Pending connect failed.
			guard(PendingFailed);
			SetProgress( LocalizeError("ConnectionFailed"), *GPendingLevel->Error, 4.0 );
			debugf( NAME_Log, LocalizeError("Pending"), *GPendingLevel->URL.String(), *GPendingLevel->Error );
			delete GPendingLevel;
			GPendingLevel = NULL;
			unguard;
		}
		else if( GPendingLevel->Success && !GPendingLevel->FilesNeeded && !GPendingLevel->SentJoin )
		{
			// Attempt to load the map.
			FString Error;
			guard(AttemptLoadPending);
			LoadMap( GPendingLevel->URL, GPendingLevel, NULL, Error );
			if( Error!=TEXT("") )
			{
				SetProgress( LocalizeError("ConnectionFailed"), *Error, 4.0 );
			}
			else if( !GPendingLevel->LonePlayer )
			{
				// Show connecting message, cause precaching to occur.
				GLevel->GetLevelInfo()->LevelAction = LEVACT_Connecting;
				GEntry->GetLevelInfo()->LevelAction = LEVACT_Connecting;
				if( Client )
					Client->Tick();

				// Send join.
				GPendingLevel->SendJoin();
				GPendingLevel->NetDriver = NULL;
				GPendingLevel->DemoRecDriver = NULL;
			}
			unguard;

			// Kill the pending level.
			guard(KillPending);
			delete GPendingLevel;
			GPendingLevel = NULL;
			unguard;
		}
	}
	unguard;

	// Render everything.
	guard(ClientTick);
	INT LocalClientCycles=0;
	if( Client )
	{
		clock(LocalClientCycles);
		Client->Tick();
		unclock(LocalClientCycles);
	}
	ClientCycles=LocalClientCycles;
	unguard;

	unclock(LocalTickCycles);
	TickCycles=LocalTickCycles;
	GTicks++;
	unguard;
}

/*-----------------------------------------------------------------------------
	Saving the game.
-----------------------------------------------------------------------------*/

//
// Save the current game state to a file.
//
void UGameEngine::SaveGame( INT Position )
{
	guard(UGameEngine::SaveGame);

	TCHAR Filename[256];
	GFileManager->MakeDirectory( *GSys->SavePath, 0 );
	appSprintf( Filename, TEXT("%s") PATH_SEPARATOR TEXT("Save%i.usa"), *GSys->SavePath, Position );
	GLevel->GetLevelInfo()->LevelAction=LEVACT_Saving;
	PaintProgress();
	GWarn->BeginSlowTask( LocalizeProgress("Saving"), 1, 0 );
	if( GLevel->BrushTracker )
	{
		delete GLevel->BrushTracker;
		GLevel->BrushTracker = NULL;
	}
	GLevel->CleanupDestroyed( 1 );
	if( SavePackage( GLevel->GetOuter(), GLevel, 0, Filename, GLog ) )
	{
		// Copy the hub stack.
		INT i;
		for( i=0; i<GLevel->GetLevelInfo()->HubStackLevel; i++ )
		{
			TCHAR Src[256], Dest[256];
			appSprintf( Src, TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, i );
			appSprintf( Dest, TEXT("%s") PATH_SEPARATOR TEXT("Save%i%i.usa"), *GSys->SavePath, Position, i );
			GFileManager->Copy( Src, Dest );
		}
		while( 1 )
		{
			appSprintf( Filename, TEXT("%s") PATH_SEPARATOR TEXT("Save%i%i.usa"), *GSys->SavePath, Position, i++ );
			if( GFileManager->FileSize(Filename)<=0 )
				break;
			GFileManager->Delete( Filename );
		}
	}
	for( INT i=0; i<GLevel->Actors.Num(); i++ )
		if( Cast<AMover>(GLevel->Actors(i)) )
			Cast<AMover>(GLevel->Actors(i))->SavedPos = FVector(-1,-1,-1);
	GLevel->BrushTracker = GNewBrushTracker( GLevel );
	GWarn->EndSlowTask();
	GLevel->GetLevelInfo()->LevelAction=LEVACT_None;
	GCache.Flush();

	unguard;
}

/*-----------------------------------------------------------------------------
	Mouse feedback.
-----------------------------------------------------------------------------*/

//
// Mouse delta while dragging.
//
void UGameEngine::MouseDelta( UViewport* Viewport, DWORD ClickFlags, FLOAT DX, FLOAT DY )
{
	guard(UGameEngine::MouseDelta);
	if
	(	(ClickFlags & MOUSE_FirstHit)
	&&	Client
	&&	Client->Viewports.Num()==1
	&&	GLevel
	&&	!Client->Viewports(0)->IsFullscreen()
	&&	GLevel->GetLevelInfo()->Pauser==TEXT("")
	&&	!Viewport->Actor->bShowMenu
	&&  !Viewport->bShowWindowsMouse )
	{
		Viewport->SetMouseCapture( 1, 1, 1 );
	}
	else if( (ClickFlags & MOUSE_LastRelease) && !Client->CaptureMouse )
	{
		Viewport->SetMouseCapture( 0, 0, 0 );
	}
	unguard;
}

//
// Absolute mouse position.
//
void UGameEngine::MousePosition( UViewport* Viewport, DWORD ClickFlags, FLOAT X, FLOAT Y )
{
	guard(UGameEngine::MousePosition);

	if( Viewport )
	{
		Viewport->WindowsMouseX = X;
		Viewport->WindowsMouseY = Y;
	}

	unguard;
}

//
// Mouse clicking.
//
void UGameEngine::Click( UViewport* Viewport, DWORD ClickFlags, FLOAT X, FLOAT Y )
{
	guard(UGameEngine::Click);
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
