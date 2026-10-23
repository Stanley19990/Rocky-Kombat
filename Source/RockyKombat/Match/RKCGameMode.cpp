// RKCGameMode.cpp

#include "Match/RKCGameMode.h"

#include "Arena/RKCArena.h"
#include "Audio/RKCAudio.h"
#include "Fighters/RKCFighter.h"
#include "Match/RKCPlayerController.h"
#include "UI/RKCFrontEnd.h"
#include "UI/RKCHUD.h"
#include "UI/RKCTouchControls.h"
#include "Visual/RKCArt.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInterface.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"
#include "Components/PrimitiveComponent.h"
#include "UnrealClient.h"
#include "RockyKombat.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

namespace
{
/** The narrowest view the match is framed for, as a width/height ratio. A window
 *  wider than this is framed as it is; a window narrower than 4:3 - a phone held
 *  upright, a square window - is rendered as a 4:3 view with bars beside it rather
 *  than a tall thin one. The arena image keeps its own aspect ratio either way and
 *  is cropped to cover the frame, so no background shows around it. */
constexpr float ArenaMinimumAspect = 4.0f / 3.0f;

// ---------------------------------------------------------------------------
// Sprite-component census (diagnostic only, inert unless asked for).
//
// WHY IT EXISTS: a rendered frame can show two fighter sprites where the source
// spawns one, and a picture cannot say which actor owns the extra one, whether it
// is a stale actor from an earlier screen, or a second component. This enumerates
// every live UPaperFlipbookComponent in the world with its owner, transform,
// flipbook, material and visibility, so the answer comes from the world rather
// than from guessing at an image. Set with  -rkc.SpriteDump=1  (optionally
// -rkc.SpriteDumpInterval=<seconds>, default 2.0).
// ---------------------------------------------------------------------------
int32 GK_SpriteDump = 0;
FAutoConsoleVariableRef CVarSpriteDump(
    TEXT("rkc.SpriteDump"), GK_SpriteDump,
    TEXT("1 = log a census of every live PaperFlipbook component each interval. Default 0."),
    ECVF_Default);

float GK_SpriteDumpInterval = 2.0f;
FAutoConsoleVariableRef CVarSpriteDumpInterval(
    TEXT("rkc.SpriteDumpInterval"), GK_SpriteDumpInterval,
    TEXT("Seconds between sprite censuses when rkc.SpriteDump=1. Default 2.0."),
    ECVF_Default);

// Diagnostic only, inert unless asked for. Hides one side's fighter sprite so a
// shape seen on screen can be attributed to a component rather than guessed at:
// hide A, then B, then both, and whatever still draws was never a fighter.
int32 GK_ArtAudit = 0;
FAutoConsoleVariableRef CVarArtAudit(
    TEXT("rkc.ArtAudit"), GK_ArtAudit,
    TEXT("1 = log every flipbook frame of both rosters and the texture it samples, once. Default 0."),
    ECVF_Default);

int32 GK_HideSide = 0;
FAutoConsoleVariableRef CVarHideSide(
    TEXT("rkc.HideSide"), GK_HideSide,
    TEXT("0 = show both fighters, 1 = hide side A's sprite, 2 = hide side B's, 3 = hide both. "),
    ECVF_Default);

// ---------------------------------------------------------------------------
// Console-tunable match setup. Every value declared here is set either from the
// in-game console or with  -rkc.<Name>=<Value>  on the command line, which is how
// an unattended run selects a quick-rules preset, a scripted menu or a capture
// run without a rebuild. The command-line form is not automatic in UE 5.8 - there
// is no FConsoleManager::SetFromCommandLine - so the game module applies those
// switches to these variables as it starts; see RockyKombatModule.cpp. A switch
// whose variable does not exist is reported there instead of being ignored.
// ---------------------------------------------------------------------------
int32 GK_BotSide = 1; // 0 = no bots, 1 = bot drives B, 2 = bot drives A, 3 = both
FAutoConsoleVariableRef CVarBotSide(
    TEXT("rkc.BotSide"), GK_BotSide,
    TEXT("Which side the offline opponent drives: 0 none, 1 B, 2 A, 3 both. Default 1."),
    ECVF_Default);

// Champion by default: the offline opponent is the one opponent a player has, and
// the brief for it is a fighter that is hard to beat. Difficulty raises the bot's
// reflexes and discipline only - never its damage, health, stamina or knowledge -
// so a harder default cannot change what the rules allow, only how well the CPU
// uses them. The difficulty screen still lets a player drop it to Rookie.
int32 GK_BotDifficulty = 2; // EBotDifficulty: Rookie 0, Contender 1, Champion 2
FAutoConsoleVariableRef CVarBotDifficulty(
    TEXT("rkc.BotDifficulty"), GK_BotDifficulty,
    TEXT("AI difficulty grade: 0 Rookie, 1 Contender, 2 Champion. Changes reflexes and "
         "discipline only, never statistics. Default 1."),
    ECVF_Default);

int32 GK_BotArchetypeA = 0; // EBotArchetype: Balanced 0, Pressure 1, Counter 2, Slugger 3
FAutoConsoleVariableRef CVarBotArchetypeA(
    TEXT("rkc.BotArchetypeA"), GK_BotArchetypeA,
    TEXT("AI personality for side A: 0 Balanced, 1 Pressure, 2 Counter, 3 Slugger."),
    ECVF_Default);

// Counter for side B, the offline opponent by default: it holds its guard and
// answers a whiffed or blocked attack, which is the archetype that punishes a
// player who mashes. Side A is left Balanced for the hot-seat case.
int32 GK_BotArchetypeB = 2;
FAutoConsoleVariableRef CVarBotArchetypeB(
    TEXT("rkc.BotArchetypeB"), GK_BotArchetypeB,
    TEXT("AI personality for side B: 0 Balanced, 1 Pressure, 2 Counter, 3 Slugger."),
    ECVF_Default);

int32 GK_QuickRules = 0;
FAutoConsoleVariableRef CVarQuickRules(
    TEXT("rkc.QuickRules"), GK_QuickRules,
    TEXT("1 = one round of 20 s instead of three rounds of 30 s. For smoke runs. Default 0."),
    ECVF_Default);

int32 GK_TrainingA = 0;
FAutoConsoleVariableRef CVarTrainingA(
    TEXT("rkc.TrainingA"), GK_TrainingA,
    TEXT("Training aid for side A: 0 off, 1 hitboxes, 2 infinite health, 3 infinite stamina, "
         "4 no opponent input, 5 frame data. Default 0."),
    ECVF_Default);

int32 GK_TrainingB = 0;
FAutoConsoleVariableRef CVarTrainingB(
    TEXT("rkc.TrainingB"), GK_TrainingB, TEXT("Training aid for side B; see rkc.TrainingA."),
    ECVF_Default);

int32 GK_Evidence = 1;
FAutoConsoleVariableRef CVarEvidence(
    TEXT("rkc.Evidence"), GK_Evidence,
    TEXT("1 = write a match record to Saved/Evidence on match end. Default 1."),
    ECVF_Default);

int32 GK_FrameData = 0;
FAutoConsoleVariableRef CVarFrameData(
    TEXT("rkc.FrameData"), GK_FrameData,
    TEXT("1 = show the frame-data overlay (F1 toggles it in game). Default 0."),
    ECVF_Default);

// A rate-limited trace of where the two fighters actually are, in world
// centimetres, once a second. "The fighters keep ending up at one end of the
// ring" is a claim about a trend over a whole round, which no single screenshot
// and no end-of-match summary can settle; this line can. Presentation/diagnostic
// only: it reads the simulation and writes the log.
int32 GK_LogPositions = 0;
FAutoConsoleVariableRef CVarLogPositions(
    TEXT("rkc.LogPositions"), GK_LogPositions,
    TEXT("1 = log both fighters' positions, facing, health and state once a second "
         "during a match. Default 0."),
    ECVF_Default);

int32 GK_RosterA = 0;
FAutoConsoleVariableRef CVarRosterA(TEXT("rkc.RosterA"), GK_RosterA,
                                    TEXT("Roster index for side A: 0 ROCKY, 1 SHADOW."),
                                    ECVF_Default);

int32 GK_RosterB = 1;
FAutoConsoleVariableRef CVarRosterB(TEXT("rkc.RosterB"), GK_RosterB,
                                    TEXT("Roster index for side B: 0 ROCKY, 1 SHADOW."),
                                    ECVF_Default);

int32 GK_BotSeedA = 20261017;
FAutoConsoleVariableRef CVarBotSeedA(TEXT("rkc.BotSeedA"), GK_BotSeedA,
                                     TEXT("Bot RNG seed for side A. Same seed = same match."),
                                     ECVF_Default);

int32 GK_BotSeedB = 911042;
FAutoConsoleVariableRef CVarBotSeedB(TEXT("rkc.BotSeedB"), GK_BotSeedB,
                                     TEXT("Bot RNG seed for side B. Same seed = same match."),
                                     ECVF_Default);

// 32 degrees with a level camera is what lets the supplied arena image fill the
// frame with its painted floor line on the fighters' feet; RKCArena solves the
// picture for whatever these are set to, so this is a framing choice rather than a
// requirement. A wider angle still covers, but the picture has to grow to do it,
// which makes the painted ring look larger beside the fighters; a much narrower one
// crops the fighters at the arena edge on a 4:3 window.
float GK_CameraFOV = 32.0f;
FAutoConsoleVariableRef CVarCameraFOV(TEXT("rkc.CameraFOV"), GK_CameraFOV,
                                      TEXT("Match camera field of view in degrees. Which axis the "
                                           "angle describes is the engine's aspect-ratio axis "
                                           "constraint, not this setting."),
                                      ECVF_Default);

float GK_CameraDistance = 1500.0f;
FAutoConsoleVariableRef CVarCameraDistance(
    TEXT("rkc.CameraDistance"), GK_CameraDistance,
    TEXT("Match camera distance from the fight line, in centimetres."), ECVF_Default);

// 120 cm, not 300. The camera's height is what decides where the fighters sit in
// the frame: at 300 cm on a level 1500 cm shot the projection put their feet
// 0.6975 of the half-frame below centre - 85% of the way down the picture, hard
// against the bottom edge, with the whole middle of the ring empty above them.
// 120 cm - about waist height on a 186 cm fighter - puts the feet at 64% and the
// head at 42%, so the pair occupies the middle of the frame. RKCArena re-solves
// the picture for whatever this is, so the painted floor line follows the feet.
float GK_CameraHeight = 120.0f;
FAutoConsoleVariableRef CVarCameraHeight(TEXT("rkc.CameraHeight"), GK_CameraHeight,
                                         TEXT("Match camera height in centimetres. Default 120. "
                                              "Raising it drops the fighters down the frame; "
                                              "lowering it lifts them."),
                                         ECVF_Default);

// Level by default. The arena image is a flat backdrop, so a level camera keeps its
// painted horizon parallel to the frame and the composition the framing was chosen
// for. A tilt still frames correctly - the arena solves the projection for the
// pitch it is given - it simply reads as a different shot.
float GK_CameraPitch = 0.0f;
FAutoConsoleVariableRef CVarCameraPitch(TEXT("rkc.CameraPitch"), GK_CameraPitch,
                                        TEXT("Match camera pitch in degrees. Default 0 (level)."),
                                        ECVF_Default);

// Presentation knob. The rule set's own value is 58 cm, and the imported sprites
// are drawn at 1 sprite pixel = 1 cm, so a fighter silhouette is roughly 180 cm
// wide: at 58 cm apart the two of them are drawn almost entirely on top of each
// other and read as one dark shape. 90 cm separates them enough to read as two
// fighters while staying inside what a JAB can still reach - a jab's reach is
// 104 cm and the bot will not swing outside it - so the CPU keeps a working range
// band instead of walking into an opponent it can never hit. ResolveRules enforces
// that ceiling rather than trusting this number.
float GK_MinSeparation = 90.0f;
FAutoConsoleVariableRef CVarMinSeparation(
    TEXT("rkc.MinSeparation"), GK_MinSeparation,
    TEXT("Optional override for the rules' minimum fighter separation, in centimetres. "
         "Default 110. Clamped at run time to keep the fighters inside the rules' "
         "punching reach."),
    ECVF_Default);

// ---------------------------------------------------------------------------
// Presentation switches. None of these can change a match result: they decide
// which screens exist, which sounds are issued and which device drives a
// fighter, never what the rules do.
// ---------------------------------------------------------------------------
int32 GK_FrontEnd = 1;
FAutoConsoleVariableRef CVarFrontEnd(
    TEXT("rkc.FrontEnd"), GK_FrontEnd,
    TEXT("1 (default) = branded startup, mode/fighter selection, results and pause "
         "screens. 0 = the straight-to-a-match behaviour of the validated build."),
    ECVF_Default);

int32 GK_Audio = 1;
FAutoConsoleVariableRef CVarAudio(
    TEXT("rkc.Audio"), GK_Audio,
    TEXT("1 (default) = resolve and play the music, ambience, stingers and combat "
         "sounds. 0 = silent; a match result is identical either way."),
    ECVF_Default);

int32 GK_TouchControls = -1;
FAutoConsoleVariableRef CVarTouchControls(
    TEXT("rkc.TouchControls"), GK_TouchControls,
    TEXT("On-screen controls: -1 (default) follow the saved setting and the platform, "
         "0 off, 1 on. 1 is how the pad is exercised on a desktop build."),
    ECVF_Default);

int32 GK_TouchForced = 0;
FAutoConsoleVariableRef CVarTouchForced(
    TEXT("rkc.TouchForced"), GK_TouchForced,
    TEXT("1 = draw the on-screen pad even where touch is not the input device, so it "
         "can be photographed on a desktop build. Routing is unchanged. Default 0."),
    ECVF_Default);

int32 GK_FrontEndAuto = 0;
FAutoConsoleVariableRef CVarFrontEndAuto(
    TEXT("rkc.FrontEndAuto"), GK_FrontEndAuto,
    TEXT("1 = drive the menus from a script, for an unattended demonstration run. "
         "A value above 1 is also the seconds the script spends on each screen, which is "
         "what a screenshot run uses to hold a card still long enough to be photographed. "
         "Never normal play. Default 0."),
    ECVF_Default);

int32 GK_CaptureStages = 0;
FAutoConsoleVariableRef CVarCaptureStages(
    TEXT("rkc.CaptureStages"), GK_CaptureStages,
    TEXT("1 = evidence run: the game itself asks the renderer for one screenshot per "
         "presentation stage (startup, loading, menu, versus, match, touch pad, results), "
         "records what the HUD had drawn at each one, writes a manifest beside them and "
         "leaves when the set is complete. 2 = the same without leaving. Needs a real "
         "renderer, so run windowed rather than with -nullrhi. Never normal play. "
         "Default 0."),
    ECVF_Default);

int32 GK_LiveShots = 0;
FAutoConsoleVariableRef CVarLiveShots(
    TEXT("rkc.LiveShots"), GK_LiveShots,
    TEXT("N = ask the renderer for N screenshots, evenly spaced one second apart, from "
         "inside the game itself while an ordinary match is being played. This is the "
         "control for a defect seen through an external window grab: the game's own "
         "screenshot is taken from the final rendered frame, so a feature that appears "
         "in both an external grab and one of these is really on screen, while a feature "
         "that appears only in the external grab is an artefact of that grab. Needs a "
         "real renderer. Never normal play. Default 0."),
    ECVF_Default);

// ---------------------------------------------------------------------------
// Small text helpers
// ---------------------------------------------------------------------------
/** True when this process was launched for the automatic in-engine self-test.
 *  Read from the command line rather than from the self-test state so the audio
 *  and front-end layers can be told "no playback, no menu" before it starts. */
bool IsSelfTestOnCommandLine()
{
    if (FParse::Param(FCommandLine::Get(), TEXT("RKCSelfTest")))
    {
        return true;
    }
    FString Ignored;
    return FParse::Value(FCommandLine::Get(), TEXT("RKCSelfTest="), Ignored);
}

/** True where the primary input is a finger. Used only as the default for the
 *  on-screen pad: rkc.TouchControls and the saved setting both override it, and
 *  no rule depends on the answer. */
bool PlatformDefaultsToTouch()
{
#if PLATFORM_ANDROID || PLATFORM_IOS
    return true;
#else
    return false;
#endif
}

RKC::ESide SideFromIndex(int32 Index)
{
    return (Index == 1) ? RKC::ESide::B : RKC::ESide::A;
}

/** Builds a match's rule set, applying the optional rkc.MinSeparation override.
 *  With the console variable left alone the preset is returned untouched. */
RKC::FMatchRules ResolveRules(bool bUseQuickRules)
{
    RKC::FMatchRules Rules = bUseQuickRules ? RKC::MakeQuickRules() : RKC::MakeDefaultRules();
    if (GK_MinSeparation > 0.0f)
    {
        // A separation the fighters cannot punch across makes the offline opponent
        // walk into the opponent for the whole round and never throw a punch - a
        // bot that stands at its own minimum distance and can never reach. The
        // rules' own reach is the ceiling for the standoff, with a margin so the
        // bot has a range band to work in rather than a single line.
        // The ceiling is the SHORTER of the two punches, not the longer one: the
        // offline opponent strikes from the jab window, so a standoff between the
        // jab's reach and the heavy's leaves it unable to connect with the punch it
        // actually throws. Measured before this clamp existed: a 106 cm standoff made
        // the CPU walk into a stationary opponent for a whole round and land nothing.
        const RKC::FFighterConfig Probe = RKC::MakeFighterConfig(RKC::ERoster::Rocky);
        const float ReachCeiling =
            FMath::Min(Probe.Jab.ReachFar, Probe.HeavyPunch.ReachFar) - 12.0f;
        const float Requested = FMath::Clamp(GK_MinSeparation, 10.0f, 600.0f);
        if (Requested > ReachCeiling)
        {
            UE_LOG(LogRockyKombat, Warning,
                   TEXT("rkc.MinSeparation %.0f cm is further apart than the fighters can "
                        "punch (%.0f cm of reach); using %.0f cm so the offline opponent "
                        "can still attack."),
                   Requested, ReachCeiling + 12.0f, ReachCeiling);
        }
        Rules.MinSeparation = FMath::Min(Requested, ReachCeiling);
    }
    return Rules;
}

const TCHAR* RoundEndText(RKC::ERoundEndReason Reason)
{
    switch (Reason)
    {
    case RKC::ERoundEndReason::TimeExpired:
        return TEXT("TIME");
    case RKC::ERoundEndReason::Knockout:
        return TEXT("KNOCKOUT");
    case RKC::ERoundEndReason::KnockdownLimit:
        return TEXT("THREE KNOCKDOWNS");
    case RKC::ERoundEndReason::DoubleKnockdown:
        return TEXT("DOUBLE KNOCKDOWN");
    default:
        return TEXT("ROUND OVER");
    }
}

const TCHAR* MatchEndText(RKC::EMatchEndReason Reason)
{
    switch (Reason)
    {
    case RKC::EMatchEndReason::Knockout:
        return TEXT("BY KNOCKOUT");
    case RKC::EMatchEndReason::TechnicalKnockout:
        return TEXT("BY TECHNICAL KNOCKOUT");
    case RKC::EMatchEndReason::RefereeStopsContest:
        return TEXT("REFEREE STOPS CONTEST");
    case RKC::EMatchEndReason::Decision:
        return TEXT("ON POINTS");
    case RKC::EMatchEndReason::DrawDecision:
        return TEXT("A DRAW");
    default:
        return TEXT("");
    }
}

const TCHAR* TrainingText(RKC::ETrainingMode Mode)
{
    switch (Mode)
    {
    case RKC::ETrainingMode::ShowHitboxes:
        return TEXT("HITBOXES");
    case RKC::ETrainingMode::InfiniteHealth:
        return TEXT("INFINITE HEALTH");
    case RKC::ETrainingMode::InfiniteStamina:
        return TEXT("INFINITE STAMINA");
    case RKC::ETrainingMode::NoOpponentInput:
        return TEXT("NO OPPONENT INPUT");
    case RKC::ETrainingMode::DisplayFrameData:
        return TEXT("FRAME DATA");
    default:
        return TEXT("OFF");
    }
}

/** FNV-1a over a 32-bit value, used to fold simulation state into one number. */
void MixHash(uint32& Hash, uint32 Value)
{
    Hash ^= Value;
    Hash *= 16777619u;
}
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ARKCGameMode::ARKCGameMode()
{
    PrimaryActorTick.bCanEverTick = true;

    // The simulation owns the fighters and moves them; there is nothing for an
    // engine default pawn to do, and a floating default pawn would be a visible
    // reminder that the world is not under simulation control.
    DefaultPawnClass = nullptr;
    PlayerControllerClass = ARKCPlayerController::StaticClass();
    HUDClass = ARKCHUD::StaticClass();
    bStartPlayersAsSpectators = false;
}

void ARKCGameMode::StartPlay()
{
    UE_LOG(LogRockyKombat, Display, TEXT("Startup: entering game mode."));
    Super::StartPlay();
    ApplyConsoleOverridesAndRestart();
    UE_LOG(LogRockyKombat, Display, TEXT("Startup: arena and fighters constructed."));

    // Audio, the front end and the lifecycle hooks come up after the world exists,
    // because the loading card's progress is measured from the world it built - so
    // this must run after BuildWorld and before anything is stepped.
    InitFrontEndAndAudio();
    UE_LOG(LogRockyKombat, Display, TEXT("Startup: front end and audio initialized."));

    // A scripted-input run (if the command line asked for one) starts here, after
    // the world and the front end exist, because the driver presses keys through
    // the player controller and reads the front end's counters. InitFromCommandLine
    // returns 0 for a normal launch, so nothing below happens unless a run passed
    // -rkc.Script=<name> or -rkc.ScriptFile=<path>.
    if (const int32 ScriptedSteps = ScriptDriver.InitFromCommandLine(); ScriptedSteps > 0)
    {
        UE_LOG(LogRockyKombat, Display, TEXT("Scripted input: %s"), *ScriptDriver.Describe());
    }

    // A self-test run drives the match itself from here; see RKCSelfTest below.
    // This is the last thing StartPlay does, so the world, the simulation and the
    // HUD are all in place before the first match is stepped.
    StartSelfTestIfRequested();
}

// ---------------------------------------------------------------------------
// World construction
// ---------------------------------------------------------------------------

void ARKCGameMode::ApplyConsoleOverridesAndRestart()
{
    Setup.bBotControlsA = (GK_BotSide == 2 || GK_BotSide == 3);
    Setup.bBotControlsB = (GK_BotSide == 1 || GK_BotSide == 3);
    Setup.BotDifficulty = static_cast<RKC::EBotDifficulty>(FMath::Clamp(GK_BotDifficulty, 0, 2));
    Setup.BotArchetypeA = static_cast<RKC::EBotArchetype>(FMath::Clamp(GK_BotArchetypeA, 0, 3));
    Setup.BotArchetypeB = static_cast<RKC::EBotArchetype>(FMath::Clamp(GK_BotArchetypeB, 0, 3));
    Setup.bUseQuickRules = (GK_QuickRules != 0);
    Setup.TrainingA = static_cast<RKC::ETrainingMode>(FMath::Clamp(GK_TrainingA, 0, 5));
    Setup.TrainingB = static_cast<RKC::ETrainingMode>(FMath::Clamp(GK_TrainingB, 0, 5));
    Setup.bCaptureEvidence = (GK_Evidence != 0);
    Setup.FighterA = (GK_RosterA == 1) ? RKC::ERoster::Shadow : RKC::ERoster::Rocky;
    Setup.FighterB = (GK_RosterB == 1) ? RKC::ERoster::Shadow : RKC::ERoster::Rocky;
    Setup.BotSeedA = static_cast<uint32>(GK_BotSeedA);
    Setup.BotSeedB = static_cast<uint32>(GK_BotSeedB);
    Setup.CameraFOV = FMath::Clamp(GK_CameraFOV, 15.0f, 110.0f);
    Setup.CameraDistance = FMath::Max(GK_CameraDistance, 200.0f);
    Setup.CameraHeight = GK_CameraHeight;
    Setup.CameraPitch = FMath::Clamp(GK_CameraPitch, -60.0f, 20.0f);

    bFrameDataVisible = (GK_FrameData != 0);
    SetFrameDataVisible(bFrameDataVisible);

    if (!bWorldBuilt)
    {
        BuildWorld();
    }
    else
    {
        RestartMatch();
    }
}

void ARKCGameMode::BuildWorld()
{
    if (bWorldBuilt)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogRockyKombat, Error, TEXT("No world; the match could not be built."));
        return;
    }

    const RKC::FMatchRules Rules = ResolveRules(Setup.bUseQuickRules);

    // The simulation is created BEFORE anything that presents it, so no actor can
    // ever read a fighter state that does not exist yet.
    Sim.Init(Rules, RKC::MakeFighterConfig(Setup.FighterA), RKC::MakeFighterConfig(Setup.FighterB),
             Setup.TrainingA, Setup.TrainingB);

    BuildLighting();

    Arena = World->SpawnActor<ARKCArena>(FVector::ZeroVector, FRotator::ZeroRotator);
    if (Arena)
    {
        Arena->BuildRing(Rules);
    }
    else
    {
        UE_LOG(LogRockyKombat, Error, TEXT("Arena failed to spawn; the ring will be missing."));
    }

    SpawnFighters();
    BuildCamera();
    bWorldBuilt = true;

    // Take control of the view explicitly. Without this the controller would keep
    // trying to point the camera at a (non-existent) possessed pawn.
    if (ARKCPlayerController* PC = GetRKCPlayerController())
    {
        PC->bAutoManageActiveCameraTarget = false;
        if (MatchCamera)
        {
            PC->SetViewTarget(MatchCamera);
        }
    }

    RestartMatch();
    LogSetup();
}

