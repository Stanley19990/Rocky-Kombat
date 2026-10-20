// RockyKombatModule.cpp
//
// Primary game module implementation, the shared log category, and the two
// pieces of engine plumbing the rest of the project depends on: the build stamp
// every run writes about itself, and the command-line console overrides.
//
// WHY THE MODULE APPLIES THE SWITCHES ITSELF
// This project documents  -rkc.<Name>=<Value>  on the command line as the way to
// configure an unattended run: a quick-rules smoke match, a scripted menu, a
// capture run, fixed bot seeds. Two things have to be true for that to work, and
// only one of them was.
//   1. The console variable exists and is read by the code that needs it. (True.)
//   2. Something applies the switch to that variable. (False: UE 5.8 has no
//      FConsoleManager::SetFromCommandLine and no equivalent pass, so a switch
//      that nobody reads is silently inert - the run then behaves as if the
//      option had been left at its default, which is exactly the kind of quiet
//      difference between "what was asked for" and "what ran" that an evidence
//      run must not have.)
// So the switches are applied here, through the console manager's own setter, as
// soon as this module loads: after the static initialisers that register the
// variables and before any actor exists. A switch that names something unknown
// is reported rather than skipped in silence.

#include "RockyKombat.h"

#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogRockyKombat);

namespace
{
/** Applies every -rkc.<Name>=<Value> switch on the command line to its console
 *  variable. Anything that is not a known variable is logged and left alone: the
 *  command line also carries engine switches, and inventing a variable for a
 *  typo would hide the typo. Returns how many overrides were applied. */
int32 ApplyCommandLineConsoleOverrides()
{
    const TCHAR* Cursor = FCommandLine::Get();
    FString Token;
    int32 Applied = 0;

    while (FParse::Token(Cursor, Token, /*bUseEscape=*/false))
    {
        if (!Token.StartsWith(TEXT("-rkc.")))
        {
            continue;
        }

        FString Name;
        FString Value;
        if (!Token.RightChop(1).Split(TEXT("="), &Name, &Value))
        {
            // A bare -rkc.Name is not an override. The console variables are set
            // with =, and the console COMMANDS of the same name are run from the
            // console or from -ExecCmds.
            UE_LOG(LogRockyKombat, Warning,
                   TEXT("command line: '%s' carries no value, so nothing was applied"),
                   *Token);
            continue;
        }

        IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name);
        if (Variable == nullptr)
        {
            UE_LOG(LogRockyKombat, Warning,
                   TEXT("command line: no console variable is named '%s'; '%s' was not applied"),
                   *Name, *Token);
            continue;
        }

        // Command-line priority, so a later default or ini value cannot quietly
        // win over what the run was asked to do.
        Variable->Set(*Value, ECVF_SetByCommandline);
        UE_LOG(LogRockyKombat, Display, TEXT("command line override: %s = %s"), *Name, *Value);
        ++Applied;
    }

    if (Applied > 0)
    {
        UE_LOG(LogRockyKombat, Display,
               TEXT("command line: %d console override(s) applied before the first frame"), Applied);
    }
    return Applied;
}

/** The build identifier a run writes about itself.
 *
 *  WHY THIS EXISTS AT ALL: a packaged executable sitting on disk is not the only
 *  one that ever was, and a window that looks the same says nothing about which
 *  binary produced it. The stamp comes from the compiler rather than from a
 *  hand-edited string, so the number a build reports cannot drift away from the
 *  build that is actually running, and the plain file written beside Saved/ lets
 *  a launched build be identified without reading a log at all. */
void ReportBuildStamp()
{
    const FString Compiled = FString::Printf(TEXT("%s %s"), ANSI_TO_TCHAR(__DATE__),
                                             ANSI_TO_TCHAR(__TIME__));
    const FString Engine = FEngineVersion::Current().ToString();
    const FString Started = FDateTime::Now().ToString();

    UE_LOG(LogRockyKombat, Display, TEXT("build stamp: compiled %s"), *Compiled);
    UE_LOG(LogRockyKombat, Display, TEXT("build stamp: engine %s"), *Engine);
    UE_LOG(LogRockyKombat, Display, TEXT("build stamp: this run started %s"), *Started);

    const FString Body = FString::Printf(
        TEXT("Rocky Kombat build stamp\n")
        TEXT("compiled:      %s\n")
        TEXT("engine:        %s\n")
        TEXT("run started:   %s\n")
        TEXT("command line:  %s\n"),
        *Compiled, *Engine, *Started, FCommandLine::Get());

    const FString File = FPaths::ProjectSavedDir() / TEXT("BuildStamp.txt");
    if (FFileHelper::SaveStringToFile(Body, *File))
    {
        UE_LOG(LogRockyKombat, Display, TEXT("build stamp: written to %s"), *File);
    }
    else
    {
        UE_LOG(LogRockyKombat, Warning, TEXT("build stamp: could not be written to %s"), *File);
    }
}
} // namespace

/** Rocky Kombat's game module. It does nothing on shutdown; on startup it makes
 *  the documented command-line switches real. */
class FRockyKombatModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FDefaultGameModuleImpl::StartupModule();
        ReportBuildStamp();
        ApplyCommandLineConsoleOverrides();
    }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FRockyKombatModule, RockyKombat, "RockyKombat");
