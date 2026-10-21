#include "Online/RKCOnlineClient.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

namespace {
constexpr const TCHAR* BuildId = TEXT("1.0.2-alpha.2-net1");
TSharedRef<FJsonObject> Message(const TCHAR* Type) { auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("type"), Type); return J; }
int32 Pack(const RKC::FInputState& I) { return int32(I.bMoveLeft) | int32(I.bMoveRight) << 1 | int32(I.bHoldBlock) << 2 | int32(I.bDodgePressed) << 3 | int32(I.bJabPressed) << 4 | int32(I.bHeavyPressed) << 5; }
RKC::FInputState Unpack(int32 B) { RKC::FInputState I; I.bMoveLeft=B&1; I.bMoveRight=B&2; I.bHoldBlock=B&4; I.bDodgePressed=B&8; I.bJabPressed=B&16; I.bHeavyPressed=B&32; return I; }
}
FRKCOnlineClient::FRKCOnlineClient() {
    GConfig->GetString(TEXT("RockyKombat.Online"), TEXT("ServerUrl"), Url, GGameIni);
    GConfig->GetString(TEXT("RockyKombat.Online"), TEXT("PlayerName"), PlayerName, GGameIni);
    FParse::Value(FCommandLine::Get(), TEXT("rkc.OnlineUrl="), Url);
    FParse::Value(FCommandLine::Get(), TEXT("rkc.PlayerName="), PlayerName);
    // Public connections require TLS. Plain WebSockets are confined to loopback testing.
    if (!Url.IsEmpty() && !Url.StartsWith(TEXT("wss://")) && !Url.StartsWith(TEXT("ws://127.0.0.1:")) && !Url.StartsWith(TEXT("ws://localhost:"))) Url.Empty();
    Status = Url.IsEmpty() ? TEXT("Online service awaiting deployment. Offline play is available.") : TEXT("Connect to see available players.");
}
FRKCOnlineClient::~FRKCOnlineClient() {
    if (Socket) { Socket->OnConnected().Clear(); Socket->OnConnectionError().Clear(); Socket->OnClosed().Clear(); Socket->OnMessage().Clear(); Socket->Close(); }
}
void FRKCOnlineClient::Send(const TSharedRef<FJsonObject>& J) {
    if (!Socket || !Socket->IsConnected()) return;
    FString Text; auto Writer = TJsonWriterFactory<>::Create(&Text); FJsonSerializer::Serialize(J, Writer); Socket->Send(Text);
}
void FRKCOnlineClient::Connect() {
    if (!IsConfigured() || (Socket && Socket->IsConnected())) return;
    if (Socket) { Socket->OnConnected().Clear(); Socket->OnConnectionError().Clear(); Socket->OnClosed().Clear(); Socket->OnMessage().Clear(); Socket->Close(); }
    Status = TEXT("Connecting..."); ++Revision;
    Socket = FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets")).CreateWebSocket(Url);
    Socket->SetTextMessageMemoryLimit(65536);
    Socket->OnConnected().AddLambda([this]() { auto J=Message(TEXT("hello")); J->SetNumberField(TEXT("protocol"),1); J->SetStringField(TEXT("build"),BuildId); J->SetStringField(TEXT("name"),PlayerName); Send(J); });
    Socket->OnConnectionError().AddLambda([this](const FString&) { Fail(TEXT("Cannot connect. Check the server and your internet connection.")); });
    Socket->OnClosed().AddLambda([this](int32, const FString&, bool) { Fail(TEXT("Disconnected from online service.")); });
    Socket->OnMessage().AddRaw(this, &FRKCOnlineClient::HandleMessage);
    Socket->Connect();
}
void FRKCOnlineClient::Fail(const FString& Reason) { bEnded |= IsInMatch(); bRunning=false; bConnected=false; bMatchPending=false; MatchId.Empty(); Frames.Reset(); Players.Reset(); IncomingId.Empty(); IncomingName.Empty(); Status=Reason; ++Revision; }
void FRKCOnlineClient::Leave() { Send(Message(TEXT("leave"))); MatchId.Empty(); bRunning=false; bMatchPending=false; Frames.Reset(); IncomingId.Empty(); IncomingName.Empty(); ++Revision; }
void FRKCOnlineClient::SelectFighter(int32 F) { SelectedFighter=F; auto J=Message(TEXT("fighter")); J->SetNumberField(TEXT("fighter"),F); Send(J); }
void FRKCOnlineClient::Invite(const FString& P) { auto J=Message(TEXT("invite")); J->SetStringField(TEXT("peer"),P); Send(J); }
void FRKCOnlineClient::Accept() { Send(Message(TEXT("accept"))); IncomingId.Empty(); IncomingName.Empty(); ++Revision; }
void FRKCOnlineClient::Decline() { Send(Message(TEXT("decline"))); IncomingId.Empty(); IncomingName.Empty(); Status=TEXT("Choose an available player to invite."); ++Revision; }
void FRKCOnlineClient::Ready() { auto J=Message(TEXT("ready")); J->SetStringField(TEXT("match"),MatchId); Send(J); }
void FRKCOnlineClient::Rematch() { auto J=Message(TEXT("rematch")); J->SetStringField(TEXT("match"),MatchId); Send(J); }
bool FRKCOnlineClient::ConsumeMatch() { bool B=bMatchPending; bMatchPending=false; return B; }
bool FRKCOnlineClient::ConsumeEnded() { bool B=bEnded; bEnded=false; return B; }
void FRKCOnlineClient::HandleMessage(const FString& Text) {
    TSharedPtr<FJsonObject> J; auto Reader=TJsonReaderFactory<>::Create(Text);
    if (!FJsonSerializer::Deserialize(Reader,J) || !J) { Fail(TEXT("Invalid service response.")); return; }
    FString Type; if (!J->TryGetStringField(TEXT("type"),Type)) return;
    if (Type==TEXT("welcome")) { bConnected=true; SelectFighter(SelectedFighter); Status=TEXT("Choose an available player to invite."); ++Revision; }
    else if (Type==TEXT("players")) {
        const TArray<TSharedPtr<FJsonValue>>* List=nullptr; if (!J->TryGetArrayField(TEXT("players"),List) || List->Num()>200) return;
        Players.Reset(); for (const auto& V:*List) { const auto O=V->AsObject(); if (!O) continue; FRKCOnlinePlayer P; if (!O->TryGetStringField(TEXT("id"),P.Id) || !O->TryGetStringField(TEXT("name"),P.Name)) continue; double F=0; O->TryGetNumberField(TEXT("fighter"),F); P.Fighter=F==1?1:0; O->TryGetBoolField(TEXT("busy"),P.bBusy); Players.Add(P); } ++Revision;
    }
    else if (Type==TEXT("invite")) { J->TryGetStringField(TEXT("peer"),IncomingId); J->TryGetStringField(TEXT("name"),IncomingName); ++Revision; }
    else if (Type==TEXT("notice")) { J->TryGetStringField(TEXT("message"),Status); if (Status==TEXT("Invitation closed.") || Status==TEXT("Invitation expired.")) { IncomingId.Empty(); IncomingName.Empty(); } ++Revision; }
    else if (Type==TEXT("ended")) { FString Reason; J->TryGetStringField(TEXT("message"),Reason); bool Connected=bConnected; Fail(Reason); bConnected=Connected; }
    else if (Type==TEXT("match")) {
        const TArray<TSharedPtr<FJsonValue>> *F=nullptr,*N=nullptr; double S=0;
        if (!J->TryGetArrayField(TEXT("fighters"),F) || F->Num()!=2 || !J->TryGetArrayField(TEXT("names"),N) || N->Num()!=2 || !J->TryGetNumberField(TEXT("side"),S) || (S!=0 && S!=1)) return;
        if (!J->TryGetStringField(TEXT("id"),MatchId)) return;
        for (int I=0;I<2;++I) { Fighters[I]=(*F)[I]->AsNumber()==1?1:0; Names[I]=(*N)[I]->AsString(); }
        Side=static_cast<int32>(S); bRunning=false; bMatchPending=true; bEnded=false; Frames.Reset(); PendingInput={}; InputClock=0; NextInput=NextFrame=AppliedFrames=0; IncomingId.Empty(); IncomingName.Empty(); ++Revision;
    }
    else if (Type==TEXT("go")) { bRunning=true; Status=TEXT("Online match connected"); ++Revision; }
    else if (Type==TEXT("frame") && bRunning) {
        double Seq,A,B; if (!J->TryGetNumberField(TEXT("seq"),Seq) || !J->TryGetNumberField(TEXT("a"),A) || !J->TryGetNumberField(TEXT("b"),B) || Seq!=NextFrame || A<0 || A>63 || B<0 || B>63 || Frames.Num()>=16) { Leave(); Fail(TEXT("Invalid match frame order.")); return; }
        FRKCOnlineFrame F; F.Seq=NextFrame++; F.A=Unpack(static_cast<int32>(A)); F.B=Unpack(static_cast<int32>(B)); Frames.Add(F);
    }
}
void FRKCOnlineClient::TickInput(float Dt,const RKC::FInputState& I) {
    if (!bRunning) return;
    PendingInput.bMoveLeft=I.bMoveLeft; PendingInput.bMoveRight=I.bMoveRight; PendingInput.bHoldBlock=I.bHoldBlock;
    PendingInput.bJabPressed|=I.bJabPressed; PendingInput.bHeavyPressed|=I.bHeavyPressed; PendingInput.bDodgePressed|=I.bDodgePressed;
    InputClock=FMath::Min(InputClock+Dt, 4.0f*RKC::FCombatSim::FixedTickSeconds);
    while (InputClock>=RKC::FCombatSim::FixedTickSeconds && NextInput-AppliedFrames<4) {
        auto J=Message(TEXT("input")); J->SetStringField(TEXT("match"),MatchId); J->SetNumberField(TEXT("seq"),NextInput++); J->SetNumberField(TEXT("bits"),Pack(PendingInput)); Send(J); PendingInput.ClearEdges(); InputClock-=RKC::FCombatSim::FixedTickSeconds;
    }
}
bool FRKCOnlineClient::PopFrame(FRKCOnlineFrame& Out) { if (!Frames.Num()) return false; Out=Frames[0]; Frames.RemoveAt(0); ++AppliedFrames; return true; }
void FRKCOnlineClient::SendHash(int32 Seq,uint32 Hash) { auto J=Message(TEXT("hash")); J->SetStringField(TEXT("match"),MatchId); J->SetNumberField(TEXT("seq"),Seq); J->SetStringField(TEXT("hash"),FString::Printf(TEXT("%08x"),Hash)); Send(J); }