void ARKCGameMode::BuildLighting()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // Greybox lighting is three movable directional lights rather than a sky and
    // an atmosphere: it costs almost nothing on integrated graphics, it cannot
    // produce the shadow acne that a flat ring invites, and it is deterministic,
    // so two machines render the same picture.
    struct FLightSpec
    {
        float Pitch;
        float Yaw;
        float Intensity;
        int32 ForwardPriority;
        const TCHAR* Name;
    };
    // ForwardPriority is deliberately distinct per light. The renderer can only
    // feed ONE directional light into forward shading, which is the path the
    // unlit imported sprite materials take; when two lights tie on priority it
    // falls back to picking the brightest and, in editor builds, writes a
    // warning over the HUD. Naming a winner here makes the choice explicit.
    const FLightSpec Specs[] = {
        {-52.0f, 35.0f, 2.6f, 2, TEXT("KeyLight")},
        {-14.0f, -55.0f, 1.2f, 1, TEXT("FillLight")},
        {-78.0f, 180.0f, 0.7f, 0, TEXT("AmbientLight")},
    };

    for (const FLightSpec& Spec : Specs)
    {
        const FTransform SpawnTransform(FRotator(Spec.Pitch, Spec.Yaw, 0.0f));

        // Deferred spawn so mobility is set to Movable before the component
        // registers. r.AllowStaticLighting is off in this project, so a
        // stationary light would be both wrong and noisy in the log.
        ADirectionalLight* Light = World->SpawnActorDeferred<ADirectionalLight>(
            ADirectionalLight::StaticClass(), SpawnTransform);
        if (!Light)
        {
            UE_LOG(LogRockyKombat, Warning, TEXT("Could not spawn %s."), Spec.Name);
            continue;
        }

        if (UDirectionalLightComponent* Comp =
                Cast<UDirectionalLightComponent>(Light->GetLightComponent()))
        {
            Comp->SetMobility(EComponentMobility::Movable);
            Comp->SetIntensity(Spec.Intensity);
            Comp->SetCastShadows(false);
            Comp->SetForwardShadingPriority(Spec.ForwardPriority);
        }

        Light->FinishSpawning(SpawnTransform);
    }
}

void ARKCGameMode::SpawnFighters()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const float FightLineY = Arena ? Arena->GetFightLineY() : 0.0f;

    FighterA = World->SpawnActor<ARKCFighter>(FVector::ZeroVector, FRotator::ZeroRotator);
    FighterB = World->SpawnActor<ARKCFighter>(FVector::ZeroVector, FRotator::ZeroRotator);

    if (FighterA)
    {
        FighterA->Init(&Sim, RKC::ESide::A, Setup.FighterA, FightLineY);
    }
    if (FighterB)
    {
        FighterB->Init(&Sim, RKC::ESide::B, Setup.FighterB, FightLineY);
    }

    if (!FighterA || !FighterB)
    {
        UE_LOG(LogRockyKombat, Error,
               TEXT("One or both fighters failed to spawn; the match cannot be presented."));
    }
}

void ARKCGameMode::BuildCamera()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // Looking along -Y makes screen-right agree with simulation +X. Looking from
    // the opposite side reversed both movement controls and the HUD's side labels.
    const FVector Location(0.0f, Setup.CameraDistance, Setup.CameraHeight);
    const FRotator Rotation(Setup.CameraPitch, -90.0f, 0.0f);

    MatchCamera = World->SpawnActor<ACameraActor>(Location, Rotation);
    if (!MatchCamera)
    {
        UE_LOG(LogRockyKombat, Error, TEXT("Match camera failed to spawn; the view is undefined."));
        return;
    }

    if (UCameraComponent* Cam = MatchCamera->GetCameraComponent())
    {
        Cam->SetFieldOfView(Setup.CameraFOV);
        // The arena picture is framed for the view that is really rendered, so the
        // aspect-ratio constraint is managed per frame by UpdateArenaFraming(): off
        // for a landscape window, and on at 4:3 - letterboxed - when the window is
        // narrower than that, where a tall thin view would squeeze the ring into it.
        // Starting unconstrained matches the ordinary landscape case.
        Cam->AspectRatio = ArenaMinimumAspect;
        Cam->SetConstraintAspectRatio(false);
    }

    // Hand the arena the camera it will be seen through, so the picture is framed
    // from the first rendered frame rather than from the first tick. The window's own
    // shape arrives with that first tick, which re-frames if it is not the 16:9 this
    // pass assumes.
    UpdateArenaFraming(0.0f);
}

// ---------------------------------------------------------------------------
// Match lifecycle
// ---------------------------------------------------------------------------

void ARKCGameMode::RestartMatch()
{
    if (!bWorldBuilt)
    {
        return;
    }

    const RKC::FMatchRules Rules = ResolveRules(Setup.bUseQuickRules);

    Sim.Init(Rules, RKC::MakeFighterConfig(Setup.FighterA), RKC::MakeFighterConfig(Setup.FighterB),
             Setup.TrainingA, Setup.TrainingB);

    // The ring geometry is derived from the rules' arena bounds, and the quick
    // preset does not move those bounds, so the ring is reused. If a future
    // preset does move them, rebuild rather than let the picture drift.
    if (Arena && Arena->IsBuilt())
    {
        // (bounds check intentionally documented here rather than guessed at)
    }

    const float FightLineY = Arena ? Arena->GetFightLineY() : 0.0f;
    if (FighterA)
    {
        FighterA->Init(&Sim, RKC::ESide::A, Setup.FighterA, FightLineY);
    }
    if (FighterB)
    {
        FighterB->Init(&Sim, RKC::ESide::B, Setup.FighterB, FightLineY);
    }

    bBotActive[0] = Setup.bBotControlsA;
    bBotActive[1] = Setup.bBotControlsB;
    if (bBotActive[0])
    {
        BotBrains[0].Init(RKC::ESide::A,
                          RKC::FBotProfile::Make(Setup.BotArchetypeA, Setup.BotDifficulty),
                          Setup.BotSeedA);
    }
    if (bBotActive[1])
    {
        BotBrains[1].Init(RKC::ESide::B,
                          RKC::FBotProfile::Make(Setup.BotArchetypeB, Setup.BotDifficulty),
                          Setup.BotSeedB);
    }

    SimAccumulator = 0.0f;
    EventFeed.Reset();
    BannerText.Reset();
    BannerAgeSeconds = 0.0f;
    bBannerProminent = false;
    bWasCountingDown = false;
    bEvidenceWritten = false;
    MatchEndedAtSimTime = -1.0f;
    SecondsSinceMatchEnd = -1.0f;
    TotalHitsA = TotalHitsB = 0;
    TotalBlocksA = TotalBlocksB = 0;
    TotalDodgesA = TotalDodgesB = 0;
    TotalAttacksA = TotalAttacksB = 0;
    TotalKnockdownsA = TotalKnockdownsB = 0;
    WorstSeparationViolation = 0.0f;
    TicksSimulated = 0;

    SetFrameDataVisible(bFrameDataVisible);

    UE_LOG(LogRockyKombat, Log, TEXT("Match started: %s vs %s, %d round(s) of %.0f s."),
           *RKC::GetRosterDisplayName(Setup.FighterA),
           *RKC::GetRosterDisplayName(Setup.FighterB), Sim.GetRules().Rounds,
           Sim.GetRules().RoundSeconds);
}

void ARKCGameMode::RequestQuitMatch()
{
#if WITH_EDITOR
    UE_LOG(LogRockyKombat, Log,
           TEXT("Quit requested. Ignored in the editor so a running session survives; this "
                "exits the process in a packaged build."));
#else
    if (ARKCPlayerController* PC = GetRKCPlayerController())
    {
        UE_LOG(LogRockyKombat, Log, TEXT("Quit requested; shutting down."));
        PC->ConsoleCommand(TEXT("quit"));
    }
#endif
}

// ---------------------------------------------------------------------------
// Front end, audio, touch and lifecycle
// ---------------------------------------------------------------------------
// Everything in this section is presentation: it decides which screen is up,
// which sounds are issued and which device drives a fighter - never what the
// rules do. The one hard rule it enforces is that the simulation cannot advance
// while a menu, a loading card or a result card owns the screen, which is what
// makes "no match runs behind the menu" a property of the code rather than a
// promise.
// ---------------------------------------------------------------------------

namespace
{
/** The on-screen pad is live when rkc.TouchControls says so, otherwise when the
 *  player has made an explicit choice, otherwise wherever the primary input is a
 *  finger. Only which device can drive a fighter changes; no rule depends on it. */
bool ResolveTouchControlsEnabled(const URKCAudioSystem* Audio)
{
    if (GK_TouchControls == 0)
    {
        return false;
    }
    if (GK_TouchControls == 1)
    {
        return true;
    }
    if (Audio != nullptr && Audio->WasTouchControlsChosen())
    {
        return Audio->GetTouchControlsEnabled();
    }
    return PlatformDefaultsToTouch();
}

/** True for the events whose actor is the fighter it happened TO rather than the
 *  fighter that acted, so the audio planner is handed the two rosters the right
 *  way round. Side means "the fighter this event is about": the attacker for a
 *  hit, the fighter who went down for a knockdown. */
bool IsDefensiveEvent(RKC::ECombatEvent Type)
{
    return Type == RKC::ECombatEvent::Knockdown || Type == RKC::ECombatEvent::Knockout ||
           Type == RKC::ECombatEvent::GetUp || Type == RKC::ECombatEvent::GuardBroken;
}
} // namespace

void ARKCGameMode::InitFrontEndAndAudio()
{
    Online = MakeUnique<FRKCOnlineClient>();
    FrontEnd.SetOnline(Online.Get());
    FParse::Value(FCommandLine::Get(), TEXT("rkc.OnlineTest="), OnlineTestRole);
    if (!OnlineTestRole.IsEmpty()) Online->Connect();
    // ---- Audio --------------------------------------------------------------
    // Built even when this run asked for silence, so the catalog is still
    // resolved and the evidence record can report what is missing instead of
    // saying nothing at all. Suppressed playback is counted, never hidden.
    if (AudioSystem == nullptr)
    {
        AudioSystem = NewObject<URKCAudioSystem>(this, TEXT("RKCAudioSystem"));
    }

    const bool bPlayback = (GK_Audio != 0) && !bSelfTestActive && !IsSelfTestOnCommandLine();
    AudioSystem->Initialise(GetWorld(), bPlayback);
    const FRKCAudioDiagnostics& AudioDiag = AudioSystem->GetDiagnostics();
    UE_LOG(LogRockyKombat, Log, TEXT("Audio: %d of %d catalog assets resolved; playback %s."),
           AudioDiag.AssetsResolved, AudioDiag.CatalogSize,
           bPlayback ? TEXT("on") : TEXT("off"));

    // ---- Lifecycle ----------------------------------------------------------
    // Two independent routes into the same pair of handlers: the core delegates
    // cover a phone going to the home screen or being interrupted, and Slate's
    // activation state covers a desktop window losing the keyboard. Both are
    // removed again in EndPlay.
    BackgroundedDelegateHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(
        this, &ARKCGameMode::HandleApplicationBackgrounded);
    ForegroundedDelegateHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(
        this, &ARKCGameMode::HandleApplicationForegrounded);
    DeactivatedDelegateHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(
        this, &ARKCGameMode::HandleApplicationBackgrounded);
    ReactivatedDelegateHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddUObject(
        this, &ARKCGameMode::HandleApplicationForegrounded);
    if (FSlateApplication::IsInitialized())
    {
        ActivationStateDelegateHandle = FSlateApplication::Get()
                                            .OnApplicationActivationStateChanged()
                                            .AddUObject(this, &ARKCGameMode::HandleSlateActivationStateChanged);
    }

    // ---- The front end ------------------------------------------------------
    bFrontEndEnabled = (GK_FrontEnd != 0) && !bSelfTestActive && !IsSelfTestOnCommandLine();

    ARKCPlayerController* PC = GetRKCPlayerController();
    if (!bFrontEndEnabled)
    {
        // The validated build's behaviour, exactly: a match exists from the first
        // frame, with the arena bed behind it and the pad wherever the platform
        // wants it.
        if (bPlayback)
        {
            AudioSystem->PlayMusic(ERKCMusic::Arena, 0.0f);
            AudioSystem->PlayAmbience();
        }
        if (PC != nullptr)
        {
            PC->SetGameplayRouting(true);
            PC->SetTouchControlsEnabled(ResolveTouchControlsEnabled(AudioSystem));
        }
        bFrontEndActive = false;
        UE_LOG(LogRockyKombat, Log, TEXT("Front end disabled (-rkc.FrontEnd=0 or a self-test): "
                                         "straight to a match, as validated."));
        return;
    }

    const bool bReturning = AudioSystem->GetHasPlayedBefore();
    FrontEnd.SetAudio(AudioSystem);
    FrontEnd.Init(bReturning);
    if (GK_FrontEndAuto != 0)
    {
        // A value above 1 is also the time the script spends on each screen, so a
        // capture run can hold a card long enough to be photographed without
        // touching the machine itself.
        const float SecondsPerScreen =
            (GK_FrontEndAuto > 1) ? static_cast<float>(GK_FrontEndAuto) : 1.2f;
        FrontEnd.SetAutoPilot(true, SecondsPerScreen);
    }

    if (PC != nullptr)
    {
        // The menu owns the screen from here: touches are taps, and nothing a key
        // press does can reach a fighter.
        PC->SetGameplayRouting(false);
        PC->SetInputEnabled(false);
        PC->SetTouchControlsEnabled(ResolveTouchControlsEnabled(AudioSystem));
    }
    bFrontEndActive = true;

    UE_LOG(LogRockyKombat, Log, TEXT("Front end: %s. Returning player: %s."), *FrontEnd.DescribeState(),
           bReturning ? TEXT("yes") : TEXT("no"));
}

RKCFrontEnd::FInput ARKCGameMode::CollectFrontEndInput()
{
    RKCFrontEnd::FInput Out;

    ARKCPlayerController* PC = GetRKCPlayerController();
    if (PC == nullptr)
    {
        return Out;
    }

    // Whatever the renderer is really using, so a tap lands on the row it looks
    // like it landed on rather than on a row scaled for some other window.
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
        if (Size.X > 0 && Size.Y > 0)
        {
            Out.ViewportSize = FVector2D(Size.X, Size.Y);
        }
    }

    // Drained every frame, on every screen: a key pressed while a menu was up
    // must not be replayed at a fighter later, and a key pressed during a match
    // must not queue a menu step for the pause screen.
    const FRKCMenuInput Menu = PC->ConsumeMenuInput(Out.ViewportSize);
    Out.Navigate = Menu.Navigate;
    Out.Adjust = Menu.Adjust;
    Out.bAccept = Menu.bAccept;
    Out.bBack = Menu.bBack;
    Out.bPause = Menu.bPause;
    Out.Taps = Menu.Taps;
    return Out;
}

void ARKCGameMode::RefreshLoadingProgress()
{
    // Progress is derived from work that actually happened, never from a timer.
    // The weights add up to exactly 1.0 so the card can complete, and the largest
    // share is the render warm-up, which is the part a black first frame lives
    // in.
    float Progress = 0.0f;
    if (bWorldBuilt)
    {
        Progress += 0.10f;
    }
    if (Arena != nullptr && Arena->IsBuilt())
    {
        Progress += 0.20f;
    }
    if (FighterA != nullptr && FighterB != nullptr && GetArenaPieceCount() > 0 &&
        GetFighterPartCount(RKC::ESide::A) > 0 && GetFighterPartCount(RKC::ESide::B) > 0)
    {
        Progress += 0.30f;
    }

    // The warm-up term is counted in drawn frames when anything is drawing, and
    // in elapsed frames when nothing is, so a run with no renderer still reaches
    // the end of the bar instead of waiting forever at 90%.
    float WarmupFraction = 1.0f;
    if (!IsRenderWarmupComplete())
    {
        WarmupFraction = bSawAnyHudDraw
                             ? static_cast<float>(WarmupFramesObserved) / RenderWarmupFramesRequired
                             : static_cast<float>(FrontEndFramesTicked) / RenderWarmupHeadlessTimeoutFrames;
        WarmupFraction = FMath::Clamp(WarmupFraction, 0.0f, 1.0f);
    }
    Progress += 0.40f * WarmupFraction;

    LoadingProgress = FMath::Clamp(Progress, 0.0f, 1.0f);
    FrontEnd.SetLoadingProgress(LoadingProgress);
    FrontEnd.SetLoadDetail(FString::Printf(
        TEXT("%d%%  ring %d pieces  rigs %d/%d  %s"), FMath::RoundToInt(LoadingProgress * 100.0f),
        GetArenaPieceCount(), GetFighterPartCount(RKC::ESide::A), GetFighterPartCount(RKC::ESide::B),
        IsRenderWarmupComplete() ? TEXT("presentation warm") : TEXT("warming presentation")));
}

bool ARKCGameMode::IsRenderWarmupComplete() const
{
#if WITH_EDITOR
    if (FAssetCompilingManager::Get().GetNumRemainingAssets() > 0 ||
        (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())) return false;
#endif
    if (bSawAnyHudDraw)
    {
        // A renderer exists: the gate is the drawn-frame count, so gameplay is
        // released by frames that were really presented.
        return WarmupFramesObserved >= RenderWarmupFramesRequired;
    }

    // Nothing has drawn at all, which is a headless, -nullrhi or off-screen run.
    // Give the renderer a generous window and then proceed rather than hang the
    // run on a screen that can never fill in.
    return FrontEndFramesTicked >= RenderWarmupHeadlessTimeoutFrames;
}

void ARKCGameMode::NotifyHudDrew()
{
    bSawAnyHudDraw = true;
    if (WarmupFramesObserved < RenderWarmupHeadlessTimeoutFrames * 10)
    {
        ++WarmupFramesObserved;
    }
}

void ARKCGameMode::BeginMatchFromFrontEnd()
{
    const RKCFrontEnd::FRKCMatchRequest& Request = FrontEnd.GetMatchRequest();

    // The request is copied into the setup the rest of the director already
    // understands. Nothing below this line needs to know a menu exists.
    bOnlineMatch = Request.Arrangement == RKCFrontEnd::EArrangement::PrivateOnline;
    if (bOnlineMatch) {
        Setup.bUseQuickRules = !OnlineTestRole.IsEmpty();
        ++OnlineTestMatches; bOnlineTestRematchSent = false;
    }
    Setup.FighterA = Request.FighterA;
    Setup.FighterB = Request.FighterB;
    Setup.bBotControlsA = !Request.bHumanA;
    Setup.bBotControlsB = !Request.bHumanB;
    Setup.BotDifficulty = Request.Difficulty;

    // Training modes come from the training screen; every other arrangement has
    // none. Without this reset, a mode chosen on an earlier visit to the training
    // screen would still be set on the next versus match - which is exactly the
    // leak this pass exists to remove.
    if (Request.Arrangement == RKCFrontEnd::EArrangement::Training)
    {
        // A training aid is carried by the side it BELONGS to: NoOpponentInput on
        // side A means "side B does not act", so the stationary dummy is owned by
        // the player's own side. Putting it on side B silenced side A - the player -
        // while the CPU boxed an opponent that could not answer, which reads from
        // the player's seat as a game where nothing they press does anything.
        // With no aid chosen the CPU is the dummy; choosing one of the other aids
        // (hitboxes, frame data, infinite health or stamina) hands the CPU its
        // input back and the aid applies to the player instead.
        Setup.TrainingA = (Request.TrainingA == RKC::ETrainingMode::Off)
                              ? RKC::ETrainingMode::NoOpponentInput
                              : Request.TrainingA;
        Setup.TrainingB = RKC::ETrainingMode::Off;
    }
    else
    {
        Setup.TrainingA = RKC::ETrainingMode::Off;
        Setup.TrainingB = RKC::ETrainingMode::Off;
    }

    // The frame-data overlay belongs to the training screen; leaving it on for an
    // ordinary match would be a presentation leak of the same family.
    bFrameDataVisible = (Setup.TrainingA == RKC::ETrainingMode::DisplayFrameData) || (GK_FrameData != 0);

    RestartMatch();
#if WITH_EDITOR
    // Complete the selected fighter resources before presenting gameplay.
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
#endif
    bMatchEndReported = false;
    bMatchWasRunning = true;
    bWasSimulating = true;

    if (ARKCPlayerController* PC = GetRKCPlayerController())
    {
        // The scheme is re-derived from the request rather than remembered, so a
        // rematch with a different arrangement cannot keep the previous
        // assignment. Local versus is one keyboard driving both sides; every
        // other arrangement is one human side with the offline opponent on the
        // other.
        if (bOnlineMatch && Online) {
            PC->SetInputScheme(ERKCInputScheme::SingleSide, Online->Side == 0 ? RKC::ESide::A : RKC::ESide::B);
            PC->SetTouchControlsEnabled(ResolveTouchControlsEnabled(AudioSystem));
        }
        else if (Request.bHumanA && Request.bHumanB)
        {
            PC->SetInputScheme(ERKCInputScheme::HotSeat, RKC::ESide::A);
            PC->SetTouchControlsEnabled(false);
        }
        else
        {
            PC->SetInputScheme(ERKCInputScheme::SingleSide, RKC::ESide::A);
            PC->SetTouchControlsEnabled(ResolveTouchControlsEnabled(AudioSystem));
        }
        PC->SetGameplayRouting(true);
        PC->SetInputEnabled(true);
    }

    if (AudioSystem != nullptr)
    {
        AudioSystem->SetHasPlayedBefore(true);
        AudioSystem->IncrementMatchesPlayed();
    }

    if (bOnlineMatch && Online) Online->Ready();
    FrontEnd.NotifyMatchStarted();

    UE_LOG(LogRockyKombat, Log, TEXT("Front end accepted: %s vs %s, human A %s / human B %s."),
           *RKC::GetRosterDisplayName(Setup.FighterA), *RKC::GetRosterDisplayName(Setup.FighterB),
           Request.bHumanA ? TEXT("yes") : TEXT("no"), Request.bHumanB ? TEXT("yes") : TEXT("no"));
}

