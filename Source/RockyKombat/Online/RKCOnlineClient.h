#pragma once
#include "CoreMinimal.h"
#include "Combat/RKCCombatCore.h"

class IWebSocket;
class FJsonObject;
struct FRKCOnlinePlayer { FString Id, Name; int32 Fighter = 0; bool bBusy = false; };
struct FRKCOnlineFrame { int32 Seq = 0; RKC::FInputState A, B; };

/** Unranked guest lobby and ordered-input relay. No fabricated presence. */
class FRKCOnlineClient
{
public:
    FRKCOnlineClient();
    ~FRKCOnlineClient();
    void Connect();
    void Leave();
    void SelectFighter(int32 Fighter);
    void Invite(const FString& Peer);
    void Accept();
    void Decline();
    void Ready();
    void Rematch();
    void TickInput(float DeltaSeconds, const RKC::FInputState& Input);
    bool PopFrame(FRKCOnlineFrame& Out);
    void SendHash(int32 Seq, uint32 Hash);
    bool ConsumeMatch();
    bool ConsumeEnded();
    bool IsConfigured() const { return !Url.IsEmpty(); }
    bool IsConnected() const { return bConnected; }
    bool IsRunning() const { return bRunning; }
    bool IsInMatch() const { return !MatchId.IsEmpty(); }
    int32 GetRevision() const { return Revision; }
    int32 Side = 0;
    int32 Fighters[2] = {0, 1};
    FString Names[2];
    FString Status, IncomingName, IncomingId;
    TArray<FRKCOnlinePlayer> Players;
private:
    void Send(const TSharedRef<FJsonObject>& Message);
    void HandleMessage(const FString& Text);
    void Fail(const FString& Reason);
    TSharedPtr<IWebSocket> Socket;
    FString Url, PlayerName, MatchId;
    TArray<FRKCOnlineFrame> Frames;
    RKC::FInputState PendingInput;
    float InputClock = 0.0f;
    int32 NextInput = 0, NextFrame = 0, AppliedFrames = 0, Revision = 0, SelectedFighter = 0;
    bool bConnected = false, bRunning = false, bMatchPending = false, bEnded = false;
};
