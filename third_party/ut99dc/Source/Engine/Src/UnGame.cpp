/*=============================================================================
	UnGame.cpp: Unreal game engine.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRender.h"
#include "UnNet.h"

#if PLATFORM_ANDROID
#ifndef UT99_ANDROID_FRAME_TRACE
#define UT99_ANDROID_FRAME_TRACE 0
#endif
#ifndef UT99_ANDROID_SHOW_FPS_COUNTER
#define UT99_ANDROID_SHOW_FPS_COUNTER 0
#endif
UBOOL GAndroidFrontendMenuRequested = 0;
INT GAndroidInFrontendConsolePostRender = 0;
INT GAndroidInterpPositionScriptOffset = INDEX_NONE;
INT GAndroidInterpRateScriptOffset = INDEX_NONE;
INT GAndroidInterpGameSpeedScriptOffset = INDEX_NONE;
INT GAndroidInterpFovScriptOffset = INDEX_NONE;
INT GAndroidInterpScreenFlashScaleScriptOffset = INDEX_NONE;
INT GAndroidInterpScreenFlashFogScriptOffset = INDEX_NONE;
#endif

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
		// debugf( NAME_Log, TEXT("UT99_ANDROID_V180_DEFAULT_OFFSET_MIGRATE class=%s property=%s old=%i native=%i size=%i"),
		// 	TestClass->GetFullName(),
		// 	Property->GetFullName(),
		// 	OldOffset,
		// 	NewOffset,
		// 	Size );
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

#if PLATFORM_ANDROID
static UBOOL AndroidKnownObject( UObject* Object )
{
	guardSlow(AndroidKnownObject);
	if( !Object )
		return 1;
	for( FObjectIterator It; It; ++It )
		if( *It == Object )
			return 1;
	return 0;
	unguardSlow;
}

static void AndroidDumpConsoleWindowState( UViewport* Viewport, const TCHAR* Context )
{
	guardSlow(AndroidDumpConsoleWindowState);
	if( !Viewport || !Viewport->Console || !Viewport->Console->GetClass() )
		return;
	UConsole* Console = Viewport->Console;
	const TCHAR* StateName =
		(Console->GetStateFrame() && Console->GetStateFrame()->StateNode)
		? Console->GetStateFrame()->StateNode->GetName()
		: TEXT("None");
	debugf( NAME_Log, TEXT("UT99_ANDROID_V278_CONSOLE_STATE context=%s console=%s class=%s state=%s drawWorld=%i viewport=%ix%i actor=%s"),
		Context,
		Console->GetFullName(),
		Console->GetClass()->GetFullName(),
		StateName,
		Console->GetDrawWorld() ? 1 : 0,
		Viewport->SizeX,
		Viewport->SizeY,
		Viewport->Actor ? Viewport->Actor->GetFullName() : TEXT("None") );
	INT Logged = 0;
	for( TFieldIterator<UProperty> It(Console->GetClass()); It && Logged<96; ++It )
	{
		UProperty* Prop = *It;
		if( !Prop )
			continue;
		if( Prop->Offset < 0 || Prop->Offset + Prop->ElementSize > Console->GetClass()->GetPropertiesSize() )
			continue;
		UObjectProperty* ObjProp = Cast<UObjectProperty>( Prop );
		UBoolProperty* BoolProp = Cast<UBoolProperty>( Prop );
		if( ObjProp )
		{
			UObject* Value = *(UObject**)((BYTE*)Console + Prop->Offset);
			UBOOL Known = AndroidKnownObject( Value );
			debugf( NAME_Log, TEXT("UT99_ANDROID_V278_CONSOLE_OBJ prop=%s offset=%i class=%s value=%p known=%i valueName=%s"),
				Prop->GetName(),
				Prop->Offset,
				ObjProp->PropertyClass ? ObjProp->PropertyClass->GetName() : TEXT("None"),
				Value,
				Known ? 1 : 0,
				(Known && Value) ? Value->GetFullName() : TEXT("None") );
			Logged++;
		}
		else if( BoolProp )
		{
			UBOOL Value = (*(BITFIELD*)((BYTE*)Console + Prop->Offset) & BoolProp->BitMask) != 0;
			debugf( NAME_Log, TEXT("UT99_ANDROID_V278_CONSOLE_BOOL prop=%s offset=%i mask=0x%08x value=%i"),
				Prop->GetName(),
				Prop->Offset,
				(DWORD)BoolProp->BitMask,
				Value ? 1 : 0 );
			Logged++;
		}
	}
	unguardSlow;
}

static void AndroidForceConsoleFrontendDefaults( UConsole* Console )
{
	guard(AndroidForceConsoleFrontendDefaults);
	if( !Console || !Console->GetClass() )
		return;
	UStrProperty* RootWindowProp = Cast<UStrProperty>( FindNativeProperty( Console->GetClass(), TEXT("RootWindow") ) );
	UBoolProperty* ShowDesktopProp = Cast<UBoolProperty>( FindNativeProperty( Console->GetClass(), TEXT("ShowDesktop") ) );
	if( RootWindowProp )
	{
		*(FString*)((BYTE*)Console + RootWindowProp->Offset) = TEXT("UMenu.UMenuRootWindow");
	}
	if( ShowDesktopProp )
	{
		*(BITFIELD*)((BYTE*)Console + ShowDesktopProp->Offset) |= ShowDesktopProp->BitMask;
	}
	debugf( NAME_Init, TEXT("UT99_ANDROID_V280_CONSOLE_FRONTEND_DEFAULTS class=%s rootProp=%i root=%s showDesktopProp=%i"),
		Console->GetClass()->GetFullName(),
		RootWindowProp ? RootWindowProp->Offset : -1,
		RootWindowProp ? **(FString*)((BYTE*)Console + RootWindowProp->Offset) : TEXT("None"),
		ShowDesktopProp ? ShowDesktopProp->Offset : -1 );
	unguard;
}

static UBOOL AndroidIsFrontendMap( ULevel* Level )
{
	guard(AndroidIsFrontendMap);
	if( !Level || !Level->GetOuter() )
		return 0;
	return appStricmp( Level->GetOuter()->GetName(), TEXT("UT-Logo-Map") ) == 0
		|| appStricmp( Level->GetOuter()->GetName(), TEXT("Entry") ) == 0;
	unguard;
}

static void AndroidWarmFrontendMenuAssets( UViewport* Viewport )
{
	guard(AndroidWarmFrontendMenuAssets);
	ULevel* ViewLevel = (Viewport && Viewport->Actor) ? Viewport->Actor->GetLevel() : NULL;
	if( !Viewport || !Viewport->Console )
		return;

	static UBOOL bWarmed = 0;
	if( bWarmed )
		return;
	bWarmed = 1;

	const DOUBLE StartTime = appSeconds();
	INT LoadedClasses = 0;
	INT LoadedObjects = 0;

	const TCHAR* ClassNames[] =
	{
		TEXT("UMenu.UMenuRootWindow"),
		TEXT("UMenu.UMenuMenuBar"),
		TEXT("UMenu.UMenuBlueLookAndFeel"),
		TEXT("UTMenu.UTGameMenu"),
		TEXT("UTMenu.UTMultiplayerMenu"),
		TEXT("UTMenu.UTOptionsMenu"),
		TEXT("UTMenu.UTFadeTextArea"),
		TEXT("UTMenu.UTConsole"),
		TEXT("UWindow.WindowConsole")
	};
	for( INT i=0; i<ARRAY_COUNT(ClassNames); i++ )
	{
		if( UObject::StaticLoadClass( UObject::StaticClass(), NULL, ClassNames[i], NULL, LOAD_NoWarn, NULL ) )
			LoadedClasses++;
	}

	const TCHAR* ObjectNames[] =
	{
		TEXT("Engine.SmallFont"),
		TEXT("Engine.MedFont"),
		TEXT("UWindowFonts.Tahoma10"),
		TEXT("UWindowFonts.Tahoma20"),
		TEXT("LadderFonts.UTLadder12"),
		TEXT("LadderFonts.UTLadder18"),
		TEXT("Engine.ConsoleBack"),
		TEXT("Engine.Border"),
		TEXT("UMenu.WindowOpen"),
		TEXT("UMenu.LittleSelect"),
		TEXT("UMenu.BigSelect")
	};
	for( INT i=0; i<ARRAY_COUNT(ObjectNames); i++ )
	{
		if( UObject::StaticLoadObject( UObject::StaticClass(), NULL, ObjectNames[i], NULL, LOAD_NoWarn, NULL ) )
			LoadedObjects++;
	}

	AndroidForceConsoleFrontendDefaults( Viewport->Console );
	UObjectProperty* ConsoleClassProp = Cast<UObjectProperty>( FindNativeProperty( Viewport->Console->GetClass(), TEXT("ConsoleClass") ) );
	UClass* ConsoleWindowClass = ConsoleClassProp ? Cast<UClass>( *(UObject**)((BYTE*)Viewport->Console + ConsoleClassProp->Offset) ) : NULL;
	if( ConsoleClassProp && !ConsoleWindowClass )
	{
		ConsoleWindowClass = UObject::StaticLoadClass( UObject::StaticClass(), NULL, TEXT("UWindow.UWindowConsoleWindow"), NULL, LOAD_NoWarn, NULL );
		if( ConsoleWindowClass )
			*(UObject**)((BYTE*)Viewport->Console + ConsoleClassProp->Offset) = ConsoleWindowClass;
	}
	const UBOOL bFrontendMap = AndroidIsFrontendMap(ViewLevel);
	if( bFrontendMap )
	{
		GAndroidFrontendMenuRequested = 1;
		if( Viewport->Actor )
			Viewport->Actor->bShowMenu = 1;
		Viewport->Console->GotoState( FName(TEXT("UWindow")) );
	}

	UFunction* LaunchFunction = Viewport->Console->FindFunction( FName(TEXT("LaunchUWindow"), FNAME_Find) );
	if( bFrontendMap && LaunchFunction && LaunchFunction->ParmsSize == 0 )
		Viewport->Console->ProcessEvent( LaunchFunction, NULL );

	debugf( NAME_Init, TEXT("UT99_ANDROID_V332_FRONTEND_MENU_WARMUP classes=%i/%i objects=%i/%i frontend=%i launch=%i consoleClass=%s rootMap=%s ms=%f console=%s actor=%s"),
		LoadedClasses,
		(INT)ARRAY_COUNT(ClassNames),
		LoadedObjects,
		(INT)ARRAY_COUNT(ObjectNames),
		bFrontendMap ? 1 : 0,
		(bFrontendMap && LaunchFunction) ? 1 : 0,
		ConsoleWindowClass ? ConsoleWindowClass->GetFullName() : TEXT("None"),
		ViewLevel && ViewLevel->GetOuter() ? ViewLevel->GetOuter()->GetName() : TEXT("None"),
		(appSeconds() - StartTime) * 1000.0,
		Viewport->Console->GetFullName(),
		Viewport->Actor ? Viewport->Actor->GetFullName() : TEXT("None") );
	unguard;
}
#endif

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

static UFunction* FindNativeFunction( UClass* Class, FName FunctionName )
{
	guard(FindNativeFunction);
	if( !Class || FunctionName==NAME_None )
		return NULL;
	for( TFieldIterator<UFunction> It(Class); It; ++It )
		if( It->GetFName()==FunctionName )
			return *It;
	return NULL;
	unguard;
}

static void FixupNativeFunctionParamOffset( UClass* Class, FName FunctionName, const TCHAR* Name, INT Offset, INT NativeParmsSize )
{
	guard(FixupNativeFunctionParamOffset);
	UFunction* Function = FindNativeFunction( Class, FunctionName );
	if( !Function )
	{
		debugf( NAME_Warning, TEXT("UT99_ANDROID_V215_FUNCTION_OFFSET_MISSING class=%s function=%s param=%s native=%i"),
			Class ? Class->GetFullName() : TEXT("None"),
			*FunctionName,
			Name,
			Offset );
		return;
	}
	FixupNativeStructMemberOffset( Function, Name, Offset );
	if( Function->ParmsSize != NativeParmsSize )
	{
		debugf( NAME_Warning, TEXT("UT99_ANDROID_V215_FUNCTION_PARMS_SIZE_FIX function=%s script=%i native=%i"),
			Function->GetFullName(),
			Function->ParmsSize,
			NativeParmsSize );
		Function->ParmsSize = NativeParmsSize;
	}
	if( Function->GetPropertiesSize() < NativeParmsSize )
	{
		debugf( NAME_Warning, TEXT("UT99_ANDROID_V215_FUNCTION_PROPS_SIZE_FIX function=%s script=%i native=%i"),
			Function->GetFullName(),
			Function->GetPropertiesSize(),
			NativeParmsSize );
		Function->SetPropertiesSize( NativeParmsSize );
	}
	unguard;
}

static void FixupNativeFunctionParams()
{
	guard(FixupNativeFunctionParams);
	FixupNativeFunctionParamOffset( APawn::StaticClass(), ENGINE_ClientHearSound, TEXT("Actor"), STRUCT_OFFSET(APawn_eventClientHearSound_Parms,Actor), sizeof(APawn_eventClientHearSound_Parms) );
	FixupNativeFunctionParamOffset( APawn::StaticClass(), ENGINE_ClientHearSound, TEXT("Id"), STRUCT_OFFSET(APawn_eventClientHearSound_Parms,Id), sizeof(APawn_eventClientHearSound_Parms) );
	FixupNativeFunctionParamOffset( APawn::StaticClass(), ENGINE_ClientHearSound, TEXT("S"), STRUCT_OFFSET(APawn_eventClientHearSound_Parms,S), sizeof(APawn_eventClientHearSound_Parms) );
	FixupNativeFunctionParamOffset( APawn::StaticClass(), ENGINE_ClientHearSound, TEXT("SoundLocation"), STRUCT_OFFSET(APawn_eventClientHearSound_Parms,SoundLocation), sizeof(APawn_eventClientHearSound_Parms) );
	FixupNativeFunctionParamOffset( APawn::StaticClass(), ENGINE_ClientHearSound, TEXT("Parameters"), STRUCT_OFFSET(APawn_eventClientHearSound_Parms,Parameters), sizeof(APawn_eventClientHearSound_Parms) );

	FixupNativeFunctionParamOffset( AActor::StaticClass(), ENGINE_HitWall, TEXT("HitNormal"), STRUCT_OFFSET(AActor_eventHitWall_Parms,HitNormal), sizeof(AActor_eventHitWall_Parms) );
	FixupNativeFunctionParamOffset( AActor::StaticClass(), ENGINE_HitWall, TEXT("HitWall"), STRUCT_OFFSET(AActor_eventHitWall_Parms,HitWall), sizeof(AActor_eventHitWall_Parms) );

	FixupNativeFunctionParamOffset( APlayerPawn::StaticClass(), ENGINE_PlayerCalcView, TEXT("ViewActor"), STRUCT_OFFSET(APlayerPawn_eventPlayerCalcView_Parms,ViewActor), sizeof(APlayerPawn_eventPlayerCalcView_Parms) );
	FixupNativeFunctionParamOffset( APlayerPawn::StaticClass(), ENGINE_PlayerCalcView, TEXT("CameraLocation"), STRUCT_OFFSET(APlayerPawn_eventPlayerCalcView_Parms,CameraLocation), sizeof(APlayerPawn_eventPlayerCalcView_Parms) );
	FixupNativeFunctionParamOffset( APlayerPawn::StaticClass(), ENGINE_PlayerCalcView, TEXT("CameraRotation"), STRUCT_OFFSET(APlayerPawn_eventPlayerCalcView_Parms,CameraRotation), sizeof(APlayerPawn_eventPlayerCalcView_Parms) );
	FixupNativeFunctionParamOffset( APlayerPawn::StaticClass(), ENGINE_PlayerTick, TEXT("Time"), STRUCT_OFFSET(APlayerPawn_eventPlayerTick_Parms,Time), sizeof(APlayerPawn_eventPlayerTick_Parms) );
	FixupNativeFunctionParamOffset( APlayerPawn::StaticClass(), ENGINE_PreRender, TEXT("Canvas"), STRUCT_OFFSET(APlayerPawn_eventPreRender_Parms,Canvas), sizeof(APlayerPawn_eventPreRender_Parms) );
	FixupNativeFunctionParamOffset( APlayerPawn::StaticClass(), ENGINE_PostRender, TEXT("Canvas"), STRUCT_OFFSET(APlayerPawn_eventPostRender_Parms,Canvas), sizeof(APlayerPawn_eventPostRender_Parms) );

	FixupNativeFunctionParamOffset( AHUD::StaticClass(), ENGINE_PreRender, TEXT("Canvas"), STRUCT_OFFSET(AHUD_eventPreRender_Parms,Canvas), sizeof(AHUD_eventPreRender_Parms) );
	FixupNativeFunctionParamOffset( AHUD::StaticClass(), ENGINE_PostRender, TEXT("Canvas"), STRUCT_OFFSET(AHUD_eventPostRender_Parms,Canvas), sizeof(AHUD_eventPostRender_Parms) );
	FixupNativeFunctionParamOffset( AMutator::StaticClass(), ENGINE_PostRender, TEXT("Canvas"), STRUCT_OFFSET(AMutator_eventPostRender_Parms,Canvas), sizeof(AMutator_eventPostRender_Parms) );

	FixupNativeFunctionParamOffset( AGameInfo::StaticClass(), ENGINE_Login, TEXT("Portal"), STRUCT_OFFSET(AGameInfo_eventLogin_Parms,Portal), sizeof(AGameInfo_eventLogin_Parms) );
	FixupNativeFunctionParamOffset( AGameInfo::StaticClass(), ENGINE_Login, TEXT("Options"), STRUCT_OFFSET(AGameInfo_eventLogin_Parms,Options), sizeof(AGameInfo_eventLogin_Parms) );
	FixupNativeFunctionParamOffset( AGameInfo::StaticClass(), ENGINE_Login, TEXT("Error"), STRUCT_OFFSET(AGameInfo_eventLogin_Parms,Error), sizeof(AGameInfo_eventLogin_Parms) );
	FixupNativeFunctionParamOffset( AGameInfo::StaticClass(), ENGINE_Login, TEXT("SpawnClass"), STRUCT_OFFSET(AGameInfo_eventLogin_Parms,SpawnClass), sizeof(AGameInfo_eventLogin_Parms) );
	FixupNativeFunctionParamOffset( AGameInfo::StaticClass(), ENGINE_Login, TEXT("ReturnValue"), STRUCT_OFFSET(AGameInfo_eventLogin_Parms,ReturnValue), sizeof(AGameInfo_eventLogin_Parms) );
	FixupNativeFunctionParamOffset( AGameInfo::StaticClass(), ENGINE_AcceptInventory, TEXT("PlayerPawn"), STRUCT_OFFSET(AGameInfo_eventAcceptInventory_Parms,PlayerPawn), sizeof(AGameInfo_eventAcceptInventory_Parms) );
	FixupNativeFunctionParamOffset( AGameInfo::StaticClass(), ENGINE_PostLogin, TEXT("NewPlayer"), STRUCT_OFFSET(AGameInfo_eventPostLogin_Parms,NewPlayer), sizeof(AGameInfo_eventPostLogin_Parms) );
	FixupNativeFunctionParamOffset( AGameInfo::StaticClass(), ENGINE_PreLogin, TEXT("Options"), STRUCT_OFFSET(AGameInfo_eventPreLogin_Parms,Options), sizeof(AGameInfo_eventPreLogin_Parms) );
	FixupNativeFunctionParamOffset( AGameInfo::StaticClass(), ENGINE_PreLogin, TEXT("Address"), STRUCT_OFFSET(AGameInfo_eventPreLogin_Parms,Address), sizeof(AGameInfo_eventPreLogin_Parms) );
	FixupNativeFunctionParamOffset( AGameInfo::StaticClass(), ENGINE_PreLogin, TEXT("Error"), STRUCT_OFFSET(AGameInfo_eventPreLogin_Parms,Error), sizeof(AGameInfo_eventPreLogin_Parms) );
	FixupNativeFunctionParamOffset( AGameInfo::StaticClass(), ENGINE_PreLogin, TEXT("FailCode"), STRUCT_OFFSET(AGameInfo_eventPreLogin_Parms,FailCode), sizeof(AGameInfo_eventPreLogin_Parms) );
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

static void FixupActorDisplayAndRuntimeOffsets()
{
	guard(FixupActorDisplayAndRuntimeOffsets);
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Physics"), STRUCT_OFFSET(AActor,Physics) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Role"), STRUCT_OFFSET(AActor,Role) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("RemoteRole"), STRUCT_OFFSET(AActor,RemoteRole) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("InitialState"), STRUCT_OFFSET(AActor,InitialState) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Group"), STRUCT_OFFSET(AActor,Group) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("TimerRate"), STRUCT_OFFSET(AActor,TimerRate) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("TimerCounter"), STRUCT_OFFSET(AActor,TimerCounter) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LifeSpan"), STRUCT_OFFSET(AActor,LifeSpan) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("AnimSequence"), STRUCT_OFFSET(AActor,AnimSequence) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("AnimFrame"), STRUCT_OFFSET(AActor,AnimFrame) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("AnimRate"), STRUCT_OFFSET(AActor,AnimRate) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("TweenRate"), STRUCT_OFFSET(AActor,TweenRate) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LODBias"), STRUCT_OFFSET(AActor,LODBias) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Tag"), STRUCT_OFFSET(AActor,Tag) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Event"), STRUCT_OFFSET(AActor,Event) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Target"), STRUCT_OFFSET(AActor,Target) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Inventory"), STRUCT_OFFSET(AActor,Inventory) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("AttachTag"), STRUCT_OFFSET(AActor,AttachTag) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("StandingCount"), STRUCT_OFFSET(AActor,StandingCount) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("MiscNumber"), STRUCT_OFFSET(AActor,MiscNumber) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LatentByte"), STRUCT_OFFSET(AActor,LatentByte) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LatentInt"), STRUCT_OFFSET(AActor,LatentInt) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LatentFloat"), STRUCT_OFFSET(AActor,LatentFloat) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LatentActor"), STRUCT_OFFSET(AActor,LatentActor) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Touching"), STRUCT_OFFSET(AActor,Touching) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Deleted"), STRUCT_OFFSET(AActor,Deleted) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("CollisionTag"), STRUCT_OFFSET(AActor,CollisionTag) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LightingTag"), STRUCT_OFFSET(AActor,LightingTag) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("NetTag"), STRUCT_OFFSET(AActor,NetTag) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("OtherTag"), STRUCT_OFFSET(AActor,OtherTag) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("ExtraTag"), STRUCT_OFFSET(AActor,ExtraTag) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("SpecialTag"), STRUCT_OFFSET(AActor,SpecialTag) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Location"), STRUCT_OFFSET(AActor,Location) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Rotation"), STRUCT_OFFSET(AActor,Rotation) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("OldLocation"), STRUCT_OFFSET(AActor,OldLocation) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("ColLocation"), STRUCT_OFFSET(AActor,ColLocation) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Velocity"), STRUCT_OFFSET(AActor,Velocity) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Acceleration"), STRUCT_OFFSET(AActor,Acceleration) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("OddsOfAppearing"), STRUCT_OFFSET(AActor,OddsOfAppearing) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("DrawType"), STRUCT_OFFSET(AActor,DrawType) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Style"), STRUCT_OFFSET(AActor,Style) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Sprite"), STRUCT_OFFSET(AActor,Sprite) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Texture"), STRUCT_OFFSET(AActor,Texture) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Skin"), STRUCT_OFFSET(AActor,Skin) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Mesh"), STRUCT_OFFSET(AActor,Mesh) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Brush"), STRUCT_OFFSET(AActor,Brush) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("DrawScale"), STRUCT_OFFSET(AActor,DrawScale) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("PrePivot"), STRUCT_OFFSET(AActor,PrePivot) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("ScaleGlow"), STRUCT_OFFSET(AActor,ScaleGlow) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("AmbientGlow"), STRUCT_OFFSET(AActor,AmbientGlow) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Fatness"), STRUCT_OFFSET(AActor,Fatness) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("VisibilityRadius"), STRUCT_OFFSET(AActor,VisibilityRadius) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("VisibilityHeight"), STRUCT_OFFSET(AActor,VisibilityHeight) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("MultiSkins"), STRUCT_OFFSET(AActor,MultiSkins) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("SoundRadius"), STRUCT_OFFSET(AActor,SoundRadius) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("SoundVolume"), STRUCT_OFFSET(AActor,SoundVolume) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("SoundPitch"), STRUCT_OFFSET(AActor,SoundPitch) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("AmbientSound"), STRUCT_OFFSET(AActor,AmbientSound) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("TransientSoundVolume"), STRUCT_OFFSET(AActor,TransientSoundVolume) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("TransientSoundRadius"), STRUCT_OFFSET(AActor,TransientSoundRadius) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("CollisionRadius"), STRUCT_OFFSET(AActor,CollisionRadius) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("CollisionHeight"), STRUCT_OFFSET(AActor,CollisionHeight) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LightType"), STRUCT_OFFSET(AActor,LightType) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LightEffect"), STRUCT_OFFSET(AActor,LightEffect) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LightBrightness"), STRUCT_OFFSET(AActor,LightBrightness) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LightHue"), STRUCT_OFFSET(AActor,LightHue) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LightSaturation"), STRUCT_OFFSET(AActor,LightSaturation) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LightRadius"), STRUCT_OFFSET(AActor,LightRadius) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LightPeriod"), STRUCT_OFFSET(AActor,LightPeriod) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LightPhase"), STRUCT_OFFSET(AActor,LightPhase) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("LightCone"), STRUCT_OFFSET(AActor,LightCone) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("VolumeBrightness"), STRUCT_OFFSET(AActor,VolumeBrightness) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("VolumeRadius"), STRUCT_OFFSET(AActor,VolumeRadius) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("VolumeFog"), STRUCT_OFFSET(AActor,VolumeFog) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("DodgeDir"), STRUCT_OFFSET(AActor,DodgeDir) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Mass"), STRUCT_OFFSET(AActor,Mass) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Buoyancy"), STRUCT_OFFSET(AActor,Buoyancy) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("RotationRate"), STRUCT_OFFSET(AActor,RotationRate) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("DesiredRotation"), STRUCT_OFFSET(AActor,DesiredRotation) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("PhysAlpha"), STRUCT_OFFSET(AActor,PhysAlpha) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("PhysRate"), STRUCT_OFFSET(AActor,PhysRate) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("PendingTouch"), STRUCT_OFFSET(AActor,PendingTouch) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("AnimLast"), STRUCT_OFFSET(AActor,AnimLast) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("AnimMinRate"), STRUCT_OFFSET(AActor,AnimMinRate) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("OldAnimRate"), STRUCT_OFFSET(AActor,OldAnimRate) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("SimAnim"), STRUCT_OFFSET(AActor,SimAnim) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("NetPriority"), STRUCT_OFFSET(AActor,NetPriority) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("NetUpdateFrequency"), STRUCT_OFFSET(AActor,NetUpdateFrequency) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("RenderIteratorClass"), STRUCT_OFFSET(AActor,RenderIteratorClass) );
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("RenderInterface"), STRUCT_OFFSET(AActor,RenderInterface) );
	unguard;
}

static void FixupPlayerPawnOffsets()
{
	guard(FixupPlayerPawnOffsets);
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("Player"), STRUCT_OFFSET(APlayerPawn,Player) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("Password"), STRUCT_OFFSET(APlayerPawn,Password) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("DodgeClickTimer"), STRUCT_OFFSET(APlayerPawn,DodgeClickTimer) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("DodgeClickTime"), STRUCT_OFFSET(APlayerPawn,DodgeClickTime) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("Bob"), STRUCT_OFFSET(APlayerPawn,Bob) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("LandBob"), STRUCT_OFFSET(APlayerPawn,LandBob) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("AppliedBob"), STRUCT_OFFSET(APlayerPawn,AppliedBob) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("bobtime"), STRUCT_OFFSET(APlayerPawn,bobtime) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("ShowFlags"), STRUCT_OFFSET(APlayerPawn,ShowFlags) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("RendMap"), STRUCT_OFFSET(APlayerPawn,RendMap) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("Misc1"), STRUCT_OFFSET(APlayerPawn,Misc1) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("Misc2"), STRUCT_OFFSET(APlayerPawn,Misc2) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("ViewTarget"), STRUCT_OFFSET(APlayerPawn,ViewTarget) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("FlashScale"), STRUCT_OFFSET(APlayerPawn,FlashScale) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("FlashFog"), STRUCT_OFFSET(APlayerPawn,FlashFog) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("myHUD"), STRUCT_OFFSET(APlayerPawn,myHUD) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("Scoring"), STRUCT_OFFSET(APlayerPawn,Scoring) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("HUDType"), STRUCT_OFFSET(APlayerPawn,HUDType) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("ScoringType"), STRUCT_OFFSET(APlayerPawn,ScoringType) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("DesiredFlashScale"), STRUCT_OFFSET(APlayerPawn,DesiredFlashScale) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("ConstantGlowScale"), STRUCT_OFFSET(APlayerPawn,ConstantGlowScale) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("InstantFlash"), STRUCT_OFFSET(APlayerPawn,InstantFlash) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("DesiredFlashFog"), STRUCT_OFFSET(APlayerPawn,DesiredFlashFog) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("ConstantGlowFog"), STRUCT_OFFSET(APlayerPawn,ConstantGlowFog) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("InstantFog"), STRUCT_OFFSET(APlayerPawn,InstantFog) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("DesiredFOV"), STRUCT_OFFSET(APlayerPawn,DesiredFOV) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("DefaultFOV"), STRUCT_OFFSET(APlayerPawn,DefaultFOV) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("Song"), STRUCT_OFFSET(APlayerPawn,Song) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("SongSection"), STRUCT_OFFSET(APlayerPawn,SongSection) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("CdTrack"), STRUCT_OFFSET(APlayerPawn,CdTrack) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("Transition"), STRUCT_OFFSET(APlayerPawn,Transition) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("shaketimer"), STRUCT_OFFSET(APlayerPawn,shaketimer) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("shakemag"), STRUCT_OFFSET(APlayerPawn,shakemag) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("shakevert"), STRUCT_OFFSET(APlayerPawn,shakevert) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("maxshake"), STRUCT_OFFSET(APlayerPawn,maxshake) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("verttimer"), STRUCT_OFFSET(APlayerPawn,verttimer) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("CarcassType"), STRUCT_OFFSET(APlayerPawn,CarcassType) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("MyAutoAim"), STRUCT_OFFSET(APlayerPawn,MyAutoAim) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("Handedness"), STRUCT_OFFSET(APlayerPawn,Handedness) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("JumpSound"), STRUCT_OFFSET(APlayerPawn,JumpSound) );
	static const TCHAR* PlayerPawnBools[] =
	{
		TEXT("bAdmin"), TEXT("bLookUpStairs"), TEXT("bSnapToLevel"), TEXT("bAlwaysMouseLook"), TEXT("bKeyboardLook"),
		TEXT("bWasForward"), TEXT("bWasBack"), TEXT("bWasLeft"), TEXT("bWasRight"), TEXT("bEdgeForward"),
		TEXT("bEdgeBack"), TEXT("bEdgeLeft"), TEXT("bEdgeRight"), TEXT("bIsCrouching"), TEXT("bShakeDir"),
		TEXT("bAnimTransition"), TEXT("bIsTurning"), TEXT("bFrozen"), TEXT("bBadConnectionAlert"), TEXT("bInvertMouse"),
		TEXT("bShowScores"), TEXT("bShowMenu"), TEXT("bSpecialMenu"), TEXT("bWokeUp"), TEXT("bPressedJump"),
		TEXT("bUpdatePosition"), TEXT("bDelayedCommand"), TEXT("bRising"), TEXT("bReducedVis"), TEXT("bCenterView"),
		TEXT("bMaxMouseSmoothing"), TEXT("bMouseZeroed"), TEXT("bReadyToPlay"), TEXT("bNoFlash"), TEXT("bNoVoices"),
		TEXT("bMessageBeep"), TEXT("bZooming"), TEXT("bSinglePlayer"), TEXT("bJustFired"), TEXT("bJustAltFired"),
		TEXT("bIsTyping"), TEXT("bFixedCamera"), TEXT("bNeverAutoSwitch"), TEXT("bJumpStatus"), TEXT("bUpdating"),
		TEXT("bCheatsEnabled")
	};
	FixupNativeBoolBlockOffset( APlayerPawn::StaticClass(), TEXT("PlayerPawnFlags"), PlayerPawnBools, ARRAY_COUNT(PlayerPawnBools), STRUCT_OFFSET(APlayerPawn,ZoomLevel)-sizeof(BITFIELD) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("ZoomLevel"), STRUCT_OFFSET(APlayerPawn,ZoomLevel) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("SpecialMenu"), STRUCT_OFFSET(APlayerPawn,SpecialMenu) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("DelayedCommand"), STRUCT_OFFSET(APlayerPawn,DelayedCommand) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("MouseSensitivity"), STRUCT_OFFSET(APlayerPawn,MouseSensitivity) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("WeaponPriority"), STRUCT_OFFSET(APlayerPawn,WeaponPriority) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("SmoothMouseX"), STRUCT_OFFSET(APlayerPawn,SmoothMouseX) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("SmoothMouseY"), STRUCT_OFFSET(APlayerPawn,SmoothMouseY) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("BorrowedMouseX"), STRUCT_OFFSET(APlayerPawn,BorrowedMouseX) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("BorrowedMouseY"), STRUCT_OFFSET(APlayerPawn,BorrowedMouseY) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("MouseSmoothThreshold"), STRUCT_OFFSET(APlayerPawn,MouseSmoothThreshold) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("MouseZeroTime"), STRUCT_OFFSET(APlayerPawn,MouseZeroTime) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aBaseX"), STRUCT_OFFSET(APlayerPawn,aBaseX) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aBaseY"), STRUCT_OFFSET(APlayerPawn,aBaseY) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aBaseZ"), STRUCT_OFFSET(APlayerPawn,aBaseZ) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aMouseX"), STRUCT_OFFSET(APlayerPawn,aMouseX) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aMouseY"), STRUCT_OFFSET(APlayerPawn,aMouseY) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aForward"), STRUCT_OFFSET(APlayerPawn,aForward) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aTurn"), STRUCT_OFFSET(APlayerPawn,aTurn) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aStrafe"), STRUCT_OFFSET(APlayerPawn,aStrafe) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aUp"), STRUCT_OFFSET(APlayerPawn,aUp) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aLookUp"), STRUCT_OFFSET(APlayerPawn,aLookUp) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aExtra4"), STRUCT_OFFSET(APlayerPawn,aExtra4) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aExtra3"), STRUCT_OFFSET(APlayerPawn,aExtra3) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aExtra2"), STRUCT_OFFSET(APlayerPawn,aExtra2) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aExtra1"), STRUCT_OFFSET(APlayerPawn,aExtra1) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("aExtra0"), STRUCT_OFFSET(APlayerPawn,aExtra0) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("SavedMoves"), STRUCT_OFFSET(APlayerPawn,SavedMoves) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("FreeMoves"), STRUCT_OFFSET(APlayerPawn,FreeMoves) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("PendingMove"), STRUCT_OFFSET(APlayerPawn,PendingMove) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("CurrentTimeStamp"), STRUCT_OFFSET(APlayerPawn,CurrentTimeStamp) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("LastUpdateTime"), STRUCT_OFFSET(APlayerPawn,LastUpdateTime) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("ServerTimeStamp"), STRUCT_OFFSET(APlayerPawn,ServerTimeStamp) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("TimeMargin"), STRUCT_OFFSET(APlayerPawn,TimeMargin) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("ClientUpdateTime"), STRUCT_OFFSET(APlayerPawn,ClientUpdateTime) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("MaxTimeMargin"), STRUCT_OFFSET(APlayerPawn,MaxTimeMargin) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("ProgressMessage"), STRUCT_OFFSET(APlayerPawn,ProgressMessage) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("ProgressColor"), STRUCT_OFFSET(APlayerPawn,ProgressColor) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("ProgressTimeOut"), STRUCT_OFFSET(APlayerPawn,ProgressTimeOut) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("QuickSaveString"), STRUCT_OFFSET(APlayerPawn,QuickSaveString) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("NoPauseMessage"), STRUCT_OFFSET(APlayerPawn,NoPauseMessage) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("ViewingFrom"), STRUCT_OFFSET(APlayerPawn,ViewingFrom) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("OwnCamera"), STRUCT_OFFSET(APlayerPawn,OwnCamera) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("FailedView"), STRUCT_OFFSET(APlayerPawn,FailedView) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("GameReplicationInfo"), STRUCT_OFFSET(APlayerPawn,GameReplicationInfo) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("ngWorldSecret"), STRUCT_OFFSET(APlayerPawn,ngWorldSecret) );
	static const TCHAR* NgBools[] = { TEXT("ngSecretSet") };
	FixupNativeBoolBlockOffset( APlayerPawn::StaticClass(), TEXT("PlayerPawnNg"), NgBools, ARRAY_COUNT(NgBools), STRUCT_OFFSET(APlayerPawn,TargetViewRotation)-sizeof(BITFIELD) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("TargetViewRotation"), STRUCT_OFFSET(APlayerPawn,TargetViewRotation) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("TargetEyeHeight"), STRUCT_OFFSET(APlayerPawn,TargetEyeHeight) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("TargetWeaponViewOffset"), STRUCT_OFFSET(APlayerPawn,TargetWeaponViewOffset) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("DemoViewPitch"), STRUCT_OFFSET(APlayerPawn,DemoViewPitch) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("DemoViewYaw"), STRUCT_OFFSET(APlayerPawn,DemoViewYaw) );
	FixupNativePropertyOffset( APlayerPawn::StaticClass(), TEXT("LastPlaySound"), STRUCT_OFFSET(APlayerPawn,LastPlaySound) );
	unguard;
}

static void FixupPawnOffsets()
{
	guard(FixupPawnOffsets);
	static const TCHAR* PawnBools[] =
	{
		TEXT("bBehindView"), TEXT("bIsPlayer"), TEXT("bJustLanded"), TEXT("bUpAndOut"), TEXT("bIsWalking"),
		TEXT("bHitSlopedWall"), TEXT("bNeverSwitchOnPickup"), TEXT("bWarping"), TEXT("bUpdatingDisplay"), TEXT("bCanStrafe"),
		TEXT("bFixedStart"), TEXT("bReducedSpeed"), TEXT("bCanJump"), TEXT("bCanWalk"), TEXT("bCanSwim"),
		TEXT("bCanFly"), TEXT("bCanOpenDoors"), TEXT("bCanDoSpecial"), TEXT("bDrowning"), TEXT("bLOSflag"),
		TEXT("bFromWall"), TEXT("bHunting"), TEXT("bAvoidLedges"), TEXT("bStopAtLedges"), TEXT("bJumpOffPawn"),
		TEXT("bShootSpecial"), TEXT("bAutoActivate"), TEXT("bIsHuman"), TEXT("bIsFemale"), TEXT("bIsMultiSkinned"),
		TEXT("bCountJumps"), TEXT("bAdvancedTactics"), TEXT("bViewTarget")
	};
	FixupNativeBoolBlockOffset( APawn::StaticClass(), TEXT("PawnFlags"), PawnBools, ARRAY_COUNT(PawnBools), STRUCT_OFFSET(APawn,SightCounter)-sizeof(BITFIELD) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SightCounter"), STRUCT_OFFSET(APawn,SightCounter) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("PainTime"), STRUCT_OFFSET(APawn,PainTime) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SpeechTime"), STRUCT_OFFSET(APawn,SpeechTime) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("AvgPhysicsTime"), STRUCT_OFFSET(APawn,AvgPhysicsTime) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("FootRegion"), STRUCT_OFFSET(APawn,FootRegion) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("HeadRegion"), STRUCT_OFFSET(APawn,HeadRegion) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("MoveTimer"), STRUCT_OFFSET(APawn,MoveTimer) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("MoveTarget"), STRUCT_OFFSET(APawn,MoveTarget) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("FaceTarget"), STRUCT_OFFSET(APawn,FaceTarget) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Destination"), STRUCT_OFFSET(APawn,Destination) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Focus"), STRUCT_OFFSET(APawn,Focus) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("DesiredSpeed"), STRUCT_OFFSET(APawn,DesiredSpeed) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("MaxDesiredSpeed"), STRUCT_OFFSET(APawn,MaxDesiredSpeed) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("MeleeRange"), STRUCT_OFFSET(APawn,MeleeRange) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("GroundSpeed"), STRUCT_OFFSET(APawn,GroundSpeed) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("WaterSpeed"), STRUCT_OFFSET(APawn,WaterSpeed) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("AirSpeed"), STRUCT_OFFSET(APawn,AirSpeed) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("AccelRate"), STRUCT_OFFSET(APawn,AccelRate) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("JumpZ"), STRUCT_OFFSET(APawn,JumpZ) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("MaxStepHeight"), STRUCT_OFFSET(APawn,MaxStepHeight) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("AirControl"), STRUCT_OFFSET(APawn,AirControl) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("MinHitWall"), STRUCT_OFFSET(APawn,MinHitWall) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Visibility"), STRUCT_OFFSET(APawn,Visibility) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Alertness"), STRUCT_OFFSET(APawn,Alertness) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Stimulus"), STRUCT_OFFSET(APawn,Stimulus) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SightRadius"), STRUCT_OFFSET(APawn,SightRadius) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("PeripheralVision"), STRUCT_OFFSET(APawn,PeripheralVision) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("HearingThreshold"), STRUCT_OFFSET(APawn,HearingThreshold) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("LastSeenPos"), STRUCT_OFFSET(APawn,LastSeenPos) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("LastSeeingPos"), STRUCT_OFFSET(APawn,LastSeeingPos) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("LastSeenTime"), STRUCT_OFFSET(APawn,LastSeenTime) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Enemy"), STRUCT_OFFSET(APawn,Enemy) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Weapon"), STRUCT_OFFSET(APawn,Weapon) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("PendingWeapon"), STRUCT_OFFSET(APawn,PendingWeapon) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SelectedItem"), STRUCT_OFFSET(APawn,SelectedItem) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("ViewRotation"), STRUCT_OFFSET(APawn,ViewRotation) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("WalkBob"), STRUCT_OFFSET(APawn,WalkBob) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("BaseEyeHeight"), STRUCT_OFFSET(APawn,BaseEyeHeight) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("EyeHeight"), STRUCT_OFFSET(APawn,EyeHeight) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Floor"), STRUCT_OFFSET(APawn,Floor) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SplashTime"), STRUCT_OFFSET(APawn,SplashTime) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("OrthoZoom"), STRUCT_OFFSET(APawn,OrthoZoom) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("FovAngle"), STRUCT_OFFSET(APawn,FovAngle) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("DieCount"), STRUCT_OFFSET(APawn,DieCount) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("ItemCount"), STRUCT_OFFSET(APawn,ItemCount) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("KillCount"), STRUCT_OFFSET(APawn,KillCount) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SecretCount"), STRUCT_OFFSET(APawn,SecretCount) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Spree"), STRUCT_OFFSET(APawn,Spree) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Health"), STRUCT_OFFSET(APawn,Health) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SelectionMesh"), STRUCT_OFFSET(APawn,SelectionMesh) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SpecialMesh"), STRUCT_OFFSET(APawn,SpecialMesh) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("ReducedDamageType"), STRUCT_OFFSET(APawn,ReducedDamageType) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("ReducedDamagePct"), STRUCT_OFFSET(APawn,ReducedDamagePct) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("DropWhenKilled"), STRUCT_OFFSET(APawn,DropWhenKilled) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("UnderWaterTime"), STRUCT_OFFSET(APawn,UnderWaterTime) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("AttitudeToPlayer"), STRUCT_OFFSET(APawn,AttitudeToPlayer) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Intelligence"), STRUCT_OFFSET(APawn,Intelligence) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Skill"), STRUCT_OFFSET(APawn,Skill) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SpecialGoal"), STRUCT_OFFSET(APawn,SpecialGoal) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SpecialPause"), STRUCT_OFFSET(APawn,SpecialPause) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("noise1spot"), STRUCT_OFFSET(APawn,noise1spot) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("noise1time"), STRUCT_OFFSET(APawn,noise1time) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("noise1other"), STRUCT_OFFSET(APawn,noise1other) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("noise1loudness"), STRUCT_OFFSET(APawn,noise1loudness) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("noise2spot"), STRUCT_OFFSET(APawn,noise2spot) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("noise2time"), STRUCT_OFFSET(APawn,noise2time) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("noise2other"), STRUCT_OFFSET(APawn,noise2other) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("noise2loudness"), STRUCT_OFFSET(APawn,noise2loudness) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("LastPainSound"), STRUCT_OFFSET(APawn,LastPainSound) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("nextPawn"), STRUCT_OFFSET(APawn,nextPawn) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("HitSound1"), STRUCT_OFFSET(APawn,HitSound1) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("HitSound2"), STRUCT_OFFSET(APawn,HitSound2) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Land"), STRUCT_OFFSET(APawn,Land) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Die"), STRUCT_OFFSET(APawn,Die) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("WaterStep"), STRUCT_OFFSET(APawn,WaterStep) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("bZoom"), STRUCT_OFFSET(APawn,bZoom) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("bRun"), STRUCT_OFFSET(APawn,bRun) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("bLook"), STRUCT_OFFSET(APawn,bLook) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("bDuck"), STRUCT_OFFSET(APawn,bDuck) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("bSnapLevel"), STRUCT_OFFSET(APawn,bSnapLevel) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("bStrafe"), STRUCT_OFFSET(APawn,bStrafe) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("bFire"), STRUCT_OFFSET(APawn,bFire) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("bAltFire"), STRUCT_OFFSET(APawn,bAltFire) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("bFreeLook"), STRUCT_OFFSET(APawn,bFreeLook) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("bExtra0"), STRUCT_OFFSET(APawn,bExtra0) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("bExtra1"), STRUCT_OFFSET(APawn,bExtra1) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("bExtra2"), STRUCT_OFFSET(APawn,bExtra2) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("bExtra3"), STRUCT_OFFSET(APawn,bExtra3) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("CombatStyle"), STRUCT_OFFSET(APawn,CombatStyle) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("home"), STRUCT_OFFSET(APawn,home) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("NextState"), STRUCT_OFFSET(APawn,NextState) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("NextLabel"), STRUCT_OFFSET(APawn,NextLabel) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SoundDampening"), STRUCT_OFFSET(APawn,SoundDampening) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("DamageScaling"), STRUCT_OFFSET(APawn,DamageScaling) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("AlarmTag"), STRUCT_OFFSET(APawn,AlarmTag) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("SharedAlarmTag"), STRUCT_OFFSET(APawn,SharedAlarmTag) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("carriedDecoration"), STRUCT_OFFSET(APawn,carriedDecoration) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("PlayerReStartState"), STRUCT_OFFSET(APawn,PlayerReStartState) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("MenuName"), STRUCT_OFFSET(APawn,MenuName) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("NameArticle"), STRUCT_OFFSET(APawn,NameArticle) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("VoicePitch"), STRUCT_OFFSET(APawn,VoicePitch) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("VoiceType"), STRUCT_OFFSET(APawn,VoiceType) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("OldMessageTime"), STRUCT_OFFSET(APawn,OldMessageTime) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("RouteCache"), STRUCT_OFFSET(APawn,RouteCache) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("PlayerReplicationInfoClass"), STRUCT_OFFSET(APawn,PlayerReplicationInfoClass) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("PlayerReplicationInfo"), STRUCT_OFFSET(APawn,PlayerReplicationInfo) );
	FixupNativePropertyOffset( APawn::StaticClass(), TEXT("Shadow"), STRUCT_OFFSET(APawn,Shadow) );
	unguard;
}

static void FixupNavigationPointOffsets()
{
	guard(FixupNavigationPointOffsets);
#if PLATFORM_ANDROID
	UProperty* AndroidInterpPosition = FindNativeProperty( AInterpolationPoint::StaticClass(), TEXT("Position") );
	UProperty* AndroidInterpRate = FindNativeProperty( AInterpolationPoint::StaticClass(), TEXT("RateModifier") );
	UProperty* AndroidInterpGameSpeed = FindNativeProperty( AInterpolationPoint::StaticClass(), TEXT("GameSpeedModifier") );
	UProperty* AndroidInterpFov = FindNativeProperty( AInterpolationPoint::StaticClass(), TEXT("FovModifier") );
	UProperty* AndroidInterpScreenFlashScale = FindNativeProperty( AInterpolationPoint::StaticClass(), TEXT("ScreenFlashScale") );
	UProperty* AndroidInterpScreenFlashFog = FindNativeProperty( AInterpolationPoint::StaticClass(), TEXT("ScreenFlashFog") );
	GAndroidInterpPositionScriptOffset = AndroidInterpPosition ? AndroidInterpPosition->Offset : INDEX_NONE;
	GAndroidInterpRateScriptOffset = AndroidInterpRate ? AndroidInterpRate->Offset : INDEX_NONE;
	GAndroidInterpGameSpeedScriptOffset = AndroidInterpGameSpeed ? AndroidInterpGameSpeed->Offset : INDEX_NONE;
	GAndroidInterpFovScriptOffset = AndroidInterpFov ? AndroidInterpFov->Offset : INDEX_NONE;
	GAndroidInterpScreenFlashScaleScriptOffset = AndroidInterpScreenFlashScale ? AndroidInterpScreenFlashScale->Offset : INDEX_NONE;
	GAndroidInterpScreenFlashFogScriptOffset = AndroidInterpScreenFlashFog ? AndroidInterpScreenFlashFog->Offset : INDEX_NONE;
	debugf( NAME_Log, TEXT("UT99_ANDROID_V264_INTERP_SCRIPT_OFFSETS pos=%i rate=%i game=%i fov=%i flashScale=%i flashFog=%i nativePos=%i nativeRate=%i nativeGame=%i nativeFov=%i nativeFlashScale=%i nativeFlashFog=%i"),
		GAndroidInterpPositionScriptOffset,
		GAndroidInterpRateScriptOffset,
		GAndroidInterpGameSpeedScriptOffset,
		GAndroidInterpFovScriptOffset,
		GAndroidInterpScreenFlashScaleScriptOffset,
		GAndroidInterpScreenFlashFogScriptOffset,
		STRUCT_OFFSET(AInterpolationPoint,Position),
		STRUCT_OFFSET(AInterpolationPoint,RateModifier),
		STRUCT_OFFSET(AInterpolationPoint,GameSpeedModifier),
		STRUCT_OFFSET(AInterpolationPoint,FovModifier),
		STRUCT_OFFSET(AInterpolationPoint,ScreenFlashScale),
		STRUCT_OFFSET(AInterpolationPoint,ScreenFlashFog) );
#endif
	FixupNativePropertyOffset( AInterpolationPoint::StaticClass(), TEXT("Position"), STRUCT_OFFSET(AInterpolationPoint,Position) );
	FixupNativePropertyOffset( AInterpolationPoint::StaticClass(), TEXT("RateModifier"), STRUCT_OFFSET(AInterpolationPoint,RateModifier) );
	FixupNativePropertyOffset( AInterpolationPoint::StaticClass(), TEXT("GameSpeedModifier"), STRUCT_OFFSET(AInterpolationPoint,GameSpeedModifier) );
	FixupNativePropertyOffset( AInterpolationPoint::StaticClass(), TEXT("FovModifier"), STRUCT_OFFSET(AInterpolationPoint,FovModifier) );
	static const TCHAR* InterpolationPointBools[] =
	{
		TEXT("bEndOfPath"), TEXT("bSkipNextPath")
	};
	FixupNativeBoolBlockOffset( AInterpolationPoint::StaticClass(), TEXT("InterpolationPointFlags"), InterpolationPointBools, ARRAY_COUNT(InterpolationPointBools), STRUCT_OFFSET(AInterpolationPoint,ScreenFlashScale)-sizeof(BITFIELD) );
	FixupNativePropertyOffset( AInterpolationPoint::StaticClass(), TEXT("ScreenFlashScale"), STRUCT_OFFSET(AInterpolationPoint,ScreenFlashScale) );
	FixupNativePropertyOffset( AInterpolationPoint::StaticClass(), TEXT("ScreenFlashFog"), STRUCT_OFFSET(AInterpolationPoint,ScreenFlashFog) );
	FixupNativePropertyOffset( AInterpolationPoint::StaticClass(), TEXT("Prev"), STRUCT_OFFSET(AInterpolationPoint,Prev) );
	FixupNativePropertyOffset( AInterpolationPoint::StaticClass(), TEXT("Next"), STRUCT_OFFSET(AInterpolationPoint,Next) );

	FixupNativePropertyOffset( APlayerStart::StaticClass(), TEXT("TeamNumber"), STRUCT_OFFSET(APlayerStart,TeamNumber) );
	static const TCHAR* PlayerStartBools[] =
	{
		TEXT("bSinglePlayerStart"), TEXT("bCoopStart"), TEXT("bEnabled")
	};
	const INT PlayerStartBoolOffset = Align<INT>( STRUCT_OFFSET(APlayerStart,TeamNumber) + sizeof(BYTE), sizeof(BITFIELD) );
	FixupNativeBoolBlockOffset( APlayerStart::StaticClass(), TEXT("PlayerStartFlags"), PlayerStartBools, ARRAY_COUNT(PlayerStartBools), PlayerStartBoolOffset );
	UBoolProperty* Single = Cast<UBoolProperty>( FindNativeProperty( APlayerStart::StaticClass(), TEXT("bSinglePlayerStart") ) );
	UBoolProperty* Coop = Cast<UBoolProperty>( FindNativeProperty( APlayerStart::StaticClass(), TEXT("bCoopStart") ) );
	UBoolProperty* Enabled = Cast<UBoolProperty>( FindNativeProperty( APlayerStart::StaticClass(), TEXT("bEnabled") ) );
	debugf( NAME_Log, TEXT("UT99_ANDROID_V229_PLAYERSTART_BOOL_FIX teamOffset=%i boolOffset=%i singleOffset=%i singleMask=0x%08x coopOffset=%i coopMask=0x%08x enabledOffset=%i enabledMask=0x%08x"),
		STRUCT_OFFSET(APlayerStart,TeamNumber),
		PlayerStartBoolOffset,
		Single ? Single->Offset : -1,
		Single ? Single->BitMask : 0,
		Coop ? Coop->Offset : -1,
		Coop ? Coop->BitMask : 0,
		Enabled ? Enabled->Offset : -1,
		Enabled ? Enabled->BitMask : 0 );

	FixupNativePropertyOffset( ATrigger::StaticClass(), TEXT("TriggerType"), STRUCT_OFFSET(ATrigger,TriggerType) );
	FixupNativePropertyOffset( ATrigger::StaticClass(), TEXT("Message"), STRUCT_OFFSET(ATrigger,Message) );
	static const TCHAR* TriggerBools[] =
	{
		TEXT("bTriggerOnceOnly"), TEXT("bInitiallyActive")
	};
	const INT TriggerBoolOffset = STRUCT_OFFSET(ATrigger,ClassProximityType)-sizeof(BITFIELD);
	FixupNativeBoolBlockOffset( ATrigger::StaticClass(), TEXT("TriggerFlags"), TriggerBools, ARRAY_COUNT(TriggerBools), TriggerBoolOffset );
	FixupNativePropertyOffset( ATrigger::StaticClass(), TEXT("ClassProximityType"), STRUCT_OFFSET(ATrigger,ClassProximityType) );
	FixupNativePropertyOffset( ATrigger::StaticClass(), TEXT("RepeatTriggerTime"), STRUCT_OFFSET(ATrigger,RepeatTriggerTime) );
	FixupNativePropertyOffset( ATrigger::StaticClass(), TEXT("ReTriggerDelay"), STRUCT_OFFSET(ATrigger,ReTriggerDelay) );
	FixupNativePropertyOffset( ATrigger::StaticClass(), TEXT("TriggerTime"), STRUCT_OFFSET(ATrigger,TriggerTime) );
	FixupNativePropertyOffset( ATrigger::StaticClass(), TEXT("DamageThreshold"), STRUCT_OFFSET(ATrigger,DamageThreshold) );
	FixupNativePropertyOffset( ATrigger::StaticClass(), TEXT("TriggerActor"), STRUCT_OFFSET(ATrigger,TriggerActor) );
	FixupNativePropertyOffset( ATrigger::StaticClass(), TEXT("TriggerActor2"), STRUCT_OFFSET(ATrigger,TriggerActor2) );
	UBoolProperty* TriggerOnce = Cast<UBoolProperty>( FindNativeProperty( ATrigger::StaticClass(), TEXT("bTriggerOnceOnly") ) );
	UBoolProperty* TriggerActive = Cast<UBoolProperty>( FindNativeProperty( ATrigger::StaticClass(), TEXT("bInitiallyActive") ) );
	debugf( NAME_Log, TEXT("UT99_ANDROID_V230_TRIGGER_BOOL_FIX typeOffset=%i messageOffset=%i boolOffset=%i onceOffset=%i onceMask=0x%08x activeOffset=%i activeMask=0x%08x classOffset=%i"),
		STRUCT_OFFSET(ATrigger,TriggerType),
		STRUCT_OFFSET(ATrigger,Message),
		TriggerBoolOffset,
		TriggerOnce ? TriggerOnce->Offset : -1,
		TriggerOnce ? TriggerOnce->BitMask : 0,
		TriggerActive ? TriggerActive->Offset : -1,
		TriggerActive ? TriggerActive->BitMask : 0,
		STRUCT_OFFSET(ATrigger,ClassProximityType) );
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

static void FixupMoverOffsets()
{
	guard(FixupMoverOffsets);
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("MoverEncroachType"), STRUCT_OFFSET(AMover,MoverEncroachType) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("MoverGlideType"), STRUCT_OFFSET(AMover,MoverGlideType) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("BumpType"), STRUCT_OFFSET(AMover,BumpType) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("KeyNum"), STRUCT_OFFSET(AMover,KeyNum) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("PrevKeyNum"), STRUCT_OFFSET(AMover,PrevKeyNum) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("NumKeys"), STRUCT_OFFSET(AMover,NumKeys) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("WorldRaytraceKey"), STRUCT_OFFSET(AMover,WorldRaytraceKey) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("BrushRaytraceKey"), STRUCT_OFFSET(AMover,BrushRaytraceKey) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("MoveTime"), STRUCT_OFFSET(AMover,MoveTime) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("StayOpenTime"), STRUCT_OFFSET(AMover,StayOpenTime) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("OtherTime"), STRUCT_OFFSET(AMover,OtherTime) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("EncroachDamage"), STRUCT_OFFSET(AMover,EncroachDamage) );
	static const TCHAR* MoverConfigBools[] =
	{
		TEXT("bTriggerOnceOnly"), TEXT("bSlave"), TEXT("bUseTriggered"), TEXT("bDamageTriggered"), TEXT("bDynamicLightMover")
	};
	FixupNativeBoolBlockOffset( AMover::StaticClass(), TEXT("MoverConfigFlags"), MoverConfigBools, ARRAY_COUNT(MoverConfigBools), STRUCT_OFFSET(AMover,PlayerBumpEvent)-sizeof(BITFIELD) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("PlayerBumpEvent"), STRUCT_OFFSET(AMover,PlayerBumpEvent) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("BumpEvent"), STRUCT_OFFSET(AMover,BumpEvent) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("SavedTrigger"), STRUCT_OFFSET(AMover,SavedTrigger) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("DamageThreshold"), STRUCT_OFFSET(AMover,DamageThreshold) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("numTriggerEvents"), STRUCT_OFFSET(AMover,numTriggerEvents) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("Leader"), STRUCT_OFFSET(AMover,Leader) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("Follower"), STRUCT_OFFSET(AMover,Follower) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("ReturnGroup"), STRUCT_OFFSET(AMover,ReturnGroup) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("DelayTime"), STRUCT_OFFSET(AMover,DelayTime) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("OpeningSound"), STRUCT_OFFSET(AMover,OpeningSound) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("OpenedSound"), STRUCT_OFFSET(AMover,OpenedSound) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("ClosingSound"), STRUCT_OFFSET(AMover,ClosingSound) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("ClosedSound"), STRUCT_OFFSET(AMover,ClosedSound) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("MoveAmbientSound"), STRUCT_OFFSET(AMover,MoveAmbientSound) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("KeyPos"), STRUCT_OFFSET(AMover,KeyPos) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("KeyRot"), STRUCT_OFFSET(AMover,KeyRot) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("BasePos"), STRUCT_OFFSET(AMover,BasePos) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("OldPos"), STRUCT_OFFSET(AMover,OldPos) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("OldPrePivot"), STRUCT_OFFSET(AMover,OldPrePivot) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("SavedPos"), STRUCT_OFFSET(AMover,SavedPos) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("BaseRot"), STRUCT_OFFSET(AMover,BaseRot) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("OldRot"), STRUCT_OFFSET(AMover,OldRot) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("SavedRot"), STRUCT_OFFSET(AMover,SavedRot) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("myMarker"), STRUCT_OFFSET(AMover,myMarker) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("TriggerActor"), STRUCT_OFFSET(AMover,TriggerActor) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("TriggerActor2"), STRUCT_OFFSET(AMover,TriggerActor2) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("WaitingPawn"), STRUCT_OFFSET(AMover,WaitingPawn) );
	static const TCHAR* MoverRuntimeBools[] =
	{
		TEXT("bOpening"), TEXT("bDelaying"), TEXT("bClientPause"), TEXT("bPlayerOnly")
	};
	FixupNativeBoolBlockOffset( AMover::StaticClass(), TEXT("MoverRuntimeFlags"), MoverRuntimeBools, ARRAY_COUNT(MoverRuntimeBools), STRUCT_OFFSET(AMover,RecommendedTrigger)-sizeof(BITFIELD) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("RecommendedTrigger"), STRUCT_OFFSET(AMover,RecommendedTrigger) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("SimOldPos"), STRUCT_OFFSET(AMover,SimOldPos) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("SimOldRotPitch"), STRUCT_OFFSET(AMover,SimOldRotPitch) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("SimOldRotYaw"), STRUCT_OFFSET(AMover,SimOldRotYaw) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("SimOldRotRoll"), STRUCT_OFFSET(AMover,SimOldRotRoll) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("SimInterpolate"), STRUCT_OFFSET(AMover,SimInterpolate) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("RealPosition"), STRUCT_OFFSET(AMover,RealPosition) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("RealRotation"), STRUCT_OFFSET(AMover,RealRotation) );
	FixupNativePropertyOffset( AMover::StaticClass(), TEXT("ClientUpdate"), STRUCT_OFFSET(AMover,ClientUpdate) );
	debugf( NAME_Log, TEXT("UT99_ANDROID_V321_MOVER_OFFSET_FIX moveTime=%i keyPos=%i keyRot=%i basePos=%i savedTrigger=%i sim=%i"),
		STRUCT_OFFSET(AMover,MoveTime),
		STRUCT_OFFSET(AMover,KeyPos),
		STRUCT_OFFSET(AMover,KeyRot),
		STRUCT_OFFSET(AMover,BasePos),
		STRUCT_OFFSET(AMover,SavedTrigger),
		STRUCT_OFFSET(AMover,SimInterpolate) );
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
	FixupActorDisplayAndRuntimeOffsets();
	FixupNativeFunctionParams();
	FixupBitmapTextureOffsets();
	FixupPlayerOffsets();
	FixupPawnOffsets();
	FixupPlayerPawnOffsets();
	FixupNavigationPointOffsets();
	FixupMoverOffsets();
	FixupNativePropertyOffset( AActor::StaticClass(), TEXT("Region"), STRUCT_OFFSET(AActor,Region) );
	FixupPointRegionStruct();
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
	{
#if defined(PLATFORM_64BIT)
		debugf( NAME_Warning, TEXT("UT99_ANDROID_V274_INITIAL_BROWSE_FAILED_CONTINUE parm=%s error=%s glevel=%s gentry=%s"),
			Parm,
			*Error,
			GLevel && GLevel->GetOuter() ? GLevel->GetOuter()->GetName() : TEXT("None"),
			GEntry && GEntry->GetOuter() ? GEntry->GetOuter()->GetName() : TEXT("None") );
		if( Client && GEntry )
		{
			GLevel = GEntry;
			Success = 1;
		}
		else
#endif
		appErrorf( LocalizeError("FailedBrowse"), Parm, *Error );
	}

	// Open initial Viewport.
	if( Client )
	{
		// Init input.!!Temporary
		UInput::StaticInitInput();

		// Create viewport.
		UViewport* Viewport = Client->NewViewport( NAME_None );
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE NewViewport viewport=%i"), Viewport != NULL );

		// Create console.
#if PLATFORM_ANDROID
		if( GConfig )
		{
			GConfig->SetString( TEXT("UTMenu.UTConsole"), TEXT("RootWindow"), TEXT("UMenu.UMenuRootWindow"), TEXT("System") );
			GConfig->SetString( TEXT("UTMenu.UTConsole"), TEXT("UWindowKey"), TEXT("IK_Esc"), TEXT("System") );
			GConfig->SetString( TEXT("UTMenu.UTConsole"), TEXT("ShowDesktop"), TEXT("True"), TEXT("System") );
			debugf( NAME_Init, TEXT("UT99_ANDROID_V279_UTCONSOLE_CONFIG_ROOTWINDOW") );
		}
#endif
		UClass* ConsoleClass = StaticLoadClass( UConsole::StaticClass(), NULL, TEXT("ini:Engine.Engine.Console"), NULL, LOAD_NoFail, NULL );
		UConsole::FixupNativeClassSize( ConsoleClass );
		Viewport->Console = ConstructObject<UConsole>( ConsoleClass );
		Viewport->Console->_Init( Viewport );
#if PLATFORM_ANDROID
		AndroidForceConsoleFrontendDefaults( Viewport->Console );
#endif
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE console initialized class=%s"), ConsoleClass ? ConsoleClass->GetName() : TEXT("None") );

		// Spawn play actor.
		FString Error;
		if( !GLevel->SpawnPlayActor( Viewport, ROLE_SimulatedProxy, URL, Error ) )
		{
#if defined(PLATFORM_64BIT)
			debugf( NAME_Warning, TEXT("UT99_ANDROID_V273_INITIAL_SPAWN_FAILED_CONTINUE url=%s level=%s error=%s"),
				*URL.String(),
				GLevel && GLevel->GetOuter() ? GLevel->GetOuter()->GetName() : TEXT("None"),
				*Error );
#else
			appErrorf( TEXT("%s"), *Error );
#endif
		}
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE play actor spawned Actor=%i"), Viewport->Actor != NULL );
		Viewport->Input->Init( Viewport );
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE input initialized opening window") );
		Viewport->OpenWindow( 0, 0, (INT) INDEX_NONE, (INT) INDEX_NONE, (INT) INDEX_NONE, (INT) INDEX_NONE );
		debugf( NAME_Init, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE OpenWindow returned Size=%ix%i RenDev=%i"), Viewport->SizeX, Viewport->SizeY, Viewport->RenDev != NULL );
#if PLATFORM_ANDROID
		AndroidWarmFrontendMenuAssets( Viewport );
#endif
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
#if PLATFORM_ANDROID
	DOUBLE AndroidDrawStart = appSeconds();
	static DOUBLE AndroidFpsWindowStart = 0.0;
	static DOUBLE AndroidLastDrawStart = 0.0;
	static DOUBLE AndroidFpsAccumIntervalMs = 0.0;
	static DOUBLE AndroidFpsMaxIntervalMs = 0.0;
	static INT AndroidFpsTotalFrames = 0;
	static DOUBLE AndroidDisplayedFps = 0.0;
	static DOUBLE AndroidDisplayedFrameMs = 0.0;
	static DOUBLE AndroidDisplayedMaxFrameMs = 0.0;
	static INT AndroidFpsWindowFrames = 0;
	if( AndroidFpsWindowStart <= 0.0 )
		AndroidFpsWindowStart = AndroidDrawStart;
	if( AndroidLastDrawStart > 0.0 )
	{
		const DOUBLE AndroidFrameIntervalMs = (AndroidDrawStart - AndroidLastDrawStart) * 1000.0;
		AndroidFpsAccumIntervalMs += AndroidFrameIntervalMs;
		AndroidFpsMaxIntervalMs = Max( AndroidFpsMaxIntervalMs, AndroidFrameIntervalMs );
	}
	AndroidLastDrawStart = AndroidDrawStart;
	AndroidFpsWindowFrames++;
	AndroidFpsTotalFrames++;
	if( AndroidDrawStart - AndroidFpsWindowStart >= 1.0 )
	{
		const DOUBLE AndroidFpsWindowSeconds = AndroidDrawStart - AndroidFpsWindowStart;
		const INT AndroidMeasuredIntervals = Max( AndroidFpsWindowFrames - 1, 0 );
		AndroidDisplayedFrameMs = AndroidMeasuredIntervals > 0 ? AndroidFpsAccumIntervalMs / AndroidMeasuredIntervals : 0.0;
		AndroidDisplayedFps = AndroidDisplayedFrameMs > 0.0 ? 1000.0 / AndroidDisplayedFrameMs : 0.0;
		AndroidDisplayedMaxFrameMs = AndroidFpsMaxIntervalMs;
		debugf( NAME_Log, TEXT("UT99_ANDROID_V311_REAL_FPS realFps=%f framesThisSecond=%i intervals=%i avgFrameMs=%f maxFrameMs=%f windowSeconds=%f totalFrames=%i"),
			AndroidDisplayedFps,
			AndroidFpsWindowFrames,
			AndroidMeasuredIntervals,
			AndroidDisplayedFrameMs,
			AndroidDisplayedMaxFrameMs,
			AndroidFpsWindowSeconds,
			AndroidFpsTotalFrames );
		AndroidFpsWindowStart = AndroidDrawStart;
		AndroidFpsWindowFrames = 0;
		AndroidFpsAccumIntervalMs = 0.0;
		AndroidFpsMaxIntervalMs = 0.0;
	}
	DOUBLE AndroidAfterLock = AndroidDrawStart;
	DOUBLE AndroidWorldStart = AndroidDrawStart;
	DOUBLE AndroidAfterWorld = AndroidDrawStart;
	DOUBLE AndroidUnlockStart = AndroidDrawStart;
	DOUBLE AndroidDrawEnd = AndroidDrawStart;
	DOUBLE AndroidAudioMs = 0.0;
	DOUBLE AndroidPreMs = 0.0;
	DOUBLE AndroidActorPreMs = 0.0;
	DOUBLE AndroidPostMs = 0.0;
	DOUBLE AndroidActorPostMs = 0.0;
	DOUBLE AndroidConsolePostMs = 0.0;
	DOUBLE AndroidConsoleNativePostMs = 0.0;
	DOUBLE AndroidConsoleEventPostMs = 0.0;
	DOUBLE AndroidAudioPostMs = 0.0;
	DOUBLE AndroidFinishMs = 0.0;
	DOUBLE AndroidRenderPreMs = 0.0;
	DOUBLE AndroidConsolePreMs = 0.0;
	DOUBLE AndroidCanvasUpdateMs = 0.0;
	DOUBLE AndroidCalcViewMs = 0.0;
	DOUBLE AndroidPointCheckMs = 0.0;
	DOUBLE AndroidFlashSetupMs = 0.0;
	UBOOL AndroidNativeCalcView = 0;
	UBOOL AndroidIsCityIntroDraw = 0;
	static DOUBLE AndroidAccumLock = 0.0;
	static DOUBLE AndroidAccumWorld = 0.0;
	static DOUBLE AndroidAccumUnlock = 0.0;
	static DOUBLE AndroidAccumAudio = 0.0;
	static DOUBLE AndroidAccumPre = 0.0;
	static DOUBLE AndroidAccumActorPre = 0.0;
	static DOUBLE AndroidAccumPost = 0.0;
	static DOUBLE AndroidAccumActorPost = 0.0;
	static DOUBLE AndroidAccumConsolePost = 0.0;
	static DOUBLE AndroidAccumAudioPost = 0.0;
	static DOUBLE AndroidAccumFinish = 0.0;
	static DOUBLE AndroidAccumRenderPre = 0.0;
	static DOUBLE AndroidAccumConsolePre = 0.0;
	static DOUBLE AndroidAccumCanvasUpdate = 0.0;
	static DOUBLE AndroidAccumCalcView = 0.0;
	static DOUBLE AndroidAccumPointCheck = 0.0;
	static DOUBLE AndroidAccumFlashSetup = 0.0;
	static INT AndroidAccumNativeCalcView = 0;
	static DOUBLE AndroidAccumTotal = 0.0;
	static DOUBLE AndroidMaxTotal = 0.0;
	static INT AndroidTimingFrames = 0;
#endif

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
	static INT AndroidCalcViewTraceCount = 0;
	UBOOL AndroidCityIntroCameraTrace = 0;
#if PLATFORM_ANDROID
	if
	(	GLevel
	&&	GLevel->GetOuter()
	&&	appStricmp( GLevel->GetOuter()->GetName(), TEXT("CityIntro") ) == 0 )
	{
		AndroidIsCityIntroDraw = 1;
		// if( AndroidCalcViewTraceCount < 64 )
		// {
		// 	AndroidCityIntroCameraTrace = 1;
		// 	APawn* AndroidPawn = Cast<APawn>(Viewport->Actor);
		// 	debugf( NAME_Log, TEXT("UT99_ANDROID_V258_CALCVIEW_BEFORE count=%i actor=%s loc=%f,%f,%f rot=%i,%i,%i viewrot=%i,%i,%i physics=%i target=%s targetRot=%i,%i,%i alpha=%f rate=%f"),
		// 		AndroidCalcViewTraceCount,
		// 		Viewport->Actor ? Viewport->Actor->GetFullName() : TEXT("None"),
		// 		ViewLocation.X,
		// 		ViewLocation.Y,
		// 		ViewLocation.Z,
		// 		ViewRotation.Pitch,
		// 		ViewRotation.Yaw,
		// 		ViewRotation.Roll,
		// 		AndroidPawn ? AndroidPawn->ViewRotation.Pitch : 0,
		// 		AndroidPawn ? AndroidPawn->ViewRotation.Yaw : 0,
		// 		AndroidPawn ? AndroidPawn->ViewRotation.Roll : 0,
		// 		Viewport->Actor ? Viewport->Actor->Physics : 0,
		// 		(Viewport->Actor && Viewport->Actor->Target) ? Viewport->Actor->Target->GetFullName() : TEXT("None"),
		// 		(Viewport->Actor && Viewport->Actor->Target) ? Viewport->Actor->Target->Rotation.Pitch : 0,
		// 		(Viewport->Actor && Viewport->Actor->Target) ? Viewport->Actor->Target->Rotation.Yaw : 0,
		// 		(Viewport->Actor && Viewport->Actor->Target) ? Viewport->Actor->Target->Rotation.Roll : 0,
		// 		Viewport->Actor ? Viewport->Actor->PhysAlpha : 0.0f,
		// 		Viewport->Actor ? Viewport->Actor->PhysRate : 0.0f );
		// }
	}
#endif
#if PLATFORM_ANDROID && UT99_ANDROID_FRAME_TRACE
	if( AndroidCalcViewTraceCount < 24 || (AndroidCalcViewTraceCount % 120) == 0 )
		debugf( NAME_Log, TEXT("UT99_ANDROID_V221_CALCVIEW_BEFORE count=%i actor=%s loc=%f,%f,%f rot=%i,%i,%i viewTarget=%p"),
			AndroidCalcViewTraceCount,
			Viewport->Actor ? Viewport->Actor->GetFullName() : TEXT("None"),
			ViewLocation.X,
			ViewLocation.Y,
			ViewLocation.Z,
			ViewRotation.Pitch,
			ViewRotation.Yaw,
			ViewRotation.Roll,
			Viewport->Actor ? Viewport->Actor->ViewTarget : NULL );
#endif
#if PLATFORM_ANDROID
	DOUBLE AndroidCalcViewStart = appSeconds();
	APlayerPawn* AndroidPlayerPawn = Cast<APlayerPawn>(Viewport->Actor);
	if
	(	AndroidIsCityIntroDraw
	&&	AndroidPlayerPawn
	&&	AndroidPlayerPawn->Physics == PHYS_Interpolating
	&&	!AndroidPlayerPawn->ViewTarget
	&&	!AndroidPlayerPawn->bBehindView
	&&	!GAndroidFrontendMenuRequested )
	{
		AndroidNativeCalcView = 1;
		ViewActor = AndroidPlayerPawn;
		ViewLocation = AndroidPlayerPawn->Location;
		ViewRotation = AndroidPlayerPawn->ViewRotation;
		ViewLocation.Z += AndroidPlayerPawn->EyeHeight;
		ViewLocation += AndroidPlayerPawn->WalkBob;
	}
	else
#endif
		Viewport->Actor->eventPlayerCalcView( ViewActor, ViewLocation, ViewRotation );
#if PLATFORM_ANDROID
	AndroidCalcViewMs = (appSeconds() - AndroidCalcViewStart) * 1000.0;
#endif
// #if PLATFORM_ANDROID
// 	if( AndroidCityIntroCameraTrace )
// 	{
// 		APawn* AndroidPawn = Cast<APawn>(Viewport->Actor);
// 		FVector AndroidDir = ViewRotation.Vector();
// 		debugf( NAME_Log, TEXT("UT99_ANDROID_V258_CALCVIEW_AFTER count=%i viewActor=%s loc=%f,%f,%f rot=%i,%i,%i viewrot=%i,%i,%i dir=%f,%f,%f behind=%i"),
// 			AndroidCalcViewTraceCount,
// 			ViewActor ? ViewActor->GetFullName() : TEXT("None"),
// 			ViewLocation.X,
// 			ViewLocation.Y,
// 			ViewLocation.Z,
// 			ViewRotation.Pitch,
// 			ViewRotation.Yaw,
// 			ViewRotation.Roll,
// 			AndroidPawn ? AndroidPawn->ViewRotation.Pitch : 0,
// 			AndroidPawn ? AndroidPawn->ViewRotation.Yaw : 0,
// 			AndroidPawn ? AndroidPawn->ViewRotation.Roll : 0,
// 			AndroidDir.X,
// 			AndroidDir.Y,
// 			AndroidDir.Z,
// 			Viewport->Actor ? Viewport->Actor->bBehindView : 0 );
// 	}
// #endif
#if PLATFORM_ANDROID && UT99_ANDROID_FRAME_TRACE
	if( AndroidCalcViewTraceCount < 24 || (AndroidCalcViewTraceCount % 120) == 0 )
		debugf( NAME_Log, TEXT("UT99_ANDROID_V221_CALCVIEW_AFTER count=%i viewActor=%s loc=%f,%f,%f rot=%i,%i,%i"),
			AndroidCalcViewTraceCount,
			ViewActor ? ViewActor->GetFullName() : TEXT("None"),
			ViewLocation.X,
			ViewLocation.Y,
			ViewLocation.Z,
			ViewRotation.Pitch,
			ViewRotation.Yaw,
			ViewRotation.Roll );
	if( AndroidCalcViewTraceCount < 16 )
	{
		FCheckResult PointHit(1.0);
		UBOOL PointClear = GLevel->Model->PointCheck( PointHit, NULL, ViewLocation, FVector(0,0,0), 0 );
		debugf( NAME_Log, TEXT("UT99_ANDROID_V228_CAMERA_POINT count=%i clear=%i hitTime=%f hitItem=%i loc=%f,%f,%f"),
			AndroidCalcViewTraceCount,
			PointClear,
			PointHit.Time,
			PointHit.Item,
			ViewLocation.X,
			ViewLocation.Y,
			ViewLocation.Z );
		for( INT Sweep=0; Sweep<8; Sweep++ )
		{
			FRotator SweepRot(0, ViewRotation.Yaw + Sweep*8192, 0);
			FVector Dir = SweepRot.Vector();
			FVector End = ViewLocation + Dir * 4096.0f;
			FCheckResult Hit(1.0);
			UBOOL Clear = GLevel->SingleLineCheck( Hit, NULL, End, ViewLocation, TRACE_VisBlocking, FVector(0,0,0) );
			debugf( NAME_Log, TEXT("UT99_ANDROID_V228_CAMERA_RAY count=%i sweep=%i yaw=%i clear=%i dist=%f hitTime=%f hitItem=%i hitLoc=%f,%f,%f dir=%f,%f,%f"),
				AndroidCalcViewTraceCount,
				Sweep,
				SweepRot.Yaw,
				Clear,
				Clear ? 4096.0f : Hit.Time * 4096.0f,
				Hit.Time,
				Hit.Item,
				Hit.Location.X,
				Hit.Location.Y,
				Hit.Location.Z,
				Dir.X,
				Dir.Y,
			Dir.Z );
		}
	}
#endif
	AndroidCalcViewTraceCount++;
	check(ViewActor);

	// Precaching message.
	BYTE SavedAction = ViewActor->Level->LevelAction;
	if( Viewport->RenDev->PrecacheOnFlip && !Viewport->bSuspendPrecaching )
		ViewActor->Level->LevelAction = LEVACT_Precaching;

	// See if viewer is inside world.
	DWORD LockFlags=0;
	FCheckResult Hit;
#if PLATFORM_ANDROID
	DOUBLE AndroidPointCheckStart = appSeconds();
#endif
	if( !GLevel->Model->PointCheck(Hit,NULL,ViewLocation,FVector(0,0,0),0) )
		LockFlags |= LOCKR_ClearScreen;
#if PLATFORM_ANDROID
	AndroidPointCheckMs = (appSeconds() - AndroidPointCheckStart) * 1000.0;
#endif

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
#if PLATFORM_ANDROID
	DOUBLE AndroidFlashSetupStart = appSeconds();
#endif
	FPlane FlashScale = Client->ScreenFlashes ? 0.5*Viewport->Actor->FlashScale : FVector(0.5,0.5,0.5);
	FPlane FlashFog   = Client->ScreenFlashes ? Viewport->Actor->FlashFog : FVector(0,0,0);
	FlashScale.X = Clamp( FlashScale.X, 0.f, 1.f );
	FlashScale.Y = Clamp( FlashScale.Y, 0.f, 1.f );
	FlashScale.Z = Clamp( FlashScale.Z, 0.f, 1.f );
	FlashFog.X   = Clamp( FlashFog.X  , 0.f, 1.f );
	FlashFog.Y   = Clamp( FlashFog.Y  , 0.f, 1.f );
	FlashFog.Z   = Clamp( FlashFog.Z  , 0.f, 1.f );
#if PLATFORM_ANDROID
	AndroidFlashSetupMs = (appSeconds() - AndroidFlashSetupStart) * 1000.0;
#endif
	if( DrawTraceCount < 5 )
		debugf( NAME_Log, TEXT("UT99_ANDROID_V141_VIEWPORT_TRACE Draw begin count=%i Size=%ix%i Blit=%i LockFlags=0x%08X Actor=%s"), DrawTraceCount, Viewport->SizeX, Viewport->SizeY, Blit, LockFlags, ViewActor ? ViewActor->GetName() : TEXT("None") );
	if( Viewport->Lock(FlashScale,FlashFog,FPlane(0,0,0,0),LockFlags,HitData,HitSize) )
	{
#if PLATFORM_ANDROID
		AndroidAfterLock = appSeconds();
#endif
		// Setup rendering coords.
		FSceneNode* Frame = Render->CreateMasterFrame( Viewport, ViewLocation, ViewRotation, NULL );
		static INT AndroidDrawGateTraceCount = 0;

		// Update level audio.
		if( Audio )
		{
#if PLATFORM_ANDROID
			DOUBLE AndroidPhaseStart = appSeconds();
#endif
			clock(GLevel->AudioTickCycles);
			Audio->Update( ViewActor->Region, Frame->Coords );
			unclock(GLevel->AudioTickCycles);
#if PLATFORM_ANDROID
			AndroidAudioMs = (appSeconds() - AndroidPhaseStart) * 1000.0;
#endif
		}

		// Render.
#if PLATFORM_ANDROID
		DOUBLE AndroidPhaseStart = appSeconds();
#endif
		Render->PreRender( Frame );
#if PLATFORM_ANDROID
		AndroidRenderPreMs = (appSeconds() - AndroidPhaseStart) * 1000.0;
		AndroidPhaseStart = appSeconds();
#endif
		Viewport->Canvas->Render = Render;
		if( Viewport->Console
#if PLATFORM_ANDROID
		&&	!(AndroidIsCityIntroDraw && !GAndroidFrontendMenuRequested)
#endif
		)
			Viewport->Console->PreRender( Frame );
#if PLATFORM_ANDROID
		AndroidConsolePreMs = (appSeconds() - AndroidPhaseStart) * 1000.0;
		AndroidPhaseStart = appSeconds();
#endif
		Viewport->Canvas->Update( Frame );
#if PLATFORM_ANDROID
		AndroidCanvasUpdateMs = (appSeconds() - AndroidPhaseStart) * 1000.0;
		AndroidPreMs = (appSeconds() - AndroidPhaseStart) * 1000.0;
		AndroidPhaseStart = appSeconds();
#endif
#if PLATFORM_ANDROID
		if( !(AndroidIsCityIntroDraw && Viewport->Actor && Viewport->Actor->Physics == PHYS_Interpolating && !GAndroidFrontendMenuRequested) )
#endif
			Viewport->Actor->eventPreRender( Viewport->Canvas );
#if PLATFORM_ANDROID
		AndroidActorPreMs = (appSeconds() - AndroidPhaseStart) * 1000.0;
		AndroidPreMs += AndroidRenderPreMs + AndroidConsolePreMs;
#endif
		UBOOL bConsoleDrawWorld = !Viewport->Console || Viewport->Console->GetDrawWorld();
#if PLATFORM_ANDROID
		if( GAndroidFrontendMenuRequested )
		{
			static INT AndroidFrontendDrawWorldSkipLogs = 0;
			if( bConsoleDrawWorld && (AndroidFrontendDrawWorldSkipLogs < 16 || (AndroidFrontendDrawWorldSkipLogs % 240) == 0) )
			{
				debugf( NAME_Log, TEXT("UT99_ANDROID_V343_SKIP_FRONTEND_DRAWWORLD actor=%s level=%s count=%i"),
					Viewport->Actor ? Viewport->Actor->GetFullName() : TEXT("None"),
					(Viewport->Actor && Viewport->Actor->GetLevel() && Viewport->Actor->GetLevel()->GetOuter()) ? Viewport->Actor->GetLevel()->GetOuter()->GetName() : TEXT("None"),
					AndroidFrontendDrawWorldSkipLogs );
			}
			AndroidFrontendDrawWorldSkipLogs++;
			bConsoleDrawWorld = 0;
		}
#endif
#if PLATFORM_ANDROID && UT99_ANDROID_FRAME_TRACE
		if( AndroidDrawGateTraceCount < 80 || (AndroidDrawGateTraceCount % 120) == 0 )
		{
			debugf( NAME_Log, TEXT("UT99_ANDROID_V219_DRAW_GATE count=%i frame=%ix%i xb=%i yb=%i canvasOrg=%f,%f canvasClip=%f,%f drawWorld=%i console=%i lockFlags=0x%08X actor=%s viewActor=%s levelAction=%i"),
				AndroidDrawGateTraceCount,
				Frame ? Frame->X : 0,
				Frame ? Frame->Y : 0,
				Frame ? Frame->XB : 0,
				Frame ? Frame->YB : 0,
				Viewport->Canvas ? Viewport->Canvas->OrgX : 0.f,
				Viewport->Canvas ? Viewport->Canvas->OrgY : 0.f,
				Viewport->Canvas ? Viewport->Canvas->ClipX : 0.f,
				Viewport->Canvas ? Viewport->Canvas->ClipY : 0.f,
				bConsoleDrawWorld,
				Viewport->Console != NULL,
				LockFlags,
				Viewport->Actor ? Viewport->Actor->GetFullName() : TEXT("None"),
				ViewActor ? ViewActor->GetFullName() : TEXT("None"),
				ViewActor && ViewActor->Level ? ViewActor->Level->LevelAction : -1 );
		}
#endif
		AndroidDrawGateTraceCount++;
#if defined(LEGEND) //MWP
		INT SaveXB = Frame->XB, SaveYB = Frame->YB, SaveX = Frame->X, SaveY = Frame->Y;
		Frame->XB += Viewport->Canvas->OrgX;
		Frame->YB += Viewport->Canvas->OrgY;
		Frame->X = Viewport->Canvas->ClipX;
		Frame->Y = Viewport->Canvas->ClipY;
		Frame->ComputeRenderSize();
#endif
		if( Frame->X>0 && Frame->Y>0 && bConsoleDrawWorld )
		{
#if PLATFORM_ANDROID
			AndroidWorldStart = appSeconds();
#endif
			Render->DrawWorld( Frame );
#if PLATFORM_ANDROID
			AndroidAfterWorld = appSeconds();
#endif
		}
#if PLATFORM_ANDROID
		else
		{
			AndroidWorldStart = AndroidAfterLock;
			AndroidAfterWorld = AndroidAfterLock;
		}
#endif
#if defined(LEGEND) //MWP
		Frame->XB = SaveXB, Frame->YB = SaveYB, Frame->X = SaveX, Frame->Y = SaveY;
		Frame->ComputeRenderSize();
#endif
		AndroidPhaseStart = appSeconds();
		Viewport->RenDev->EndFlash();
		AndroidPostMs += (appSeconds() - AndroidPhaseStart) * 1000.0;
		AndroidPhaseStart = appSeconds();
#if defined(__ANDROID__)
		UBOOL AndroidSkipNullHudPostRender = 0;
		if( Viewport->Actor && Viewport->Actor->myHUD )
		{
			const TCHAR* AndroidHudClassName = Viewport->Actor->myHUD->GetClass()->GetName();
			static UBOOL AndroidLoggedHudPostState = 0;
			if( !AndroidLoggedHudPostState )
			{
				AndroidLoggedHudPostState = 1;
				debugf( NAME_Log, TEXT("UT99_ANDROID_V251_HUD_POST_STATE actor=%s hud=%s hudClass=%s menu=%p showMenu=%i"),
					Viewport->Actor->GetFullName(),
					Viewport->Actor->myHUD->GetFullName(),
					AndroidHudClassName ? AndroidHudClassName : TEXT("None"),
					Viewport->Actor->myHUD->MainMenu,
					Viewport->Actor->bShowMenu ? 1 : 0 );
			}
			if( AndroidHudClassName && appStricmp( AndroidHudClassName, TEXT("CHNullHUD") ) == 0 && !GAndroidFrontendMenuRequested )
			{
				static UBOOL AndroidLoggedSkipNullHudPostRender = 0;
				AndroidSkipNullHudPostRender = 1;
				if( !AndroidLoggedSkipNullHudPostRender )
				{
					AndroidLoggedSkipNullHudPostRender = 1;
					debugf( NAME_Log, TEXT("UT99_ANDROID_V251_SKIP_NULL_HUD_POSTRENDER actor=%s hud=%s menu=%p"),
						Viewport->Actor->GetFullName(),
						Viewport->Actor->myHUD->GetFullName(),
						Viewport->Actor->myHUD->MainMenu );
				}
			}
		}
		if( !AndroidSkipNullHudPostRender )
#endif
#if PLATFORM_ANDROID
		if( !GAndroidFrontendMenuRequested )
#endif
			Viewport->Actor->eventPostRender( Viewport->Canvas );
		AndroidActorPostMs = (appSeconds() - AndroidPhaseStart) * 1000.0;
		UBOOL AndroidSkipLogoConsolePostRender = 0;
#if defined(__ANDROID__)
		if
		(	Viewport->Actor
		&&	Viewport->Actor->GetLevel()
		&&	Viewport->Actor->GetLevel()->GetOuter()
		&&	( appStricmp( Viewport->Actor->GetLevel()->GetOuter()->GetName(), TEXT("UT-Logo-Map") ) == 0
			|| appStricmp( Viewport->Actor->GetLevel()->GetOuter()->GetName(), TEXT("Entry") ) == 0 )
		&&	!GAndroidFrontendMenuRequested )
		{
			static UBOOL AndroidLoggedSkipLogoConsolePostRender = 0;
			AndroidSkipLogoConsolePostRender = 1;
			if( !AndroidLoggedSkipLogoConsolePostRender )
			{
				AndroidLoggedSkipLogoConsolePostRender = 1;
				debugf( NAME_Log, TEXT("UT99_ANDROID_V252_SKIP_LOGO_CONSOLE_POSTRENDER actor=%s"),
					Viewport->Actor->GetFullName() );
			}
		}
		if
		(	Viewport->Actor
		&&	Viewport->Actor->GetLevel()
		&&	Viewport->Actor->GetLevel()->GetOuter()
		&&	appStricmp( Viewport->Actor->GetLevel()->GetOuter()->GetName(), TEXT("CityIntro") ) == 0
		&&	Viewport->Actor->Physics == PHYS_Interpolating
		&&	!GAndroidFrontendMenuRequested )
		{
			static UBOOL AndroidLoggedSkipCityIntroConsolePostRender = 0;
			AndroidSkipLogoConsolePostRender = 1;
			if( !AndroidLoggedSkipCityIntroConsolePostRender )
			{
				AndroidLoggedSkipCityIntroConsolePostRender = 1;
				debugf( NAME_Log, TEXT("UT99_ANDROID_V260_SKIP_CITYINTRO_CONSOLE_POSTRENDER actor=%s"),
					Viewport->Actor->GetFullName() );
			}
		}
#endif
		if( Viewport->Console && !AndroidSkipLogoConsolePostRender )
		{
			AndroidPhaseStart = appSeconds();
#if defined(__ANDROID__)
			if( GAndroidFrontendMenuRequested )
			{
				static INT AndroidConsolePostRenderPreDumpCount = 0;
				if( AndroidConsolePostRenderPreDumpCount < 1 )
				{
					AndroidConsolePostRenderPreDumpCount++;
					AndroidDumpConsoleWindowState( Viewport, TEXT("ConsolePostRender") );
				}
			}
#endif
			DOUBLE AndroidConsoleNativeStart = appSeconds();
			Viewport->Console->PostRender( Frame );
			AndroidConsoleNativePostMs = (appSeconds() - AndroidConsoleNativeStart) * 1000.0;
			DOUBLE AndroidConsoleEventStart = appSeconds();
#if defined(__ANDROID__)
			if( GAndroidFrontendMenuRequested )
				GAndroidInFrontendConsolePostRender++;
#endif
			Viewport->Console->eventPostRender( Viewport->Canvas );
#if defined(__ANDROID__)
			if( GAndroidFrontendMenuRequested && GAndroidInFrontendConsolePostRender > 0 )
				GAndroidInFrontendConsolePostRender--;
#endif
			AndroidConsoleEventPostMs = (appSeconds() - AndroidConsoleEventStart) * 1000.0;
#if defined(__ANDROID__)
			if( GAndroidFrontendMenuRequested )
			{
				static INT AndroidConsolePostRenderAfterDumpCount = 0;
				if( AndroidConsolePostRenderAfterDumpCount < 4 )
				{
					AndroidConsolePostRenderAfterDumpCount++;
					AndroidDumpConsoleWindowState( Viewport, TEXT("ConsolePostRenderAfterEvent") );
				}
			}
#endif
			AndroidConsolePostMs = (appSeconds() - AndroidPhaseStart) * 1000.0;
#if defined(__ANDROID__)
			if( GAndroidFrontendMenuRequested && AndroidConsolePostMs > 100.0 )
			{
				static INT AndroidSlowConsolePostLogs = 0;
				if( AndroidSlowConsolePostLogs < 64 )
				{
					debugf( NAME_Log, TEXT("UT99_ANDROID_V345_SLOW_CONSOLE_POST totalMs=%f nativePostMs=%f eventPostMs=%f console=%s state=%s actor=%s count=%i"),
						AndroidConsolePostMs,
						AndroidConsoleNativePostMs,
						AndroidConsoleEventPostMs,
						Viewport->Console ? Viewport->Console->GetFullName() : TEXT("None"),
						(Viewport->Console && Viewport->Console->GetStateFrame() && Viewport->Console->GetStateFrame()->StateNode) ? Viewport->Console->GetStateFrame()->StateNode->GetName() : TEXT("None"),
						Viewport->Actor ? Viewport->Actor->GetFullName() : TEXT("None"),
						AndroidSlowConsolePostLogs );
				}
				AndroidSlowConsolePostLogs++;
			}
#endif
		}
		if( Audio )
		{
			AndroidPhaseStart = appSeconds();
			Audio->PostRender( Frame );
			AndroidAudioPostMs = (appSeconds() - AndroidPhaseStart) * 1000.0;
		}

#if PLATFORM_ANDROID && UT99_ANDROID_SHOW_FPS_COUNTER
		if( Viewport->Canvas && Viewport->Canvas->SmallFont && !GAndroidFrontendMenuRequested )
		{
			Viewport->Canvas->Color = FColor(255,255,255);
			Viewport->Canvas->CurX = 4;
			Viewport->Canvas->CurY = 4;
			Viewport->Canvas->WrappedPrintf( Viewport->Canvas->SmallFont, 0, TEXT("FPS %.1f  %.1fms  max %.1fms"),
				(FLOAT)AndroidDisplayedFps,
				(FLOAT)AndroidDisplayedFrameMs,
				(FLOAT)AndroidDisplayedMaxFrameMs );
		}
#endif

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
		AndroidPhaseStart = appSeconds();
		Render->PostRender( Frame );
		AndroidPostMs += (appSeconds() - AndroidPhaseStart) * 1000.0;
#if PLATFORM_ANDROID
		AndroidUnlockStart = appSeconds();
#endif
		Viewport->Unlock( Blit );
		DOUBLE AndroidAfterUnlock = appSeconds();
		Render->FinishMasterFrame();
#if PLATFORM_ANDROID
		AndroidDrawEnd = appSeconds();
		AndroidFinishMs = (AndroidDrawEnd - AndroidAfterUnlock) * 1000.0;
		const DOUBLE AndroidLockMs = (AndroidAfterLock - AndroidDrawStart) * 1000.0;
		const DOUBLE AndroidWorldMs = (AndroidAfterWorld - AndroidWorldStart) * 1000.0;
		const DOUBLE AndroidUnlockMs = (AndroidAfterUnlock - AndroidUnlockStart) * 1000.0;
		const DOUBLE AndroidTotalMs = (AndroidDrawEnd - AndroidDrawStart) * 1000.0;
		static INT AndroidSlowDrawLogs = 0;
		if( AndroidTotalMs > 100.0 && AndroidSlowDrawLogs < 64 )
		{
			debugf( NAME_Log, TEXT("UT99_ANDROID_V343_SLOW_DRAW totalMs=%f lockMs=%f audioMs=%f preMs=%f renderPreMs=%f consolePreMs=%f canvasUpdateMs=%f actorPreMs=%f worldMs=%f postMs=%f actorPostMs=%f consolePostMs=%f consoleNativeMs=%f consoleEventMs=%f audioPostMs=%f unlockMs=%f finishMs=%f frontend=%i drawWorld=%i size=%ix%i count=%i"),
				AndroidTotalMs,
				AndroidLockMs,
				AndroidAudioMs,
				AndroidPreMs,
				AndroidRenderPreMs,
				AndroidConsolePreMs,
				AndroidCanvasUpdateMs,
				AndroidActorPreMs,
				AndroidWorldMs,
				AndroidPostMs,
				AndroidActorPostMs,
				AndroidConsolePostMs,
				AndroidConsoleNativePostMs,
				AndroidConsoleEventPostMs,
				AndroidAudioPostMs,
				AndroidUnlockMs,
				AndroidFinishMs,
				GAndroidFrontendMenuRequested ? 1 : 0,
				bConsoleDrawWorld ? 1 : 0,
				Viewport->SizeX,
				Viewport->SizeY,
				AndroidSlowDrawLogs );
			AndroidSlowDrawLogs++;
		}
		AndroidAccumLock += AndroidLockMs;
		AndroidAccumWorld += AndroidWorldMs;
		AndroidAccumUnlock += AndroidUnlockMs;
		AndroidAccumAudio += AndroidAudioMs;
		AndroidAccumPre += AndroidPreMs;
		AndroidAccumActorPre += AndroidActorPreMs;
		AndroidAccumPost += AndroidPostMs;
		AndroidAccumActorPost += AndroidActorPostMs;
		AndroidAccumConsolePost += AndroidConsolePostMs;
		AndroidAccumAudioPost += AndroidAudioPostMs;
		AndroidAccumFinish += AndroidFinishMs;
		AndroidAccumRenderPre += AndroidRenderPreMs;
		AndroidAccumConsolePre += AndroidConsolePreMs;
		AndroidAccumCanvasUpdate += AndroidCanvasUpdateMs;
		AndroidAccumCalcView += AndroidCalcViewMs;
		AndroidAccumPointCheck += AndroidPointCheckMs;
		AndroidAccumFlashSetup += AndroidFlashSetupMs;
		AndroidAccumNativeCalcView += AndroidNativeCalcView ? 1 : 0;
		AndroidAccumTotal += AndroidTotalMs;
		AndroidMaxTotal = Max( AndroidMaxTotal, AndroidTotalMs );
		AndroidTimingFrames++;
		if( AndroidTimingFrames >= 60 )
		{
			debugf( NAME_Log, TEXT("UT99_ANDROID_V247_DRAW_TIMING frames=%i avgTotalMs=%f maxTotalMs=%f avgLockMs=%f avgAudioMs=%f avgPreMs=%f avgActorPreMs=%f avgWorldMs=%f avgPostMs=%f avgActorPostMs=%f avgConsolePostMs=%f avgAudioPostMs=%f avgUnlockMs=%f avgFinishMs=%f size=%ix%i"),
				AndroidTimingFrames,
				AndroidAccumTotal / AndroidTimingFrames,
				AndroidMaxTotal,
				AndroidAccumLock / AndroidTimingFrames,
				AndroidAccumAudio / AndroidTimingFrames,
				AndroidAccumPre / AndroidTimingFrames,
				AndroidAccumActorPre / AndroidTimingFrames,
				AndroidAccumWorld / AndroidTimingFrames,
				AndroidAccumPost / AndroidTimingFrames,
				AndroidAccumActorPost / AndroidTimingFrames,
				AndroidAccumConsolePost / AndroidTimingFrames,
				AndroidAccumAudioPost / AndroidTimingFrames,
				AndroidAccumUnlock / AndroidTimingFrames,
				AndroidAccumFinish / AndroidTimingFrames,
				Viewport->SizeX,
				Viewport->SizeY );
			debugf( NAME_Log, TEXT("UT99_ANDROID_V298_DRAW_PRE_TIMING frames=%i avgRenderPreMs=%f avgConsolePreMs=%f avgCanvasUpdateMs=%f avgActorPreMs=%f cityIntro=%i frontend=%i"),
				AndroidTimingFrames,
				AndroidAccumRenderPre / AndroidTimingFrames,
				AndroidAccumConsolePre / AndroidTimingFrames,
				AndroidAccumCanvasUpdate / AndroidTimingFrames,
				AndroidAccumActorPre / AndroidTimingFrames,
				AndroidIsCityIntroDraw,
				GAndroidFrontendMenuRequested );
			debugf( NAME_Log, TEXT("UT99_ANDROID_V301_DRAW_FRONT_TIMING frames=%i avgCalcViewMs=%f avgPointCheckMs=%f avgFlashSetupMs=%f nativeCalc=%i/%i"),
				AndroidTimingFrames,
				AndroidAccumCalcView / AndroidTimingFrames,
				AndroidAccumPointCheck / AndroidTimingFrames,
				AndroidAccumFlashSetup / AndroidTimingFrames,
				AndroidAccumNativeCalcView,
				AndroidTimingFrames );
			AndroidAccumLock = AndroidAccumWorld = AndroidAccumUnlock = AndroidAccumTotal = AndroidMaxTotal = 0.0;
			AndroidAccumAudio = AndroidAccumPre = AndroidAccumActorPre = AndroidAccumPost = 0.0;
			AndroidAccumActorPost = AndroidAccumConsolePost = AndroidAccumAudioPost = AndroidAccumFinish = 0.0;
			AndroidAccumRenderPre = AndroidAccumConsolePre = AndroidAccumCanvasUpdate = 0.0;
			AndroidAccumCalcView = AndroidAccumPointCheck = AndroidAccumFlashSetup = 0.0;
			AndroidAccumNativeCalcView = 0;
			AndroidTimingFrames = 0;
		}
#endif
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
#if PLATFORM_ANDROID
	const DOUBLE AndroidEngineTickStart = appSeconds();
	DOUBLE AndroidAfterPause = AndroidEngineTickStart;
	DOUBLE AndroidAfterStatic = AndroidEngineTickStart;
	DOUBLE AndroidAfterLevel = AndroidEngineTickStart;
	DOUBLE AndroidAfterTravel = AndroidEngineTickStart;
	DOUBLE AndroidAfterPending = AndroidEngineTickStart;
	DOUBLE AndroidAfterClient = AndroidEngineTickStart;
	static DOUBLE AndroidTickWindowStart = 0.0;
	static DOUBLE AndroidAccumPauseMs = 0.0;
	static DOUBLE AndroidAccumStaticMs = 0.0;
	static DOUBLE AndroidAccumLevelMs = 0.0;
	static DOUBLE AndroidAccumTravelMs = 0.0;
	static DOUBLE AndroidAccumPendingMs = 0.0;
	static DOUBLE AndroidAccumClientMs = 0.0;
	static DOUBLE AndroidAccumTotalMs = 0.0;
	static DOUBLE AndroidMaxTotalMs = 0.0;
	static DOUBLE AndroidAccumDeltaSeconds = 0.0;
	static INT AndroidEngineTickFrames = 0;
	if( AndroidTickWindowStart <= 0.0 )
		AndroidTickWindowStart = AndroidEngineTickStart;
#endif
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
#if PLATFORM_ANDROID
	AndroidAfterPause = appSeconds();
#endif

	// Update subsystems.
	UObject::StaticTick();				
	GCache.Tick();
#if PLATFORM_ANDROID
	AndroidAfterStatic = appSeconds();
#endif

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
#if PLATFORM_ANDROID
		UBOOL bAndroidFrontendUiActive =
			GAndroidFrontendMenuRequested
		&&	Client
		&&	Client->Viewports.Num()
		&&	Client->Viewports(0)
		&&	( Client->Viewports(0)->bShowWindowsMouse || (Client->Viewports(0)->Actor && Client->Viewports(0)->Actor->bShowMenu) );
		if( bAndroidFrontendUiActive )
		{
			static INT AndroidSkipFrontendLevelTickLogs = 0;
			if( AndroidSkipFrontendLevelTickLogs < 16 || (AndroidSkipFrontendLevelTickLogs % 240) == 0 )
			{
				debugf( NAME_Log, TEXT("UT99_ANDROID_V339_SKIP_FRONTEND_LEVEL_TICK level=%s delta=%f actor=%s mouse=%i showMenu=%i count=%i"),
					GLevel->GetOuter() ? GLevel->GetOuter()->GetName() : TEXT("None"),
					DeltaSeconds,
					Client->Viewports(0)->Actor ? Client->Viewports(0)->Actor->GetFullName() : TEXT("None"),
					Client->Viewports(0)->bShowWindowsMouse ? 1 : 0,
					(Client->Viewports(0)->Actor && Client->Viewports(0)->Actor->bShowMenu) ? 1 : 0,
					AndroidSkipFrontendLevelTickLogs );
			}
			AndroidSkipFrontendLevelTickLogs++;
		}
		else
#endif
		// tick the level
		GLevel->Tick( LEVELTICK_All, DeltaSeconds );
	}
	if( GEntry && GEntry!=GLevel )
		GEntry->Tick( LEVELTICK_All, DeltaSeconds );
	if( Client && Client->Viewports.Num() && Client->Viewports(0)->Actor->GetLevel()!=GLevel )
		Client->Viewports(0)->Actor->GetLevel()->Tick( LEVELTICK_All, DeltaSeconds );
	unclock(GameCycles);
	unguard;
#if PLATFORM_ANDROID
	AndroidAfterLevel = appSeconds();
#endif

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
					if( P && P->Player && P->PlayerReplicationInfo )
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
#if PLATFORM_ANDROID
					else if( P && P->Player )
					{
						static INT AndroidSkipBadServerTravelLogs = 0;
						if( AndroidSkipBadServerTravelLogs < 16 )
						{
							debugf( NAME_Warning, TEXT("UT99_ANDROID_V341_SKIP_BAD_SERVER_TRAVEL_ITEMS player=%s pri=%p level=%s count=%i"),
								P->GetFullName(),
								P->PlayerReplicationInfo,
								GLevel && GLevel->GetOuter() ? GLevel->GetOuter()->GetName() : TEXT("None"),
								AndroidSkipBadServerTravelLogs );
						}
						AndroidSkipBadServerTravelLogs++;
					}
#endif
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
#if defined(PLATFORM_64BIT)
		if( appStricmp(*Viewport->TravelURL,TEXT("?entry"))==0 || appStricmp(*Viewport->TravelURL,TEXT("?failed"))==0 )
		{
			debugf( NAME_Warning, TEXT("UT99_ANDROID_V276_SKIP_ENTRY_CLIENT_TRAVEL url=%s actor=%s level=%s"),
				*Viewport->TravelURL,
				Viewport->Actor ? Viewport->Actor->GetFullName() : TEXT("None"),
				GLevel && GLevel->GetOuter() ? GLevel->GetOuter()->GetName() : TEXT("None") );
			if( Viewport->Actor )
			{
				Viewport->Actor->bShowMenu = 1;
				Viewport->Actor->WalkBob = FVector(0,0,0);
			}
			if( Viewport->Console )
			{
				Viewport->Console->GotoState( FName(TEXT("UWindow")) );
			}
			debugf( NAME_Log, TEXT("UT99_ANDROID_V277_MENU_FORCED_AFTER_ENTRY_SKIP actor=%s showMenu=%i consoleState=%s hud=%s mainMenu=%p"),
				Viewport->Actor ? Viewport->Actor->GetFullName() : TEXT("None"),
				Viewport->Actor ? Viewport->Actor->bShowMenu : 0,
				(Viewport->Console && Viewport->Console->GetStateFrame() && Viewport->Console->GetStateFrame()->StateNode) ? Viewport->Console->GetStateFrame()->StateNode->GetName() : TEXT("None"),
				(Viewport->Actor && Viewport->Actor->myHUD) ? Viewport->Actor->myHUD->GetFullName() : TEXT("None"),
				(Viewport->Actor && Viewport->Actor->myHUD) ? Viewport->Actor->myHUD->MainMenu : NULL );
			AndroidDumpConsoleWindowState( Viewport, TEXT("EntrySkip") );
			Viewport->TravelURL = TEXT("");
			return;
		}
#endif
		TMap<FString,FString> TravelInfo;

		// Export items.
		if( appStricmp(*Viewport->TravelURL,TEXT("?RESTART"))==0 )
		{
			TravelInfo = GLevel->TravelInfo;
		}
		else if( Viewport->bTravelItems )
		{
			if( Viewport->Actor && Viewport->Actor->PlayerReplicationInfo )
			{
				debugf( TEXT("Export travel for: %s"), *Viewport->Actor->PlayerReplicationInfo->PlayerName );
				FStringOutputDevice PlayerTravelInfo;
				ExportTravel( PlayerTravelInfo, Viewport->Actor );
				for( AActor* Inv=Viewport->Actor->Inventory; Inv; Inv=Inv->Inventory )
					ExportTravel( PlayerTravelInfo, Inv );
				TravelInfo.Set( *Viewport->Actor->PlayerReplicationInfo->PlayerName, *PlayerTravelInfo );
			}
#if PLATFORM_ANDROID
			else
			{
				debugf( NAME_Warning, TEXT("UT99_ANDROID_V336_SKIP_BAD_TRAVEL_ITEMS url=%s actor=%s pri=%p level=%s"),
					*Viewport->TravelURL,
					Viewport->Actor ? Viewport->Actor->GetFullName() : TEXT("None"),
					Viewport->Actor ? Viewport->Actor->PlayerReplicationInfo : NULL,
					GLevel && GLevel->GetOuter() ? GLevel->GetOuter()->GetName() : TEXT("None") );
				Viewport->bTravelItems = 0;
			}
#endif
		}
		FString Error;
		debugf( NAME_Log, TEXT("UT99_ANDROID_V275_CLIENT_TRAVEL_BEGIN url=%s type=%i actor=%s"),
			*Viewport->TravelURL,
			Viewport->TravelType,
			Viewport->Actor ? Viewport->Actor->GetFullName() : TEXT("None") );
		UBOOL bBrowseOk = Browse( FURL(&LastURL,*Viewport->TravelURL,Viewport->TravelType), &TravelInfo, Error );
		debugf( NAME_Log, TEXT("UT99_ANDROID_V275_CLIENT_TRAVEL_DONE ok=%i error=%s level=%s"),
			bBrowseOk,
			*Error,
			GLevel && GLevel->GetOuter() ? GLevel->GetOuter()->GetName() : TEXT("None") );
		Viewport->TravelURL=TEXT("");

		return;
	}
	unguard;
#if PLATFORM_ANDROID
	AndroidAfterTravel = appSeconds();
#endif

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
#if PLATFORM_ANDROID
	AndroidAfterPending = appSeconds();
#endif

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
#if PLATFORM_ANDROID
	AndroidAfterClient = appSeconds();
	AndroidAccumPauseMs += (AndroidAfterPause - AndroidEngineTickStart) * 1000.0;
	AndroidAccumStaticMs += (AndroidAfterStatic - AndroidAfterPause) * 1000.0;
	AndroidAccumLevelMs += (AndroidAfterLevel - AndroidAfterStatic) * 1000.0;
	AndroidAccumTravelMs += (AndroidAfterTravel - AndroidAfterLevel) * 1000.0;
	AndroidAccumPendingMs += (AndroidAfterPending - AndroidAfterTravel) * 1000.0;
	AndroidAccumClientMs += (AndroidAfterClient - AndroidAfterPending) * 1000.0;
	AndroidAccumTotalMs += (AndroidAfterClient - AndroidEngineTickStart) * 1000.0;
	AndroidMaxTotalMs = Max( AndroidMaxTotalMs, (AndroidAfterClient - AndroidEngineTickStart) * 1000.0 );
	AndroidAccumDeltaSeconds += DeltaSeconds;
	AndroidEngineTickFrames++;
	if( AndroidAfterClient - AndroidTickWindowStart >= 1.0 )
	{
		debugf( NAME_Log, TEXT("UT99_ANDROID_V304_ENGINE_TICK_TIMING frames=%i seconds=%f avgDeltaMs=%f avgTotalMs=%f maxTotalMs=%f avgPauseMs=%f avgStaticMs=%f avgLevelMs=%f avgTravelMs=%f avgPendingMs=%f avgClientMs=%f level=%s actors=%i"),
			AndroidEngineTickFrames,
			AndroidAfterClient - AndroidTickWindowStart,
			AndroidEngineTickFrames ? (AndroidAccumDeltaSeconds * 1000.0) / AndroidEngineTickFrames : 0.0,
			AndroidEngineTickFrames ? AndroidAccumTotalMs / AndroidEngineTickFrames : 0.0,
			AndroidMaxTotalMs,
			AndroidEngineTickFrames ? AndroidAccumPauseMs / AndroidEngineTickFrames : 0.0,
			AndroidEngineTickFrames ? AndroidAccumStaticMs / AndroidEngineTickFrames : 0.0,
			AndroidEngineTickFrames ? AndroidAccumLevelMs / AndroidEngineTickFrames : 0.0,
			AndroidEngineTickFrames ? AndroidAccumTravelMs / AndroidEngineTickFrames : 0.0,
			AndroidEngineTickFrames ? AndroidAccumPendingMs / AndroidEngineTickFrames : 0.0,
			AndroidEngineTickFrames ? AndroidAccumClientMs / AndroidEngineTickFrames : 0.0,
			GLevel && GLevel->GetOuter() ? GLevel->GetOuter()->GetName() : TEXT("None"),
			GLevel ? GLevel->Actors.Num() : 0 );
		AndroidTickWindowStart = AndroidAfterClient;
		AndroidAccumPauseMs = AndroidAccumStaticMs = AndroidAccumLevelMs = AndroidAccumTravelMs = 0.0;
		AndroidAccumPendingMs = AndroidAccumClientMs = AndroidAccumTotalMs = AndroidMaxTotalMs = 0.0;
		AndroidAccumDeltaSeconds = 0.0;
		AndroidEngineTickFrames = 0;
	}
#endif

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