void ARKCGameMode::ReturnToFrontEnd()
{
    ARKCPlayerController* PC = GetRKCPlayerController();
    if (PC != nullptr)
    {
        // Nothing a player pressed on the way out may fire on the way back in.
        PC->ClearAllHumanInput(TEXT("returned-to-front-end"));
        PC->SetGameplayRouting(false);
        PC->SetInputEnabled(false);
    }

    // Releases the audio bed without starting anything: the menu decides what
    // plays next, and a stray arena loop behind a menu is the defect this is
    // here to prevent.
    if (AudioSystem != nullptr)
    {
        AudioSystem->OnLeftGameplayToFrontEnd();
    }

    bMatchEndReported = false;
    bMatchWasRunning = false;
    bWasSimulating = false;
    FrontEnd.ReturnToMainMenu();

    UE_LOG(LogRockyKombat, Log, TEXT("Match released; the front end has the screen again."));
}

void ARKCGameMode::TickFrontEnd(float DeltaSeconds)
{
    // A menu cannot be stepped by a frame so long that a single key press walks
    // several rows; a long frame is clamped rather than trusted.
    const float SafeDelta = FMath::Clamp(DeltaSeconds, 0.0f, 0.25f);
    ++FrontEndFramesTicked;

    ARKCPlayerController* PC = GetRKCPlayerController();
    if (PC != nullptr)
    {
        // The device layer follows the settings and the console switches every
        // frame, so the options screen can turn the pad on and off while it is
        // open and the change is visible immediately.
        const bool bWanted = ResolveTouchControlsEnabled(AudioSystem);
        if (PC->AreTouchControlsEnabled() != bWanted)
        {
            PC->SetTouchControlsEnabled(bWanted);
        }
        PC->SetTouchForcedVisible(GK_TouchForced != 0);
    }

    // ---- Real work, measured before the machine decides anything ------------
    RefreshLoadingProgress();
    const bool bWarmupComplete = IsRenderWarmupComplete();

    // ---- One frame of the machine ------------------------------------------
    if (!OnlineTestRole.IsEmpty() && Online) {
        OnlineTestAge += SafeDelta;
        if (OnlineTestAge > 120.0f || Online->ConsumeEnded()) {
            UE_LOG(LogRockyKombat, Error, TEXT("RKC_ONLINE_TEST_FAIL: %s"), *Online->Status);
            FPlatformMisc::RequestExit(true);
        }
        if (OnlineTestRole == TEXT("guest") && !Online->IncomingId.IsEmpty()) Online->Accept();
        if (OnlineTestRole == TEXT("host") && !bOnlineTestInviteSent && Online->IsConnected()) {
            for (const auto& Player : Online->Players) if (Player.Name == TEXT("RKC-Test-Guest") && Player.Fighter == 1 && !Player.bBusy) {
                Online->Invite(Player.Id); bOnlineTestInviteSent = true; break;
            }
        }
        if (OnlineTestRole == TEXT("guest") && Online->IsConnected() && !Online->IsInMatch()) Online->SelectFighter(1);
        if (FrontEnd.GetScreen() == RKCFrontEnd::EScreen::Results && !bOnlineTestRematchSent) {
            bOnlineTestRematchSent = true;
            const bool Passed = !bBotActive[0] && !bBotActive[1] && Setup.FighterA == RKC::ERoster::Rocky && Setup.FighterB == RKC::ERoster::Shadow && Sim.GetFighter(RKC::ESide::A).AttacksThrownTotal > 0 && Sim.GetFighter(RKC::ESide::B).AttacksThrownTotal > 0;
            UE_LOG(LogRockyKombat, Display, TEXT("RKC_ONLINE_MATCH_%s role=%s match=%d ticks=%d checksum=%08x Aattacks=%d Battacks=%d Ahits=%d Bhits=%d"),
                Passed ? TEXT("PASS") : TEXT("FAIL"), *OnlineTestRole, OnlineTestMatches, Sim.GetMatch().TickCount, ComputeStateChecksum(Sim), Sim.GetFighter(RKC::ESide::A).AttacksThrownTotal, Sim.GetFighter(RKC::ESide::B).AttacksThrownTotal, Sim.GetFighter(RKC::ESide::A).HitsLandedTotal, Sim.GetFighter(RKC::ESide::B).HitsLandedTotal);
            if (OnlineTestMatches >= 2 || !Passed) FPlatformMisc::RequestExit(false);
            else Online->Rematch();
        }
    }
    FrontEnd.Tick(SafeDelta, CollectFrontEndInput(), bWarmupComplete);

    // ---- What it asked for -------------------------------------------------
    if (FrontEnd.ConsumeMatchStartRequest())
    {
        BeginMatchFromFrontEnd();
    }

    if (FrontEnd.ConsumeRematchRequest())
    {
        if (FrontEnd.IsMatchPlayable())
        {
            // Restart from the pause screen: the machine is already back on the
            // match screen, so the new match begins on this frame.
            RestartMatch();
            bMatchEndReported = false;
            bMatchWasRunning = true;
            bWasSimulating = true;
            if (PC != nullptr)
            {
                PC->SetGameplayRouting(true);
                PC->SetInputEnabled(true);
            }
            FrontEnd.NotifyMatchStarted();
            UE_LOG(LogRockyKombat, Log, TEXT("Match restarted from the pause screen."));
        }
        else
        {
            // Rematch from the result card: the machine walks Loading -> Versus ->
            // Match and raises the start request itself, so restarting here would
            // start a match behind the loading card.
            UE_LOG(LogRockyKombat, Log,
                   TEXT("Rematch accepted; the match starts when the loading card releases."));
        }
    }

    if (FrontEnd.ConsumeQuitRequest())
    {
        RequestQuitMatch();
    }

    // ---- A finished match becomes the result card --------------------------
    // Reported before the routing below is recomputed, so the very first frame on
    // the result card already routes a touch as a menu tap rather than a punch.
    if (!bMatchEndReported && Sim.IsMatchOver())
    {
        const RKC::FMatchState& M = Sim.GetMatch();
        const bool bDraw = (M.Result == RKC::EMatchResult::Draw);
        const bool bSideAWon = (M.Result == RKC::EMatchResult::FighterAWins);
        const FString WinnerName =
            bDraw ? FString() : RKC::GetRosterDisplayName(bSideAWon ? Setup.FighterA : Setup.FighterB);

        FrontEnd.NotifyMatchEnded(bSideAWon, WinnerName);
        bMatchEndReported = true;
        bMatchWasRunning = false;
        UE_LOG(LogRockyKombat, Log, TEXT("Match end reported to the front end: %s."),
               WinnerName.IsEmpty() ? TEXT("draw") : *WinnerName);
    }

    // ---- Leaving the match for a menu releases it ---------------------------
    // Quit-to-menu from the pause screen, or leaving the result card, is the one
    // transition that abandons a match. It happens here, exactly once.
    if (bMatchWasRunning && FrontEnd.GetScreen() == RKCFrontEnd::EScreen::Menu)
    {
        ReturnToFrontEnd();
    }

    // ---- Screen-derived state ----------------------------------------------
    const bool bPaused = FrontEnd.IsGamePaused();
    if (bPaused && !bWasPaused)
    {
        // The machine has already paused the audio bed itself; this adds the
        // input half, which is the part that leaves a fighter walking.
        if (PC != nullptr)
        {
            PC->ClearAllHumanInput(TEXT("paused"));
        }
    }
    else if (!bPaused && bWasPaused && AudioSystem != nullptr)
    {
        AudioSystem->OnGameResumed();
    }
    bWasPaused = bPaused;

    const bool bSimulate = FrontEnd.ShouldSimulationRun();
    if (bWasSimulating && !bSimulate && PC != nullptr)
    {
        // Leaving gameplay drops every held key and every tracked finger, so
        // nothing a player was holding can fire on the way back in. The audio bed
        // is the front end's business from here and must not be frozen here.
        PC->ClearAllHumanInput(TEXT("left-gameplay"));
    }
    bWasSimulating = bSimulate;
    bFrontEndActive = FrontEnd.IsFrontEndScreen();

    if (PC != nullptr)
    {
        PC->SetGameplayRouting(FrontEnd.IsMatchPlayable());
        if (!bSimulate)
        {
            PC->SetInputEnabled(false);
        }
    }

    // ---- The match itself --------------------------------------------------
    if (bSimulate)
    {
        AdvanceMatchFrame(SafeDelta);
    }
}

// ---------------------------------------------------------------------------
// Stage capture (rkc.CaptureStages; evidence runs only)
// ---------------------------------------------------------------------------
// WHY THE GAME TAKES THE PICTURE AND NOT A SCRIPT OUTSIDE IT: a script that grabs
// the window can photograph the wrong window, a stale front buffer, or a black
// rectangle, and it cannot say what the HUD had drawn when it did. Asking the
// renderer for the shot from inside the frame, on the screen the front end has
// just moved to, ties every image to a named screen and to a HUD draw count
// taken at the same moment - so "the HUD draws" becomes a measurement.
//
// The plan is a table so the set of pictures is reviewable and cannot quietly
// shrink, and every stage is either photographed or recorded as missed.
// ---------------------------------------------------------------------------

namespace
{
/** One picture the evidence run has to bring home. */
struct FStageCaptureStep
{
    /** The front-end screen the picture is taken on. */
    RKCFrontEnd::EScreen Screen;
    /** File name without extension, and the manifest label. Numbered so the
     *  folder sorts itself into the order the player sees. */
    const TCHAR* Name;
    /** Seconds the screen must have been up before the shot is asked for, so no
     *  card is photographed on its first frame and the two stages that share the
     *  match screen stay separate pictures. */
    float MinAge;
    /** True for the stage that only counts when the on-screen pad is really being
     *  drawn, which needs rkc.TouchForced on a desktop build. */
    bool bNeedsTouchPad;
};

const FStageCaptureStep StageCapturePlan[] =
{
    {RKCFrontEnd::EScreen::Boot, TEXT("01_startup"), 0.15f, false},
    {RKCFrontEnd::EScreen::Loading, TEXT("02_loading"), 0.20f, false},
    {RKCFrontEnd::EScreen::Menu, TEXT("03_menu"), 0.40f, false},
    {RKCFrontEnd::EScreen::Versus, TEXT("04_versus"), 0.30f, false},
    {RKCFrontEnd::EScreen::Match, TEXT("05_match"), 1.20f, false},
    {RKCFrontEnd::EScreen::Match, TEXT("06_touchpad"), 3.50f, true},
    {RKCFrontEnd::EScreen::Results, TEXT("07_results"), 0.50f, false},
};

constexpr int32 StageCapturePlanCount =
    static_cast<int32>(sizeof(StageCapturePlan) / sizeof(StageCapturePlan[0]));

/** How long to wait for a stage that is not arriving before recording it as
 *  missed. Generous on purpose: it bounds an unattended run, it does not pace the
 *  game, and the first windowed run on a machine like this one spends minutes
 *  compiling shaders while the match that leads to the result card is still
 *  running. */
constexpr double StageCaptureStepTimeoutSeconds = 120.0;

/** After the last request the process is kept alive until the engine has had time
 *  to write the pending image: a few frames AND a wall-clock minimum, because a
 *  frame count alone means nothing on a machine that is not rendering slowly. */
constexpr int32 StageCaptureDrainFrames = 10;
constexpr double StageCaptureDrainSeconds = 1.5;

/** Regions the HUD must have reported for a stage to count as drawn. The front
 *  end emits one block per card; the match readout is the multi-panel layout. */
int32 StageCaptureMinimumRegions(const FStageCaptureStep& Step)
{
    return (Step.Screen == RKCFrontEnd::EScreen::Match) ? 5 : 1;
}
} // namespace

bool ARKCGameMode::IsTouchPadOnScreen() const
{
    const ARKCPlayerController* PC = GetRKCPlayerController();
    return (PC != nullptr) && PC->GetTouchController().ShouldDraw();
}

void ARKCGameMode::MaybeCaptureStageShots()
{
    if (GK_CaptureStages == 0 || !bFrontEndEnabled || bStageCaptureExiting)
    {
        return;
    }

    if (!bStageCaptureStarted)
    {
        bStageCaptureStarted = true;
        StageCaptureStartedAt = FDateTime::Now().ToString();
        StageCaptureDirectory = FPaths::ProjectSavedDir() / TEXT("Evidence/Stages") /
                                FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
        IFileManager::Get().MakeDirectory(*StageCaptureDirectory, true);
        StageCaptureStepDeadlineSeconds = FPlatformTime::Seconds() + StageCaptureStepTimeoutSeconds;

        UE_LOG(LogRockyKombat, Display,
               TEXT("stage capture: %d stage(s) requested into %s (autopilot %d, touch forced %d, "
                    "quick rules %d)"),
               StageCapturePlanCount, *StageCaptureDirectory, GK_FrontEndAuto, GK_TouchForced,
               GK_QuickRules);
        if (GK_TouchForced == 0)
        {
            UE_LOG(LogRockyKombat, Warning,
                   TEXT("stage capture: the touch-pad stage needs the pad on screen; on a desktop "
                        "build pass -rkc.TouchForced=1 or that stage will be recorded as missed."));
        }
        WriteStageCaptureManifest();
    }

    if (StageCaptureStep < StageCapturePlanCount)
    {
        const FStageCaptureStep& Step = StageCapturePlan[StageCaptureStep];
        const bool bReached = (FrontEnd.GetScreen() == Step.Screen) &&
                              (FrontEnd.GetScreenAge() >= Step.MinAge) &&
                              (!Step.bNeedsTouchPad || IsTouchPadOnScreen());
        if (bReached)
        {
            RequestStageShot(StageCaptureStep);
            ++StageCaptureStep;
            StageCaptureStepDeadlineSeconds =
                FPlatformTime::Seconds() + StageCaptureStepTimeoutSeconds;
            return;
        }

        if (FPlatformTime::Seconds() >= StageCaptureStepDeadlineSeconds)
        {
            // Recorded rather than passed over: an empty folder has to mean "never
            // reached", never "never looked for".
            ++StageCaptureMissed;
            StageCaptureRows.Add(FString::Printf(
                TEXT("%-12s  screen %-8s  MISSED (not reached within %.0f s)"), Step.Name,
                *RKCFrontEnd::FMachine::ScreenName(Step.Screen), StageCaptureStepTimeoutSeconds));
            UE_LOG(LogRockyKombat, Warning, TEXT("stage capture: %s was never reached"), Step.Name);
            WriteStageCaptureManifest();
            ++StageCaptureStep;
            StageCaptureStepDeadlineSeconds =
                FPlatformTime::Seconds() + StageCaptureStepTimeoutSeconds;
            return;
        }
        return;
    }

    // ---- Everything asked for: let the engine write the last image ----------
    ++StageCaptureFramesSinceShot;
    const bool bDrained = (StageCaptureFramesSinceShot >= StageCaptureDrainFrames) &&
                          ((FPlatformTime::Seconds() - StageCaptureLastShotSeconds) >=
                           StageCaptureDrainSeconds);
    if (!bDrained)
    {
        return;
    }

    bStageCaptureExiting = true;
    WriteStageCaptureManifest();

    UE_LOG(LogRockyKombat, Display,
           TEXT("stage capture complete: %d of %d stage(s) photographed, %d of those with the HUD "
                "drawing, %d missed; manifest %s"),
           StageCaptureRequested, StageCapturePlanCount, StageCaptureHudDrew, StageCaptureMissed,
           *(StageCaptureDirectory / TEXT("manifest.txt")));

    // Only value 1 leaves: an interactive run that wants the screenshots and the
    // window still open asks for 2.
    if (GK_CaptureStages == 1)
    {
        FGenericPlatformMisc::RequestExitWithStatus(true, 0);
    }
}

void ARKCGameMode::RequestStageShot(int32 StepIndex)
{
    const FStageCaptureStep& Step = StageCapturePlan[StepIndex];

    ARKCPlayerController* PC = GetRKCPlayerController();
    ARKCHUD* Hud = (PC != nullptr) ? Cast<ARKCHUD>(PC->GetHUD()) : nullptr;
    const int32 HudFrames = (Hud != nullptr) ? Hud->GetDrawnFrameCount() : -1;
    const int32 HudRegions = (Hud != nullptr) ? Hud->GetLastRegionCount() : -1;
    const FIntPoint HudCanvas =
        (Hud != nullptr) ? Hud->GetLastCanvasSize() : FIntPoint::ZeroValue;

    // The HUD's counters are read one frame behind the request, which is exactly
    // what is wanted: they describe the frame that is already on screen. Growth
    // since the previous stage is required as well, so a stage cannot pass on a
    // counter that was already high when the run started.
    const int32 HudFramesSinceLastStage = (HudFrames >= 0) ? (HudFrames - StageCaptureLastHudFrames) : -1;
    if (HudFrames >= 0)
    {
        StageCaptureLastHudFrames = HudFrames;
    }

    const FString ShotPath = StageCaptureDirectory / (FString(Step.Name) + TEXT(".png"));
    FScreenshotRequest::RequestScreenshot(ShotPath, /*bInShowUI=*/true,
                                          /*bAddFilenameSuffix=*/false);

    ++StageCaptureRequested;
    StageCaptureFramesSinceShot = 0;
    StageCaptureLastShotSeconds = FPlatformTime::Seconds();

    const bool bPadVisible = IsTouchPadOnScreen();
    const bool bHudDrew = (HudFramesSinceLastStage >= 1) &&
                          (HudRegions >= StageCaptureMinimumRegions(Step));
    if (bHudDrew)
    {
        ++StageCaptureHudDrew;
    }

    StageCaptureRows.Add(FString::Printf(
        TEXT("%-12s  screen %-8s  age %5.2f s  shot %s\n")
        TEXT("              HUD %s: draw passes since the previous stage %d, layout regions %d, "
             "canvas %dx%d%s\n"),
        Step.Name, *RKCFrontEnd::FMachine::ScreenName(Step.Screen), FrontEnd.GetScreenAge(),
        *FPaths::ConvertRelativePathToFull(ShotPath), bHudDrew ? TEXT("DREW") : TEXT("DID NOT DRAW"),
        HudFramesSinceLastStage, HudRegions, HudCanvas.X, HudCanvas.Y,
        (!Step.bNeedsTouchPad || bPadVisible)
            ? TEXT("")
            : TEXT("  [the pad was not being drawn: pass -rkc.TouchForced=1]")));

    UE_LOG(LogRockyKombat, Display,
           TEXT("stage capture %s: %s at age %.2f s, HUD %s (draw passes since previous stage %d, "
                "regions %d, canvas %dx%d)"),
           Step.Name, *RKCFrontEnd::FMachine::ScreenName(Step.Screen), FrontEnd.GetScreenAge(),
           bHudDrew ? TEXT("drew") : TEXT("did not draw"), HudFramesSinceLastStage, HudRegions,
           HudCanvas.X, HudCanvas.Y);

    WriteStageCaptureManifest();
}

void ARKCGameMode::WriteStageCaptureManifest()
{
    if (StageCaptureDirectory.IsEmpty())
    {
        return;
    }

    const bool bComplete = (StageCaptureStep >= StageCapturePlanCount);

    FString Text;
    Text += TEXT("Rocky Kombat - presentation stage capture\n");
    Text += TEXT("\n");
    Text += TEXT("Every image in this folder was requested by the game itself, on the frame the named\n");
    Text += TEXT("screen was up, and each row records what the HUD reported at that moment. The run\n");
    Text += TEXT("is the real front end and the real match: the capture adds no input and cannot\n");
    Text += TEXT("change a screen.\n");
    Text += TEXT("\n");
    Text += FString::Printf(TEXT("folder:   %s\n"),
                            *FPaths::ConvertRelativePathToFull(StageCaptureDirectory));
    Text += FString::Printf(TEXT("started:  %s\n"), *StageCaptureStartedAt);
    Text += FString::Printf(TEXT("written:  %s\n"), *FDateTime::Now().ToString());
    Text += FString::Printf(TEXT("engine:   %s\n"), *FEngineVersion::Current().ToString());
    Text += FString::Printf(TEXT("command:  %s\n"), FCommandLine::Get());
    Text += FString::Printf(TEXT("settings: rkc.FrontEndAuto=%d (scripted menu, %.0f s per screen), "
                                 "rkc.TouchForced=%d, rkc.QuickRules=%d, rkc.FrontEnd=%d\n"),
                            GK_FrontEndAuto, (GK_FrontEndAuto > 1) ? float(GK_FrontEndAuto) : 1.2f,
                            GK_TouchForced, GK_QuickRules, GK_FrontEnd);

    // The acceptance question for this arena is "is exactly one ring on screen?", so
    // the manifest records what the arena itself reports about that, together with the
    // two screen positions whose closeness proves the fighters' feet are on the
    // picture's painted floor line.
    if (Arena != nullptr)
    {
        Text += FString::Printf(
            TEXT("arena:    picture %.0f x %.0f cm (aspect %.4f, source 1672 x 941 = %.4f) framed for ")
            TEXT("view aspect %.3f; painted floor line Z %.1f; feet at %+.4f of the half-frame, ")
            TEXT("picture floor line at %+.4f; narrowest cover margin %.0f cm; ")
            TEXT("imported image %s, procedural greybox ring %s\n"),
            Arena->GetPictureSizeCm().X, Arena->GetPictureSizeCm().Y,
            (Arena->GetPictureSizeCm().Y > 0.0f)
                ? (Arena->GetPictureSizeCm().X / Arena->GetPictureSizeCm().Y)
                : 0.0f,
            1672.0f / 941.0f, Arena->GetFramedAspect(), Arena->GetFloorLineZ(),
            Arena->GetFeetScreenPosition(), Arena->GetImageFloorScreenPosition(),
            Arena->GetCoverMarginCm(),
            Arena->IsUsingArtBackdrop() ? TEXT("in use") : TEXT("NOT IN USE"),
            Arena->IsGreyboxVisible() ? TEXT("VISIBLE (diagnostic)") : TEXT("hidden"));
    }

    Text += TEXT("\n--- stages ---\n");
    if (StageCaptureRows.Num() == 0)
    {
        Text += TEXT("(none yet)\n");
    }
    for (const FString& Row : StageCaptureRows)
    {
        Text += Row;
    }

    Text += TEXT("\n--- result ---\n");
    Text += FString::Printf(TEXT("photographed %d of %d stage(s); HUD drawing reported on %d; missed %d\n"),
                            StageCaptureRequested, StageCapturePlanCount, StageCaptureHudDrew,
                            StageCaptureMissed);
    Text += FString::Printf(TEXT("run: %s\n"),
                            bComplete ? TEXT("the plan is complete")
                                      : TEXT("still in progress when this file was written"));

    const FString ManifestPath = StageCaptureDirectory / TEXT("manifest.txt");
    if (!FFileHelper::SaveStringToFile(Text, *ManifestPath))
    {
        UE_LOG(LogRockyKombat, Error, TEXT("stage capture could not write its manifest to %s"),
               *ManifestPath);
    }
}

