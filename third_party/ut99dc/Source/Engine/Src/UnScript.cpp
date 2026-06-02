/*=============================================================================
	UnScript.cpp: UnrealScript engine support code.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Description:
	UnrealScript execution and support code.

Revision history:
	* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRender.h"
#include "UnNet.h"

/*-----------------------------------------------------------------------------
	Tim's physics modes.
-----------------------------------------------------------------------------*/

FLOAT Splerp( FLOAT F )
{
	FLOAT S = Square(F);
	return (1.0/16.0)*S*S - (1.0/2.0)*S + 1;
}

#if PLATFORM_ANDROID
static inline UBOOL AndroidIsCityIntroLevel( ULevel* Level )
{
	return Level && Level->GetOuter()
	&&	appStricmp( Level->GetOuter()->GetName(), TEXT("CityIntro") ) == 0;
}

static inline INT AndroidUnwrapRotComponent( INT Base, INT Value )
{
	while( Value - Base > 32768 )
		Value -= 65536;
	while( Value - Base < -32768 )
		Value += 65536;
	return Value;
}

static inline FRotator AndroidUnwrapRotatorNear( const FRotator& Base, const FRotator& Value )
{
	return FRotator
	(
		AndroidUnwrapRotComponent( Base.Pitch, Value.Pitch ),
		AndroidUnwrapRotComponent( Base.Yaw,   Value.Yaw   ),
		AndroidUnwrapRotComponent( Base.Roll,  Value.Roll  )
	);
}

static FLOAT AndroidDistanceSquared2DToSegment( const FVector& Point, const FVector& A, const FVector& B, FVector& Closest )
{
	const FVector AB = B - A;
	const FLOAT LenSq = AB.SizeSquared2D();
	FLOAT T = 0.0f;
	if( LenSq > 1.0f )
		T = Clamp( ((Point.X-A.X)*AB.X + (Point.Y-A.Y)*AB.Y) / LenSq, 0.0f, 1.0f );
	Closest = A + (B-A) * T;
	return Square(Point.X-Closest.X) + Square(Point.Y-Closest.Y);
}

static inline UBOOL AndroidValidMoverVector( const FVector& V )
{
	return !appIsNan(V.X) && !appIsNan(V.Y) && !appIsNan(V.Z)
	&&	Abs(V.X)<1000000.0f
	&&	Abs(V.Y)<1000000.0f
	&&	Abs(V.Z)<1000000.0f;
}

static INT AndroidCityIntroRepairMoverAttachments( AMover* Mover )
{
	guardSlow(AndroidCityIntroRepairMoverAttachments);
	if( !Mover || !AndroidIsCityIntroLevel( Mover->GetLevel() ) || Mover->Tag==NAME_None )
		return 0;

	ULevel* Level = Mover->GetLevel();
	INT Attached = 0;
	INT Lights = 0;
	static INT AndroidAttachLogs = 0;
	for( INT iActor=0; iActor<Level->Actors.Num(); iActor++ )
	{
		AActor* Other = Level->Actors(iActor);
		if( !Other || Other==Mover || Other->bDeleteMe || Other->AttachTag!=Mover->Tag )
			continue;

		if( Other->Base != Mover )
		{
			Other->SetBase( Mover, 0 );
			Attached++;
		}
		if( Other->LightType!=LT_None || Other->bCorona || Other->bDynamicLight )
		{
			Other->bDynamicLight = 1;
			Other->bLightChanged = 1;
			Lights++;
		}
		if( AndroidAttachLogs < 96 )
		{
			debugf( NAME_Log, TEXT("UT99_ANDROID_V325_CITYINTRO_ATTACH_REPAIR count=%i mover=%s actor=%s class=%s attachTag=%s baseNow=%s light=%i corona=%i mesh=%s"),
				AndroidAttachLogs,
				Mover->GetFullName(),
				Other->GetFullName(),
				Other->GetClass() ? Other->GetClass()->GetName() : TEXT("None"),
				*Other->AttachTag,
				Other->Base ? Other->Base->GetFullName() : TEXT("None"),
				Other->LightType!=LT_None,
				Other->bCorona,
				Other->Mesh ? Other->Mesh->GetFullName() : TEXT("None") );
			AndroidAttachLogs++;
		}
	}
	if( (Attached || Lights) && AndroidAttachLogs < 128 )
	{
		debugf( NAME_Log, TEXT("UT99_ANDROID_V325_CITYINTRO_ATTACH_REPAIR count=%i mover=%s attached=%i lights=%i standing=%i tag=%s"),
			AndroidAttachLogs,
			Mover->GetFullName(),
			Attached,
			Lights,
			Mover->StandingCount,
			*Mover->Tag );
		AndroidAttachLogs++;
	}
	return Attached;
	unguardSlow;
}

static void AndroidCityIntroDispatchTriggerEvent( ATrigger* Trigger, AActor* Other )
{
	guardSlow(AndroidCityIntroDispatchTriggerEvent);
	if( !Trigger || !Other || Trigger->Event==NAME_None || !AndroidIsCityIntroLevel( Trigger->GetLevel() ) )
		return;

	APawn* EventInstigator = Cast<APawn>( Other );
	if( EventInstigator )
		Other->Instigator = EventInstigator;
	else
		EventInstigator = Other->Instigator;

	ULevel* Level = Trigger->GetLevel();
	INT Recipients = 0;
	static INT AndroidDispatchLogs = 0;
	for( INT iActor=0; iActor<Level->Actors.Num(); iActor++ )
	{
		AActor* Target = Level->Actors(iActor);
		if( !Target || Target->bDeleteMe || Target==Trigger || Target->Tag!=Trigger->Event )
			continue;

		if( AndroidDispatchLogs < 128 )
		{
			debugf( NAME_Log, TEXT("UT99_ANDROID_V317_CITYINTRO_TRIGGER_DISPATCH count=%i trigger=%s event=%s target=%s class=%s state=%s physics=%i drawType=%i hidden=%i other=%s instigator=%s"),
				AndroidDispatchLogs,
				Trigger->GetFullName(),
				*Trigger->Event,
				Target->GetFullName(),
				Target->GetClass() ? Target->GetClass()->GetName() : TEXT("None"),
				Target->GetStateFrame() && Target->GetStateFrame()->StateNode ? Target->GetStateFrame()->StateNode->GetName() : TEXT("None"),
				Target->Physics,
				Target->DrawType,
				Target->bHidden,
				Other->GetFullName(),
				EventInstigator ? EventInstigator->GetFullName() : TEXT("None") );
			AndroidDispatchLogs++;
		}
		Target->eventTrigger( Other, EventInstigator );
		if( AMover* Mover = Cast<AMover>( Target ) )
		{
			AndroidCityIntroRepairMoverAttachments( Mover );
			if( !Mover->bInterpolating && !Mover->bDeleteMe && AndroidIsCityIntroLevel( Mover->GetLevel() ) )
			{
				const BYTE OldKeyNum = Mover->KeyNum;
				const BYTE NewKeyNum = Clamp<INT>( 1, 0, ARRAY_COUNT(Mover->KeyPos)-1 );
				const FVector TargetLocation = Mover->BasePos + Mover->KeyPos[NewKeyNum];
				if( !AndroidValidMoverVector(Mover->BasePos) || !AndroidValidMoverVector(Mover->KeyPos[NewKeyNum]) || !AndroidValidMoverVector(TargetLocation) )
				{
					static INT AndroidBadMoverLogs = 0;
					if( AndroidBadMoverLogs < 64 )
					{
							debugf( NAME_Log, TEXT("UT99_ANDROID_V322_CITYINTRO_BAD_MOVER_KEY count=%i mover=%p event=%s key=%i prev=%i numKeys=%i moveTime=%f base=%f,%f,%f key=%f,%f,%f target=%f,%f,%f"),
							AndroidBadMoverLogs,
							Mover,
							*Trigger->Event,
							Mover->KeyNum,
							Mover->PrevKeyNum,
							Mover->NumKeys,
							Mover->MoveTime,
							Mover->BasePos.X,
							Mover->BasePos.Y,
							Mover->BasePos.Z,
							Mover->KeyPos[NewKeyNum].X,
							Mover->KeyPos[NewKeyNum].Y,
							Mover->KeyPos[NewKeyNum].Z,
							TargetLocation.X,
							TargetLocation.Y,
							TargetLocation.Z );
						AndroidBadMoverLogs++;
					}
					continue;
				}
				Mover->OldPos = Mover->Location;
				Mover->OldRot = Mover->Rotation;
				Mover->PhysAlpha = 0.0f;
				Mover->setPhysics( PHYS_MovingBrush );
				Mover->bInterpolating = 1;
				Mover->PrevKeyNum = OldKeyNum;
				Mover->KeyNum = NewKeyNum;
				const FLOAT AndroidMoveTime = Mover->MoveTime > 0.005f ? Mover->MoveTime : 1.0f;
				Mover->PhysRate = 1.0f / AndroidMoveTime;
				Mover->SavedTrigger = Other;
				Mover->Instigator = EventInstigator;
				Mover->bOpening = 1;
				Mover->bDelaying = 0;
				Mover->ClientUpdate++;
				Mover->SimOldPos = Mover->OldPos;
				Mover->SimOldRotYaw = Mover->OldRot.Yaw;
				Mover->SimOldRotPitch = Mover->OldRot.Pitch;
				Mover->SimOldRotRoll = Mover->OldRot.Roll;
				Mover->SimInterpolate.X = 100.0f * Mover->PhysAlpha;
				Mover->SimInterpolate.Y = 100.0f * Max( 0.01f, Mover->PhysRate );
				Mover->SimInterpolate.Z = 256 * Mover->PrevKeyNum + Mover->KeyNum;
				static INT AndroidMoverFallbackLogs = 0;
				if( AndroidMoverFallbackLogs < 64 )
				{
					debugf( NAME_Log, TEXT("UT99_ANDROID_V320_CITYINTRO_MOVER_TRIGGER_FALLBACK count=%i mover=%p event=%s oldKey=%i newKey=%i numKeys=%i moveTime=%f usedTime=%f rate=%f loc=%f,%f,%f target=%f,%f,%f"),
						AndroidMoverFallbackLogs,
						Mover,
						*Trigger->Event,
						OldKeyNum,
						Mover->KeyNum,
						Mover->NumKeys,
						Mover->MoveTime,
						AndroidMoveTime,
						Mover->PhysRate,
						Mover->Location.X,
						Mover->Location.Y,
						Mover->Location.Z,
						TargetLocation.X,
						TargetLocation.Y,
						TargetLocation.Z );
					AndroidMoverFallbackLogs++;
				}
			}
		}
		Recipients++;
	}
	if( AndroidDispatchLogs < 128 )
	{
		debugf( NAME_Log, TEXT("UT99_ANDROID_V317_CITYINTRO_TRIGGER_DISPATCH count=%i trigger=%s event=%s recipients=%i instigator=%s"),
			AndroidDispatchLogs,
			Trigger->GetFullName(),
			*Trigger->Event,
			Recipients,
			EventInstigator ? EventInstigator->GetFullName() : TEXT("None") );
		AndroidDispatchLogs++;
	}
	unguardSlow;
}