void ARKCGameMode::PlayAudioForEvent(const RKC::FCombatEventRecord& Event)
{
    if (AudioSystem == nullptr || !AudioSystem->IsInitialised())
    {
        return;
    }

    // Match end is the one event the front end also voices: the result card
    // raises its own victory or defeat stinger, and feeding the event as well
    // would double it. With no front end there is no result card, so the event is
    // the only source of that cue and is needed.
    if (Event.Type == RKC::ECombatEvent::MatchEnded && bFrontEndEnabled)
    {
        return;
    }

    const bool bEventSideIsA = (Event.Side == RKC::ESide::A);
    const RKC::ERoster ActorRoster = bEventSideIsA ? Setup.FighterA : Setup.FighterB;
    const RKC::ERoster OtherRoster = bEventSideIsA ? Setup.FighterB : Setup.FighterA;

    FRKCEventContext Context;
    Context.Type = Event.Type;
    Context.Side = Event.Side;
    Context.Action = Event.Action;
    Context.Value = Event.Value;
    Context.Tick = Sim.GetMatch().TickCount;

    if (IsDefensiveEvent(Event.Type))
    {
        // The event names the fighter it happened to, so the planner gets that
        // fighter as the defender and the other one as the attacker.
        Context.Attacker = OtherRoster;
        Context.Defender = ActorRoster;
    }
    else
    {
        Context.Attacker = ActorRoster;
        Context.Defender = OtherRoster;
    }

    if (Event.Type == RKC::ECombatEvent::MatchEnded)
    {
        // The planner reads Value as "the winning side", and the simulation puts
        // an EMatchResult there instead, so it is translated rather than passed
        // through. A draw reports as the neutral case, which the planner voices as
        // the losing sting; the result card is the screen that says "DRAW".
        Context.Value = (Event.Side == RKC::ESide::A) ? 1.0f : 0.0f;
    }

    AudioSystem->PlayEvent(Context);
}

void ARKCGameMode::DropHumanInputAndFreezeAudio(const TCHAR* Reason)
{
    if (ARKCPlayerController* PC = GetRKCPlayerController())
    {
        const int32 Dropped = PC->ClearAllHumanInput(Reason);
        UE_LOG(LogRockyKombat, Verbose, TEXT("Lifecycle '%s': %d touch(es) cancelled."),
               Reason ? Reason : TEXT("unspecified"), Dropped);
    }
    if (AudioSystem != nullptr)
    {
        // Freezes the loops where they are instead of restarting them, so a
        // player who comes back hears the music carry on rather than start over.
        AudioSystem->OnGamePaused();
    }
}

void ARKCGameMode::HandleApplicationBackgrounded()
{
    // A phone going to the home screen, or a desktop window losing focus. While a
    // match is what would otherwise keep playing, the machine is moved to its
    // pause screen first, so the player comes back to a deliberate screen instead
    // of to a fight they could not see. The pause screen also means the resume
    // path is the same one the pause key uses, rather than a second one.
    if (bFrontEndEnabled && !FrontEnd.IsGamePaused() &&
        (FrontEnd.IsMatchPlayable() || FrontEnd.GetScreen() == RKCFrontEnd::EScreen::Results))
    {
        FrontEnd.OnPauseRequested();
    }

    DropHumanInputAndFreezeAudio(TEXT("application-backgrounded"));
    bWasPaused = FrontEnd.IsGamePaused();
    bWasSimulating = false;

    UE_LOG(LogRockyKombat, Log, TEXT("Application backgrounded: input dropped, audio frozen%s."),
           FrontEnd.IsGamePaused() ? TEXT(", match paused") : TEXT(""));
}

void ARKCGameMode::HandleApplicationForegrounded()
{
    // Never resume the audio on top of the pause screen: the machine owns the bed
    // while it is up, and a resume here would play arena music behind a menu.
    if (AudioSystem != nullptr && !FrontEnd.IsGamePaused())
    {
        AudioSystem->OnGameResumed();
    }

    // A key or a thumb held across the background period is not a press.
    if (ARKCPlayerController* PC = GetRKCPlayerController())
    {
        PC->ClearAllHumanInput(TEXT("application-foregrounded"));
    }

    UE_LOG(LogRockyKombat, Log, TEXT("Application foregrounded: input cleared%s."),
           FrontEnd.IsGamePaused() ? TEXT(", still on the pause screen") : TEXT(""));
}

void ARKCGameMode::HandleSlateActivationStateChanged(bool bIsActive)
{
    // Slate reports focus for the whole application. For a fighter, losing it is
    // the same event as being backgrounded, so both routes end here.
    if (bIsActive)
    {
        HandleApplicationForegrounded();
    }
    else
    {
        HandleApplicationBackgrounded();
    }
}

void ARKCGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    FrontEnd.SetOnline(nullptr);
    Online.Reset();
    // A scripted-input run leaves its record here, before the world starts coming
    // apart, so an unattended run always produces its evidence file even if it was
    // asked to quit early. WriteRecord keeps everything already recorded, so the
    // call in Tick (on a completed timeline) and this one cannot disagree.
    if (ScriptDriver.IsActive())
    {
        const FString ScriptRecordPath = ScriptDriver.WriteRecord(TEXT("end play"));
        UE_LOG(LogRockyKombat, Display, TEXT("Scripted input record: %s"), *ScriptRecordPath);
    }

    // Delegates first: a global hook must not be able to reach this object after
    // the world it belongs to has started coming apart.
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundedDelegateHandle);
    FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundedDelegateHandle);
    FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(DeactivatedDelegateHandle);
    FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(ReactivatedDelegateHandle);
    if (ActivationStateDelegateHandle.IsValid() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationStateDelegateHandle);
    }
    BackgroundedDelegateHandle.Reset();
    ForegroundedDelegateHandle.Reset();
    DeactivatedDelegateHandle.Reset();
    ReactivatedDelegateHandle.Reset();
    ActivationStateDelegateHandle.Reset();

    if (AudioSystem != nullptr)
    {
        AudioSystem->Shutdown();
    }

    Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// Per-frame director
// ---------------------------------------------------------------------------

/** Logs one census of every live PaperFlipbook component in the world.
 *
 *  Inert unless rkc.SpriteDump is non-zero. Each line carries the owner actor
 *  (class and name), the component name, the world location, the flipbook asset,
 *  the material and the two visibility flags, which together are enough to tell
 *  a stale leftover actor apart from a second component on a live fighter, and to
 *  tell a missing material apart from a correctly textured sprite. */
static void DumpSpriteCensus(UWorld* World, int32 Index)
{
    if (World == nullptr)
    {
        return;
    }

    int32 Sprites = 0;
    int32 Prims = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (Actor == nullptr)
        {
            continue;
        }

        // Every primitive component, not just flipbooks: a duplicate fighter drawn
        // from a UPaperSpriteComponent or a mesh would be invisible to a
        // flipbook-only census, which is exactly the hole a black twin could hide in.
        TArray<UPrimitiveComponent*> Components;
        Actor->GetComponents<UPrimitiveComponent>(Components);
        for (UPrimitiveComponent* Prim : Components)
        {
            if (Prim == nullptr || !Prim->IsVisible())
            {
                continue;
            }
            ++Prims;
            // Non-const: GetFlipbook() is a non-const accessor on the component.
            UPaperFlipbookComponent* Sprite = Cast<UPaperFlipbookComponent>(Prim);
            // Non-const: GetSprite() is a non-const accessor on the component.
            UPaperSpriteComponent* Single = Cast<UPaperSpriteComponent>(Prim);
            const UMaterialInterface* Mat = Prim->GetMaterial(0);
            const FVector Where = Prim->GetComponentLocation();

            // Where that component actually lands on screen. A picture cannot name a
            // component, but a projected rectangle can be matched to a shape on it, so
            // a stray silhouette is attributed instead of guessed at.
            FString OnScreen;
            if (APlayerController* ProjectingPC = World->GetFirstPlayerController())
            {
                const FBox Box = Prim->Bounds.GetBox();
                const FVector Mid(
                    (Box.Min.X + Box.Max.X) * 0.5, (Box.Min.Y + Box.Max.Y) * 0.5, 0.0);
                FVector2D Feet;
                FVector2D Head;
                const bool bFeet =
                    ProjectingPC->ProjectWorldLocationToScreen(FVector(Mid.X, Mid.Y, Box.Min.Z), Feet);
                const bool bHead =
                    ProjectingPC->ProjectWorldLocationToScreen(FVector(Mid.X, Mid.Y, Box.Max.Z), Head);
                if (bFeet && bHead)
                {
                    OnScreen = FString::Printf(TEXT(" px=(%.0f,%.0f)-(%.0f,%.0f)"), Feet.X, Feet.Y,
                                               Head.X, Head.Y);
                }
            }
            if (Sprite != nullptr)
            {
                ++Sprites;
                const UPaperFlipbook* Book = Sprite->GetFlipbook();
                UE_LOG(LogRockyKombat, Display,
                       TEXT("census %d.%d: actor=%s (%s) comp=%s(flipbook) at (%.1f, %.1f, %.1f) "
                            "book=%s frame=%d mat=%s hiddenInGame=%d bounds=%s%s"),
                       Index, Prims, *Actor->GetName(), *Actor->GetClass()->GetName(),
                       *Prim->GetName(), Where.X, Where.Y, Where.Z,
                       Book ? *Book->GetName() : TEXT("(none)"),
                       Sprite->GetPlaybackPositionInFrames(),
                       Mat ? *Mat->GetName() : TEXT("(none)"), Prim->bHiddenInGame ? 1 : 0,
                       *Prim->Bounds.GetBox().ToString(), *OnScreen);
            }
            else
            {
                // Non-flipbook primitives only matter when they are sprite-like or
                // mesh-like and near the ring, so the class name carries the meaning.
                UE_LOG(LogRockyKombat, Display,
                       TEXT("census %d.%d: actor=%s (%s) comp=%s(%s) at (%.1f, %.1f, %.1f) "
                            "sprite=%s mat=%s hiddenInGame=%d bounds=%s%s"),
                       Index, Prims, *Actor->GetName(), *Actor->GetClass()->GetName(),
                       *Prim->GetName(), *Prim->GetClass()->GetName(), Where.X, Where.Y, Where.Z,
                       Single && Single->GetSprite() ? *Single->GetSprite()->GetName() : TEXT("(none)"),
                       Mat ? *Mat->GetName() : TEXT("(none)"), Prim->bHiddenInGame ? 1 : 0,
                       *Prim->Bounds.GetBox().ToString(), *OnScreen);
            }
        }
    }

    UE_LOG(LogRockyKombat, Display,
           TEXT("census %d: %d visible primitive(s), of which %d flipbook(s), in %d actor(s)%s"),
           Index, Prims, Sprites, World->GetActorCount(),
           (Sprites > 2) ? TEXT(" -- more than the two fighters, so something else draws a fighter")
                         : TEXT(""));
}

/** Logs every imported flipbook frame and the texture it draws with, once.
 *
 *  WHY IT EXISTS: a sprite frame whose source texture is missing draws as a solid
 *  black shape - the same silhouette as the sprite, in the right place, with no
 *  light or shadow involved. That is indistinguishable on screen from a second
 *  actor, so the question "is the black figure another actor or an untextured
 *  frame?" is answered from the assets instead of from a picture. Inert unless
 *  rkc.ArtAudit is non-zero. */
static void DumpArtAudit()
{
    const RKC::ERoster Rosters[] = { RKC::ERoster::Rocky, RKC::ERoster::Shadow };
    for (RKC::ERoster Roster : Rosters)
    {
        const RKCArt::FArtSet Set = RKCArt::LoadArtSet(Roster);
        const TCHAR* const Names[] = { TEXT("Idle"), TEXT("Walk"), TEXT("Jab"), TEXT("HeavyPunch"),
                                       TEXT("Block"), TEXT("Dodge"), TEXT("Knockdown"),
                                       TEXT("Victory") };
        UPaperFlipbook* const Books[] = { Set.Idle, Set.Walk, Set.Jab, Set.HeavyPunch,
                                          Set.Block, Set.Dodge, Set.Knockdown, Set.Victory };
        for (int32 Anim = 0; Anim < UE_ARRAY_COUNT(Books); ++Anim)
        {
            UPaperFlipbook* Book = Books[Anim];
            if (Book == nullptr)
            {
                UE_LOG(LogRockyKombat, Display, TEXT("art audit: %s %s MISSING"),
                       *RKC::GetRosterDisplayName(Roster), Names[Anim]);
                continue;
            }

            const int32 FrameCount = Book->GetNumFrames();
            int32 Untextured = 0;
            for (int32 Frame = 0; Frame < FrameCount; ++Frame)
            {
                UPaperSprite* const FrameSprite = Book->GetSpriteAtFrame(Frame);
                // Source texture access is editor-only; the baked texture is
                // what Android and other cooked targets actually render.
                UTexture2D* const Tex = FrameSprite ? FrameSprite->GetBakedTexture() : nullptr;
                if (Tex == nullptr)
                {
                    ++Untextured;
                    UE_LOG(LogRockyKombat, Warning,
                           TEXT("art audit: %s %s frame %d/%d (%s) has NO TEXTURE - it draws black"),
                           *RKC::GetRosterDisplayName(Roster), Names[Anim], Frame, FrameCount,
                           FrameSprite ? *FrameSprite->GetName() : TEXT("(no sprite)"));
                }
            }
            UE_LOG(LogRockyKombat, Display,
                   TEXT("art audit: %s %s: %d frame(s), %d without a texture"),
                   *RKC::GetRosterDisplayName(Roster), Names[Anim], FrameCount, Untextured);
        }
    }
}

void ARKCGameMode::TickLiveShots(float DeltaSeconds)
{
    if (GK_LiveShots <= 0 || LiveShotsTaken >= GK_LiveShots)
    {
        return;
    }

    LiveShotsElapsed += DeltaSeconds;

    // One second between shots, with the first taken as soon as the switch is read
    // so a short run still yields a picture.
    const bool bFirst = (LiveShotsTaken == 0);
    if (!bFirst && LiveShotsElapsed < 1.0f)
    {
        return;
    }
    LiveShotsElapsed = 0.0f;

    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Evidence") / TEXT("LiveShots");
    IFileManager::Get().MakeDirectory(*Directory, true);

    const FString ShotPath =
        Directory / FString::Printf(TEXT("live_%03d.png"), LiveShotsTaken + 1);
    FScreenshotRequest::RequestScreenshot(ShotPath, /*bInShowUI=*/true,
                                          /*bAddFilenameSuffix=*/false);

    ++LiveShotsTaken;

    // Record what the world said at the instant the frame was asked for, so the
    // picture can be tied to named components instead of read as a shape. The
    // fighter positions are the simulation's own, which is what decides whether a
    // second figure on screen belongs to a fighter actor at all.
    FString Row = FString::Printf(TEXT("shot %03d at frame %d, screen %s, world time %.2f s"),
                                  LiveShotsTaken, GFrameCounter,
                                  *RKCFrontEnd::FMachine::ScreenName(FrontEnd.GetScreen()),
                                  GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);
    if (const ARKCFighter* A = GetFighterActor(RKC::ESide::A))
    {
        const RKC::FFighterState& S = Sim.GetFighter(RKC::ESide::A);
        Row += FString::Printf(TEXT("\n    A %s x=%.1f sprite=%s visible=%s"),
                               *RKC::GetRosterDisplayName(A->GetRoster()), S.PositionX,
                               *A->GetCurrentArtName(),
                               (A->GetSpriteComponent() &&
                                A->GetSpriteComponent()->IsVisible()) ? TEXT("yes") : TEXT("no"));
    }
    if (const ARKCFighter* B = GetFighterActor(RKC::ESide::B))
    {
        const RKC::FFighterState& S = Sim.GetFighter(RKC::ESide::B);
        Row += FString::Printf(TEXT("\n    B %s x=%.1f sprite=%s visible=%s"),
                               *RKC::GetRosterDisplayName(B->GetRoster()), S.PositionX,
                               *B->GetCurrentArtName(),
                               (B->GetSpriteComponent() &&
                                B->GetSpriteComponent()->IsVisible()) ? TEXT("yes") : TEXT("no"));
    }

    LiveShotRows.Add(Row);
    FFileHelper::SaveStringArrayToFile(LiveShotRows, *(Directory / TEXT("LiveShots.txt")));

    UE_LOG(LogRockyKombat, Display, TEXT("live shot %d/%d written to %s"), LiveShotsTaken,
           GK_LiveShots, *FPaths::ConvertRelativePathToFull(ShotPath));
}

void ARKCGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!bWorldBuilt)
    {
        return;
    }

    // Evidence runs only, and inert otherwise: the game asks the renderer for its
    // own screenshots while an ordinary match is played, so anything seen through
    // an external window grab can be confirmed against the rendered frame itself.
    TickLiveShots(DeltaSeconds);

    // Diagnostic only, and off by default: every flipbook frame and the texture it
    // draws with, once, so an untextured frame is named rather than reconstructed.
    if (GK_ArtAudit != 0)
    {
        static bool bArtAuditDone = false;
        if (!bArtAuditDone)
        {
            bArtAuditDone = true;
            DumpArtAudit();
        }
    }

    // Diagnostic only, and off by default: a census of the sprites that are
    // actually alive in the world, so a frame that shows two fighters when one
    // actor was spawned can be attributed to a named component instead of a guess.
    if (GK_SpriteDump != 0)
    {
        static float SinceLastCensus = 0.0f;
        static int32 CensusIndex = 0;
        SinceLastCensus += DeltaSeconds;
        if (SinceLastCensus >= FMath::Max(GK_SpriteDumpInterval, 0.05f))
        {
            SinceLastCensus = 0.0f;
            DumpSpriteCensus(GetWorld(), ++CensusIndex);
        }
    }

    // Diagnostic only, and off by default: hide one side's sprite so a shape seen on
    // screen can be tied to a named component - or shown to be no fighter at all,
    // because it survives hiding both. Re-applied every frame because a rebuilt match
    // creates new components.
    if (GK_HideSide != 0)
    {
        if (ARKCFighter* HiddenA = GetFighterActor(RKC::ESide::A))
        {
            if (UPaperFlipbookComponent* SpriteA = HiddenA->GetSpriteComponent())
            {
                SpriteA->SetVisibility((GK_HideSide & 1) == 0);
            }
        }
        if (ARKCFighter* HiddenB = GetFighterActor(RKC::ESide::B))
        {
            if (UPaperFlipbookComponent* SpriteB = HiddenB->GetSpriteComponent())
            {
                SpriteB->SetVisibility((GK_HideSide & 2) == 0);
            }
        }
    }

    // Scripted input first, before anything reads a latch this frame: the driver
    // hands one synthesised key event to APlayerController::InputKey, which is the
    // same entry point a real keyboard reaches through, so whatever the driver
    // presses travels the binding table and the controller's handlers exactly as a
    // human press would. Inert unless -rkc.Script / -rkc.ScriptFile was passed.
    ScriptDriver.Tick(*this, DeltaSeconds);

    // The arena picture stands behind every screen this game has - startup, loading,
    // menu, versus, match, results - so it is framed before anything else this frame
    // looks at the camera, and it is framed whether or not the front end owns the
    // screen.
    UpdateArenaFraming(DeltaSeconds);

    ARKCPlayerController* PC = GetRKCPlayerController();

    // ---- Menu input, before the simulation sees anything -------------------
    if (PC)
    {
        if (PC->ConsumeFrameDataToggle())
        {
            SetFrameDataVisible(!bFrameDataVisible);
        }
        // Always drained, even when the front end owns the screen, so a key
        // pressed at the wrong moment cannot fire at a match later.
        const bool bQuickRematch = PC->ConsumeRematchRequest();
        if (bQuickRematch && !bFrontEndActive)
        {
            UE_LOG(LogRockyKombat, Log, TEXT("Rematch requested."));
            RestartMatch();
        }
        else if (bQuickRematch)
        {
            UE_LOG(LogRockyKombat, Log,
                   TEXT("Rematch key ignored: the front end owns the screen, so its own "
                        "result card is the only place a rematch can be chosen."));
        }
        if (PC->ConsumeQuitRequest())
        {
            RequestQuitMatch();
        }
    }

    // A self-test run has already driven its matches by the time the first frame
    // arrives; from then on the frames exist only so the renderer and the HUD can
    // do their work before the counters are read. Stepping a finished match again
    // would add nothing and could age the event feed out from under the check.
    if (bSelfTestActive && bSelfTestFinished)
    {
        if (!bSelfTestExitScheduled)
        {
            ++SelfTestFramesSinceMatches;
            if (SelfTestFramesSinceMatches >= SelfTestFramesToAwaitHud)
            {
                FinishSelfTestAndExit();
            }
        }
        return;
    }

    // ---- The front end owns the screen -------------------------------------
    // While it does, the simulation cannot advance at all: the menus, the loading
    // card and the result card are the only things running, and which of those is
    // up is decided in one place.
    if (bFrontEndEnabled)
    {
        TickFrontEnd(DeltaSeconds);

        // The evidence run photographs the stages the front end has just moved
        // through. It adds no input and changes no screen: it only asks the
        // renderer for a picture of what is already there.
        MaybeCaptureStageShots();
        return;
    }

    AdvanceMatchFrame(DeltaSeconds);
}