static void AndroidCityIntroTouchPathTriggers( AActor* CameraActor, const FVector& OldLocation, const FVector& NewLocation )
{
	guardSlow(AndroidCityIntroTouchPathTriggers);
	if( !CameraActor || !AndroidIsCityIntroLevel( CameraActor->GetLevel() ) || !CameraActor->IsA(APlayerPawn::StaticClass()) )
		return;

	static TArray<AActor*> TouchedTriggers;
	static ULevel* TouchedLevel = NULL;
	ULevel* Level = CameraActor->GetLevel();
	if( TouchedLevel != Level )
	{
		TouchedTriggers.Empty();
		TouchedLevel = Level;
	}

	INT Scanned = 0;
	INT Active = 0;
	INT Proximity = 0;
	INT HitCount = 0;
	FLOAT BestDistSq = 1.0e30f;
	FLOAT BestZ = 1.0e30f;
	ATrigger* BestTrigger = NULL;
	for( INT iActor=0; iActor<Level->Actors.Num(); iActor++ )
	{
		ATrigger* Trigger = Cast<ATrigger>( Level->Actors(iActor) );
		if( !Trigger || Trigger->bDeleteMe )
			continue;
		Scanned++;
		if( !Trigger->bInitiallyActive || Trigger->TriggerType==TT_Shoot )
			continue;
		Active++;

		INT FoundIndex = INDEX_NONE;
		if( TouchedTriggers.FindItem( Trigger, FoundIndex ) )
			continue;

		const FLOAT Radius = Max( Trigger->CollisionRadius + CameraActor->CollisionRadius + 32.0f, 32.0f );
		const FLOAT Height = Max( Trigger->CollisionHeight + CameraActor->CollisionHeight + 32.0f, 32.0f );
		FVector Closest;
		const FLOAT DistSq = AndroidDistanceSquared2DToSegment( Trigger->Location, OldLocation, NewLocation, Closest );
		const FLOAT ZDist = Abs( Trigger->Location.Z - Closest.Z );
		if( DistSq < BestDistSq )
		{
			BestDistSq = DistSq;
			BestZ = ZDist;
			BestTrigger = Trigger;
		}
		if( ZDist <= Height && DistSq <= Square(Radius) )
		{
			Proximity++;
			if( TouchedTriggers.Num() < 128 )
				TouchedTriggers.AddItem( Trigger );
			if( APawn* CameraPawn = Cast<APawn>( CameraActor ) )
				CameraActor->Instigator = CameraPawn;
			static INT AndroidTriggerTouchLogs = 0;
			if( AndroidTriggerTouchLogs < 96 )
			{
				debugf( NAME_Log, TEXT("UT99_ANDROID_V315_CITYINTRO_TRIGGER_TOUCH count=%i trigger=%s event=%s type=%i once=%i active=%i collide=%i loc=%f,%f,%f old=%f,%f,%f new=%f,%f,%f closest=%f,%f,%f dist=%f z=%f radius=%f height=%f instigator=%s"),
					AndroidTriggerTouchLogs,
					Trigger->GetFullName(),
					*Trigger->Event,
					Trigger->TriggerType,
					Trigger->bTriggerOnceOnly,
					Trigger->bInitiallyActive,
					Trigger->bCollideActors,
					Trigger->Location.X,
					Trigger->Location.Y,
					Trigger->Location.Z,
					OldLocation.X,
					OldLocation.Y,
					OldLocation.Z,
					NewLocation.X,
					NewLocation.Y,
					NewLocation.Z,
					Closest.X,
					Closest.Y,
					Closest.Z,
					appSqrt(DistSq),
					ZDist,
					Radius,
					Height,
					CameraActor->Instigator ? CameraActor->Instigator->GetFullName() : TEXT("None") );
				AndroidTriggerTouchLogs++;
			}
			HitCount++;
			Trigger->BeginTouch( CameraActor );
			AndroidCityIntroDispatchTriggerEvent( Trigger, CameraActor );
		}
	}
	static INT AndroidTriggerScanLogs = 0;
	if( AndroidTriggerScanLogs < 64 && (HitCount || (AndroidTriggerScanLogs % 8)==0) )
	{
		debugf( NAME_Log, TEXT("UT99_ANDROID_V316_CITYINTRO_TRIGGER_SCAN count=%i scanned=%i active=%i proximity=%i touched=%i best=%s bestDist=%f bestZ=%f old=%f,%f,%f new=%f,%f,%f"),
			AndroidTriggerScanLogs,
			Scanned,
			Active,
			Proximity,
			HitCount,
			BestTrigger ? BestTrigger->GetFullName() : TEXT("None"),
			BestTrigger ? appSqrt(BestDistSq) : -1.0f,
			BestTrigger ? BestZ : -1.0f,
			OldLocation.X,
			OldLocation.Y,
			OldLocation.Z,
			NewLocation.X,
			NewLocation.Y,
			NewLocation.Z );
		AndroidTriggerScanLogs++;
	}
	unguardSlow;
}
#endif

//
// Interpolating along a path.
//
void AActor::physPathing( FLOAT DeltaTime )
{
	guard(AActor::physPathing);
#if PLATFORM_ANDROID
	const DOUBLE AndroidPathStart = appSeconds();
	DOUBLE AndroidPathLast = AndroidPathStart;
	DOUBLE AndroidPathSetupMs = 0.0;
	DOUBLE AndroidPathInterpMs = 0.0;
	DOUBLE AndroidPathMoveMs = 0.0;
	DOUBLE AndroidPathEventMs = 0.0;
	INT AndroidPathIterations = 0;
	AInterpolationPoint* AndroidPathLastDest = NULL;
#define UT99_ANDROID_PATH_PHASE_MS(Target) do { DOUBLE AndroidNow = appSeconds(); Target += (AndroidNow - AndroidPathLast) * 1000.0; AndroidPathLast = AndroidNow; } while(0)
#endif

	// Linear interpolate from Target to Target.Next.
	while( PhysRate!=0.0 && bInterpolating && DeltaTime>0.0 )
	{
#if PLATFORM_ANDROID
		AndroidPathIterations++;
#endif
		// Find destination interpolation point, if any.
		AInterpolationPoint* Dest = Cast<AInterpolationPoint>( Target );
#if PLATFORM_ANDROID
		AndroidPathLastDest = Dest;
#endif

		// Compute rate modifier.
		FLOAT RateModifier = 1.0;
		if( Dest && Dest->Next )
			RateModifier = Dest->RateModifier * (1.0 - PhysAlpha) + Dest->Next->RateModifier * PhysAlpha;

		// Update level slomo.
		Level->TimeDilation = Dest->GameSpeedModifier * (1.0 - PhysAlpha) + Dest->Next->GameSpeedModifier * PhysAlpha;

		// Update screenflash and FOV.
		if( IsA(APlayerPawn::StaticClass()) )
		{
			((APlayerPawn*)this)->FlashScale = FVector(1,1,1)*(((APlayerPawn*)this)->DesiredFlashScale = (Dest->ScreenFlashScale * (1.0 - PhysAlpha) + Dest->Next->ScreenFlashScale * PhysAlpha));
			((APlayerPawn*)this)->FlashFog   = ((APlayerPawn*)this)->DesiredFlashFog   = (Dest->ScreenFlashFog   * (1.0 - PhysAlpha) + Dest->Next->ScreenFlashFog   * PhysAlpha);
			((APlayerPawn*)this)->FovAngle                                             = (Dest->FovModifier      * (1.0 - PhysAlpha) + Dest->Next->FovModifier      * PhysAlpha) * ((APlayerPawn*)GetClass()->GetDefaultObject())->FovAngle;
		}
#if PLATFORM_ANDROID
		UT99_ANDROID_PATH_PHASE_MS(AndroidPathSetupMs);
#endif

		// Update alpha.
		FLOAT OldAlpha  = PhysAlpha;
		FLOAT DestAlpha = PhysAlpha + PhysRate * RateModifier * DeltaTime;
		PhysAlpha       = Clamp( DestAlpha, 0.f, 1.f );

		// Move and rotate.
		if( Dest && Dest->Next )
		{
			FCheckResult Hit;
			FVector NewLocation;
			FRotator NewRotation;
			if( Dest->Prev && Dest->Next->Next )
			{
				// Cubic spline interpolation.
				FLOAT W0 = Splerp(PhysAlpha+1.0);
				FLOAT W1 = Splerp(PhysAlpha+0.0);
				FLOAT W2 = Splerp(PhysAlpha-1.0);
				FLOAT W3 = Splerp(PhysAlpha-2.0);
				FLOAT RW = 1.0 / (W0 + W1 + W2 + W3);
				NewLocation = (W0*Dest->Prev->Location + W1*Dest->Location + W2*Dest->Next->Location + W3*Dest->Next->Next->Location)*RW;
#if PLATFORM_ANDROID
				FRotator R1 = Dest->Rotation;
				FRotator R0 = AndroidUnwrapRotatorNear( R1, Dest->Prev->Rotation );
				FRotator R2 = AndroidUnwrapRotatorNear( R1, Dest->Next->Rotation );
				FRotator R3 = AndroidUnwrapRotatorNear( R2, Dest->Next->Next->Rotation );
				NewRotation = (W0*R0 + W1*R1 + W2*R2 + W3*R3)*RW;
#else
				NewRotation = (W0*Dest->Prev->Rotation + W1*Dest->Rotation + W2*Dest->Next->Rotation + W3*Dest->Next->Next->Rotation)*RW;
#endif
			}
			else
			{
				// Linear interpolation.
				FLOAT W0 = 1.0 - PhysAlpha;
				FLOAT W1 = PhysAlpha;
				NewLocation = W0*Dest->Location + W1*Dest->Next->Location;
#if PLATFORM_ANDROID
				FRotator R0 = Dest->Rotation;
				FRotator R1 = AndroidUnwrapRotatorNear( R0, Dest->Next->Rotation );
				NewRotation = W0*R0 + W1*R1;
#else
				NewRotation = W0*Dest->Rotation + W1*Dest->Next->Rotation;
#endif
			}
#if PLATFORM_ANDROID
			UT99_ANDROID_PATH_PHASE_MS(AndroidPathInterpMs);
			if
			(	AndroidIsCityIntroLevel( GetLevel() )
			&&	IsA(APlayerPawn::StaticClass())
			&&	Physics == PHYS_Interpolating )
			{
				static INT AndroidFastPathMoveLogs = 0;
				const FVector AndroidOldLocation = Location;
				Location = NewLocation;
				OldLocation = NewLocation;
				Rotation = NewRotation;
				if( GetLevel() && GetLevel()->Model )
					Region = GetLevel()->Model->PointRegion( GetLevel()->GetLevelInfo(), Location );
				AndroidCityIntroTouchPathTriggers( this, AndroidOldLocation, NewLocation );
				if( AndroidFastPathMoveLogs < 8 )
				{
					debugf( NAME_Log, TEXT("UT99_ANDROID_V310_FAST_INTERP_CAMERA_MOVE actor=%s loc=%f,%f,%f rot=%i,%i,%i target=%s alpha=%f"),
						GetFullName(),
						Location.X,
						Location.Y,
						Location.Z,
						Rotation.Pitch,
						Rotation.Yaw,
						Rotation.Roll,
						Dest ? Dest->GetFullName() : TEXT("None"),
						PhysAlpha );
					AndroidFastPathMoveLogs++;
				}
			}
			else
#endif
			{
				GetLevel()->MoveActor( this, NewLocation - Location, NewRotation, Hit );
			}
#if PLATFORM_ANDROID
			UT99_ANDROID_PATH_PHASE_MS(AndroidPathMoveMs);
#endif
#if PLATFORM_ANDROID
			static INT AndroidInterpRotLogs = 0;
			if
			(	AndroidInterpRotLogs < 32
			&&	GetLevel()
			&&	GetLevel()->GetOuter()
			&&	appStricmp( GetLevel()->GetOuter()->GetName(), TEXT("CityIntro") ) == 0 )
			{
				debugf( NAME_Log, TEXT("UT99_ANDROID_V257_INTERP_ROT actor=%s alpha=%f target=%s raw=%i,%i,%i next=%i,%i,%i new=%i,%i,%i physRate=%f"),
					GetFullName(),
					PhysAlpha,
					Dest ? Dest->GetFullName() : TEXT("None"),
					Dest ? Dest->Rotation.Pitch : 0,
					Dest ? Dest->Rotation.Yaw : 0,
					Dest ? Dest->Rotation.Roll : 0,
					(Dest && Dest->Next) ? Dest->Next->Rotation.Pitch : 0,
					(Dest && Dest->Next) ? Dest->Next->Rotation.Yaw : 0,
					(Dest && Dest->Next) ? Dest->Next->Rotation.Roll : 0,
					NewRotation.Pitch,
					NewRotation.Yaw,
					NewRotation.Roll,
					PhysRate );
				AndroidInterpRotLogs++;
			}
#endif
			if( IsA(APawn::StaticClass()) )
				((APawn*)this)->ViewRotation = Rotation;
		}

		// If overflowing, notify and go to next place.
		if( PhysRate>0.0 && DestAlpha>1.0 )
		{
			PhysAlpha = 0.0;
			DeltaTime *= (DestAlpha - 1.0) / (DestAlpha - OldAlpha);
			if( Target )
			{
				Target->eventInterpolateEnd(this);
				eventInterpolateEnd(Target);
#if PLATFORM_ANDROID
				UT99_ANDROID_PATH_PHASE_MS(AndroidPathEventMs);
#endif
				if( Dest )
				{
					do
					{
						Target = Dest->Next;
						Dest = Cast<AInterpolationPoint>( Target );
					} while( Dest && Dest->bSkipNextPath );
				}
			}
		}
		else if( PhysRate<0.0 && DestAlpha<0.0 )
		{
			PhysAlpha = 1.0;
			DeltaTime *= (0.0 - DestAlpha) / (OldAlpha - DestAlpha);
			if( Target )
			{
				Target->eventInterpolateEnd(this);
				eventInterpolateEnd(Target);
#if PLATFORM_ANDROID
				UT99_ANDROID_PATH_PHASE_MS(AndroidPathEventMs);
#endif
				if( Dest )
				{
					do
					{
						Target = Dest->Prev;
						Dest = Cast<AInterpolationPoint>( Target );
					} while( Dest && Dest->bSkipNextPath );
				}
			}
			eventInterpolateEnd(NULL);
#if PLATFORM_ANDROID
			UT99_ANDROID_PATH_PHASE_MS(AndroidPathEventMs);
#endif
		}
		else DeltaTime=0.0;
	};
#if PLATFORM_ANDROID
	const DOUBLE AndroidPathTotalMs = (appSeconds() - AndroidPathStart) * 1000.0;
	if( AndroidPathTotalMs > 10.0 && AndroidIsCityIntroLevel( GetLevel() ) )
	{
		static INT AndroidSlowPathLogs = 0;
		if( AndroidSlowPathLogs < 64 || (AndroidSlowPathLogs % 120) == 0 )
			debugf( NAME_Log, TEXT("UT99_ANDROID_V312_SLOW_PHYSPATHING count=%i ms=%f actor=%s target=%s iter=%i delta=%f alpha=%f rate=%f setup=%f interp=%f move=%f events=%f loc=%f,%f,%f"),
				AndroidSlowPathLogs,
				AndroidPathTotalMs,
				GetFullName(),
				AndroidPathLastDest ? AndroidPathLastDest->GetFullName() : TEXT("None"),
				AndroidPathIterations,
				DeltaTime,
				PhysAlpha,
				PhysRate,
				AndroidPathSetupMs,
				AndroidPathInterpMs,
				AndroidPathMoveMs,
				AndroidPathEventMs,
				Location.X,
				Location.Y,
				Location.Z );
		AndroidSlowPathLogs++;
	}
#undef UT99_ANDROID_PATH_PHASE_MS
#endif
	unguard;
}