void ARKCGameMode::AdvanceMatchFrame(float DeltaSeconds)
{
    ARKCPlayerController* PC = GetRKCPlayerController();

    // Human input is only honoured while the fight is live. The latches are still
    // drained every frame so a press made during the countdown or on the result
    // screen cannot be replayed into the next round.
    const bool bFighting = Sim.IsFighting();
    if (PC)
    {
        PC->SetInputEnabled(bFighting);
    }

    // ---- 1. input ----------------------------------------------------------
    RKC::FInputState InA;
    RKC::FInputState InB;
    GatherInputs(DeltaSeconds, InA, InB);

    // ---- 2. simulation -----------------------------------------------------
    const RKC::FFighterState& BeforeA = Sim.GetFighter(RKC::ESide::A);
    const RKC::FFighterState& BeforeB = Sim.GetFighter(RKC::ESide::B);
    const int32 HitsBeforeA = BeforeA.HitsLandedTotal;
    const int32 HitsBeforeB = BeforeB.HitsLandedTotal;
    const int32 BlocksBeforeA = BeforeA.BlocksThisRound;
    const int32 BlocksBeforeB = BeforeB.BlocksThisRound;
    const int32 DodgesBeforeA = BeforeA.DodgesThisRound;
    const int32 DodgesBeforeB = BeforeB.DodgesThisRound;
    const int32 AttacksBeforeA = BeforeA.AttacksThrownTotal;
    const int32 AttacksBeforeB = BeforeB.AttacksThrownTotal;
    const int32 KnockdownsBeforeA = BeforeA.KnockdownsTotal;
    const int32 KnockdownsBeforeB = BeforeB.KnockdownsTotal;

    if (bOnlineMatch && Online) {
        Online->TickInput(DeltaSeconds, Online->Side == 0 ? InA : InB);
        FRKCOnlineFrame Frame;
        int32 Stepped = 0;
        while (Stepped < 4 && Online->PopFrame(Frame)) {
            Sim.StepFixed(Frame.A, Frame.B); ++Stepped;
            if ((Frame.Seq+1)%60 == 0) Online->SendHash(Frame.Seq+1, ComputeStateChecksum(Sim));
        }
        DeltaSeconds = Stepped * RKC::FCombatSim::FixedTickSeconds;
    }
    else Sim.Advance(DeltaSeconds, InA, InB);

    // ---- integrity monitor: the anti-overlap rule, measured every tick ------
    const RKC::FFighterState& PostA = Sim.GetFighter(RKC::ESide::A);
    const RKC::FFighterState& PostB = Sim.GetFighter(RKC::ESide::B);
    const float Separation = FMath::Abs(PostA.PositionX - PostB.PositionX);
    const float Required = Sim.GetRules().MinSeparation;
    if (Separation < Required)
    {
        WorstSeparationViolation = FMath::Max(WorstSeparationViolation, Required - Separation);
    }

    TotalHitsA += FMath::Max(0, PostA.HitsLandedTotal - HitsBeforeA);
    TotalHitsB += FMath::Max(0, PostB.HitsLandedTotal - HitsBeforeB);
    TotalBlocksA += FMath::Max(0, PostA.BlocksThisRound - BlocksBeforeA);
    TotalBlocksB += FMath::Max(0, PostB.BlocksThisRound - BlocksBeforeB);
    TotalDodgesA += FMath::Max(0, PostA.DodgesThisRound - DodgesBeforeA);
    TotalDodgesB += FMath::Max(0, PostB.DodgesThisRound - DodgesBeforeB);
    TotalAttacksA += FMath::Max(0, PostA.AttacksThrownTotal - AttacksBeforeA);
    TotalAttacksB += FMath::Max(0, PostB.AttacksThrownTotal - AttacksBeforeB);
    TotalKnockdownsA += FMath::Max(0, PostA.KnockdownsTotal - KnockdownsBeforeA);
    TotalKnockdownsB += FMath::Max(0, PostB.KnockdownsTotal - KnockdownsBeforeB);

    TicksSimulated = Sim.GetMatch().TickCount;

    // ---- optional position trace -------------------------------------------
    if (GK_LogPositions != 0)
    {
        PositionTraceCountdown -= DeltaSeconds;
        if (PositionTraceCountdown <= 0.0f)
        {
            PositionTraceCountdown = 1.0f;
            const float TraceSeparation = FMath::Abs(PostA.PositionX - PostB.PositionX);
            UE_LOG(LogRockyKombat, Log,
                   TEXT("rtrace tick %d | A x %+.0f cm facing %+d health %.0f stamina %.0f "
                        "state %d action %d | B x %+.0f cm facing %+d health %.0f stamina %.0f "
                        "state %d action %d | separation %.0f cm (rule %.0f, arena [%.0f, %.0f], "
                        "pair midpoint %+.0f)"),
                   TicksSimulated, PostA.PositionX, PostA.Facing, PostA.Health, PostA.Stamina,
                   static_cast<int32>(PostA.State), static_cast<int32>(PostA.CurrentAction),
                   PostB.PositionX, PostB.Facing, PostB.Health, PostB.Stamina,
                   static_cast<int32>(PostB.State), static_cast<int32>(PostB.CurrentAction),
                   TraceSeparation, Required, Sim.GetRules().ArenaMinX, Sim.GetRules().ArenaMaxX,
                   (PostA.PositionX + PostB.PositionX) * 0.5f);
        }
    }

    // ---- 3. events ---------------------------------------------------------
    DrainEvents();

    // ---- 3b. presentation-only audio ---------------------------------------
    // Footfalls and the pre-round bell have no simulation event behind them, so
    // they are measured here from the state the simulation just produced. Still
    // after the advance, still presentation only.
    DrivePresentationAudio();

    // ---- 4. presentation ---------------------------------------------------
    if (FighterA)
    {
        FighterA->SyncFromSim();
    }
    if (FighterB)
    {
        FighterB->SyncFromSim();
    }

    // ---- 5. banner and feed ------------------------------------------------
    UpdateBanner(DeltaSeconds);

    for (FRKCEventFeedLine& Line : EventFeed)
    {
        Line.AgeSeconds += DeltaSeconds;
    }
    EventFeed.RemoveAll([](const FRKCEventFeedLine& Line) { return Line.AgeSeconds > 6.0f; });

    // ---- match end ---------------------------------------------------------
    if (Sim.IsMatchOver())
    {
        if (!bEvidenceWritten)
        {
            OnMatchEnded();
        }
        SecondsSinceMatchEnd += DeltaSeconds;
    }
    else
    {
        SecondsSinceMatchEnd = -1.0f;
    }
}

void ARKCGameMode::GatherInputs(float DeltaSeconds, RKC::FInputState& OutA, RKC::FInputState& OutB)
{
    ARKCPlayerController* PC = GetRKCPlayerController();

    // Drain both human latches unconditionally, even for bot-driven sides, so a
    // human pressing the other player's keys cannot accumulate a stale press.
    const RKC::FInputState HumanA = PC ? PC->ConsumeInputForSide(RKC::ESide::A) : RKC::FInputState();
    const RKC::FInputState HumanB = PC ? PC->ConsumeInputForSide(RKC::ESide::B) : RKC::FInputState();

    OutA = bBotActive[0] ? BotBrains[0].ComputeInput(DeltaSeconds, Sim) : HumanA;
    OutB = bBotActive[1] ? BotBrains[1].ComputeInput(DeltaSeconds, Sim) : HumanB;
    // Explicit automation switch only: real matches always consume device input.
    if (bOnlineMatch && Online && !OnlineTestRole.IsEmpty()) {
        auto& In = Online->Side == 0 ? OutA : OutB;
        const int32 Tick = Sim.GetMatch().TickCount;
        const auto& Me = Sim.GetFighter(Online->Side == 0 ? RKC::ESide::A : RKC::ESide::B);
        if (FMath::Abs(Sim.GetFighter(RKC::ESide::A).PositionX-Sim.GetFighter(RKC::ESide::B).PositionX)>95) {
            In.bMoveRight=Online->Side==0; In.bMoveLeft=Online->Side==1;
        }
        In.bHoldBlock=Tick%140>100;
        In.bJabPressed=Tick%17==0; In.bHeavyPressed=Tick%43==0;
        In.bDodgePressed=Tick%181==0 && Me.Stamina>30;
    }
}

void ARKCGameMode::DrainEvents()
{
    auto NameOf = [this](RKC::ESide Side) -> FString
    {
        return RKC::GetRosterDisplayName(Side == RKC::ESide::A ? Setup.FighterA : Setup.FighterB);
    };

    RKC::FCombatEventRecord Ev;
    while (Sim.PopEvent(Ev))
    {
        switch (Ev.Type)
        {
        case RKC::ECombatEvent::AttackStarted:
            PushFeed(FString::Printf(TEXT("%s throws %s"), *NameOf(Ev.Side),
                                     Ev.Action == RKC::EAction::HeavyPunch ? TEXT("a heavy")
                                                                          : TEXT("a jab")));
            break;

        case RKC::ECombatEvent::AttackHit:
            PushFeed(FString::Printf(TEXT("%s lands %s (-%.0f)"), *NameOf(Ev.Side),
                                     Ev.Action == RKC::EAction::HeavyPunch ? TEXT("heavy") : TEXT("jab"),
                                     Ev.Value));
            break;

        case RKC::ECombatEvent::AttackBlocked:
            PushFeed(FString::Printf(TEXT("%s's %s is blocked (chip %.1f)"), *NameOf(Ev.Side),
                                     Ev.Action == RKC::EAction::HeavyPunch ? TEXT("heavy") : TEXT("jab"),
                                     Ev.Value));
            break;

        case RKC::ECombatEvent::AttackWhiffed:
            PushFeed(FString::Printf(TEXT("%s misses"), *NameOf(Ev.Side)));
            break;

        case RKC::ECombatEvent::GuardBroken:
            PushFeed(FString::Printf(TEXT("%s's guard breaks"), *NameOf(Ev.Side)));
            break;

        case RKC::ECombatEvent::DodgeStarted:
            PushFeed(FString::Printf(TEXT("%s slips %s"), *NameOf(Ev.Side),
                                     Ev.Value >= 0.0f ? TEXT("forward") : TEXT("back")));
            break;

        case RKC::ECombatEvent::StaminaExhausted:
            PushFeed(FString::Printf(TEXT("%s is out of stamina"), *NameOf(Ev.Side)));
            break;

        case RKC::ECombatEvent::Knockdown:
            PushFeed(FString::Printf(TEXT("%s is knocked down"), *NameOf(Ev.Side)));
            break;

        case RKC::ECombatEvent::GetUp:
            PushFeed(FString::Printf(TEXT("%s beats the count"), *NameOf(Ev.Side)));
            break;

        case RKC::ECombatEvent::Knockout:
            PushFeed(FString::Printf(TEXT("KNOCKOUT: %s is down"), *NameOf(Ev.Side)));
            break;

        case RKC::ECombatEvent::RoundStarted:
            PushFeed(FString::Printf(TEXT("Round %d begins"), static_cast<int32>(Ev.Value)));
            break;

        case RKC::ECombatEvent::RoundEnded:
            PushFeed(FString::Printf(TEXT("Round %d ends: %s"), static_cast<int32>(Ev.Value),
                                     *NameOf(Ev.Side)));
            break;

        case RKC::ECombatEvent::MatchEnded:
            PushFeed(TEXT("Match complete."));
            break;

        default:
            break;
        }

        // Presentation last, from the same record the feed was built from: the
        // simulation has already advanced, so a sound can never influence a match.
        PlayAudioForEvent(Ev);
    }
}

void ARKCGameMode::DrivePresentationAudio()
{
    if (AudioSystem == nullptr || !AudioSystem->IsInitialised())
    {
        return;
    }

    const RKC::FMatchState& Match = Sim.GetMatch();
    const RKC::FFighterState& A = Sim.GetFighter(RKC::ESide::A);
    const RKC::FFighterState& B = Sim.GetFighter(RKC::ESide::B);
    const int32 Tick = Match.TickCount;

    // ---- pre-round countdown -------------------------------------------------
    // The simulation has no countdown tick: the phase clock is the only source.
    // Each whole second that passes is one cue, and the cue itself rises in pitch
    // as the start approaches, so the player can hear how close it is. Only the
    // last three seconds are voiced.
    if (Match.Phase == RKC::EMatchPhase::Countdown)
    {
        const int32 Second = FMath::CeilToInt(Match.PhaseTimeRemaining);
        if (Second != LastCountdownSecond)
        {
            LastCountdownSecond = Second;
            if (Second >= 1 && Second <= 3)
            {
                AudioSystem->PlayCountdownCue(Tick, Second);
            }
        }
    }
    else
    {
        // Re-arm for the next round so its first cue is never swallowed by a
        // stale second number left over from this one.
        LastCountdownSecond = -1;
    }

    // ---- footfalls -----------------------------------------------------------
    // There is no move event, so a step is measured instead: how far each fighter
    // actually travelled this frame, banked until it spans a stride. A stride of
    // 95 cm is about half a second of the walk speed, which is the cadence of the
    // walk art.
    constexpr float StrideCm = 95.0f;

    if (Match.Phase != RKC::EMatchPhase::Fighting)
    {
        // Between rounds the fighters are idle, teleported to their start marks,
        // or walking in a result pose. Re-anchor here so the first step of the
        // next round cannot fire from stale movement.
        FootstepLastPosA = A.PositionX;
        FootstepLastPosB = B.PositionX;
        FootstepAccumA = 0.0f;
        FootstepAccumB = 0.0f;
        bFootstepAnchored = true;
        return;
    }

    if (!bFootstepAnchored)
    {
        FootstepLastPosA = A.PositionX;
        FootstepLastPosB = B.PositionX;
        bFootstepAnchored = true;
        return;
    }

    // A footstep is a walking sound, so only a walking fighter makes one. Every
    // other state - hit stun, a knockback, a knockdown, a win pose - either is
    // silent or carries its own cue from the event stream.
    const auto DriveOne = [&](const RKC::FFighterState& F, float& LastPos, float& Accum,
                              int32 FighterSalt)
    {
        const float Step = FMath::Abs(F.PositionX - LastPos);
        LastPos = F.PositionX;

        if (F.State != RKC::EFighterState::Moving)
        {
            Accum = 0.0f;
            return;
        }

        Accum += Step;
        if (Accum >= StrideCm)
        {
            // One step per frame at most: the audio system also de-duplicates on
            // the tick, so a long teleport cannot turn into a burst of steps.
            Accum = FMath::Fmod(Accum, StrideCm);
            AudioSystem->PlayFootstep(Tick, FighterSalt);
        }
    };

    DriveOne(A, FootstepLastPosA, FootstepAccumA, 0);
    DriveOne(B, FootstepLastPosB, FootstepAccumB, 1);
}

void ARKCGameMode::PushFeed(const FString& Line)
{
    FRKCEventFeedLine Entry;
    Entry.Text = Line;
    Entry.AgeSeconds = 0.0f;
    EventFeed.Add(Entry);

    // Keep the feed short enough that it is read rather than scrolled.
    constexpr int32 MaxFeedLines = 6;
    while (EventFeed.Num() > MaxFeedLines)
    {
        EventFeed.RemoveAt(0);
    }
}

void ARKCGameMode::UpdateBanner(float DeltaSeconds)
{
    const RKC::FMatchState& M = Sim.GetMatch();

    auto SetBanner = [this](const FString& Text, bool bProminent)
    {
        if (BannerText != Text)
        {
            BannerText = Text;
            BannerAgeSeconds = 0.0f;
        }
        bBannerProminent = bProminent;
    };

    auto NameOf = [this](RKC::ESide Side) -> FString
    {
        if (Side == RKC::ESide::None)
        {
            return TEXT("NOBODY");
        }
        return RKC::GetRosterDisplayName(Side == RKC::ESide::A ? Setup.FighterA : Setup.FighterB);
    };

    switch (M.Phase)
    {
    case RKC::EMatchPhase::NotStarted:
        SetBanner(TEXT(""), false);
        break;

    case RKC::EMatchPhase::Countdown:
        bWasCountingDown = true;
        SetBanner(FString::Printf(TEXT("ROUND %d   %d"), M.Round,
                                  FMath::Max(1, FMath::CeilToInt(M.PhaseTimeRemaining))),
                  true);
        break;

    case RKC::EMatchPhase::Fighting:
        if (bWasCountingDown)
        {
            bWasCountingDown = false;
            SetBanner(TEXT("FIGHT!"), true);
            BannerAgeSeconds = 0.0f;
        }
        else if (BannerText == TEXT("FIGHT!") && BannerAgeSeconds > 0.8f)
        {
            SetBanner(TEXT(""), false);
        }
        break;

    case RKC::EMatchPhase::RoundEnd:
    {
        const bool bDraw = (M.LastRoundEndReason == RKC::ERoundEndReason::TimeExpired);
        const FString Winner = NameOf(M.LastRoundWinner);
        SetBanner(FString::Printf(TEXT("%s%s%s%s   %d - %d"), RoundEndText(M.LastRoundEndReason),
                                  bDraw ? TEXT("") : TEXT(": "), bDraw ? TEXT("") : *Winner,
                                  TEXT(""), M.ScoreA, M.ScoreB),
                  true);
        break;
    }

    case RKC::EMatchPhase::MatchEnd:
    {
        if (M.Result == RKC::EMatchResult::Draw)
        {
            SetBanner(FString::Printf(TEXT("DRAW %s   %d - %d"), MatchEndText(M.EndReason),
                                      M.ScoreA, M.ScoreB),
                      true);
        }
        else
        {
            const RKC::ESide Winner = (M.Result == RKC::EMatchResult::FighterAWins)
                                          ? RKC::ESide::A
                                          : RKC::ESide::B;
            SetBanner(FString::Printf(TEXT("%s WINS %s   %d - %d"), *NameOf(Winner),
                                      MatchEndText(M.EndReason), M.ScoreA, M.ScoreB),
                      true);
        }
        break;
    }

    default:
        break;
    }

    // Use the director's clock, which also advances in headless self-tests.
    // A cleared banner stays cleared until another countdown or match event.
    BannerAgeSeconds += FMath::Max(0.0f, DeltaSeconds);
}

// ---------------------------------------------------------------------------
// Accessors used by the HUD
// ---------------------------------------------------------------------------

const RKC::FBotBrain* ARKCGameMode::GetBot(RKC::ESide Side) const
{
    const int32 Index = (Side == RKC::ESide::B) ? 1 : 0;
    return bBotActive[Index] ? &BotBrains[Index] : nullptr;
}

ARKCFighter* ARKCGameMode::GetFighterActor(RKC::ESide Side) const
{
    return (Side == RKC::ESide::A) ? FighterA : FighterB;
}

ARKCPlayerController* ARKCGameMode::GetRKCPlayerController() const
{
    if (UWorld* World = GetWorld())
    {
        return Cast<ARKCPlayerController>(World->GetFirstPlayerController());
    }
    return nullptr;
}

int32 ARKCGameMode::GetArenaPieceCount() const
{
    return Arena ? Arena->GetPieceCount() : 0;
}

int32 ARKCGameMode::GetFighterPartCount(RKC::ESide Side) const
{
    const ARKCFighter* Fighter = (Side == RKC::ESide::A) ? FighterA : FighterB;
    return Fighter ? Fighter->GetPartCount() : 0;
}

void ARKCGameMode::SetFrameDataVisible(bool bVisible)
{
    bFrameDataVisible = bVisible;

    // In a hitbox training match the same toggle reveals the gameplay hitbox, so
    // the debug picture and the overlay can be compared side by side.
    const bool bShowHitboxes = (Setup.TrainingA == RKC::ETrainingMode::ShowHitboxes) ||
                               (Setup.TrainingB == RKC::ETrainingMode::ShowHitboxes);
    if (bShowHitboxes)
    {
        if (FighterA)
        {
            FighterA->SetShowHitboxes(bVisible);
        }
        if (FighterB)
        {
            FighterB->SetShowHitboxes(bVisible);
        }
    }
}

float ARKCGameMode::GetSecondsSinceMatchEnd() const
{
    return SecondsSinceMatchEnd;
}

bool ARKCGameMode::IsRematchOffered() const
{
    return Sim.IsMatchOver() && SecondsSinceMatchEnd >= 1.0f;
}

// ---------------------------------------------------------------------------
// Evidence
// ---------------------------------------------------------------------------

uint32 ARKCGameMode::ComputeStateChecksum(const RKC::FCombatSim& InSim)
{
    uint32 Hash = 2166136261u;

    const RKC::FMatchState& M = InSim.GetMatch();
    MixHash(Hash, static_cast<uint32>(M.TickCount));
    MixHash(Hash, static_cast<uint32>(M.Round));
    MixHash(Hash, static_cast<uint32>(M.Phase));
    MixHash(Hash, static_cast<uint32>(M.Result));
    MixHash(Hash, static_cast<uint32>(M.ScoreA));
    MixHash(Hash, static_cast<uint32>(M.ScoreB));

    for (int32 Index = 0; Index < 2; ++Index)
    {
        const RKC::ESide Side = (Index == 0) ? RKC::ESide::A : RKC::ESide::B;
        const RKC::FFighterState& F = InSim.GetFighter(Side);
        // Quantise to millimetre / tenth-of-a-point resolution so the checksum is
        // about the rules, not about float noise in the last mantissa bit.
        MixHash(Hash, static_cast<uint32>(FMath::RoundToInt(F.PositionX * 10.0f)));
        MixHash(Hash, static_cast<uint32>(FMath::RoundToInt(F.Health * 10.0f)));
        MixHash(Hash, static_cast<uint32>(FMath::RoundToInt(F.Stamina * 10.0f)));
        MixHash(Hash, static_cast<uint32>(F.State));
        MixHash(Hash, static_cast<uint32>(F.CurrentAction));
        MixHash(Hash, static_cast<uint32>(F.HitsLandedTotal));
        MixHash(Hash, static_cast<uint32>(F.KnockdownsTotal));
    }

    return Hash;
}

void ARKCGameMode::OnMatchEnded()
{
    const RKC::FMatchState& M = Sim.GetMatch();
    MatchEndedAtSimTime = static_cast<float>(M.TickCount) * RKC::FCombatSim::FixedTickSeconds;
    SecondsSinceMatchEnd = 0.0f;

    UE_LOG(LogRockyKombat, Log, TEXT("%s"), *DescribeCurrentMatch());

    if (Setup.bCaptureEvidence)
    {
        const FString Path = WriteEvidenceRecord(TEXT("match-end"));
        bEvidenceWritten = !Path.IsEmpty();
    }
    else
    {
        // Mark as handled so the record is not attempted again on every frame.
        bEvidenceWritten = true;
    }
}

FString ARKCGameMode::DescribeCurrentMatch() const
{
    const RKC::FMatchState& M = Sim.GetMatch();

    if (M.Phase == RKC::EMatchPhase::NotStarted)
    {
        return TEXT("No match in progress.");
    }
    if (M.Phase != RKC::EMatchPhase::MatchEnd)
    {
        return FString::Printf(TEXT("Match in progress: round %d, score %d-%d, %.1f s left."),
                               M.Round, M.ScoreA, M.ScoreB, M.RoundTimeRemaining);
    }

    const FString NameA = RKC::GetRosterDisplayName(Setup.FighterA);
    const FString NameB = RKC::GetRosterDisplayName(Setup.FighterB);

    FString Winner = TEXT("DRAW");
    if (M.Result == RKC::EMatchResult::FighterAWins)
    {
        Winner = NameA;
    }
    else if (M.Result == RKC::EMatchResult::FighterBWins)
    {
        Winner = NameB;
    }

    return FString::Printf(
        TEXT("RESULT %s %s | score %d-%d | %d round(s) | %d ticks (%.2f s) | "
             "hits %d-%d attacks %d-%d blocks %d-%d dodges %d-%d knockdowns %d-%d"),
        *Winner, MatchEndText(M.EndReason), M.ScoreA, M.ScoreB, M.Round, M.TickCount,
        static_cast<float>(M.TickCount) * RKC::FCombatSim::FixedTickSeconds,
        Sim.GetFighter(RKC::ESide::A).HitsLandedTotal, Sim.GetFighter(RKC::ESide::B).HitsLandedTotal,
        Sim.GetFighter(RKC::ESide::A).AttacksThrownTotal,
        Sim.GetFighter(RKC::ESide::B).AttacksThrownTotal, TotalBlocksA, TotalBlocksB, TotalDodgesA,
        TotalDodgesB, Sim.GetFighter(RKC::ESide::A).KnockdownsTotal,
        Sim.GetFighter(RKC::ESide::B).KnockdownsTotal);
}

bool ARKCGameMode::CrossCheckAgainstStandalone(FString& OutReport) const
{
    if (!bBotActive[0] || !bBotActive[1])
    {
        OutReport = TEXT("not run: at least one side was human-controlled, so there is no "
                         "reference run to compare against");
        return false;
    }

    const RKC::FBotMatchOutcome Reference = RKC::RunBotMatch(
        Sim.GetRules(), Sim.GetConfig(RKC::ESide::A), Sim.GetConfig(RKC::ESide::B),
        RKC::FBotProfile::Make(Setup.BotArchetypeA, Setup.BotDifficulty),
        RKC::FBotProfile::Make(Setup.BotArchetypeB, Setup.BotDifficulty), Setup.BotSeedA,
        Setup.BotSeedB, 60 * 60 * 10, Setup.TrainingA, Setup.TrainingB);

    char Buffer[640];
    RKC::DescribeMatchOutcome(Reference, Buffer, sizeof(Buffer));

    const RKC::FMatchState& M = Sim.GetMatch();
    const bool bAgrees = (Reference.Result == M.Result) && (Reference.EndReason == M.EndReason) &&
                         (Reference.ScoreA == M.ScoreA) && (Reference.ScoreB == M.ScoreB) &&
                         (Reference.RoundsFought == M.Round) &&
                         (Reference.HitsLandedA == Sim.GetFighter(RKC::ESide::A).HitsLandedTotal) &&
                         (Reference.HitsLandedB == Sim.GetFighter(RKC::ESide::B).HitsLandedTotal);

    OutReport = FString::Printf(TEXT("reference run (pure C++ harness, same seeds):\n%s\n"
                                     "agreement with the live engine match: %s"),
                                ANSI_TO_TCHAR(Buffer), bAgrees ? TEXT("YES") : TEXT("NO"));
    return bAgrees;
}

FString ARKCGameMode::WriteEvidenceRecord(const TCHAR* Reason)
{
    const RKC::FMatchState& M = Sim.GetMatch();
    const RKC::FMatchRules& R = Sim.GetRules();
    const RKC::FFighterState& FA = Sim.GetFighter(RKC::ESide::A);
    const RKC::FFighterState& FB = Sim.GetFighter(RKC::ESide::B);
    const RKC::FBotBrain* BotA = GetBot(RKC::ESide::A);
    const RKC::FBotBrain* BotB = GetBot(RKC::ESide::B);

    FString Report;
    Report += TEXT("ROCKY KOMBAT - MATCH EVIDENCE RECORD\n");
    Report += TEXT("=====================================\n\n");
    Report += FString::Printf(TEXT("written:        %s\n"),
                              *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")));
    Report += FString::Printf(TEXT("engine:         %s\n"), *FEngineVersion::Current().ToString());
    Report += FString::Printf(TEXT("reason:         %s\n\n"), Reason);

    Report += TEXT("--- SETUP ---\n");
    Report += FString::Printf(TEXT("roster A:       %s (%s)\n"),
                              *RKC::GetRosterDisplayName(Setup.FighterA),
                              *RKC::GetRosterDivisionName(Setup.FighterA));
    Report += FString::Printf(TEXT("roster B:       %s (%s)\n"),
                              *RKC::GetRosterDisplayName(Setup.FighterB),
                              *RKC::GetRosterDivisionName(Setup.FighterB));
    Report += FString::Printf(TEXT("rules:          %d round(s) of %.0f s, countdown %.0f s, "
                                   "3-knockdown limit %d\n"),
                              R.Rounds, R.RoundSeconds, R.CountdownSeconds,
                              R.MaxKnockdownsPerRound);
    Report += FString::Printf(TEXT("min separation: %.0f cm   arena X: %.0f .. %.0f\n"),
                              R.MinSeparation, R.ArenaMinX, R.ArenaMaxX);
    Report += FString::Printf(TEXT("training A/B:   %s / %s\n"), TrainingText(Setup.TrainingA),
                              TrainingText(Setup.TrainingB));
    Report += FString::Printf(TEXT("bot A:          %s\n"),
                              BotA ? ANSI_TO_TCHAR(RKC::GetDifficultyName(BotA->GetProfile().Difficulty))
                                   : TEXT("human"));
    Report += FString::Printf(TEXT("bot B:          %s\n"),
                              BotB ? ANSI_TO_TCHAR(RKC::GetDifficultyName(BotB->GetProfile().Difficulty))
                                   : TEXT("human"));
    if (BotA)
    {
        Report += FString::Printf(
            TEXT("bot A detail:   %s, seed %u, %d decisions, %d attacks, %d reactions\n"),
            ANSI_TO_TCHAR(RKC::GetArchetypeName(BotA->GetProfile().Archetype)), BotA->GetRngState(),
            BotA->GetDecisionCount(), BotA->GetAttackOrders(), BotA->GetReactionCount());
    }
    if (BotB)
    {
        Report += FString::Printf(
            TEXT("bot B detail:   %s, seed %u, %d decisions, %d attacks, %d reactions\n"),
            ANSI_TO_TCHAR(RKC::GetArchetypeName(BotB->GetProfile().Archetype)), BotB->GetRngState(),
            BotB->GetDecisionCount(), BotB->GetAttackOrders(), BotB->GetReactionCount());
    }

    Report += TEXT("\n--- OUTCOME ---\n");
    Report += DescribeCurrentMatch();
    Report += TEXT("\n");
    Report += FString::Printf(TEXT("last round end: %s\n"), RoundEndText(M.LastRoundEndReason));
    Report += FString::Printf(TEXT("sim ticks:      %d  (%.3f s at 1/60 s)\n"), M.TickCount,
                              static_cast<float>(M.TickCount) * RKC::FCombatSim::FixedTickSeconds);
    if (MatchEndedAtSimTime >= 0.0f)
    {
        Report += FString::Printf(TEXT("match end at:   %.3f s\n"), MatchEndedAtSimTime);
    }

    Report += TEXT("\n--- INTEGRITY ---\n");
    Report += FString::Printf(TEXT("worst separation violation: %.4f cm  %s\n"),
                              WorstSeparationViolation,
                              WorstSeparationViolation <= 0.0001f ? TEXT("(legal)" )
                                                                  : TEXT("(RULE FAILURE)"));
    Report += FString::Printf(TEXT("final separation:           %.1f cm\n"),
                              FMath::Abs(FA.PositionX - FB.PositionX));
    Report += FString::Printf(TEXT("final state checksum:       0x%08X\n"),
                              ComputeStateChecksum(Sim));
    Report += FString::Printf(TEXT("presentation:               ring pieces %d, rig parts A %d / B %d\n"),
                              GetArenaPieceCount(), GetFighterPartCount(RKC::ESide::A),
                              GetFighterPartCount(RKC::ESide::B));
    Report += FString::Printf(TEXT("fighter A:                  health %.1f stamina %.1f state %d\n"),
                              FA.Health, FA.Stamina, static_cast<int32>(FA.State));
    Report += FString::Printf(TEXT("fighter B:                  health %.1f stamina %.1f state %d\n"),
                              FB.Health, FB.Stamina, static_cast<int32>(FB.State));

    // ---- Presentation layers ------------------------------------------------
    // Reported as measurements, not as claims: a front end that never changed
    // screen and an audio layer that never issued a sound both look like silence
    // here rather than like success.
    Report += TEXT("\n--- FRONT END AND RENDER WARM-UP ---\n");
    Report += FString::Printf(TEXT("front end:           %s\n"),
                              bFrontEndEnabled ? TEXT("enabled")
                                               : TEXT("disabled (-rkc.FrontEnd=0 or a self-test)"));
    Report += FString::Printf(TEXT("screen:              %s\n"), *FrontEnd.DescribeState());
    Report += FString::Printf(TEXT("loading progress:    %.2f\n"), LoadingProgress);
    Report += FString::Printf(TEXT("warm-up complete:    %s\n"),
                              IsRenderWarmupComplete() ? TEXT("yes") : TEXT("no"));
    Report += FString::Printf(TEXT("hud draw passes:     %d (warm-up needs %d)\n"),
                              WarmupFramesObserved, RenderWarmupFramesRequired);
    Report += FString::Printf(TEXT("front-end frames:    %d\n"), FrontEndFramesTicked);
    {
        const RKCFrontEnd::FDiagnostics& F = FrontEnd.GetDiagnostics();
        Report += FString::Printf(
            TEXT("menu activity:       %d screen change(s), %d accept(s), %d back(s), %d tap(s), "
                 "%d option change(s), %d pause(s), %d resume(s)\n"),
            F.ScreenChanges, F.Accepts, F.Backs, F.TapSelections, F.OptionsChanged, F.Pauses,
            F.Resumes);
        Report += FString::Printf(TEXT("selections kept:     %d, refused accepts: %d\n"),
                                  F.SelectionsPreserved, F.RejectedAccepts);

        // The last few transitions are the only honest way to see the path a
        // player actually took without a video of it.
        const int32 FirstLogIndex = FMath::Max(0, F.TransitionLog.Num() - 12);
        if (F.TransitionLog.Num() > 0)
        {
            Report += TEXT("last screen changes:\n");
            for (int32 Index = FirstLogIndex; Index < F.TransitionLog.Num(); ++Index)
            {
                Report += FString::Printf(TEXT("  %s\n"), *F.TransitionLog[Index]);
            }
        }
    }

    if (AudioSystem != nullptr)
    {
        const FRKCAudioDiagnostics& A = AudioSystem->GetDiagnostics();
        Report += TEXT("\n--- AUDIO ---\n");
        Report += FString::Printf(TEXT("catalog:             %d entr(ies), %d resolved, %d missing\n"),
                                  A.CatalogSize, A.AssetsResolved, A.AssetsMissing);
        Report += FString::Printf(TEXT("events planned:      %d (%d with a primary, %d with a voice)\n"),
                                  A.EventsPlanned, A.EventsWithPrimary, A.EventsWithVoice);
        Report += FString::Printf(
            TEXT("plays issued:        %d (max %d sound(s) per event; muted %d, duplicate %d)\n"),
            A.PlaysIssued, A.SoundsIssuedPerEventMax, A.PlaysSuppressedByMute,
            A.PlaysSuppressedAsDuplicate);
        Report += FString::Printf(TEXT("music start/stop:    %d / %d, pause/resume %d / %d\n"),
                                  A.MusicStarts, A.MusicStops, A.MusicPauses, A.MusicResumes);
        // Asked-for transitions, counted whether or not a music component was live.
        // On a self-test run there is no device, so the two numbers above stay at
        // zero and these are the ones that show the lifecycle reached the audio
        // layer at all.
        Report += FString::Printf(TEXT("freeze/resume asked: %d / %d\n"), A.PauseRequests,
                                  A.ResumeRequests);
        Report += FString::Printf(TEXT("ambience start/stop: %d / %d\n"), A.AmbienceStarts,
                                  A.AmbienceStops);
        Report += FString::Printf(TEXT("playback attempted:  %s\n"),
                                  A.bPlaybackAttempted ? TEXT("yes") : TEXT("no"));

        // Sorted so two runs of the same match produce the same record.
        TArray<FString> EventKeys;
        A.PlaysByEventType.GetKeys(EventKeys);
        EventKeys.Sort();
        if (EventKeys.Num() > 0)
        {
            Report += TEXT("sounds per event type:\n");
            for (const FString& Key : EventKeys)
            {
                Report += FString::Printf(TEXT("  %s: %d\n"), *Key, A.PlaysByEventType[Key]);
            }
        }
        if (A.MissingAssets.Num() > 0)
        {
            Report += FString::Printf(TEXT("missing (first 8 of %d):\n"), A.MissingAssets.Num());
            for (int32 Index = 0; Index < FMath::Min(8, A.MissingAssets.Num()); ++Index)
            {
                Report += FString::Printf(TEXT("  %s\n"), *A.MissingAssets[Index]);
            }
        }
    }
    else
    {
        Report += TEXT("\n--- AUDIO ---\nno audio layer was created for this run\n");
    }

    if (const ARKCPlayerController* ReadOnlyPC = GetRKCPlayerController())
    {
        const RKCTouch::FController& Touch = ReadOnlyPC->GetTouchController();
        const RKCTouch::FDiagnostics& T = Touch.GetDiagnostics();
        Report += TEXT("\n--- TOUCH CONTROLS ---\n");
        Report += FString::Printf(TEXT("enabled:             %s (forced visible: %s)\n"),
                                  ReadOnlyPC->AreTouchControlsEnabled() ? TEXT("yes") : TEXT("no"),
                                  ReadOnlyPC->IsTouchForcedVisible() ? TEXT("yes") : TEXT("no"));
        Report += FString::Printf(TEXT("widgets:             %d\n"), Touch.GetWidgets().Num());
        Report += FString::Printf(TEXT("touches:             %d begin, %d move, %d end, %d cancelled\n"),
                                  T.Begins, T.Moves, T.Ends, T.Cancellations);
        Report += FString::Printf(TEXT("rejected off-pad:    %d\n"), T.BeginsRejectedOutsideControls);
        Report += FString::Printf(TEXT("presses:             jab %d, heavy %d, dodge %d, block %d down / %d up\n"),
                                  T.PressesJab, T.PressesHeavy, T.PressesDodge, T.BlockPresses,
                                  T.BlockReleases);
        Report += FString::Printf(TEXT("edges delivered:     %d, stick changes %d\n"),
                                  T.EdgesDelivered, T.StickDirectionChanges);
        Report += FString::Printf(TEXT("max fingers at once: %d, stuck touches cleared %d\n"),
                                  T.MaxSimultaneousFingers, T.StuckTouchesCleared);
        Report += FString::Printf(TEXT("touch state now:     %s\n"), *Touch.DescribeState());
    }

    // The strongest available cross-check: run the identical match through the
    // pure-C++ harness in this same process and compare outcomes. A green result
    // means the engine build and the standalone build really do implement the
    // same rules, rather than that they merely both compile.
    Report += TEXT("\n--- STANDALONE CROSS-CHECK ---\n");
    FString CrossReport;
    if (CrossCheckAgainstStandalone(CrossReport))
    {
        Report += FString::Printf(TEXT("%s\n"), *CrossReport);
    }
    else
    {
        Report += FString::Printf(TEXT("%s\n"), *CrossReport);
    }

    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Evidence");
    IFileManager::Get().MakeDirectory(*Directory, true);

    FString SafeReason = Reason ? FString(Reason) : FString(TEXT("record"));
    SafeReason.ReplaceInline(TEXT(" "), TEXT("-"));

    const FString FileName = FString::Printf(
        TEXT("RKC_%s_%s.txt"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")), *SafeReason);
    const FString FullPath = Directory / FileName;

    if (!FFileHelper::SaveStringToFile(Report, *FullPath))
    {
        UE_LOG(LogRockyKombat, Error, TEXT("Could not write evidence record to %s"), *FullPath);
        return FString();
    }

    UE_LOG(LogRockyKombat, Log, TEXT("Evidence record written: %s"), *FullPath);
    return FullPath;
}

void ARKCGameMode::LogSetup() const
{
    UE_LOG(LogRockyKombat, Log,
           TEXT("Rocky Kombat world built: ring pieces=%d, rig parts=%d/%d, bots A=%s B=%s, "
                "evidence=%s."),
           GetArenaPieceCount(), GetFighterPartCount(RKC::ESide::A),
           GetFighterPartCount(RKC::ESide::B), Setup.bBotControlsA ? TEXT("yes") : TEXT("no"),
           Setup.bBotControlsB ? TEXT("yes") : TEXT("no"),
           Setup.bCaptureEvidence ? TEXT("on") : TEXT("off"));
}

// ---------------------------------------------------------------------------
// Console commands
//
// These exist so a test run can drive the match and gather evidence without any
// UI: they are the scripted path that a CI job or an on-device check uses.
// ---------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldAndArgs CmdRKCRematch(
    TEXT("rkc.Rematch"),
    TEXT("rkc.Rematch - restart the current match with the current setup."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
        [](const TArray<FString>& /*Args*/, UWorld* World)
        {
            if (ARKCGameMode* GM = World ? Cast<ARKCGameMode>(World->GetAuthGameMode()) : nullptr)
            {
                GM->RestartMatch();
            }
        }));

static FAutoConsoleCommandWithWorldAndArgs CmdRKCReconfigure(
    TEXT("rkc.Reconfigure"),
    TEXT("rkc.Reconfigure - re-read the rkc.* console variables and restart the match."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
        [](const TArray<FString>& /*Args*/, UWorld* World)
        {
            if (ARKCGameMode* GM = World ? Cast<ARKCGameMode>(World->GetAuthGameMode()) : nullptr)
            {
                GM->ApplyConsoleOverridesAndRestart();
            }
        }));

static FAutoConsoleCommandWithWorldAndArgs CmdRKCDump(
    TEXT("rkc.DumpState"),
    TEXT("rkc.DumpState - log the current match state and write an evidence record."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
        [](const TArray<FString>& /*Args*/, UWorld* World)
        {
            if (ARKCGameMode* GM = World ? Cast<ARKCGameMode>(World->GetAuthGameMode()) : nullptr)
            {
                UE_LOG(LogRockyKombat, Log, TEXT("%s"), *GM->DescribeCurrentMatch());
                GM->WriteEvidenceRecord(TEXT("manual-dump"));
            }
        }));

static FAutoConsoleCommandWithWorldAndArgs CmdRKCBotMatch(
    TEXT("rkc.BotMatch"),
    TEXT("rkc.BotMatch [count] [seedBase] - run N complete bot-vs-bot matches through the "
         "pure C++ harness inside this process, and verify determinism by repeating each "
         "match exactly."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* /*World*/)
        {
            const int32 Count = (Args.Num() > 0) ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 200) : 5;
            const int32 SeedBase = (Args.Num() > 1) ? FCString::Atoi(*Args[1]) : 1000;

            const RKC::FMatchRules Rules = ResolveRules(GK_QuickRules != 0);
            const RKC::FFighterConfig ConfigA = RKC::MakeFighterConfig(
                (GK_RosterA == 1) ? RKC::ERoster::Shadow : RKC::ERoster::Rocky);
            const RKC::FFighterConfig ConfigB = RKC::MakeFighterConfig(
                (GK_RosterB == 1) ? RKC::ERoster::Shadow : RKC::ERoster::Rocky);

            const RKC::FBotProfile ProfileA = RKC::FBotProfile::Make(
                static_cast<RKC::EBotArchetype>(FMath::Clamp(GK_BotArchetypeA, 0, 3)),
                static_cast<RKC::EBotDifficulty>(FMath::Clamp(GK_BotDifficulty, 0, 2)));
            const RKC::FBotProfile ProfileB = RKC::FBotProfile::Make(
                static_cast<RKC::EBotArchetype>(FMath::Clamp(GK_BotArchetypeB, 0, 3)),
                static_cast<RKC::EBotDifficulty>(FMath::Clamp(GK_BotDifficulty, 0, 2)));

            int32 Timeouts = 0;
            int32 SeparationFailures = 0;
            int32 DeterminismFailures = 0;
            int32 WinsA = 0;
            int32 WinsB = 0;
            int32 Draws = 0;
            int32 DecidedByKo = 0;

            for (int32 Index = 0; Index < Count; ++Index)
            {
                const uint32 SeedA = static_cast<uint32>(SeedBase + Index * 2);
                const uint32 SeedB = static_cast<uint32>(SeedBase + Index * 2 + 1);

                const RKC::FBotMatchOutcome Run = RKC::RunBotMatch(
                    Rules, ConfigA, ConfigB, ProfileA, ProfileB, SeedA, SeedB, 60 * 60 * 10);

                // Same seeds, run again: every counter must match exactly.
                const RKC::FBotMatchOutcome Repeat = RKC::RunBotMatch(
                    Rules, ConfigA, ConfigB, ProfileA, ProfileB, SeedA, SeedB, 60 * 60 * 10);

                const bool bDeterministic =
                    Run.Ticks == Repeat.Ticks && Run.Result == Repeat.Result &&
                    Run.ScoreA == Repeat.ScoreA && Run.ScoreB == Repeat.ScoreB &&
                    Run.HitsLandedA == Repeat.HitsLandedA && Run.HitsLandedB == Repeat.HitsLandedB &&
                    Run.AttacksThrownA == Repeat.AttacksThrownA &&
                    Run.AttacksThrownB == Repeat.AttacksThrownB &&
                    Run.DamageDealtA == Repeat.DamageDealtA &&
                    Run.DamageDealtB == Repeat.DamageDealtB &&
                    Run.BlocksA == Repeat.BlocksA && Run.BlocksB == Repeat.BlocksB &&
                    Run.FinalSeparation == Repeat.FinalSeparation;

                if (!bDeterministic)
                {
                    ++DeterminismFailures;
                }
                if (Run.bTimedOut)
                {
                    ++Timeouts;
                }
                if (Run.WorstSeparationViolation > 0.0001f)
                {
                    ++SeparationFailures;
                }

                if (Run.Result == RKC::EMatchResult::FighterAWins)
                {
                    ++WinsA;
                }
                else if (Run.Result == RKC::EMatchResult::FighterBWins)
                {
                    ++WinsB;
                }
                else
                {
                    ++Draws;
                }

                if (Run.EndReason == RKC::EMatchEndReason::Knockout ||
                    Run.EndReason == RKC::EMatchEndReason::TechnicalKnockout)
                {
                    ++DecidedByKo;
                }

                char Buffer[640];
                RKC::DescribeMatchOutcome(Run, Buffer, sizeof(Buffer));
                UE_LOG(LogRockyKombat, Log, TEXT("[bot match %d/%d] %s | deterministic=%s"), Index + 1,
                       Count, ANSI_TO_TCHAR(Buffer), bDeterministic ? TEXT("yes") : TEXT("NO"));
            }

            UE_LOG(LogRockyKombat, Log,
                   TEXT("rkc.BotMatch summary: %d match(es), A wins %d, B wins %d, draws %d, "
                        "KO/TKO %d, timeouts %d, separation failures %d, determinism failures %d"),
                   Count, WinsA, WinsB, Draws, DecidedByKo, Timeouts, SeparationFailures,
                   DeterminismFailures);

            if (Timeouts == 0 && SeparationFailures == 0 && DeterminismFailures == 0)
            {
                UE_LOG(LogRockyKombat, Log, TEXT("rkc.BotMatch: ALL CHECKS PASSED"));
            }
            else
            {
                UE_LOG(LogRockyKombat, Error, TEXT("rkc.BotMatch: CHECKS FAILED"));
            }
        }));

// ---------------------------------------------------------------------------
// In-engine self-test
//
// WHY THIS EXISTS
// Tools/AITests proves the rules with a plain C++ compiler and no engine at all.
// That is the strongest possible statement about the simulation source, but it
// says nothing about whether the ENGINE build of the same source, the world this
// class constructs, the fighter and arena actors and the HUD actually behave the
// same way when they run in Unreal. This mode is the other half of the proof, and
// it is deliberately built out of the shipped code path: the matches below are
// advanced by AdvanceMatchFrame(), which is the function Tick() calls. There is
// no test-only simulation loop to drift away from the real one.
//
// HOW TO RUN IT
//     UnrealEditor-Cmd.exe RockyKombat.uproject -game -nullrhi -RKCSelfTest=3
// or, with rendering (which additionally verifies that the HUD really draws):
//     UnrealEditor-Cmd.exe RockyKombat.uproject -game -windowed -RKCSelfTest=3
//
// WHAT IT CHECKS
//   world      - the ring, both fighter rigs and the camera exist, and the HUD
//                class really is ARKCHUD.
//   matches    - complete bot-vs-bot matches finish inside the tick budget with
//                a decided result, a result banner and an event feed, and no
//                tick ever breached the minimum-separation rule.
//   agreement  - the identical match, replayed through the pure-C++ harness in
//                this same process, produces the same outcome numbers.
//   determinism- repeating the same seeds reproduces the same final state
//                checksum, while different seeds do not.
//   HUD        - after a few rendering frames, the HUD reports how many draw
//                passes and how many layout regions it emitted. Skipped, and
//                reported as skipped, on a -nullrhi run where no canvas exists.
//
// The verdict is written to Saved/Evidence/RKC_SELFTEST_*.txt and the process
// exits 0 on success or 1 on failure, so a CI job or an acceptance gate can read
// the result without a human looking at a screen.
// ---------------------------------------------------------------------------