//
// Moving brush.
//
void AActor::physMovingBrush( FLOAT DeltaTime )
{
	guard(physMovingBrush);
	if( IsA(AMover::StaticClass()) )
	{
		AMover* Mover  = (AMover*)this;
		INT KeyNum     = Clamp( (INT)Mover->KeyNum, (INT)0, (INT)ARRAY_COUNT(Mover->KeyPos) );
#if PLATFORM_ANDROID
		static INT AndroidMoverPhysLogs = 0;
		if( AndroidMoverPhysLogs < 96 && AndroidIsCityIntroLevel( GetLevel() ) && Mover->bInterpolating )
		{
			debugf( NAME_Log, TEXT("UT99_ANDROID_V319_CITYINTRO_MOVER_PHYS count=%i mover=%s state=%s delta=%f physics=%i key=%i prev=%i alpha=%f rate=%f interp=%i opening=%i delaying=%i loc=%f,%f,%f old=%f,%f,%f keyTarget=%f,%f,%f"),
				AndroidMoverPhysLogs,
				Mover->GetFullName(),
				Mover->GetStateFrame() && Mover->GetStateFrame()->StateNode ? Mover->GetStateFrame()->StateNode->GetName() : TEXT("None"),
				DeltaTime,
				Mover->Physics,
				Mover->KeyNum,
				Mover->PrevKeyNum,
				Mover->PhysAlpha,
				Mover->PhysRate,
				Mover->bInterpolating,
				Mover->bOpening,
				Mover->bDelaying,
				Mover->Location.X,
				Mover->Location.Y,
				Mover->Location.Z,
				Mover->OldPos.X,
				Mover->OldPos.Y,
				Mover->OldPos.Z,
				Mover->BasePos.X + Mover->KeyPos[KeyNum].X,
				Mover->BasePos.Y + Mover->KeyPos[KeyNum].Y,
				Mover->BasePos.Z + Mover->KeyPos[KeyNum].Z );
			AndroidMoverPhysLogs++;
		}
#endif
		while( Mover->bInterpolating && DeltaTime>0.0 )
		{
			// We are moving.
			FLOAT NewAlpha = Mover->PhysAlpha + DeltaTime * Mover->PhysRate;
			if( NewAlpha > 1.0 )
			{
				DeltaTime *= (NewAlpha - 1.0) / (NewAlpha - PhysAlpha);
				NewAlpha   = 1.0;
			}
			else DeltaTime = 0.0;

			// Compute alpha.
			FLOAT RenderAlpha;
			if( Mover->MoverGlideType == MV_GlideByTime )
			{
				// Make alpha time-smooth and time-continuous.
				// f(0)=0, f(1)=1, f'(0)=f'(1)=0.
				RenderAlpha = 3.0*NewAlpha*NewAlpha - 2.0*NewAlpha*NewAlpha*NewAlpha;
			}
			else RenderAlpha = NewAlpha;

			// Move.
			FCheckResult Hit(1.0);
			if( GetLevel()->MoveActor
			(
				Mover,
				Mover->OldPos + ((Mover->BasePos + Mover->KeyPos[KeyNum]) - Mover->OldPos) * RenderAlpha - Mover->Location,
				Mover->OldRot + ((Mover->BaseRot + Mover->KeyRot[KeyNum]) - Mover->OldRot) * RenderAlpha,
				Hit
			) )
			{
				// Successfully moved.
				Mover->PhysAlpha = NewAlpha;
#if PLATFORM_ANDROID
				if( AndroidIsCityIntroLevel( GetLevel() ) && Mover->StandingCount > 0 )
				{
					static INT AndroidMovingLightLogs = 0;
					INT Based = 0;
					INT Lights = 0;
					for( INT iActor=0; iActor<GetLevel()->Actors.Num(); iActor++ )
					{
						AActor* Other = GetLevel()->Actors(iActor);
						if( !Other || Other->Base!=Mover )
							continue;
						Based++;
						if( Other->LightType!=LT_None || Other->bCorona || Other->bDynamicLight )
						{
							Other->bDynamicLight = 1;
							Other->bLightChanged = 1;
							Lights++;
						}
					}
					if( (Based || Lights) && AndroidMovingLightLogs < 96 )
					{
						debugf( NAME_Log, TEXT("UT99_ANDROID_V326_CITYINTRO_MOVER_BASED_UPDATE count=%i mover=%s based=%i lights=%i loc=%f,%f,%f alpha=%f key=%i"),
							AndroidMovingLightLogs,
							Mover->GetFullName(),
							Based,
							Lights,
							Mover->Location.X,
							Mover->Location.Y,
							Mover->Location.Z,
							Mover->PhysAlpha,
							Mover->KeyNum );
						AndroidMovingLightLogs++;
					}
				}
#endif
				if( NewAlpha == 1.0 )
				{
					// Just finished moving.
					Mover->bInterpolating = 0;
					Mover->eventInterpolateEnd(NULL);
				}
			}
		}
	}
	unguard;
}

//
// Initialize execution.
//
void AActor::InitExecution()
{
	guard(AActor::InitExecution);

	UObject::InitExecution();

	check(GetStateFrame());
	check(GetStateFrame()->Object==this);
	check(GetLevel()!=NULL);
	check(GetLevel()->Actors(0)!=NULL);
	check(GetLevel()->Actors(0)==Level);
	check(Level!=NULL);

	unguardobj;
}

/*-----------------------------------------------------------------------------
	Natives.
-----------------------------------------------------------------------------*/

//////////////////////
// Console Commands //
//////////////////////

void AActor::execConsoleCommand( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execConsoleCommand);

	P_GET_STR(Command);
	P_FINISH;

	FStringOutputDevice StrOut;
	GetLevel()->Engine->Exec( *Command, StrOut );
	*(FString*)Result = *StrOut;

	unguard;
}

/////////////////////////////
// Log and error functions //
/////////////////////////////

void AActor::execError( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execError);

	P_GET_STR(S);
	P_FINISH;

	Stack.Log( *S );
	GetLevel()->DestroyActor( this );

	unguardexecSlow;
}

//////////////////////////
// Clientside functions //
//////////////////////////

void APlayerPawn::execClientTravel( FFrame& Stack, RESULT_DECL )
{
	guardSlow(APlayerPawn::execClientTravel);

	P_GET_STR(URL);
	P_GET_BYTE(TravelType);
	P_GET_UBOOL(bItems);
	P_FINISH;

	if( Player )
	{
		// Warn the client.
		eventPreClientTravel();

		// Do the travel.
		GetLevel()->Engine->SetClientTravel( Player, *URL, bItems, (ETravelType)TravelType );
	}

	unguardexecSlow;
}

void APlayerPawn::execGetPlayerNetworkAddress( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerPawn::execGetPlayerNetworkAddress);
	P_FINISH;

	if( Player && Player->IsA(UNetConnection::StaticClass()) )
		*(FString*)Result = Cast<UNetConnection>(Player)->LowLevelGetRemoteAddress();
	else
		*(FString*)Result = TEXT("");
	unguard;
}

void APlayerPawn::execCopyToClipboard( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerPawn::execCopyToClipboard);
	P_GET_STR(Text);
	P_FINISH;
	appClipboardCopy(*Text);
	unguard;
}

void APlayerPawn::execPasteFromClipboard( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerPawn::execCopyToClipboard);
	P_GET_STR(Text);
	P_FINISH;
	*(FString*)Result = appClipboardPaste();
	unguard;
}

void ALevelInfo::execGetLocalURL( FFrame& Stack, RESULT_DECL )
{
	guardSlow(ALevelInfo::execGetLocalURL);

	P_FINISH;

	*(FString*)Result = GetLevel()->URL.String();

	unguardexecSlow;
}

void ALevelInfo::execGetAddressURL( FFrame& Stack, RESULT_DECL )
{
	guardSlow(ALevelInfo::execGetAddressURL);

	P_FINISH;

	*(FString*)Result = FString::Printf( TEXT("%s:%i"), *GetLevel()->URL.Host, GetLevel()->URL.Port );

	unguardexecSlow;
}

///////////////////////////
// Client-side functions //
///////////////////////////

static UBOOL IsKnownClientHearActorPointer( AActor* Actor )
{
	if( !Actor )
		return 0;
	for( TObjectIterator<AActor> It; It; ++It )
		if( *It==Actor )
			return 1;
	return 0;
}

static UBOOL IsKnownClientHearSoundPointer( USound* Sound )
{
	if( !Sound )
		return 0;
	for( TObjectIterator<USound> It; It; ++It )
		if( *It==Sound )
			return 1;
	return 0;
}