void ARKCGameMode::StartSelfTestIfRequested()
{
    if (bSelfTestActive || bSelfTestFinished)
    {
        return;
    }

    // FParse::Value handles "-RKCSelfTest=3"; FParse::Param handles the bare
    // switch, which uses the default count.
    FString Value;
    int32 MatchCount = 3;
    if (FParse::Value(FCommandLine::Get(), TEXT("RKCSelfTest="), Value))
    {
        MatchCount = FMath::Clamp(FCString::Atoi(*Value), 1, 50);
    }
    else if (!FParse::Param(FCommandLine::Get(), TEXT("RKCSelfTest")))
    {
        return;
    }

    RunInEngineSelfTest(MatchCount);
}

namespace
{
/** Whether the round the fight stopped in also ended as a round break.
 *
 *  A knockout stops the contest outright: the simulation calls EnterMatchEnd, not
 *  EnterRoundEnd, so that round produces no round-end event and therefore no
 *  round-end cue. Every other outcome breaks the round first, including a round
 *  that expires in the last round and is then decided by the judges.
 *
 *  The self-test uses this to predict how many round-end cues a card owed. Without
 *  it the only available expectation would be "one per match", which is a false
 *  expectation on a knockout card and would report a working game as broken. */
bool SelfTestFinalRoundBroke(const RKC::FMatchState& Match)
{
    return Match.EndReason != RKC::EMatchEndReason::Knockout &&
           Match.EndReason != RKC::EMatchEndReason::TechnicalKnockout;
}
}

void ARKCGameMode::RunInEngineSelfTest(int32 MatchCount)
{
    SelfTestMatchesRequested = FMath::Clamp(MatchCount, 1, 50);
    SelfTestRoundsStarted = 0;
    SelfTestRoundsThatBroke = 0;
    SelfTestFailures = 0;
    SelfTestLog.Reset();
    SelfTestMatchLines.Reset();
    SelfTestChecksums.Reset();

    // A self-test has to be fully automatic, so both sides become bots for the
    // duration regardless of rkc.BotSide. Evidence is captured by this test's own
    // record instead of the per-match record, so a 5-match run does not leave five
    // half-relevant files in Saved/Evidence.
    Setup.bBotControlsA = true;
    Setup.bBotControlsB = true;
    Setup.bCaptureEvidence = false;

    bSelfTestActive = true;
    bSelfTestFinished = false;
    SelfTestFramesSinceMatches = 0;
    bSelfTestExitScheduled = false;

    UE_LOG(LogRockyKombat, Display,
           TEXT("RKC SELF-TEST starting: %d bot-vs-bot match(es) driven through the live "
                "director loop."),
           SelfTestMatchesRequested);

    RunSelfTestMatches();
    RunSelfTestPresentationSystems();

    // The HUD half runs a few frames later, from Tick, so the renderer has had a
    // chance to draw. See FinishSelfTestAndExit().
    bSelfTestFinished = true;
}

void ARKCGameMode::SelfTestCheck(bool bCondition, const FString& Message)
{
    if (!bCondition)
    {
        ++SelfTestFailures;
    }

    SelfTestLog += FString::Printf(TEXT("%s  %s\n"), bCondition ? TEXT("PASS") : TEXT("FAIL"),
                                   *Message);
    UE_LOG(LogRockyKombat, Log, TEXT("[self-test] %s  %s"), bCondition ? TEXT("PASS") : TEXT("FAIL"),
           *Message);
}