void APawn::execClientHearSound( FFrame& Stack, RESULT_DECL )
{
	guard(APawn::execClientHearSound);

	P_GET_OBJECT(AActor,Actor);
	P_GET_INT(Id);
	P_GET_OBJECT(USound,Sound);
	P_GET_VECTOR(SoundLocation);
	P_GET_VECTOR(Parameters);
	P_FINISH;

	FLOAT Volume = 0.01 * Parameters.X;
	FLOAT Radius = Parameters.Y;
	FLOAT Pitch  = 0.01 * Parameters.Z;
	static INT ClientHearTraceCount = 0;
	if( ClientHearTraceCount++ < 16 )
		debugf( NAME_Init, TEXT("UT99_ANDROID_V214_CLIENT_HEAR_SOUND this=0x%08x actor=0x%08x id=%i sound=0x%08x radius=%f pitch=%f"),
			(DWORD)(QWORD)this,
			(DWORD)(QWORD)Actor,
			Id,
			(DWORD)(QWORD)Sound,
			Radius,
			Pitch );
	if
	(	IsA(APlayerPawn::StaticClass()) 
	&&	((APlayerPawn*)this)->Player
	&&	((APlayerPawn*)this)->Player->IsA(UViewport::StaticClass())
	&&	GetLevel()->Engine->Audio )
	{
		if( Actor && !IsKnownClientHearActorPointer(Actor) )
		{
			debugf( NAME_Init, TEXT("UT99_ANDROID_V214_CLIENT_HEAR_BAD_ACTOR actor=0x%08x id=%i sound=0x%08x"), (DWORD)(QWORD)Actor, Id, (DWORD)(QWORD)Sound );
			Actor = NULL;
		}
		if( Sound && !IsKnownClientHearSoundPointer(Sound) )
		{
			debugf( NAME_Init, TEXT("UT99_ANDROID_V214_CLIENT_HEAR_BAD_SOUND actor=0x%08x id=%i sound=0x%08x"), (DWORD)(QWORD)Actor, Id, (DWORD)(QWORD)Sound );
			return;
		}
		if( Actor && Actor->bDeleteMe )
			Actor = NULL;
		GetLevel()->Engine->Audio->PlaySound( Actor, Id, Sound, SoundLocation, Volume, Radius ? Radius : 1600.f, Pitch );
	}
	unguardexec;
}

////////////////////////////////
// Latent function initiators //
////////////////////////////////

void AActor::execSleep( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSleep);

	P_GET_FLOAT(Seconds);
	P_FINISH;

	GetStateFrame()->LatentAction = EPOLL_Sleep;
	LatentFloat  = Seconds;

	unguardexecSlow;
}

void AActor::execFinishAnim( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execFinishAnim);

	P_FINISH;

	// If we are looping, finish at the next sequence end.
	if( bAnimLoop )
	{
		bAnimLoop     = 0;
		bAnimFinished = 0;
	}

	// If animation is playing, wait for it to finish.
	if( IsAnimating() && AnimFrame<AnimLast )
		GetStateFrame()->LatentAction = EPOLL_FinishAnim;

	unguardexecSlow;
}

void AActor::execFinishInterpolation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execFinishInterpolation);

	P_FINISH;

	GetStateFrame()->LatentAction = EPOLL_FinishInterpolation;

	unguardexecSlow;
}

///////////////////////////
// Slow function pollers //
///////////////////////////

void AActor::execPollSleep( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execPollSleep);

#ifdef PLATFORM_DREAMCAST
	// try to avoid potential unaligned accesses
	FLOAT DeltaSeconds = 0.0f;
	__builtin_memcpy( (void*)&DeltaSeconds, (void*)Result, sizeof(FLOAT) );
#else
	FLOAT DeltaSeconds = *(FLOAT*)Result;
#endif
	if( (LatentFloat-=DeltaSeconds) < 0.5 * DeltaSeconds )
	{
		// Awaken.
		GetStateFrame()->LatentAction = 0;
	}
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( AActor, EPOLL_Sleep, execPollSleep );

void AActor::execPollFinishAnim( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execPollFinishAnim);

	if( bAnimFinished )
		GetStateFrame()->LatentAction = 0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( AActor, EPOLL_FinishAnim, execPollFinishAnim );

void AActor::execPollFinishInterpolation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execPollFinishInterpolation);

	if( !bInterpolating )
		GetStateFrame()->LatentAction = 0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( AActor, EPOLL_FinishInterpolation, execPollFinishInterpolation );

/////////////////////////
// Animation functions //
/////////////////////////

static const TCHAR* SafeScriptNameString( FName Name )
{
	return Name.IsValid() ? *Name : TEXT("InvalidName");
}

static UBOOL IsKnownMeshPointer( UMesh* Mesh )
{
	if( !Mesh )
		return 0;
	for( TObjectIterator<UMesh> It; It; ++It )
		if( *It==Mesh )
			return 1;
	return 0;
}

static UBOOL IsKnownActorPointer( AActor* Actor )
{
	if( !Actor )
		return 0;
	for( TObjectIterator<AActor> It; It; ++It )
		if( *It==Actor )
			return 1;
	return 0;
}

void AActor::execPlayAnim( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execPlayAnim);

	P_GET_NAME(SequenceName);
	P_GET_FLOAT_OPTX(PlayAnimRate,1.0);
	P_GET_FLOAT_OPTX(TweenTime,-1.0);
	P_FINISH;

	// Set one-shot animation.
	if( Mesh && IsKnownMeshPointer( Mesh ) )
	{
		const FMeshAnimSeq* Seq = Mesh->GetAnimSeq( SequenceName );
		if( Seq && Seq->NumFrames>0 )
		{
			if( AnimSequence == NAME_None )
				TweenTime = 0.0;
			AnimSequence  = SequenceName;
			AnimRate      = PlayAnimRate * Seq->Rate / Seq->NumFrames;
			AnimLast      = 1.0 - 1.0 / Seq->NumFrames;
			bAnimNotify   = Seq->Notifys.Num()!=0;
			bAnimFinished = 0;
			bAnimLoop     = 0;
			if( AnimLast == 0.0 )
			{
				AnimMinRate   = 0.0;
				bAnimNotify   = 0;
				OldAnimRate   = 0;
				if( TweenTime > 0.0 )
					TweenRate = 1.0 / TweenTime;
				else
					TweenRate = 10.0; //tween in 0.1 sec
				AnimFrame = -1.0/Seq->NumFrames;
				AnimRate = 0;
			}
			else if( TweenTime>0.0 )
			{
				TweenRate = 1.0 / (TweenTime * Seq->NumFrames);
				AnimFrame = -1.0/Seq->NumFrames;
			}
			else if ( TweenTime == -1.0 )
			{
				AnimFrame = -1.0/Seq->NumFrames;
				if ( OldAnimRate > 0 )
					TweenRate = OldAnimRate;
				else if ( OldAnimRate < 0 ) //was velocity based looping
					TweenRate = ::Max(0.5f * AnimRate, -1 * Velocity.Size() * OldAnimRate );
				else
					TweenRate =  1.0/(0.025 * Seq->NumFrames);
			}
			else
			{
				TweenRate = 0.0;
				AnimFrame = 0.001;
			}
			FPlane OldSimAnim = SimAnim;
			SimAnim.X = 10000 * AnimFrame;
			SimAnim.Y = 5000 * AnimRate;
			if ( SimAnim.Y > 32767 )
				SimAnim.Y = 32767;
			SimAnim.Z = 1000 * TweenRate;
			SimAnim.W = 10000 * AnimLast;
			/*
			if ( IsA(AWeapon::StaticClass())
				&& (PlayAnimRate * Seq->Rate < 0.21) )
			{
				SimAnim.X = 0;
				SimAnim.Z = 0;
			} */
				
			if ( OldSimAnim == SimAnim )
				SimAnim.W = SimAnim.W + 1;
			OldAnimRate = AnimRate;
			//debugf("%s PlayAnim %f %f %f %f", GetName(), SimAnim.X, SimAnim.Y, SimAnim.Z, SimAnim.W);
		}
		else Stack.Logf( TEXT("PlayAnim: Sequence '%s' not found in Mesh '%s'"), SafeScriptNameString( SequenceName ), Mesh->GetName() );
	} else Stack.Logf( TEXT("PlayAnim: No mesh") );
	unguardexecSlow;
}

void AActor::execLoopAnim( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execLoopAnim);

	P_GET_NAME(SequenceName);
	P_GET_FLOAT_OPTX(PlayAnimRate,1.0);
	P_GET_FLOAT_OPTX(TweenTime,-1.0);
	P_GET_FLOAT_OPTX(MinRate,0.0);
	P_FINISH;

	// Set looping animation.
	if( Mesh && IsKnownMeshPointer( Mesh ) )
	{
		const FMeshAnimSeq* Seq = Mesh->GetAnimSeq( SequenceName );
		if( Seq && Seq->NumFrames>0 )
		{
			if ( (AnimSequence == SequenceName) && bAnimLoop && IsAnimating() )
			{
				AnimRate      = PlayAnimRate * Seq->Rate / Seq->NumFrames;
				bAnimFinished = 0;
				AnimMinRate   = MinRate!=0.0 ? MinRate * (Seq->Rate / Seq->NumFrames) : 0.0;
				FPlane OldSimAnim = SimAnim;
				OldAnimRate   = AnimRate;		
				SimAnim.Y = 5000 * AnimRate;
				SimAnim.W = -10000 * (1.0 - 1.0 / Seq->NumFrames);
				if ( OldSimAnim == SimAnim )
					SimAnim.W = SimAnim.W + 1;
				return;
			}
			if( AnimSequence == NAME_None )
				TweenTime = 0.0;
			AnimSequence  = SequenceName;
			AnimRate      = PlayAnimRate * Seq->Rate / Seq->NumFrames;
			AnimLast      = 1.0 - 1.0 / Seq->NumFrames;
			AnimMinRate   = MinRate!=0.0 ? MinRate * (Seq->Rate / Seq->NumFrames) : 0.0;
			bAnimNotify   = Seq->Notifys.Num()!=0;
			bAnimFinished = 0;
			bAnimLoop     = 1;
			if ( AnimLast == 0.0 )
			{
				AnimMinRate   = 0.0;
				bAnimNotify   = 0;
				OldAnimRate   = 0;
				if ( TweenTime > 0.0 )
					TweenRate = 1.0 / TweenTime;
				else
					TweenRate = 10.0; //tween in 0.1 sec
				AnimFrame = -1.0/Seq->NumFrames;
				AnimRate = 0;
			}
			else if( TweenTime>0.0 )
			{
				TweenRate = 1.0 / (TweenTime * Seq->NumFrames);
				AnimFrame = -1.0/Seq->NumFrames;
			}
			else if ( TweenTime == -1.0 )
			{
				AnimFrame = -1.0/Seq->NumFrames;
				if ( OldAnimRate > 0 )
					TweenRate = OldAnimRate;
				else if ( OldAnimRate < 0 ) //was velocity based looping
					TweenRate = ::Max(0.5f * AnimRate, -1 * Velocity.Size() * OldAnimRate );
				else
					TweenRate =  1.0/(0.025 * Seq->NumFrames);
			}
			else
			{
				TweenRate = 0.0;
				AnimFrame = 0.0001;
			}
			OldAnimRate = AnimRate;
			SimAnim.X = 10000 * AnimFrame;
			SimAnim.Y = 5000 * AnimRate;
			if ( SimAnim.Y > 32767 )
				SimAnim.Y = 32767;
			SimAnim.Z = 1000 * TweenRate;
			SimAnim.W = -10000 * AnimLast;
			//debugf("%s LoopAnim %f %f %f %f", GetName(), SimAnim.X, SimAnim.Y, SimAnim.Z, SimAnim.W);
		}
		else Stack.Logf( TEXT("LoopAnim: Sequence '%s' not found in Mesh '%s'"), SafeScriptNameString( SequenceName ), Mesh->GetName() );
	} else Stack.Logf( TEXT("LoopAnim: No mesh") );
	unguardexecSlow;
}

void AActor::execTweenAnim( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execTweenAnim);

	P_GET_NAME(SequenceName);
	P_GET_FLOAT(TweenTime);
	P_FINISH;

	// Tweening an animation from wherever it is, to the start of a specified sequence.
	if( Mesh && IsKnownMeshPointer( Mesh ) )
	{
		const FMeshAnimSeq* Seq = Mesh->GetAnimSeq( SequenceName );
		if( Seq && Seq->NumFrames>0 )
		{
			AnimSequence  = SequenceName;
			AnimLast      = 0.0;
			AnimMinRate   = 0.0;
			bAnimNotify   = 0;
			bAnimFinished = 0;
			bAnimLoop     = 0;
			AnimRate      = 0;
			OldAnimRate   = 0;
			if( TweenTime>0.0 )
			{
				TweenRate =  1.0/(TweenTime * Seq->NumFrames);
				AnimFrame = -1.0/Seq->NumFrames;
			}
			else
			{
				TweenRate = 0.0;
				AnimFrame = 0.0;
			}
			SimAnim.X = 10000 * AnimFrame;
			SimAnim.Y = 5000 * AnimRate;
			if ( SimAnim.Y > 32767 )
				SimAnim.Y = 32767;
			SimAnim.Z = 1000 * TweenRate;
			SimAnim.W = 10000 * AnimLast;
			//debugf("%s TweenAnim %f %f %f %f", GetName(), SimAnim.X, SimAnim.Y, SimAnim.Z, SimAnim.W);
		}
		else Stack.Logf( TEXT("TweenAnim: Sequence '%s' not found in Mesh '%s'"), SafeScriptNameString( SequenceName ), Mesh->GetName() );
	} else Stack.Logf( TEXT("TweenAnim: No mesh") );
	unguardexecSlow;
}

void AActor::execIsAnimating( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execIsAnimating);

	P_FINISH;

	*(DWORD*)Result = IsAnimating();

	unguardexecSlow;
}

void AActor::execGetAnimGroup( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execGetAnimGroup);

	P_GET_NAME(SequenceName);
	P_FINISH;

	// Return the animation group.
	*(FName*)Result = NAME_None;
	if( Mesh && IsKnownMeshPointer( Mesh ) )
	{
		const FMeshAnimSeq* Seq = Mesh->GetAnimSeq( SequenceName );
		if( Seq )
		{
			*(FName*)Result = Seq->Group;
		}
		else Stack.Logf( TEXT("GetAnimGroup: Sequence '%s' not found in Mesh '%s'"), SafeScriptNameString( SequenceName ), Mesh->GetName() );
	} else Stack.Logf( TEXT("GetAnimGroup: No mesh") );

	unguardexecSlow;
}

void AActor::execHasAnim( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execHasAnim);

	P_GET_NAME(SequenceName);
	P_FINISH;

	// Check for a certain anim sequence.
	if( Mesh && IsKnownMeshPointer( Mesh ) )
	{
		const FMeshAnimSeq* Seq = Mesh->GetAnimSeq( SequenceName );
		if( Seq )
		{
			*(DWORD*)Result = 1;
		} else
			*(DWORD*)Result = 0;
	} else Stack.Logf( TEXT("HasAnim: No mesh") );
	unguardexecSlow;
}

///////////////
// Collision //
///////////////

void AActor::execSetCollision( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetCollision);

	P_GET_UBOOL_OPTX(NewCollideActors,bCollideActors);
	P_GET_UBOOL_OPTX(NewBlockActors,  bBlockActors  );
	P_GET_UBOOL_OPTX(NewBlockPlayers, bBlockPlayers );
	P_FINISH;

	SetCollision( NewCollideActors, NewBlockActors, NewBlockPlayers );

	unguardexecSlow;
}

void AActor::execSetCollisionSize( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetCollisionSize);

	P_GET_FLOAT(NewRadius);
	P_GET_FLOAT(NewHeight);
	P_FINISH;

	SetCollisionSize( NewRadius, NewHeight );

	// Return boolean success or failure.
	*(DWORD*)Result = 1;

	unguardexecSlow;
}

void AActor::execSetBase( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetFloor);

	P_GET_OBJECT(AActor,NewBase);
	P_FINISH;

	SetBase( NewBase );

	unguardSlow;
}

///////////
// Audio //
///////////
void AActor::CheckHearSound(APawn* Hearer, INT Id, USound* Sound, FVector Parameters, FLOAT RadiusSquared)
{
	guardSlow(AActor::CheckHearSound);

	ULevel* SoundLevel = IsKnownActorPointer(this) ? GetLevel() : NULL;
	if( !Sound || !Hearer || !IsKnownActorPointer(Hearer) || !IsKnownActorPointer(this) || !SoundLevel || !SoundLevel->Model )
	{
		debugf( NAME_Warning, TEXT("UT99_ANDROID_V205_BAD_HEAR_SOUND actor=%s hearer=%p sound=%p level=%p model=%p"),
			IsKnownActorPointer(this) ? GetFullName() : TEXT("BadActor"),
			Hearer,
			Sound,
			SoundLevel,
			SoundLevel ? SoundLevel->Model : NULL );
		return;
	}

	FVector HearSource;
	if ( Hearer->IsA(APlayerPawn::StaticClass()) && ((APlayerPawn *)Hearer)->ViewTarget && IsKnownActorPointer(((APlayerPawn *)Hearer)->ViewTarget) )
		HearSource = ((APlayerPawn *)Hearer)->ViewTarget->Location;
	else
		HearSource = Hearer->Location;

	FLOAT NewRadiusSquared = RadiusSquared/1.3f;
	FLOAT DistSq = (HearSource-Location).SizeSquared();
	if( DistSq < NewRadiusSquared )
	{
		if ( !GetLevel()->Model->FastLineCheck(HearSource,Location) )
		{
			// if no line of sight, reduce radius and volume
			if ( Instigator != Hearer )
				NewRadiusSquared *= 0.6f;

			Parameters.X *= 0.35f;
			if ( DistSq > NewRadiusSquared )
				return;
		}
		Hearer->eventClientHearSound( this, Id, Sound, Location, Parameters );
	}

	unguardexecSlow;
}

void AActor::execDemoPlaySound( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execDemoPlaySound);

	// Get parameters.
	P_GET_OBJECT(USound,Sound);
	P_GET_BYTE_OPTX(Slot,SLOT_Misc);
	P_GET_FLOAT_OPTX(Volume,TransientSoundVolume);
	P_GET_UBOOL_OPTX(bNoOverride, 0);
	P_GET_FLOAT_OPTX(Radius,TransientSoundRadius);
	P_GET_FLOAT_OPTX(Pitch,1.0);
	P_FINISH;

	if( !Sound )
		return;

	// Play the sound locally
	INT Id = GetIndex()*16 + Slot*2 + bNoOverride;
	FLOAT RadiusSquared = Square( Radius ? Radius : 1600.f );
	FVector Parameters = FVector(100 * Volume, Radius, 100 * Pitch);

	UClient* Client = GetLevel()->Engine->Client;
	if( Client )
	{
		for( INT i=0; i<Client->Viewports.Num(); i++ )
		{
			APlayerPawn* Hearer = Client->Viewports(i)->Actor;
			if( Hearer && IsKnownActorPointer(Hearer) && Hearer->GetLevel()==GetLevel() )
				CheckHearSound(Hearer, Id, Sound, Parameters,RadiusSquared);
		}
	}
	unguardexecSlow;
}

#pragma DISABLE_OPTIMIZATION
void AActor::execPlaySound( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execPlaySound);

	// Get parameters.
	P_GET_OBJECT(USound,Sound);
	P_GET_BYTE_OPTX(Slot,SLOT_Misc);
	P_GET_FLOAT_OPTX(Volume,TransientSoundVolume);
	P_GET_UBOOL_OPTX(bNoOverride, 0);
	P_GET_FLOAT_OPTX(Radius,TransientSoundRadius);
	P_GET_FLOAT_OPTX(Pitch,1.0);
	P_FINISH;

	if( !Sound )
		return;

	// Server-side demo needs a call to execDemoPlaySound for the DemoRecSpectator
	if(		GetLevel() && GetLevel()->DemoRecDriver
		&&	!GetLevel()->DemoRecDriver->ServerConnection
		&&	GetLevel()->GetLevelInfo()->NetMode != NM_Client )
		eventDemoPlaySound(Sound, Slot, Volume, bNoOverride, Radius, Pitch);

	INT Id = GetIndex()*16 + Slot*2 + bNoOverride;
	FLOAT RadiusSquared = Square( Radius ? Radius : 1600.f );
	FVector Parameters = FVector(100 * Volume, Radius, 100 * Pitch);

	// See if the function is simulated.
	UFunction* Caller = Cast<UFunction>( Stack.Node );
	if( (GetLevel()->GetLevelInfo()->NetMode == NM_Client) || (Caller && (Caller->FunctionFlags & FUNC_Simulated)) )
	{
		// Called from a simulated function, so propagate locally only.
		UClient* Client = GetLevel()->Engine->Client;
		if( Client )
		{
			for( INT i=0; i<Client->Viewports.Num(); i++ )
			{
				APlayerPawn* Hearer = Client->Viewports(i)->Actor;
				if( Hearer && IsKnownActorPointer(Hearer) && Hearer->GetLevel()==GetLevel() )
					CheckHearSound(Hearer, Id, Sound, Parameters,RadiusSquared);
			}
		}
	}
	else
	{
		// Propagate to all player actors.
		for( APawn* Hearer=Level->PawnList; Hearer; )
		{
			if( !IsKnownActorPointer(Hearer) )
			{
				debugf( NAME_Warning, TEXT("UT99_ANDROID_V205_BAD_PAWNLIST context=PlaySound actor=%s hearer=%p"), GetFullName(), Hearer );
				break;
			}
			APawn* NextHearer = Hearer->nextPawn;
			if( Hearer->bIsPlayer )
				CheckHearSound(Hearer, Id, Sound, Parameters,RadiusSquared);
			Hearer = NextHearer;
		}
	}
	unguardexecSlow;
}
#pragma ENABLE_OPTIMIZATION

void AActor::execPlayOwnedSound( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execPlayOwnedSound);

	// Get parameters.
	P_GET_OBJECT(USound,Sound);
	P_GET_BYTE_OPTX(Slot,SLOT_Misc);
	P_GET_FLOAT_OPTX(Volume,TransientSoundVolume);
	P_GET_UBOOL_OPTX(bNoOverride, 0);
	P_GET_FLOAT_OPTX(Radius,TransientSoundRadius);
	P_GET_FLOAT_OPTX(Pitch,1.0);
	P_FINISH;

	if( !Sound )
		return;
	// if we're recording a demo, make a call to execDemoPlaySound()
	if( (GetLevel() && GetLevel()->DemoRecDriver && !GetLevel()->DemoRecDriver->ServerConnection) )
		eventDemoPlaySound(Sound, Slot, Volume, bNoOverride, Radius, Pitch);

	INT Id = GetIndex()*16 + Slot*2 + bNoOverride;
	FLOAT RadiusSquared = Square( Radius ? Radius : 1600.f );
	FVector Parameters = FVector(100 * Volume, Radius, 100 * Pitch);

	if( GetLevel()->GetLevelInfo()->NetMode == NM_Client )
	{
		UClient* Client = GetLevel()->Engine->Client;
		if( Client )
		{
			for( INT i=0; i<Client->Viewports.Num(); i++ )
			{
				APlayerPawn* Hearer = Client->Viewports(i)->Actor;
				if( Hearer && IsKnownActorPointer(Hearer) && Hearer->GetLevel()==GetLevel() )
					CheckHearSound(Hearer, Id, Sound, Parameters,RadiusSquared);
			}
		}
	}
	else
	{
		AActor *RemoteOwner = NULL;
		if( GetLevel()->GetLevelInfo()->NetMode != NM_Standalone )
		{
			if ( IsA(APlayerPawn::StaticClass()) )
			{
				if ( ((APlayerPawn *)this)->Player
					&& !((APlayerPawn*)this)->Player->IsA(UViewport::StaticClass()) )
					RemoteOwner = this;
			}
			else if ( Owner && Owner->IsA(APlayerPawn::StaticClass()) && ((APlayerPawn *)Owner)->Player
					&& !((APlayerPawn*)Owner)->Player->IsA(UViewport::StaticClass()) )
				RemoteOwner = Owner;
		}

		for( APawn* Hearer=Level->PawnList; Hearer; )
		{
			if( !IsKnownActorPointer(Hearer) )
			{
				debugf( NAME_Warning, TEXT("UT99_ANDROID_V205_BAD_PAWNLIST context=PlayOwnedSound actor=%s hearer=%p"), GetFullName(), Hearer );
				break;
			}
			APawn* NextHearer = Hearer->nextPawn;
			if( Hearer->bIsPlayer && (Hearer != RemoteOwner) )
				CheckHearSound(Hearer, Id, Sound, Parameters,RadiusSquared);
			Hearer = NextHearer;
		}
	}
	unguardexecSlow;
}

void AActor::execGetSoundDuration( FFrame& Stack, RESULT_DECL )
{
	guard(AActor::execGetSoundDuration);

	// Get parameters.
	P_GET_OBJECT(USound,Sound);
	P_FINISH;

	*(FLOAT*)Result = Sound->GetDuration();

	unguardexec;
}

//////////////
// Movement //
//////////////