void ARKCGameMode::RunSelfTestMatches()
{
    const bool bHeadless = FParse::Param(FCommandLine::Get(), TEXT("nullrhi"));

    SelfTestLog += TEXT("ROCKY KOMBAT - IN-ENGINE SELF-TEST\n");
    SelfTestLog += TEXT("===================================\n\n");
    SelfTestLog += FString::Printf(TEXT("engine:       %s\n"), *FEngineVersion::Current().ToString());
    SelfTestLog += FString::Printf(TEXT("written:      %s\n"),
                                   *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")));
    SelfTestLog += FString::Printf(TEXT("headless:     %s\n"),
                                   bHeadless ? TEXT("yes (-nullrhi, HUD draw counts not asserted)")
                                             : TEXT("no"));
    SelfTestLog += FString::Printf(TEXT("matches:      %d\n"), SelfTestMatchesRequested);
    SelfTestLog += FString::Printf(TEXT("rules:        %d round(s) of %.0f s, countdown %.0f s, "
                                       "min separation %.0f cm\n"),
                                   Sim.GetRules().Rounds, Sim.GetRules().RoundSeconds,
                                   Sim.GetRules().CountdownSeconds, Sim.GetRules().MinSeparation);

    SelfTestLog += TEXT("\n--- WORLD ---\n");
    SelfTestCheck(bWorldBuilt, TEXT("the match world was built"));
    SelfTestCheck(Arena != nullptr, TEXT("a ring actor exists"));
    SelfTestCheck(GetArenaPieceCount() > 0,
                  FString::Printf(TEXT("the ring has geometry (%d pieces)"), GetArenaPieceCount()));

    // Exactly one ring actor. A second ring on screen can come from two places: the
    // hidden greybox pieces inside this actor, or a second actor spawned beside it
    // by a stale level or a leftover spawn call. The pieces are counted in the
    // artwork block below; this counts the actors, so a duplicate that a visibility
    // flag would hide is still reported instead of being tolerated.
    if (UWorld* const ArenaWorld = GetWorld())
    {
        int32 ArenaActors = 0;
        for (TActorIterator<ARKCArena> It(ArenaWorld); It; ++It)
        {
            ++ArenaActors;
        }
        SelfTestCheck(ArenaActors == 1,
                      FString::Printf(TEXT("the world holds exactly one ring actor (found %d)"),
                                      ArenaActors));
    }
    SelfTestCheck(FighterA != nullptr && FighterB != nullptr, TEXT("both fighter actors exist"));
    SelfTestCheck(GetFighterPartCount(RKC::ESide::A) > 0 && GetFighterPartCount(RKC::ESide::B) > 0,
                  FString::Printf(TEXT("both fighters have a rig (%d / %d parts)"),
                                  GetFighterPartCount(RKC::ESide::A),
                                  GetFighterPartCount(RKC::ESide::B)));
    SelfTestCheck(MatchCamera != nullptr, TEXT("a match camera exists"));
    SelfTestCheck(HUDClass == ARKCHUD::StaticClass(), TEXT("the match HUD class is ARKCHUD"));
    SelfTestCheck(MatchCamera != nullptr && MatchCamera->GetActorRightVector().X > 0.99f,
                  TEXT("screen-right agrees with simulation +X, so player A and left/right controls match the HUD"));
    if (FighterA != nullptr && FighterB != nullptr &&
        FighterA->GetSpriteComponent() != nullptr && FighterB->GetSpriteComponent() != nullptr)
    {
        SelfTestCheck(FighterA->GetSpriteComponent()->GetComponentLocation().Y >
                          FighterB->GetSpriteComponent()->GetComponentLocation().Y + 1.0f,
                      TEXT("overlapping fighters occupy distinct sprite depth layers"));
    }
    SelfTestCheck(Sim.GetRules().IsValid(), TEXT("the live simulation carries a valid rule set"));

    // ---- Imported artwork ----------------------------------------------------
    // These checks are conditional by design. A clean source checkout has no
    // imported art and this test must still pass there. But when the artwork IS
    // present the presentation is required to be using it, because a silent
    // fall back to the greybox rig would otherwise look identical to success.
    {
        const bool bArtPresent = RKCArt::IsArtAvailable();
        SelfTestLog += FString::Printf(TEXT("\n--- IMPORTED ARTWORK (%s) ---\n"),
                                       bArtPresent ? TEXT("present") : TEXT("absent"));

        // WHY THIS CHECK EXISTS: everything below is gated on the artwork being
        // present, because a clean source checkout has none and must still pass.
        // That gate had a hole: a cooked build with the art missing would skip the
        // whole arena block and still report a full PASS, so the one failure the
        // checks existed for was the one failure they could not report. A cooked
        // build is required to carry the art (the cook is what puts it in the
        // package); only the editor is allowed to run without it.
#if WITH_EDITOR
        const bool bArtRequired = false;
#else
        const bool bArtRequired = true;
#endif
        SelfTestCheck(!bArtRequired || bArtPresent,
                      FString::Printf(TEXT("a cooked build carries the imported artwork (art %s)"),
                                      bArtPresent ? TEXT("resolved") : TEXT("MISSING")));

        if (bArtPresent)
        {
            SelfTestCheck(Arena != nullptr && Arena->IsUsingArtBackdrop(),
                          TEXT("the arena presents the imported backdrop image"));

            // ---- One visible arena, measured ---------------------------------
            // The mandatory requirement as numbers rather than as an impression:
            // with the supplied image on screen no procedural piece may be drawn
            // around it, the picture must still carry the image's own aspect ratio
            // rather than a stretched one, its painted floor line must land on the
            // fighters' feet, and it must cover the frame on every side so no
            // background, floor or placeholder shows through.
            const int32 VisibleGreybox = Arena ? Arena->CountVisibleGreyboxPieces() : -1;
            const int32 TotalGreybox = Arena ? Arena->GetPieceCount() : 0;
            const FVector2D Picture = Arena ? Arena->GetPictureSizeCm() : FVector2D::ZeroVector;
            const float PictureAspect = (Picture.Y > 0.0f) ? (Picture.X / Picture.Y) : 0.0f;
            const float ImageAspect = Arena ? Arena->GetImageAspect() : 0.0f;
            const float FloorDelta =
                Arena ? FMath::Abs(Arena->GetFeetScreenPosition() - Arena->GetImageFloorScreenPosition())
                      : -1.0f;
            const float CoverMargin = Arena ? Arena->GetCoverMarginCm() : -1.0f;

            SelfTestLog += FString::Printf(
                TEXT("  arena: %d of %d greybox piece(s) visible | picture %.0f x %.0f cm, aspect ")
                TEXT("%.4f vs image %.4f | feet/floor delta %.5f | narrowest cover margin %.0f cm\n"),
                VisibleGreybox, TotalGreybox, Picture.X, Picture.Y, PictureAspect, ImageAspect,
                FloorDelta, CoverMargin);

            SelfTestCheck(VisibleGreybox == 0,
                          FString::Printf(TEXT("nothing else is drawn around the imported image ")
                                          TEXT("(%d of %d greybox piece(s) visible)"),
                                          VisibleGreybox, TotalGreybox));
            SelfTestCheck(Arena != nullptr && !Arena->IsGreyboxVisible(),
                          TEXT("the ring reports its own greybox ring as hidden"));
            SelfTestCheck(Picture.Y > 0.0f && FMath::IsNearlyEqual(PictureAspect, ImageAspect, 0.001f),
                          FString::Printf(TEXT("the picture keeps the image's aspect ratio ")
                                          TEXT("(image %.4f, picture %.4f)"),
                                          ImageAspect, PictureAspect));
            SelfTestCheck(FloorDelta >= 0.0f && FloorDelta <= 0.001f,
                          FString::Printf(TEXT("the fighters' feet sit on the picture's painted floor ")
                                          TEXT("line (delta %.5f of the half-frame)"),
                                          FloorDelta));
            SelfTestCheck(CoverMargin > 0.0f,
                          FString::Printf(TEXT("the picture covers the whole frame, with no background ")
                                          TEXT("or floor beside it (narrowest margin %.0f cm)"),
                                          CoverMargin));

            const RKC::ESide BothSides[2] = { RKC::ESide::A, RKC::ESide::B };
            for (const RKC::ESide Side : BothSides)
            {
                const TCHAR* const Label = (Side == RKC::ESide::A) ? TEXT("A") : TEXT("B");
                ARKCFighter* const Fighter = GetFighterActor(Side);
                SelfTestCheck(Fighter != nullptr && Fighter->IsUsingArtwork(),
                              FString::Printf(TEXT("fighter %s presents imported flipbooks"), Label));
                SelfTestCheck(Fighter != nullptr && Fighter->GetArtFlipbookCount() == 8,
                              FString::Printf(TEXT("fighter %s resolved all 8 animations (got %d)"),
                                              Label, Fighter ? Fighter->GetArtFlipbookCount() : 0));

                // The material the flipbook path actually shades with is recorded
                // rather than assumed. It is what decides whether a light, a
                // shadow or a 180-degree turn can darken a fighter at all, so it
                // answers from inside the running game a question that could not be
                // settled from outside it.
                UPaperFlipbookComponent* const FighterSprite =
                    (Fighter != nullptr) ? Fighter->GetSpriteComponent() : nullptr;
                UMaterialInterface* const SpriteMaterial =
                    (FighterSprite != nullptr) ? FighterSprite->GetMaterial(0) : nullptr;
                const FString MaterialName =
                    (SpriteMaterial != nullptr) ? SpriteMaterial->GetPathName() : FString(TEXT("(none)"));
                SelfTestLog += FString::Printf(TEXT("  fighter %s sprite material: %s\n"), Label,
                                               *MaterialName);
                SelfTestCheck(SpriteMaterial != nullptr &&
                                  (MaterialName.Contains(TEXT("Unlit")) ||
                                   MaterialName.Contains(TEXT("M_RKC_Sprite"))),
                              FString::Printf(TEXT("fighter %s draws with an unlit sprite material ")
                                              TEXT("(found %s)"),
                                              Label, *MaterialName));
            }
        }
        else
        {
            SelfTestLog += TEXT("  artwork not imported; the greybox presentation is the ")
                           TEXT("expected result here and is not a failure\n");
        }
    }

    SelfTestLog += TEXT("\n--- MATCHES (real director loop, fixed 1/60 s steps) ---\n");

    const float Dt = RKC::FCombatSim::FixedTickSeconds;
    const int32 TickBudget = 60 * 60 * 10;
    const uint32 SeedBaseA = Setup.BotSeedA;
    const uint32 SeedBaseB = Setup.BotSeedB;

    int32 Timeouts = 0;
    int32 Undecided = 0;
    int32 TotalTicks = 0;
    int32 TotalHits = 0;
    float WorstSeparation = 0.0f;

    // The arena's piece count before any match, so a restart that rebuilt the ring or
    // spawned a second one shows up as a changed number rather than as an impression.
    const int32 ArenaPiecesBeforeMatches = GetArenaPieceCount();

    for (int32 Index = 0; Index < SelfTestMatchesRequested; ++Index)
    {
        Setup.BotSeedA = SeedBaseA + static_cast<uint32>(Index * 2);
        Setup.BotSeedB = SeedBaseB + static_cast<uint32>(Index * 2 + 1);
        RestartMatch();

        // ---- The ring, again, after every restart -----------------------------
        // A rematch re-runs the setup path, which is exactly where a second ring
        // would appear if the setup spawned one per match or rebuilt the existing one
        // on top of itself. The pieces' own visibility flags and the piece count are
        // both read, so a re-shown ring and a doubled-up ring are both caught, and the
        // check is repeated for every restart rather than only for the first match.
        if (Arena != nullptr)
        {
            const int32 VisibleGreybox = Arena->CountVisibleGreyboxPieces();
            const int32 Pieces = GetArenaPieceCount();
            SelfTestCheck(VisibleGreybox == 0 && Pieces == ArenaPiecesBeforeMatches,
                          FString::Printf(TEXT("match %d restart: still one visible arena ")
                                          TEXT("(%d greybox piece(s) visible, %d piece(s) built, was %d)"),
                                          Index + 1, VisibleGreybox, Pieces, ArenaPiecesBeforeMatches));
            SelfTestCheck(!RKCArt::IsArtAvailable() || Arena->IsUsingArtBackdrop(),
                          FString::Printf(TEXT("match %d restart: the imported image is still the arena"),
                                          Index + 1));
        }

        int32 Ticks = 0;
        // Side order is checked on EVERY simulated frame, not only at the end.
        // A fighter drawn on the wrong side of its opponent is a momentary thing -
        // it happens while a fighter is crossing over or while a rematch is being
        // set up - so an end-of-match snapshot can easily miss it. Both the rules
        // and the pictures are sampled: the simulation's own X for each side, and
        // the actor X the renderer actually uses.
        int32 SideOrderBreaksSim = 0;
        int32 SideOrderBreaksActors = 0;
        int32 StaleFightBannerFrames = 0;
        while (!Sim.IsMatchOver() && Ticks < TickBudget)
        {
            AdvanceMatchFrame(Dt);
            ++Ticks;
            if (Sim.IsFighting() &&
                Sim.GetRules().RoundSeconds - Sim.GetMatch().RoundTimeRemaining > 1.2f &&
                BannerText == TEXT("FIGHT!"))
            {
                ++StaleFightBannerFrames;
            }

            if (Sim.GetFighter(RKC::ESide::A).PositionX > Sim.GetFighter(RKC::ESide::B).PositionX)
            {
                ++SideOrderBreaksSim;
            }
            if (FighterA != nullptr && FighterB != nullptr &&
                FighterA->GetActorLocation().X > FighterB->GetActorLocation().X)
            {
                ++SideOrderBreaksActors;
            }
        }

        SelfTestCheck(StaleFightBannerFrames == 0,
                      FString::Printf(TEXT("match %d clears FIGHT after the introduction and does not redisplay it (%d stale frames)"),
                                      Index + 1, StaleFightBannerFrames));

        // Where the fighters are, measured on both sides of the presentation
        // boundary. Each sprite reads its X straight off the simulation, so if the
        // two agree and the separation is small, fighters that look merged are the
        // rule set (58 cm) plus the 1 px = 1 cm sprite scale, not a placement bug.
        // The footprint reports that scale as a number instead of an impression.
        const float SimXA = Sim.GetFighter(RKC::ESide::A).PositionX;
        const float SimXB = Sim.GetFighter(RKC::ESide::B).PositionX;
        const float ActorXA = FighterA ? FighterA->GetActorLocation().X : 0.0f;
        const float ActorXB = FighterB ? FighterB->GetActorLocation().X : 0.0f;
        const FVector FootprintA = FighterA ? FighterA->GetComponentsBoundingBox(true).GetSize()
                                            : FVector::ZeroVector;
        SelfTestLog += FString::Printf(
            TEXT("  fighters: sim A x=%.1f B x=%.1f (sep %.1f cm) | actor A x=%.1f B x=%.1f | "
                 "A footprint %.0f x %.0f x %.0f cm\n"),
            SimXA, SimXB, FMath::Abs(SimXB - SimXA), ActorXA, ActorXB, FootprintA.X,
            FootprintA.Y, FootprintA.Z);
        SelfTestCheck(FMath::IsNearlyEqual(SimXA, ActorXA, 1.0f) &&
                          FMath::IsNearlyEqual(SimXB, ActorXB, 1.0f),
                      FString::Printf(TEXT("match %d draws each fighter at the simulation's own X"),
                                      Index + 1));

        // The side-order invariant. Side A is the left fighter and side B is the
        // right fighter for the whole contest; a human who chooses the first
        // character is side A, so this is the machine-readable form of "the player
        // is never put on the wrong side". Checked on every frame above.
        SelfTestCheck(SideOrderBreaksSim == 0,
                      FString::Printf(TEXT("match %d keeps side A left of side B in the rules on all "
                                           "%d frame(s) (breaks %d)"),
                                      Index + 1, Ticks, SideOrderBreaksSim));
        SelfTestCheck(SideOrderBreaksActors == 0,
                      FString::Printf(TEXT("match %d draws side A left of side B on all %d frame(s) "
                                           "(breaks %d)"),
                                      Index + 1, Ticks, SideOrderBreaksActors));

        const RKC::FMatchState& M = Sim.GetMatch();
        const bool bTimedOut = !Sim.IsMatchOver();
        const uint32 Checksum = ComputeStateChecksum(Sim);
        const int32 HitsA = Sim.GetFighter(RKC::ESide::A).HitsLandedTotal;
        const int32 HitsB = Sim.GetFighter(RKC::ESide::B).HitsLandedTotal;

        SelfTestChecksums.Add(Checksum);
        TotalTicks += Ticks;
        TotalHits += HitsA + HitsB;
        WorstSeparation = FMath::Max(WorstSeparation, WorstSeparationViolation);

        // What the audio layer owes for this match: one round-start cue per round
        // that started, and one round-end cue per round that actually broke. Both
        // are read back by the presentation half, which is why they are counted
        // here rather than inferred there.
        SelfTestRoundsStarted += M.Round;
        SelfTestRoundsThatBroke += M.Round - (SelfTestFinalRoundBroke(M) ? 0 : 1);

        if (bTimedOut)
        {
            ++Timeouts;
        }
        if (M.Result == RKC::EMatchResult::Undecided)
        {
            ++Undecided;
        }

        const FString Line = FString::Printf(
            TEXT("match %d: seeds %u/%u  ticks %d (%.1f s)  result %d  end %d  score %d-%d  "
                 "hits %d-%d  checksum 0x%08X"),
            Index + 1, Setup.BotSeedA, Setup.BotSeedB, Ticks,
            static_cast<float>(Ticks) * Dt, static_cast<int32>(M.Result),
            static_cast<int32>(M.EndReason), M.ScoreA, M.ScoreB, HitsA, HitsB, Checksum);
        SelfTestMatchLines.Add(Line);
        SelfTestLog += Line + TEXT("\n");
        UE_LOG(LogRockyKombat, Log, TEXT("[self-test] %s"), *Line);

        SelfTestCheck(!bTimedOut,
                      FString::Printf(TEXT("match %d finished inside the tick budget"), Index + 1));
        SelfTestCheck(M.Result != RKC::EMatchResult::Undecided,
                      FString::Printf(TEXT("match %d has a decided result"), Index + 1));
        SelfTestCheck(BannerText.Len() > 0,
                      FString::Printf(TEXT("match %d raised a result banner (\"%s\")"), Index + 1,
                                      *BannerText));
        SelfTestCheck(EventFeed.Num() > 0,
                      FString::Printf(TEXT("match %d filled the HUD event feed (%d lines)"),
                                      Index + 1, EventFeed.Num()));
        SelfTestCheck(TotalHits > 0, TEXT("the matches produced landed punches"));
    }

    SelfTestCheck(Timeouts == 0, TEXT("no match timed out"));
    SelfTestCheck(Undecided == 0, TEXT("no match was left undecided"));
    SelfTestCheck(WorstSeparation <= 0.0005f,
                  FString::Printf(TEXT("no tick breached minimum separation (worst %.6f cm)"),
                                  WorstSeparation));
    SelfTestLog += FString::Printf(TEXT("total: %d tick(s) simulated, %d landed punch(es)\n"),
                                   TotalTicks, TotalHits);

    SelfTestLog += TEXT("\n--- ENGINE vs PURE-C++ CROSS-CHECK ---\n");
    {
        FString CrossReport;
        const bool bAgrees = CrossCheckAgainstStandalone(CrossReport);
        SelfTestLog += CrossReport + TEXT("\n");
        SelfTestCheck(bAgrees,
                      TEXT("the live engine match and the pure-C++ harness agree on the same seeds"));
    }

    SelfTestLog += TEXT("\n--- DETERMINISM ---\n");
    {
        const uint32 Before = ComputeStateChecksum(Sim);

        // Re-run the final seed pair from scratch. A copied checksum would prove
        // only that nothing changed since; re-running proves the match itself
        // reproduces.
        RestartMatch();
        int32 Ticks = 0;
        while (!Sim.IsMatchOver() && Ticks < TickBudget)
        {
            AdvanceMatchFrame(Dt);
            ++Ticks;
        }
        const uint32 After = ComputeStateChecksum(Sim);

        // The repeat run is a real match through the same director loop, so it
        // starts a round and - unless it ends by knockout - breaks one too. Counted
        // here as well, because the audio checks read these totals afterwards and a
        // round that was started but not counted would show up as a missing cue.
        {
            const RKC::FMatchState& Repeat = Sim.GetMatch();
            SelfTestRoundsStarted += Repeat.Round;
            SelfTestRoundsThatBroke += Repeat.Round - (SelfTestFinalRoundBroke(Repeat) ? 0 : 1);
        }

        SelfTestLog += FString::Printf(TEXT("repeat run: ticks %d, checksum 0x%08X vs 0x%08X\n"), Ticks,
                                       After, Before);
        SelfTestCheck(After == Before, TEXT("repeating the same seeds reproduces the same final state"));

        bool bSeedsMatter = false;
        for (const uint32 Candidate : SelfTestChecksums)
        {
            if (Candidate != SelfTestChecksums[0])
            {
                bSeedsMatter = true;
                break;
            }
        }
        SelfTestLog += FString::Printf(TEXT("distinct match checksums: %s\n"),
                                       bSeedsMatter ? TEXT("yes") : TEXT("no"));
        SelfTestCheck(SelfTestChecksums.Num() < 2 || bSeedsMatter,
                      TEXT("different seeds produce different matches"));
    }

    // ---- The arena after everything -----------------------------------------
    // Whatever the run settles on, the thing left on screen must still be the
    // supplied image alone with no second ring: this is the state a player is in
    // during a rematch, and the determinism re-run above restarts the match too.
    if (Arena != nullptr)
    {
        const int32 VisibleGreybox = Arena->CountVisibleGreyboxPieces();
        const int32 Pieces = GetArenaPieceCount();
        SelfTestCheck(VisibleGreybox == 0 && Pieces == ArenaPiecesBeforeMatches,
                      FString::Printf(TEXT("at the end of the run the arena is still one ring ")
                                      TEXT("(%d greybox piece(s) visible, %d piece(s) built)"),
                                      VisibleGreybox, Pieces));
        SelfTestCheck(!RKCArt::IsArtAvailable() || Arena->IsUsingArtBackdrop(),
                      TEXT("the imported image is still the arena at the end of the run"));
    }

    // ---- Roster change on a reused actor ------------------------------------
    // Every match above restarts through RestartMatch(), which reuses the same two
    // fighter actors instead of spawning new ones - exactly what a rematch does. The
    // self-test used to keep the same roster for all of its matches, so it could not
    // see what a captured rematch run showed: a player who picked the other character
    // kept being drawn as the previous one. The swap below reproduces that rematch on
    // the live actors and reads back the flipbook that is actually on screen, not the
    // one the code meant to show.
    if (FighterA != nullptr && FighterB != nullptr && RKCArt::IsArtAvailable())
    {
        const RKC::ERoster OriginalA = Setup.FighterA;
        const RKC::ERoster OriginalB = Setup.FighterB;
        const auto Other = [](RKC::ERoster Who)
        { return (Who == RKC::ERoster::Rocky) ? RKC::ERoster::Shadow : RKC::ERoster::Rocky; };
        const RKC::ERoster SwappedA = Other(OriginalA);
        const RKC::ERoster SwappedB = Other(OriginalB);
        const float SelfTestFightLineY = Arena ? Arena->GetFightLineY() : 0.0f;

        // Which character's artwork a flipbook belongs to is read from the roster's own
        // art path, not spelled out here by hand: FlipbookDir ends in
        // .../Characters/<Roster>/Flipbooks and every clip is named FB_<Roster>_<Anim>,
        // so the segment before "Flipbooks" is exactly the prefix to test for.
        //
        // The comparison is a prefix, not a single expected clip, because Init() finishes
        // by syncing to the simulation: right after a swap the sim can already be in
        // knockdown or victory (the check runs after the matches), so which clip is
        // showing is the sim's business. What matters here is WHOSE clip it is.
        const auto ArtPrefix = [](RKC::ERoster Who)
        {
            const FString Dir = RKCArt::FlipbookDir(Who);
            TArray<FString> Segments;
            Dir.ParseIntoArray(Segments, TEXT("/"), true);
            const FString Token =
                (Segments.Num() >= 2) ? Segments[Segments.Num() - 2] : FString();
            return FString::Printf(TEXT("FB_%s_"), *Token);
        };

        FighterA->Init(&Sim, RKC::ESide::A, SwappedA, SelfTestFightLineY);
        FighterB->Init(&Sim, RKC::ESide::B, SwappedB, SelfTestFightLineY);

        SelfTestCheck(FighterA->GetRoster() == SwappedA && FighterB->GetRoster() == SwappedB,
                      TEXT("roster swap: both reused actors carry the newly selected roster"));
        SelfTestCheck(FighterA->GetCurrentArtName().StartsWith(ArtPrefix(SwappedA)),
                      FString::Printf(TEXT("roster swap: side A presents the new character's art ")
                                      TEXT("(showing %s, expected a %s clip)"),
                                      *FighterA->GetCurrentArtName(), *ArtPrefix(SwappedA)));
        SelfTestCheck(FighterB->GetCurrentArtName().StartsWith(ArtPrefix(SwappedB)),
                      FString::Printf(TEXT("roster swap: side B presents the new character's art ")
                                      TEXT("(showing %s, expected a %s clip)"),
                                      *FighterB->GetCurrentArtName(), *ArtPrefix(SwappedB)));
        SelfTestCheck(FighterA->IsUsingArtwork() && FighterB->IsUsingArtwork(),
                      TEXT("roster swap: the swap did not fall back to the greybox rig"));

        // Swap back, so the run ends on the roster it started with and the reverse
        // direction of the change is proven too.
        FighterA->Init(&Sim, RKC::ESide::A, OriginalA, SelfTestFightLineY);
        FighterB->Init(&Sim, RKC::ESide::B, OriginalB, SelfTestFightLineY);
        SelfTestCheck(FighterA->GetCurrentArtName().StartsWith(ArtPrefix(OriginalA)) &&
                          FighterB->GetCurrentArtName().StartsWith(ArtPrefix(OriginalB)),
                      FString::Printf(TEXT("roster swap back: original artwork restored ")
                                      TEXT("(A %s, B %s)"),
                                      *FighterA->GetCurrentArtName(), *FighterB->GetCurrentArtName()));
    }

    // ---- Application lifecycle ----------------------------------------------
    // Backgrounding is a phone's home button, an incoming call, or a desktop
    // window losing focus. The promises are narrow enough to measure: no input
    // survives the gap, the music is frozen where it is rather than stopped or
    // restarted, and the first input the simulation would read after the return is
    // neutral, so a key or a thumb that was held while the window was inactive
    // cannot fire an attack into a match nobody was watching.
    if (ARKCPlayerController* LifecyclePC = GetRKCPlayerController())
    {
        SelfTestLog += TEXT("\n--- APPLICATION LIFECYCLE (background and return) ---\n");

        RKCTouch::FController& Pad = LifecyclePC->GetTouchController();
        const bool bPadWasEnabled = Pad.IsEnabled();
        const bool bPadWasForced = Pad.IsForcedVisible();

        // Real control positions, taken from the pad's own layout and confirmed by
        // asking the pad what it thinks is there, so an overlapping layout cannot
        // make this hold the wrong control.
        Pad.SetEnabled(true);
        const FVector2D PadViewport = Pad.GetViewportSize();
        const FMargin PadInsets = Pad.GetSafeInsetsPx();
        FVector2D StickCentre = FVector2D::ZeroVector;
        FVector2D BlockCentre = FVector2D::ZeroVector;
        bool bHaveStick = false;
        bool bHaveBlock = false;
        for (const RKCTouch::FWidget& Widget : Pad.GetWidgets())
        {
            const FVector2D Centre = RKCTouch::FController::CentrePx(Widget, PadViewport, PadInsets);
            if (Pad.ControlAt(Centre) != Widget.Control)
            {
                continue;
            }
            if (Widget.Control == RKCTouch::EControl::Stick && !bHaveStick)
            {
                StickCentre = Centre;
                bHaveStick = true;
            }
            else if (Widget.Control == RKCTouch::EControl::Block && !bHaveBlock)
            {
                BlockCentre = Centre;
                bHaveBlock = true;
            }
        }

        // Two fingers on the pad - one owning the stick and pushed off centre, one
        // resting on the guard - plus a keyboard latch with a movement key and the
        // guard held and one punch still pending. That is the state a player is in
        // the moment the window goes away.
        int32 PadFingers = 0;
        if (bHaveStick && Pad.TouchBegin(0, StickCentre))
        {
            ++PadFingers;
            Pad.TouchMove(0, StickCentre + FVector2D(PadViewport.X * 0.10f, 0.0f));
        }
        if (bHaveBlock && Pad.TouchBegin(1, BlockCentre))
        {
            ++PadFingers;
        }
        LifecyclePC->SimulateHeldKeyboardForTest();

        SelfTestCheck(PadFingers == 2 && Pad.IsStickActive() && Pad.IsBlockHeld(),
                      FString::Printf(TEXT("lifecycle: %d pad finger(s) holding the stick (%s) and the ")
                                      TEXT("guard (%s)"),
                                      PadFingers, Pad.IsStickActive() ? TEXT("yes") : TEXT("no"),
                                      Pad.IsBlockHeld() ? TEXT("yes") : TEXT("no")));
        SelfTestCheck(LifecyclePC->GetLatchedPressCount() > 0,
                      FString::Printf(TEXT("lifecycle: %d latched keyboard press(es) are pending, so ")
                                      TEXT("the drop below has something to clear"),
                                      LifecyclePC->GetLatchedPressCount()));
        SelfTestCheck(LifecyclePC->IsAnyHumanInputHeld(),
                      TEXT("lifecycle: held input is detected before the application goes away"));

        // A known un-paused audio state, so the freeze below is caused by the
        // transition under test rather than by something earlier in the run.
        int32 MusicStartsBefore = 0;
        int32 MusicStopsBefore = 0;
        int32 PauseRequestsBefore = 0;
        int32 ResumeRequestsBefore = 0;
        if (AudioSystem != nullptr)
        {
            AudioSystem->OnGameResumed();
            MusicStartsBefore = AudioSystem->GetDiagnostics().MusicStarts;
            MusicStopsBefore = AudioSystem->GetDiagnostics().MusicStops;
            PauseRequestsBefore = AudioSystem->GetDiagnostics().PauseRequests;
            ResumeRequestsBefore = AudioSystem->GetDiagnostics().ResumeRequests;
        }
        SelfTestLog += FString::Printf(TEXT("  controller: input enabled %s, gameplay routing %s, pad ")
                                       TEXT("enabled %s\n"),
                                       LifecyclePC->IsInputEnabled() ? TEXT("yes") : TEXT("no"),
                                       LifecyclePC->IsGameplayRouting() ? TEXT("yes") : TEXT("no"),
                                       bPadWasEnabled ? TEXT("yes") : TEXT("no"));
        const int32 TouchCancellationsBefore = Pad.GetDiagnostics().Cancellations;

        HandleApplicationBackgrounded();

        SelfTestCheck(!LifecyclePC->IsAnyHumanInputHeld(),
                      TEXT("lifecycle: no held input survives backgrounding ")
                      TEXT("(no held guard, no movement axis, no pending press, no finger)"));
        SelfTestCheck(LifecyclePC->GetLatchedPressCount() == 0,
                      FString::Printf(TEXT("lifecycle: the pending keyboard press(es) were dropped ")
                                      TEXT("(%d left)"),
                                      LifecyclePC->GetLatchedPressCount()));
        SelfTestCheck(Pad.GetTrackedFingerCount() == 0 && !Pad.IsStickActive() && !Pad.IsBlockHeld(),
                      FString::Printf(TEXT("lifecycle: the pad dropped every finger (tracked %d), the ")
                                      TEXT("stick and the guard"),
                                      Pad.GetTrackedFingerCount()));
        SelfTestCheck(Pad.GetDiagnostics().Cancellations > TouchCancellationsBefore,
                      FString::Printf(TEXT("lifecycle: the touches were cancelled rather than left ")
                                      TEXT("hanging (%d cancellation(s))\n"),
                                      Pad.GetDiagnostics().Cancellations));
        {
            const RKC::FInputState HeldAcrossGap = LifecyclePC->ConsumeInputForSide(RKC::ESide::A);
            RKC::FInputState PadState;
            Pad.ApplyTo(PadState);
            const bool bKeyboardNeutral = !HeldAcrossGap.bJabPressed && !HeldAcrossGap.bHeavyPressed &&
                                          !HeldAcrossGap.bDodgePressed && !HeldAcrossGap.bHoldBlock &&
                                          !HeldAcrossGap.bMoveLeft && !HeldAcrossGap.bMoveRight;
            const bool bPadNeutral = !PadState.bJabPressed && !PadState.bHeavyPressed &&
                                     !PadState.bDodgePressed && !PadState.bHoldBlock &&
                                     !PadState.bMoveLeft && !PadState.bMoveRight;
            SelfTestCheck(bKeyboardNeutral,
                          TEXT("lifecycle: the first keyboard input the simulation reads after the gap ")
                          TEXT("is neutral, so no attack fires from a key held across it"));
            SelfTestCheck(bPadNeutral,
                          TEXT("lifecycle: the first pad input after the gap is neutral too ")
                          TEXT("(no phantom punch from a thumb left on a button)"));
        }
        if (AudioSystem != nullptr)
        {
            const FRKCAudioDiagnostics& A = AudioSystem->GetDiagnostics();
            SelfTestCheck(AudioSystem->IsPaused(),
                          TEXT("lifecycle: the music is frozen while the application is away"));
            SelfTestCheck(A.PauseRequests == PauseRequestsBefore + 1,
                          FString::Printf(TEXT("lifecycle: the freeze request reached the audio layer ")
                                          TEXT("exactly once (%d -> %d, counted whether or not a device ")
                                          TEXT("was live)"),
                                          PauseRequestsBefore, A.PauseRequests));
            SelfTestCheck(A.MusicStarts == MusicStartsBefore && A.MusicStops == MusicStopsBefore,
                          FString::Printf(TEXT("lifecycle: freezing did not stop or restart the music ")
                                          TEXT("(starts %d -> %d, stops %d -> %d)"),
                                          MusicStartsBefore, A.MusicStarts, MusicStopsBefore, A.MusicStops));
        }

        HandleApplicationForegrounded();

        SelfTestCheck(!LifecyclePC->IsAnyHumanInputHeld(),
                      TEXT("lifecycle: returning clears input as well, so a key held across the gap ")
                      TEXT("is not a press"));
        if (AudioSystem != nullptr)
        {
            const FRKCAudioDiagnostics& A = AudioSystem->GetDiagnostics();
            SelfTestCheck(!AudioSystem->IsPaused(), TEXT("lifecycle: the music resumes on return"));
            SelfTestCheck(A.ResumeRequests == ResumeRequestsBefore + 1,
                          FString::Printf(TEXT("lifecycle: the resume request reached the audio layer ")
                                          TEXT("exactly once (%d -> %d), so the freeze is undone rather ")
                                          TEXT("than left behind"),
                                          ResumeRequestsBefore, A.ResumeRequests));
            SelfTestCheck(A.MusicStarts == MusicStartsBefore,
                          FString::Printf(TEXT("lifecycle: the return resumed the track that was there ")
                                          TEXT("rather than starting another (starts %d, resumes %d)"),
                                          A.MusicStarts, A.MusicResumes));
        }

        // Put the pad back exactly as the run found it.
        Pad.SetForcedVisible(bPadWasForced);
        Pad.SetEnabled(bPadWasEnabled);
    }
}

FString ARKCGameMode::WriteSelfTestRecord()
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Evidence");
    IFileManager::Get().MakeDirectory(*Directory, true);

    const FString FileName =
        FString::Printf(TEXT("RKC_SELFTEST_%s.txt"),
                        *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
    const FString FullPath = Directory / FileName;

    if (!FFileHelper::SaveStringToFile(SelfTestLog, *FullPath))
    {
        UE_LOG(LogRockyKombat, Error, TEXT("self-test could not write its record to %s"), *FullPath);
        return FString();
    }

    UE_LOG(LogRockyKombat, Display, TEXT("self-test record written: %s"), *FullPath);
    return FullPath;
}

void ARKCGameMode::FinishSelfTestAndExit()
{
    bSelfTestExitScheduled = true;

    const bool bHeadless = FParse::Param(FCommandLine::Get(), TEXT("nullrhi"));

    // Ask the HUD what it actually drew. A HUD that is assigned but never draws
    // is exactly the failure this check exists to catch, so the numbers come from
    // the live HUD object rather than from the fact that HUDClass was set.
    if (ARKCPlayerController* PC = GetRKCPlayerController())
    {
        if (ARKCHUD* Hud = Cast<ARKCHUD>(PC->GetHUD()))
        {
            SelfTestHudFrames = Hud->GetDrawnFrameCount();
            SelfTestHudRegions = Hud->GetLastRegionCount();
            SelfTestHudCanvas = Hud->GetLastCanvasSize();
        }
    }

    SelfTestLog += TEXT("\n--- HUD ---\n");
    if (SelfTestHudFrames < 0)
    {
        SelfTestLog += TEXT("no HUD object was reachable, so HUD drawing was NOT verified\n");
        SelfTestCheck(false, TEXT("the match HUD object is reachable"));
    }
    else
    {
        SelfTestLog += FString::Printf(TEXT("drawn frames %d, layout regions %d, canvas %dx%d\n"),
                                       SelfTestHudFrames, SelfTestHudRegions, SelfTestHudCanvas.X,
                                       SelfTestHudCanvas.Y);
        if (bHeadless)
        {
            SelfTestLog += TEXT("headless run (-nullrhi): there is no canvas, so the draw counts "
                                "above are reported but not asserted. Run again without -nullrhi "
                                "to verify the HUD actually draws.\n");
        }
        else
        {
            SelfTestCheck(SelfTestHudFrames > 0, TEXT("the HUD completed at least one draw pass"));
            SelfTestCheck(SelfTestHudRegions >= 5,
                          FString::Printf(TEXT("the HUD emitted the full layout (%d regions)"),
                                          SelfTestHudRegions));
            SelfTestCheck(SelfTestHudCanvas.X > 0 && SelfTestHudCanvas.Y > 0,
                          TEXT("the HUD drew into a real canvas"));
        }
    }

    SelfTestLog += FString::Printf(TEXT("\n--- RESULT ---\nfailed checks: %d\nresult:        %s\n"),
                                   SelfTestFailures,
                                   (SelfTestFailures == 0) ? TEXT("PASS") : TEXT("FAIL"));

    const FString RecordPath = WriteSelfTestRecord();

    UE_LOG(LogRockyKombat, Display, TEXT("RKC SELF-TEST %s: %d failed check(s), record: %s"),
           (SelfTestFailures == 0) ? TEXT("PASS") : TEXT("FAIL"), SelfTestFailures,
           RecordPath.IsEmpty() ? TEXT("(not written)") : *RecordPath);

    // Exit with a machine-readable status so a CI job or an acceptance gate can
    // take the verdict from the process exit code.
    FGenericPlatformMisc::RequestExitWithStatus(true, (SelfTestFailures == 0) ? 0 : 1);
}

// ---------------------------------------------------------------------------
// Arena framing
// ---------------------------------------------------------------------------

void ARKCGameMode::UpdateArenaFraming(float DeltaSeconds)
{
    if (Arena == nullptr)
    {
        return;
    }

    // Describe the camera that is really in the world rather than the setup values,
    // so a console override of the field of view, the distance, the height or the
    // pitch is framed for on the next frame without a rebuild. Before BuildCamera has
    // run, the setup values describe the very camera the director is about to spawn.
    float FOV = Setup.CameraFOV;
    float Distance = Setup.CameraDistance;
    float Height = Setup.CameraHeight;
    float Pitch = Setup.CameraPitch;

    if (MatchCamera != nullptr)
    {
        const FVector CameraLocation = MatchCamera->GetActorLocation();
        Distance = FMath::Max(CameraLocation.Y, 1.0f);
        Height = CameraLocation.Z;
        Pitch = MatchCamera->GetActorRotation().Pitch;
        if (UCameraComponent* Camera = MatchCamera->GetCameraComponent())
        {
            FOV = Camera->FieldOfView;
        }
    }

    // Which axis that field of view describes is the engine's aspect-ratio axis
    // constraint, so it is read rather than assumed. UE 5.3 changed the default to
    // keeping the VERTICAL field of view: the camera angle is documented as
    // horizontal, but the constraint converts it through the aspect ratio, so the
    // same number means a different view depending on this setting. The arena has to
    // know which half-angle it is framing for. EAspectRatioAxisConstraint is 0 for
    // Maintain Y FOV, 1 for Maintain X FOV and 2 for Major Axis FOV, which lands on
    // the horizontal axis in the landscape windows a match is played in.
    // Config\DefaultEngine.ini pins 0, so the meaning cannot drift with an engine
    // default; reading it here means a deliberate change to that file still frames
    // correctly instead of silently mis-framing.
    bool bFOVIsHorizontal = false;
    if (const ARKCPlayerController* PlayerController = GetRKCPlayerController())
    {
        if (const ULocalPlayer* Player = Cast<ULocalPlayer>(PlayerController->GetLocalPlayer()))
        {
            const int32 Constraint = static_cast<int32>(Player->AspectRatioAxisConstraint.GetValue());
            bFOVIsHorizontal = (Constraint == 1) || (Constraint == 2);
        }
    }

    Arena->SetFramingCamera(FOV, bFOVIsHorizontal, Distance, Height, Pitch);

    // ---- The shape of the view that is actually rendered ---------------------
    FIntPoint Size = FIntPoint::ZeroValue;
    if (GEngine != nullptr && GEngine->GameViewport != nullptr &&
        GEngine->GameViewport->Viewport != nullptr)
    {
        Size = GEngine->GameViewport->Viewport->GetSizeXY();
    }
    if (Size.X <= 0 || Size.Y <= 0)
    {
        // No measured window yet, which is normal on the first frames of a headless
        // run. The arena keeps the 16:9 framing it starts with and is re-framed as
        // soon as there is a size to frame for.
        return;
    }

    const float WindowAspect = static_cast<float>(Size.X) / static_cast<float>(Size.Y);
    const bool bLetterbox = WindowAspect < ArenaMinimumAspect;
    const float RenderAspect = bLetterbox ? ArenaMinimumAspect : WindowAspect;

    if (MatchCamera != nullptr && (bLetterbox != bLetterboxApplied ||
                                   !FMath::IsNearlyEqual(RenderAspect, RenderAspectApplied, 0.0005f)))
    {
        if (UCameraComponent* Camera = MatchCamera->GetCameraComponent())
        {
            // A window narrower than 4:3 is rendered as a 4:3 view with bars rather
            // than a tall thin one. The arena is then framed - and its picture
            // cropped - to cover that 4:3 view, which is the same cover framing every
            // landscape aspect gets.
            Camera->AspectRatio = ArenaMinimumAspect;
            Camera->SetConstraintAspectRatio(bLetterbox);
        }

        bLetterboxApplied = bLetterbox;
        RenderAspectApplied = RenderAspect;

        UE_LOG(LogRockyKombat, Log,
               TEXT("Arena framing: window aspect %.4f (%d x %d) rendered as %.4f, 4:3 letterbox %s."),
               WindowAspect, Size.X, Size.Y, RenderAspect, bLetterbox ? TEXT("on") : TEXT("off"));
    }

    Arena->UpdatePresentation(DeltaSeconds, RenderAspect);
}