void AActor::execMove( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execMove);

	P_GET_VECTOR(Delta);
	P_FINISH;

	FCheckResult Hit(1.0);
	*(DWORD*)Result = GetLevel()->MoveActor( this, Delta, Rotation, Hit );

	unguardexecSlow;
}

void AActor::execSetLocation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetLocation);

	P_GET_VECTOR(NewLocation);
	P_FINISH;

	*(DWORD*)Result = GetLevel()->FarMoveActor( this, NewLocation );

	unguardexecSlow;
}

void AActor::execSetRotation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetRotation);

	P_GET_ROTATOR(NewRotation);
	P_FINISH;

	FCheckResult Hit(1.0);
	*(DWORD*)Result = GetLevel()->MoveActor( this, FVector(0,0,0), NewRotation, Hit );

	unguardexecSlow;
}

///////////////
// Relations //
///////////////

void AActor::execSetOwner( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetOwner);

	P_GET_ACTOR(NewOwner);
	P_FINISH;

	SetOwner( NewOwner );

	unguardexecSlow;
}

//////////////////
// Line tracing //
//////////////////

void AActor::execTrace( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execTrace);

	P_GET_VECTOR_REF(HitLocation);
	P_GET_VECTOR_REF(HitNormal);
	P_GET_VECTOR(TraceEnd);
	P_GET_VECTOR_OPTX(TraceStart,Location);
	P_GET_UBOOL_OPTX(bTraceActors,bCollideActors);
	P_GET_VECTOR_OPTX(TraceExtent,FVector(0,0,0));
	P_FINISH;

	// Trace the line.
	FCheckResult Hit(1.0);
	DWORD TraceFlags;
	if( bTraceActors )
		TraceFlags = TRACE_AllColliding | TRACE_ProjTargets;
	else
		TraceFlags = TRACE_VisBlocking;

	GetLevel()->SingleLineCheck( Hit, this, TraceEnd, TraceStart, TraceFlags, TraceExtent );
	/*if( Hit.Actor && Hit.Item!=INDEX_NONE )
	{
		UModel*  Model = Hit.Actor->IsA(ULevelInfo::StaticClass) ? XLevel->Model : Actor->Model;
		FBspNode& Node = Model->Nodes( Hit.Item );
		FBspSurf& Surf = Model->Surfs( Node.iSurf );
		UTexture* HitTexture = Surf->Texture;
		//do something with HitTexture
	}*/
	*(AActor**)Result = Hit.Actor;
	*HitLocation      = Hit.Location;
	*HitNormal        = Hit.Normal;

	unguardexecSlow;
}

void AActor::execFastTrace( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execTrace);

	P_GET_VECTOR(TraceEnd);
	P_GET_VECTOR_OPTX(TraceStart,Location);
	P_FINISH;

	// Trace the line.
	*(DWORD*)Result = GetLevel()->Model->FastLineCheck(TraceEnd, TraceStart);

	unguardexecSlow;
}

///////////////////////
// Spawn and Destroy //
///////////////////////

void AActor::execSpawn( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSpawn);

	P_GET_OBJECT(UClass,SpawnClass);
	P_GET_OBJECT_OPTX(AActor,SpawnOwner,NULL); 
	P_GET_NAME_OPTX(SpawnName,NAME_None);
	P_GET_VECTOR_OPTX(SpawnLocation,Location);
	P_GET_ROTATOR_OPTX(SpawnRotation,Rotation);
	P_FINISH;

	// Spawn and return actor.
	AActor* Spawned = SpawnClass ? GetLevel()->SpawnActor
	(
		SpawnClass,
		NAME_None,
		SpawnOwner,
		Instigator,
		SpawnLocation,
		SpawnRotation
	) : NULL;
	if( Spawned )
		Spawned->Tag = SpawnName;
	*(AActor**)Result = Spawned;

	unguardexecSlow;
}

void AActor::execDestroy( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execDestroy);

	P_FINISH;
	
	*(DWORD*)Result = GetLevel()->DestroyActor( this );

	unguardexecSlow;
}

////////////
// Timing //
////////////

void AActor::execSetTimer( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetTimer);

	P_GET_FLOAT(NewTimerRate);
	P_GET_UBOOL(bLoop);
	P_FINISH;

	TimerCounter = 0.0;
	TimerRate    = NewTimerRate;
	bTimerLoop   = bLoop;

	unguardexecSlow;
}

////////////////
// Warp zones //
////////////////

void AWarpZoneInfo::execWarp( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AWarpZoneInfo::execWarp);

	P_GET_VECTOR_REF(WarpLocation);
	P_GET_VECTOR_REF(WarpVelocity);
	P_GET_ROTATOR_REF(WarpRotation);
	P_FINISH;

	// Perform warping.
	*WarpLocation = (*WarpLocation).TransformPointBy ( WarpCoords.Transpose() );
	*WarpVelocity = (*WarpVelocity).TransformVectorBy( WarpCoords.Transpose() );
	*WarpRotation = (GMath.UnitCoords / *WarpRotation * WarpCoords.Transpose()).OrthoRotation();

	unguardexecSlow;
}

void AWarpZoneInfo::execUnWarp( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AWarpZoneInfo::execUnWarp);

	P_GET_VECTOR_REF(WarpLocation);
	P_GET_VECTOR_REF(WarpVelocity);
	P_GET_ROTATOR_REF(WarpRotation);
	P_FINISH;

	// Perform unwarping.
	*WarpLocation = (*WarpLocation).TransformPointBy ( WarpCoords );
	*WarpVelocity = (*WarpVelocity).TransformVectorBy( WarpCoords );
	*WarpRotation = (GMath.UnitCoords / *WarpRotation * WarpCoords).OrthoRotation();

	unguardexecSlow;
}

/*-----------------------------------------------------------------------------
	Native iterator functions.
-----------------------------------------------------------------------------*/

void AActor::execAllActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execAllActors);

	// Get the parms.
	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_GET_NAME_OPTX(TagName,NAME_None);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iActor=0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		while( iActor<GetLevel()->Actors.Num() && *OutActor==NULL )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if(	TestActor && TestActor->IsA(BaseClass) && (TagName==NAME_None || TestActor->Tag==TagName) )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AActor::execChildActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execChildActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iActor=0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		while( iActor<GetLevel()->Actors.Num() && *OutActor==NULL )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if(	TestActor && TestActor->IsA(BaseClass) && TestActor->IsOwnedBy( this ) )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AActor::execBasedActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execBasedActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iActor=0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		while( iActor<GetLevel()->Actors.Num() && *OutActor==NULL )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if(	TestActor && TestActor->IsA(BaseClass) && TestActor->Base==this )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AActor::execTouchingActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execTouchingActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iTouching=0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		for( iTouching; iTouching<ARRAY_COUNT(Touching) && *OutActor==NULL; iTouching++ )
		{
			AActor* TestActor = Touching[iTouching];
			if(	TestActor && TestActor->IsA(BaseClass) )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AActor::execTraceActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execTraceActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_GET_VECTOR_REF(HitLocation);
	P_GET_VECTOR_REF(HitNormal);
	P_GET_VECTOR(End);
	P_GET_VECTOR_OPTX(Start,Location);
	P_GET_VECTOR_OPTX(TraceExtent,FVector(0,0,0));
	P_FINISH;

	FMemMark Mark(GMem);
	BaseClass         = BaseClass ? BaseClass : AActor::StaticClass();
	FCheckResult* Hit = GetLevel()->MultiLineCheck( GMem, End, Start, TraceExtent, 1, Level, 0 );

	PRE_ITERATOR;
		if( Hit )
		{
			*OutActor    = Hit->Actor;
			*HitLocation = Hit->Location;
			*HitNormal   = Hit->Normal;
			Hit          = Hit->GetNext();
		}
		else
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			*OutActor = NULL;
			break;
		}
	POST_ITERATOR;
	Mark.Pop();

	unguardexecSlow;
}

void AActor::execRadiusActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execRadiusActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_GET_FLOAT(Radius);
	P_GET_VECTOR_OPTX(TraceLocation,Location);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iActor=0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		while( iActor<GetLevel()->Actors.Num() && *OutActor==NULL )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if
			(	TestActor
			&&	TestActor->IsA(BaseClass) 
			&&	(TestActor->Location - TraceLocation).SizeSquared() < Square(Radius + TestActor->CollisionRadius) )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AActor::execVisibleActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execVisibleActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_GET_FLOAT_OPTX(Radius,0.0);
	P_GET_VECTOR_OPTX(TraceLocation,Location);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iActor=0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		while( iActor<GetLevel()->Actors.Num() && *OutActor==NULL )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if
			(	TestActor
			&& !TestActor->bHidden
			&&	TestActor->IsA(BaseClass)
			&&	(Radius==0.0 || (TestActor->Location-TraceLocation).SizeSquared() < Square(Radius))
			&&	GetLevel()->Model->FastLineCheck(TestActor->Location, TraceLocation) )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AActor::execVisibleCollidingActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execVisibleCollidingActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_GET_FLOAT_OPTX(Radius,0.0);
	P_GET_VECTOR_OPTX(TraceLocation,Location);
	P_GET_UBOOL_OPTX(bIgnoreHidden, 0); 
	P_FINISH;

	Radius = Radius ? Radius : 1000;
	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	FMemMark Mark(GMem);
	FCheckResult* Link=GetLevel()->Hash->ActorRadiusCheck( GMem, TraceLocation, Radius, 0 );
	
	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		if ( Link )
		{
			while
			(	Link
			&&	(!Link->Actor
			||	!Link->Actor->IsA(BaseClass) 
			||  (bIgnoreHidden && Link->Actor->bHidden)
			||	!GetLevel()->Model->FastLineCheck(Link->Actor->Location, TraceLocation)) )
				Link=Link->GetNext();

			if ( Link )
			{
				*OutActor = Link->Actor;
				Link=Link->GetNext();
			}
		}
		if ( *OutActor == NULL ) 
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	Mark.Pop();
	unguardexecSlow;
}

void AZoneInfo::execZoneActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AZoneInfo::execZoneActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iActor=0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		while( iActor<GetLevel()->Actors.Num() && *OutActor==NULL )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if
			(	TestActor
			&&	TestActor->IsA(BaseClass)
			&&	TestActor->IsInZone(this) )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

/*-----------------------------------------------------------------------------
	Script processing function.
-----------------------------------------------------------------------------*/

//
// Execute the state code of the actor.
//
void AActor::ProcessState( FLOAT DeltaSeconds )
{
	if
	(	GetStateFrame()
	&&	GetStateFrame()->Code
	&&	(Role>=ROLE_Authority || (GetStateFrame()->StateNode->StateFlags & STATE_Simulated))
	&&	!IsPendingKill() )
	{
		UState* OldStateNode = GetStateFrame()->StateNode;
		guard(AActor::ProcessState);
		if( ++GScriptEntryTag==1 )
			clock(GScriptCycles);

		// If a latent action is in progress, update it.
		if( GetStateFrame()->LatentAction )
			(this->*GNatives[GetStateFrame()->LatentAction])( *GetStateFrame(), (BYTE*)&DeltaSeconds );

		// Execute code.
		INT NumStates=0;
		while( !bDeleteMe && GetStateFrame()->Code && !GetStateFrame()->LatentAction )
		{
			BYTE Buffer[MAX_CONST_SIZE];
			GetStateFrame()->Step( this, Buffer );
			if( GetStateFrame()->StateNode!=OldStateNode )
			{
				OldStateNode = GetStateFrame()->StateNode;
				if( ++NumStates > 4 )
				{
					//GetStateFrame().Logf( "Pause going from %s to %s", xx, yy );
					break;
				}
			}
		}
		if( --GScriptEntryTag==0 )
			unclock(GScriptCycles);
		unguardf(( TEXT("Object %s, Old State %s, New State %s"), GetFullName(), OldStateNode->GetFullName(), GetStateFrame()->StateNode->GetFullName() ));
	}
}

//
// Internal RPC calling.
//
static inline void InternalProcessRemoteFunction
(
	AActor*			Actor,
	UNetConnection*	Connection,
	UFunction*		Function,
	void*			Parms,
	FFrame*			Stack,
	UBOOL			IsServer
)
{
	guardSlow(InternalProcessRemoteFunction);
	Actor->GetLevel()->NumRPC++;

	// Make sure this function exists for both parties.
	FClassNetCache* ClassCache = Connection->PackageMap->GetClassNetCache( Actor->GetClass() );
	if( !ClassCache )
		return;
	FFieldNetCache* FieldCache = ClassCache->GetFromField( Function );
	if( !FieldCache )
		return;

	// Get the actor channel.
	UActorChannel* Ch = Connection->ActorChannels.FindRef(Actor);
	if( !Ch )
	{
		if( IsServer )
			Ch = (UActorChannel *)Connection->CreateChannel( CHTYPE_Actor, 1 );
		if( !Ch )
			return;
		if( IsServer )
			Ch->SetChannelActor( Actor );
	}

	// Make sure initial channel-opening replication has taken place.
	if( Ch->OpenPacketId==INDEX_NONE )
	{
		if( !IsServer )
			return;
		Ch->ReplicateActor();
	}

	// Form the RPC preamble.
	FOutBunch Bunch( Ch, 0 );
	//debugf(TEXT("   Call %s"),Function->GetFullName());
	Bunch.WriteInt( FieldCache->FieldNetIndex, ClassCache->GetMaxIndex() );

	// Form the RPC parameters.
	if( Stack )
	{
		appMemzero( Parms, Function->ParmsSize );
		for( TFieldIterator<UProperty> It(Function); It && (It->PropertyFlags & (CPF_Parm|CPF_ReturnParm))==CPF_Parm; ++It )
			Stack->Step( Stack->Object, (BYTE*)Parms + It->Offset );
		checkSlow(*Stack->Code==EX_EndFunctionParms);
	}
	for( TFieldIterator<UProperty> It(Function); It && (It->PropertyFlags & (CPF_Parm|CPF_ReturnParm))==CPF_Parm; ++It )
	{
		if( Connection->PackageMap->ObjectToIndex(*It)!=INDEX_NONE )
		{
			UBOOL Send = 1;
			if( !It->IsA(UBoolProperty::StaticClass()) )
			{
				Send = !It->Matches(Parms,NULL,0);
				Bunch.WriteBit( Send );
			}
			if( Send )
				It->NetSerializeItem( Bunch, Connection->PackageMap, (BYTE*)Parms + It->Offset );
		}
	}

	// Reliability.
	//warning: RPC's might overflow, preventing reliable functions from getting thorough.
	if( Function->FunctionFlags & FUNC_NetReliable )
		Bunch.bReliable = 1;

	// Send the bunch.
	if( !Bunch.IsError() )
		Ch->SendBunch( &Bunch, 1 );
	else
		debugf( NAME_DevNet, TEXT("RPC bunch overflowed") );

	unguardSlow;
}

//
// Return whether a function should be executed remotely.
//
UBOOL AActor::ProcessRemoteFunction( UFunction* Function, void* Parms, FFrame* Stack )
{
	guard(AActor::ProcessRemoteFunction);

	// Quick reject.
	if( (Function->FunctionFlags & FUNC_Static) || bDeleteMe )
		return 0;
	UBOOL Absorb = Role<=ROLE_SimulatedProxy && !(Function->FunctionFlags & FUNC_Simulated);
	if( GetLevel()->DemoRecDriver )
	{
		if( GetLevel()->DemoRecDriver->ServerConnection )
			return Absorb;
		ProcessDemoRecFunction( Function, Parms, Stack );
	}
	if( Level->NetMode==NM_Standalone )
		return 0;
	if( !(Function->FunctionFlags & FUNC_Net) )
		return Absorb;

	// Check if the actor can potentially call remote functions.
	APlayerPawn*    Top              = Cast<APlayerPawn>(GetTopOwner());
	UNetConnection* ClientConnection = NULL;
	if
	(	(Role==ROLE_Authority)
	&&	(Top==NULL || (ClientConnection=Cast<UNetConnection>(Top->Player))==NULL) )
		return Absorb;

	// See if UnrealScript replication condition is met.
	while( Function->GetSuperFunction() )
		Function = Function->GetSuperFunction();
	UBOOL Val=0;
	FFrame( this, Function->GetOwnerClass(), Function->RepOffset, NULL ).Step( this, &Val );
	if( !Val )
		return Absorb;

	// Get the connection.
	UBOOL           IsServer   = Level->NetMode==NM_DedicatedServer || Level->NetMode==NM_ListenServer;
	UNetConnection* Connection = IsServer ? ClientConnection : GetLevel()->NetDriver->ServerConnection;
	check(Connection);

	// If saturated and function is unimportant, skip it.
	if( !(Function->FunctionFlags & FUNC_NetReliable) && !Connection->IsNetReady(0) )
		return 1;

	// Send function data to remote.
	InternalProcessRemoteFunction( this, Connection, Function, Parms, Stack, IsServer );
	return 1;

	unguardf(( TEXT("(%s)"), Function->GetFullName() ));
}

// Replicate a function call to a demo recording file
void AActor::ProcessDemoRecFunction( UFunction* Function, void* Parms, FFrame* Stack )
{
	guard(AActor::ProcessDemoRecFunction);

	// Check if the function is replicatable
	if( (Function->FunctionFlags & (FUNC_Static|FUNC_Net))!=FUNC_Net || bNetTemporary )
		return;

	UBOOL IsNetClient = (GetLevel()->GetLevelInfo()->NetMode == NM_Client);

	// Check if actor was spawned locally in a client-side demo 
	if(IsNetClient && Role == ROLE_Authority)
		return;

	// See if UnrealScript replication condition is met.
	while( Function->GetSuperFunction() )
		Function = Function->GetSuperFunction();

	UBOOL Val=0;
	if(IsNetClient)
		Exchange(RemoteRole, Role);
	bDemoRecording = 1;
	bClientDemoRecording = IsNetClient;
	FFrame( this, Function->GetOwnerClass(), Function->RepOffset, NULL ).Step( this, &Val );
	bDemoRecording = 0;
	bClientDemoRecording = 0;
	if(IsNetClient)
		Exchange(RemoteRole, Role);
	bClientDemoNetFunc = 0;
	if( !Val )
		return;

	// Get the channel.
	UNetConnection* Connection = GetLevel()->DemoRecDriver->ClientConnections(0);
	check(Connection);

	// Send function data to remote.
	BYTE* SavedCode = Stack ? Stack->Code : NULL;
	InternalProcessRemoteFunction( this, Connection, Function, Parms, Stack, 1 );
	if( Stack )
		Stack->Code = SavedCode;

	unguardf(( TEXT("(%s/%s)"), GetName(), Function->GetFullName() ));
}

/*-----------------------------------------------------------------------------
	GameInfo
-----------------------------------------------------------------------------*/

//
// Network
//
void AGameInfo::execGetNetworkNumber( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execNetworkNumber);
	P_FINISH;

	*(FString*)Result = XLevel->NetDriver ? XLevel->NetDriver->LowLevelGetNetworkNumber() : FString(TEXT(""));

	unguardexec;
}

//
// Deathmessage parsing.
//
void AGameInfo::execParseKillMessage( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execParseKillMessage);
	P_GET_STR(KillerName);
	P_GET_STR(VictimName);
	P_GET_STR(WeaponName);
	P_GET_STR(KillMessage);
	P_FINISH;

	FString Message, Temp;
	INT Offset;

	Temp = KillMessage;

	Offset = Temp.InStr(TEXT("%k"));
	if (Offset != -1)
	{
		Message = Temp.Left(Offset);
		Message += KillerName;
		Message += Temp.Right(Temp.Len() - Offset - 2);
	}
	Temp = Message;

	Offset = Temp.InStr(TEXT("%o"));
	if (Offset != -1)
	{
		Message = Temp.Left(Offset);
		Message += VictimName;
		Message += Temp.Right(Temp.Len() - Offset - 2);
	}
	Temp = Message;

	Offset = Temp.InStr(TEXT("%w"));
	if (Offset != -1)
	{
		Message = Temp.Left(Offset);
		Message += WeaponName;
		Message += Temp.Right(Temp.Len() - Offset - 2);
	}

	*(FString*)Result = Message;

	unguardexec;
}

/*-----------------------------------------------------------------------------
	ADecal Implementation
-----------------------------------------------------------------------------*/

// Find the coplanar surface corresponding to this intersection point.
static INT FindCoplanarSurface( UModel* Model, INT iNode, FVector IntersectionPoint, INT Depth )
{
	guard(FindCoplanarSurface);
	if( iNode == INDEX_NONE )
		return INDEX_NONE;

	FBspNode* Node = &Model->Nodes( iNode );
	if( Node->NumVertices > 0)
	{
		// check if this intersection point lies inside this node.
		FVert* Verts = &Model->Verts( Node->iVertPool );
		FVector &SurfNormal = Model->Vectors( Model->Surfs( Node->iSurf).vNormal );

		FVector* PrevVertex = &Model->Points( Verts[Node->NumVertices - 1].pVertex );
		UBOOL Success = 1;
		FLOAT PrevDot = 0;
		for( INT i=0;i<Node->NumVertices;i++ )
		{
			FVector* Vertex = &Model->Points(Verts[i].pVertex);
			FVector ClipNorm = SurfNormal ^ (*Vertex - *PrevVertex);
			FPlane ClipPlane( *Vertex, ClipNorm );

			FLOAT Dot = ClipPlane.PlaneDot( IntersectionPoint );
			
			if( (Dot < 0 && PrevDot > 0) ||
				(Dot > 0 && PrevDot < 0) )
			{
				Success = 0;
				break;
			}
			PrevDot = Dot;
			PrevVertex = Vertex;
		}
		if( Success )
			return Node->iSurf;
	}

	// check next co-planars to see if it contains this intersection point.
	return FindCoplanarSurface( Model, Node->iPlane, IntersectionPoint, Depth + 1 );
	unguard;
}

static void CalcClippedNodes( UModel* Model, FBspSurf& Surf, FVector* DecalVerts, TArray<INT>& NodeArray )
{
	guard(CalcClippedNodes);

	for( INT n=0;n<Surf.Nodes.Num(); n++)
	{
		FBspNode* Node = &Model->Nodes( Surf.Nodes(n) );
		
		if( Node->NumVertices > 0)
		{
			static FVector	Pts[FBspNode::MAX_FINAL_VERTICES];
			static FLOAT	Dots[FBspNode::MAX_FINAL_VERTICES];
			int NumPts;

			for( INT i=0;i<4;i++ )
				Pts[i] = DecalVerts[i];
			NumPts = 4;
			INT i;

			// check if this node contains any of the decal
			FVert* Verts = &Model->Verts( Node->iVertPool );
			FVector &SurfNormal = Model->Vectors( Surf.vNormal );

			FVector* PrevVertex = &Model->Points( Verts[Node->NumVertices - 1].pVertex );
			UBOOL Success = 1;
			for( i=0;i<Node->NumVertices;i++ )
			{
				FVector* Vertex = &Model->Points(Verts[i].pVertex);
				FVector ClipNorm = SurfNormal ^ (*Vertex - *PrevVertex);
				FPlane ClipPlane( *Vertex, ClipNorm );

				INT j;
				for(j=0;j<NumPts;j++)
					Dots[j] = ClipPlane.PlaneDot( Pts[j] );
				for(j=0;j<NumPts;j++)
				{
					if(		(Dots[j] > 0 && Dots[(j+1)%NumPts] < 0) 
						||	(Dots[j] < 0 && Dots[(j+1)%NumPts] > 0))
					{
						guard(InsertClippingPoint);
						FVector NewPoint = FLinePlaneIntersection( Pts[j], Pts[(j+1)%NumPts], ClipPlane );
						if(j < NumPts-1)
						{	
							// move Dots[] and Pts[] arrays along
							appMemmove( &Dots[j+2], &Dots[j+1], sizeof(FLOAT) * (NumPts - j - 1));
							appMemmove( &Pts[j+2], &Pts[j+1], sizeof(FVector) * (NumPts - j - 1));
						}
						Pts[j+1] = NewPoint;
						Dots[j+1] = 0; 
						NumPts++;
						j++;
						check(NumPts < FBspNode::MAX_FINAL_VERTICES);
						unguard;
					}			
				}
				guard(DeleteClippedPoints);
				for(j=0;j<NumPts;j++)
				{
					if( Dots[j] < 0 )
					{
						appMemmove( &Dots[j], &Dots[j+1], sizeof(FLOAT) * (NumPts - j - 1));
						appMemmove( &Pts[j], &Pts[j+1], sizeof(FVector) * (NumPts - j - 1) );
						j--;
						NumPts--;
					}
				}
				unguard;
				if( NumPts == 0 )
				{
					Success = 0;
					break;
				}
				PrevVertex = Vertex;
			}
			if( Success )
				NodeArray.AddItem( Surf.Nodes(n) );
		}
	}
	unguard;
}

void ADecal::execAttachDecal( FFrame& Stack, RESULT_DECL )
{
	guard(ADecal::execAttachDecal);
	P_GET_FLOAT(TraceDistance);
	P_GET_VECTOR_OPTX(DecalDir,FVector(0,0,0));
	P_FINISH;

	*(INT*)Result = 0;
	if( !GetLevel()->Engine->Client || !GetLevel()->Engine->Client->Decals )
		return;

#ifndef NODECALS
	if( Region.Zone->bFogZone )
		return;
	if(!Texture)
	{
		debugf(TEXT("AttachDecal: No Texture"));
		return;
	}
	MultiDecalLevel = Min<INT>(MultiDecalLevel, 4);

	UModel *Model = Level->XLevel->Model;
	FCheckResult Hit(1.0);
	FVector EndVect = -1 * Rotation.Vector(); // assume rotation oriented in direction of hitnormal
	EndVect.Normalize(); // FIXME - Jack, no need to do this - EndVect already normalized
	EndVect *= TraceDistance;

	INT RandDir = 0;
	if ( DecalDir.IsZero() )
	{
		DecalDir = VRand();
		RandDir = 1;
	}

	if( Model->LineCheck( Hit, NULL, Location + EndVect, Location, FVector(0, 0, 0), TRACE_VisBlocking ) != 0 ||
	    Hit.Item == INDEX_NONE )
	{
		return;
	}
	else
	{
		FBspSurf &Surf = Model->Surfs( Model->Nodes(Hit.Item).iSurf );
		FVector &SurfNormal = Model->Vectors(Surf.vNormal);
		FVector &SurfBase = Model->Points(Surf.pBase);
		FVector Intersection = FLinePlaneIntersection( Location,  Location + EndVect, SurfBase, SurfNormal );
		INT SurfIndex = FindCoplanarSurface( Model, Hit.Item, Intersection, 0 );
	
		if( SurfIndex == INDEX_NONE )
			return;
	
		// setup vertices for main decal surface.
		guard(FindMainSurfaceVertices);
		FBspSurf &Surf = Model->Surfs(SurfIndex);
		FVector &SurfNormal = Model->Vectors(Surf.vNormal);
		FVector &SurfBase = Model->Points(Surf.pBase);
		FVector DecalCenter = FLinePlaneIntersection( Location, Location + EndVect, SurfBase, SurfNormal );

		FLOAT d = Rotation.Vector() | SurfNormal;
		if( !d )
		{
			//debugf(TEXT("AttachDecal: decal ray is parallel to surface"));
			return;
		}
		if(Abs(((SurfBase - DecalCenter) | SurfNormal)) > 0.001f )
		{
			//debugf(TEXT("AttachDecal: Couldn't place decal: dot product is %f"), ((SurfBase - DecalCenter) | SurfNormal));
			return;
		}

		if( Surf.PolyFlags & (PF_AutoUPan|PF_AutoVPan) )
			return;

		// attach decal to new surface
		FDecal* MainDecal = NULL;
		for( INT j=0;j<Surf.Decals.Num();j++)
			if(Surf.Decals(j).Actor->Texture == Texture)
			{
				Surf.Decals.InsertZeroed(j);
				MainDecal = &Surf.Decals(j);					
				break;
			}
		if(!MainDecal) 
			MainDecal = &Surf.Decals(Surf.Decals.AddZeroed());		
		MainDecal->Actor = this;
		SurfList.AddItem(SurfIndex);

		guard(SetVertices);
		FLOAT diag = appSqrt( DrawScale * DrawScale * Texture->USize * Texture->USize / 2.f );
		// calculate decal co-ordinates - ASSUME DECALS ARE SQUARE

		if ( !RandDir )
		{
			// Project DecalDir onto the surface
			FVector MainAxis = DecalDir - (DecalDir | SurfNormal) * SurfNormal;

			if ( MainAxis.IsNearlyZero() )
			{
				MainAxis = DecalDir = ( SurfBase - DecalCenter );
				RandDir = 1;
			}
			else
			{
				// then we cross with the normal to get the other axis.
				FVector OtherAxis = MainAxis ^ SurfNormal;
				MainAxis.Normalize();
				OtherAxis.Normalize();

				// calculate the vector from the center to the diagonal.
				MainDecal->Vertices[0] = MainAxis + OtherAxis;
				MainDecal->Vertices[1] = MainAxis - OtherAxis;
			}
		}
		if ( RandDir )
		{
			// calculate the vector from the center to the diagonal.
			MainDecal->Vertices[0] = DecalDir - (DecalDir | SurfNormal) * SurfNormal;
			MainDecal->Vertices[1] = MainDecal->Vertices[0] ^ SurfNormal;
		}

		MainDecal->Vertices[0].Normalize();
		MainDecal->Vertices[1].Normalize();
		MainDecal->Vertices[0] *= diag;
		MainDecal->Vertices[1] *= diag;
		MainDecal->Vertices[2] = -MainDecal->Vertices[0];
		MainDecal->Vertices[3] = -MainDecal->Vertices[1];
		MainDecal->Vertices[0] += DecalCenter;
		MainDecal->Vertices[1] += DecalCenter;
		MainDecal->Vertices[2] += DecalCenter;
		MainDecal->Vertices[3] += DecalCenter;
		CalcClippedNodes( Model, Surf, MainDecal->Vertices, MainDecal->Nodes );
		unguard;

		guard(FindSecondarySurface);
		FLOAT NormSize = SurfNormal.Size();
		FVector TraceVect = -50*(SurfNormal / NormSize);
		FVector XVect = MainDecal->Vertices[1] - MainDecal->Vertices[0];
		FVector YVect = MainDecal->Vertices[3] - MainDecal->Vertices[0];

		for( INT X=0; X < MultiDecalLevel; X++ )
		{
			for( INT Y=0; Y < MultiDecalLevel; Y++ )
			{
				FVector TracePoint = MainDecal->Vertices[0] + (((FLOAT)(X+1.))/MultiDecalLevel)*XVect + (((FLOAT)(Y+1.))/MultiDecalLevel)*YVect;
				if( Model->LineCheck( Hit, NULL, TracePoint + TraceVect, TracePoint - TraceVect, FVector(0, 0, 0), TRACE_VisBlocking ) == 0 && Hit.Item != INDEX_NONE )
				{
					FBspSurf &SecSurf = Model->Surfs( Model->Nodes(Hit.Item).iSurf );
					FVector &SecNormal = Model->Vectors(SecSurf.vNormal);
					FVector &SecBase = Model->Points(SecSurf.pBase);
					FVector SecInt = FLinePlaneIntersection( TracePoint - TraceVect,  TracePoint + TraceVect, SecBase, SecNormal );
					SurfIndex = FindCoplanarSurface( Model, Hit.Item, SecInt, 0 );
				}
				else
					continue;

				if( SurfIndex == INDEX_NONE )
					continue;

				FBspSurf &SecSurf = Model->Surfs(SurfIndex);
				FVector &SecNormal = Model->Vectors(SecSurf.vNormal);
				FVector &SecBase = Model->Points(SecSurf.pBase);

				INT Found;
				if(SurfList.FindItem(SurfIndex, Found))
					continue;

				if( SecSurf.PolyFlags & (PF_AutoUPan|PF_AutoVPan) )
					continue;

				FLOAT costheta = (SurfNormal | SecNormal) / (SurfNormal.Size() * SecNormal.Size());
				if( Abs(costheta) <= 0.7 ) 
					continue;	// angle is too close to 90 degrees	

				// attach decal to seondary surface
				FDecal* SecDecal = NULL;
				INT j;
				for( j=0;j<SecSurf.Decals.Num();j++)
					if(SecSurf.Decals(j).Actor->Texture == Texture)
					{
						SecSurf.Decals.InsertZeroed(j);
						SecDecal = &SecSurf.Decals(j);					
						break;
					}
				if(!SecDecal) 
					SecDecal = &SecSurf.Decals(SecSurf.Decals.AddZeroed());
				SecDecal->Actor = this;
				SurfList.AddItem(SurfIndex);

				for( j=0;j<4;j++)
				{
					// Locate texture-wrapped point on secondary surface, for each vertex
					FVector A = FLinePlaneIntersection( MainDecal->Vertices[j]-SurfNormal, MainDecal->Vertices[j], SecBase, SecNormal );
					FVector B = FLinePlaneIntersection( MainDecal->Vertices[j]-SecNormal, MainDecal->Vertices[j], SecBase, SecNormal );
					FLOAT X = (MainDecal->Vertices[j] - B).Size() / costheta;
					FLOAT H = (MainDecal->Vertices[j] - A).Size() / costheta;
					FVector AB = B - A;
					AB.Normalize();
					SecDecal->Vertices[j] = B - (H-X)*AB;
				}
				CalcClippedNodes( Model, SecSurf, SecDecal->Vertices, SecDecal->Nodes );
				SecDecal->Vertices[0] -= SecBase;
				SecDecal->Vertices[1] -= SecBase;
				SecDecal->Vertices[2] -= SecBase;
				SecDecal->Vertices[3] -= SecBase;
			}
		}
		unguard;

		MainDecal->Vertices[0] -= SurfBase;
		MainDecal->Vertices[1] -= SurfBase;
		MainDecal->Vertices[2] -= SurfBase;
		MainDecal->Vertices[3] -= SurfBase;
		unguard;
	}
	*(INT*)Result = 1;
#endif
	unguard;
}

void ADecal::execDetachDecal( FFrame& Stack, RESULT_DECL )
{
	guard(ADecal::execDetachDecal);
	P_FINISH;

#ifndef NODECALS
	while( SurfList.Num() > 0 )
	{
		// detach decal from old surface
		FBspSurf& Surf = Level->XLevel->Model->Surfs(SurfList(SurfList.Num()-1));
		UBOOL RemovedDecal = 0;
		for( INT i=0; i<Surf.Decals.Num(); i++ )
			if( Surf.Decals(i).Actor == this )
			{
				Surf.Decals.Remove(i);
				RemovedDecal = 1;
				break;
			}

		//!! check(RemovedDecal);  // caused a crash with shadows during GC...
		SurfList.Remove(SurfList.Num()-1);
	}
#endif
	unguard;
}

// Color functions
#define P_GET_COLOR(var)            P_GET_STRUCT(FColor,var)

void AActor::execMultiply_ColorFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execMultiply_ColorFloat);

	P_GET_COLOR(A);
	P_GET_FLOAT(B);
	P_FINISH;

	A.R = (BYTE) (A.R * B);
	A.G = (BYTE) (A.G * B);
	A.B = (BYTE) (A.B * B);
	*(FColor*)Result = A;

	unguardexecSlow;
}	

void AActor::execMultiply_FloatColor( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execMultiply_FloatColor);

	P_GET_FLOAT (A);
	P_GET_COLOR(B);
	P_FINISH;

	B.R = (BYTE) (B.R * A);
	B.G = (BYTE) (B.G * A);
	B.B = (BYTE) (B.B * A);
	*(FColor*)Result = B;

	unguardexecSlow;
}	

void AActor::execAdd_ColorColor( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execAdd_ColorColor);

	P_GET_COLOR(A);
	P_GET_COLOR(B);
	P_FINISH;

	A.R = A.R + B.R;
	A.G = A.G + B.G;
	A.B = A.B + B.B;
	*(FColor*)Result = A;

	unguardexecSlow;
}

void AActor::execSubtract_ColorColor( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSubtract_ColorColor);

	P_GET_COLOR(A);
	P_GET_COLOR(B);
	P_FINISH;

	A.R = A.R - B.R;
	A.G = A.G - B.G;
	A.B = A.B - B.B;
	*(FColor*)Result = A;

	unguardexecSlow;
}
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
