// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemSteamPrivatePCH.h"
#include "OnlineSubsystemSteam.h"
#include "ModuleManager.h"

#include "IPAddressSteam.h"
#include "OnlineSubsystemUtilsClasses.h"
#include "OnlineSubsystemSteamClasses.h"


#include "SocketSubsystemSteam.h"

#include "OnlineSessionInterfaceSteam.h"
#include "OnlineIdentityInterfaceSteam.h"
#include "OnlineFriendsInterfaceSteam.h"
#include "OnlineSharedCloudInterfaceSteam.h"
#include "OnlineUserCloudInterfaceSteam.h"
#include "OnlineLeaderboardInterfaceSteam.h"
#include "VoiceInterfaceSteam.h"
#include "OnlineExternalUIInterfaceSteam.h"
#include "OnlineAchievementsInterfaceSteam.h"

extern "C" 
{ 
	static void __cdecl SteamworksWarningMessageHook(int Severity, const char *Message); 
	static void __cdecl SteamworksWarningMessageHookNoOp(int Severity, const char *Message);
}

/** 
 * Callback function into Steam error messaging system
 * @param Severity - error level
 * @param Message - message from Steam
 */
static void __cdecl SteamworksWarningMessageHook(int Severity, const char *Message)
{
	const TCHAR *MessageType;
	switch (Severity)
	{
		case 0: MessageType = TEXT("message"); break;
		case 1: MessageType = TEXT("warning"); break;
		default: MessageType = TEXT("notification"); break;  // Unknown severity; new SDK?
	}
	UE_LOG_ONLINE(Warning, TEXT("Steamworks SDK %s: %s"), MessageType, UTF8_TO_TCHAR(Message));
}

/** 
 * Callback function into Steam error messaging system that outputs nothing
 * @param Severity - error level
 * @param Message - message from Steam
 */
static void __cdecl SteamworksWarningMessageHookNoOp(int Severity, const char *Message)
{
	// no-op.
}

class FScopeSandboxContext
{
private:
	/** Previous state of sandbox enable */
	bool bSandboxWasEnabled;

	FScopeSandboxContext() {}

public:
	FScopeSandboxContext(bool bSandboxEnabled)
	{
		bSandboxWasEnabled = IFileManager::Get().IsSandboxEnabled();
		IFileManager::Get().SetSandboxEnabled(bSandboxEnabled);
	}

	~FScopeSandboxContext()
	{
		IFileManager::Get().SetSandboxEnabled(bSandboxWasEnabled);
	}
};

inline FString GetSteamAppIdFilename()
{
	return FString::Printf(TEXT("%s%s"), FPlatformProcess::BaseDir(), STEAMAPPIDFILENAME);
}

#if !UE_BUILD_SHIPPING && !UE_BUILD_SHIPPING_WITH_EDITOR
/** 
 * Write out the steam app id to the steam_appid.txt file before initializing the API
 * @param SteamAppId id assigned to the application by Steam
 */
static void WriteSteamAppIdToDisk(int32 SteamAppId)
{
	if (SteamAppId > 0)
	{
		// Turn off sandbox temporarily to make sure file is where it's always expected
		FScopeSandboxContext ScopedSandbox(false);
		FArchive* AppIdFile = IFileManager::Get().CreateFileWriter(*GetSteamAppIdFilename());
		
		if (AppIdFile)
		{
			FString AppId = FString::Printf(TEXT("%d"), SteamAppId);
			AppIdFile->Serialize((void*)TCHAR_TO_ANSI(*AppId), AppId.Len());

			// Close the file
			delete AppIdFile;
			AppIdFile = NULL;
		}
	}
}

/**
 * Deletes the app id file from disk
 */
static void DeleteSteamAppIdFromDisk()
{
	const FString SteamAppIdFilename = GetSteamAppIdFilename();
	// Turn off sandbox temporarily to make sure file is where it's always expected
	FScopeSandboxContext ScopedSandbox(false);
	if (FPaths::FileExists(*SteamAppIdFilename))
	{
		bool bSuccessfullyDeleted = IFileManager::Get().Delete(*SteamAppIdFilename);
	}
}

#endif // !UE_BUILD_SHIPPING && !UE_BUILD_SHIPPING_WITH_EDITOR

/**
 * Configure various dev options before initializing Steam
 *
 * @param RequireRelaunch enforce the Steam client running precondition
 * @param RelaunchAppId appid to launch when the Steam client is loaded
 */
void ConfigureSteamInitDevOptions(bool& RequireRelaunch, int32& RelaunchAppId)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_SHIPPING_WITH_EDITOR
	// Write out the steam_appid.txt file before launching
	if (!GConfig->GetInt(TEXT("OnlineSubsystemSteam"), TEXT("SteamDevAppId"), RelaunchAppId, GEngineIni))
	{
		UE_LOG_ONLINE(Warning, TEXT("Missing SteamDevAppId key in OnlineSubsystemSteam of DefaultEngine.ini"));
	}
	else
	{
		WriteSteamAppIdToDisk(RelaunchAppId);
	}

	// Should the game force a relaunch in Steam if the client isn't already loaded
	if (!GConfig->GetBool(TEXT("OnlineSubsystemSteam"), TEXT("bRelaunchInSteam"), RequireRelaunch, GEngineIni))
	{
		UE_LOG_ONLINE(Warning, TEXT("Missing bRelaunchInSteam key in OnlineSubsystemSteam of DefaultEngine.ini"));
	}

#else
	// Always check against the Steam client when shipping
	RequireRelaunch = true;
	// Enter shipping app id here
	RelaunchAppId = 0;
#endif
}

IOnlineSessionPtr FOnlineSubsystemSteam::GetSessionInterface() const
{
	return SessionInterface;
}

IOnlineFriendsPtr FOnlineSubsystemSteam::GetFriendsInterface() const
{
	return FriendInterface;
}

IOnlineSharedCloudPtr FOnlineSubsystemSteam::GetSharedCloudInterface() const
{
	return SharedCloudInterface;
}

IOnlineUserCloudPtr FOnlineSubsystemSteam::GetUserCloudInterface() const
{
	return UserCloudInterface;
}

IOnlineLeaderboardsPtr FOnlineSubsystemSteam::GetLeaderboardsInterface() const
{
	return LeaderboardsInterface;
}

IOnlineVoicePtr FOnlineSubsystemSteam::GetVoiceInterface() const
{
	return VoiceInterface;
}

IOnlineExternalUIPtr FOnlineSubsystemSteam::GetExternalUIInterface() const
{
	return ExternalUIInterface;
}

IOnlineTimePtr FOnlineSubsystemSteam::GetTimeInterface() const
{
	return NULL;
}

IOnlineIdentityPtr FOnlineSubsystemSteam::GetIdentityInterface() const
{
	return IdentityInterface;
}

IOnlinePartyPtr FOnlineSubsystemSteam::GetPartyInterface() const
{
	return NULL;
}

IOnlineTitleFilePtr FOnlineSubsystemSteam::GetTitleFileInterface() const
{
	return NULL;
}

IOnlineEntitlementsPtr FOnlineSubsystemSteam::GetEntitlementsInterface() const
{
	return NULL;
}

IOnlineStorePtr FOnlineSubsystemSteam::GetStoreInterface() const
{
	return NULL;
}

IOnlineEventsPtr FOnlineSubsystemSteam::GetEventsInterface() const
{
	return NULL;
}

IOnlineAchievementsPtr FOnlineSubsystemSteam::GetAchievementsInterface() const
{
	return AchievementsInterface;
}

IOnlineSharingPtr FOnlineSubsystemSteam::GetSharingInterface() const
{
	return NULL;
}

IOnlineUserPtr FOnlineSubsystemSteam::GetUserInterface() const
{
	return NULL;
}

IOnlineMessagePtr FOnlineSubsystemSteam::GetMessageInterface() const
{
	return NULL;
}

IOnlinePresencePtr FOnlineSubsystemSteam::GetPresenceInterface() const
{
	return NULL;
}

IOnlineConnectionPtr FOnlineSubsystemSteam::GetConnectionInterface() const
{
	return NULL;
}


void FOnlineSubsystemSteam::QueueAsyncTask(FOnlineAsyncTask* AsyncTask)
{
	check(OnlineAsyncTaskThreadRunnable);
	OnlineAsyncTaskThreadRunnable->AddToInQueue(AsyncTask);
}

void FOnlineSubsystemSteam::QueueAsyncOutgoingItem(FOnlineAsyncItem* AsyncItem)
{
	check(OnlineAsyncTaskThreadRunnable);
	OnlineAsyncTaskThreadRunnable->AddToOutQueue(AsyncItem);
}

void FOnlineSubsystemSteam::QueueAsyncMsg(FOnlineAsyncMsgSteam* AsyncMsg)
{
	check(OnlineAsyncTaskThreadRunnable);
	OnlineAsyncTaskThreadRunnable->AddToInMsgQueue(AsyncMsg);
}

bool FOnlineSubsystemSteam::Tick(float DeltaTime)
{
	if (OnlineAsyncTaskThreadRunnable)
	{
		OnlineAsyncTaskThreadRunnable->GameTick();
	}

	if (SessionInterface.IsValid())
	{
		SessionInterface->Tick(DeltaTime);
	}

	if (VoiceInterface.IsValid())
	{
		VoiceInterface->Tick(DeltaTime);
	}
	return true;
}

bool FOnlineSubsystemSteam::Init()
{
	bool bRelaunchInSteam = false;
	int RelaunchAppId = 0;
	ConfigureSteamInitDevOptions(bRelaunchInSteam, RelaunchAppId);

	const bool bIsServer = IsRunningDedicatedServer();
	
	// Don't initialize the Steam Client API if we are launching as a server
	bool bClientInitSuccess = !bIsServer ? InitSteamworksClient(bRelaunchInSteam, RelaunchAppId) : true;

	// Initialize the Steam Server API if this is a dedicated server or
	//  the Client API was successfully initialized
	bool bServerInitSuccess = bClientInitSuccess ? InitSteamworksServer() : false;

	if (bClientInitSuccess && bServerInitSuccess)
	{
		CreateSteamSocketSubsystem();

		// Create the online async task thread
		OnlineAsyncTaskThreadRunnable = new FOnlineAsyncTaskManagerSteam(this);
		check(OnlineAsyncTaskThreadRunnable);		
		OnlineAsyncTaskThread = FRunnableThread::Create(OnlineAsyncTaskThreadRunnable, TEXT("OnlineAsyncTaskThreadSteam"), 128 * 1024, TPri_Normal);
		check(OnlineAsyncTaskThread);
		UE_LOG_ONLINE(Verbose, TEXT("Created thread (ID:%d)."), OnlineAsyncTaskThread->GetThreadID() );

		SessionInterface = MakeShareable(new FOnlineSessionSteam(this));
		SessionInterface->CheckPendingSessionInvite();

		IdentityInterface = MakeShareable(new FOnlineIdentitySteam());

		if (!bIsServer)
		{
			FriendInterface = MakeShareable(new FOnlineFriendsSteam(this));
			UserCloudInterface = MakeShareable(new FOnlineUserCloudSteam(this));
			SharedCloudInterface = MakeShareable(new FOnlineSharedCloudSteam(this));
			LeaderboardsInterface = MakeShareable(new FOnlineLeaderboardsSteam(this));
			VoiceInterface = MakeShareable(new FOnlineVoiceSteam(this));
			if (!VoiceInterface->Init())
			{
				VoiceInterface = NULL;
			}
			ExternalUIInterface = MakeShareable(new FOnlineExternalUISteam(this));
			AchievementsInterface = MakeShareable(new FOnlineAchievementsSteam(this));

			// Kick off a download/cache of the current user's stats
			LeaderboardsInterface->CacheCurrentUsersStats();
		}
		else
		{
			// Need a voice interface here to serialize packets but NOT add to VoiceData.RemotePackets
		}
	}
	else
	{
		Shutdown();
	}

	return bClientInitSuccess && bServerInitSuccess;
}

bool FOnlineSubsystemSteam::Shutdown()
{
	UE_LOG_ONLINE(Display, TEXT("OnlineSubsystemSteam::Shutdown()"));

	if (OnlineAsyncTaskThread)
	{
		// Destroy the online async task thread
		delete OnlineAsyncTaskThread;
		OnlineAsyncTaskThread = NULL;
	}

	if (OnlineAsyncTaskThreadRunnable)
	{
		delete OnlineAsyncTaskThreadRunnable;
		OnlineAsyncTaskThreadRunnable = NULL;
	}
	
	#define DESTRUCT_INTERFACE(Interface) \
	if (Interface.IsValid()) \
	{ \
		ensure(Interface.IsUnique()); \
		Interface = NULL; \
	}

	// Destruct the interfaces
	DESTRUCT_INTERFACE(AchievementsInterface);
	DESTRUCT_INTERFACE(SessionInterface);
	DESTRUCT_INTERFACE(IdentityInterface);
	DESTRUCT_INTERFACE(FriendInterface);
	DESTRUCT_INTERFACE(SharedCloudInterface);
	DESTRUCT_INTERFACE(UserCloudInterface);
	DESTRUCT_INTERFACE(LeaderboardsInterface);
	DESTRUCT_INTERFACE(VoiceInterface);
	DESTRUCT_INTERFACE(ExternalUIInterface);

	#undef DESTRUCT_INTERFACE

	ClearUserCloudFiles();

	DestroySteamSocketSubsystem();

	ShutdownSteamworks();

#if !UE_BUILD_SHIPPING && !UE_BUILD_SHIPPING_WITH_EDITOR
	DeleteSteamAppIdFromDisk();
#endif // !UE_BUILD_SHIPPING && !UE_BUILD_SHIPPING_WITH_EDITOR

	return true;
}

bool FOnlineSubsystemSteam::IsEnabled()
{
	if (bSteamworksClientInitialized || bSteamworksGameServerInitialized)
	{
		return true;
	}

	// Check the ini for disabling Steam
	bool bEnableSteam = true;
	if (GConfig->GetBool(TEXT("OnlineSubsystemSteam"), TEXT("bEnabled"), bEnableSteam, GEngineIni) && bEnableSteam)
	{
		// Check the commandline for disabling Steam
		bEnableSteam = !FParse::Param(FCommandLine::Get(),TEXT("NOSTEAM"));
#if UE_EDITOR
		if (bEnableSteam)
		{
			bEnableSteam = IsRunningDedicatedServer() || IsRunningGame();
		}
#endif
	}

	return bEnableSteam;
}

bool FOnlineSubsystemSteam::InitSteamworksClient(bool bRelaunchInSteam, int32 SteamAppId)
{
	bSteamworksClientInitialized = false;

	// If the game was not launched from within Steam, but is supposed to, trigger a Steam launch and exit
	if (bRelaunchInSteam && SteamAppId != 0 && SteamAPI_RestartAppIfNecessary(SteamAppId))
	{
		if (FPlatformProperties::IsGameOnly() || FPlatformProperties::IsServerOnly())
		{
			UE_LOG_ONLINE(Log, TEXT("Game restarting within Steam client, exiting"));
			FPlatformMisc::RequestExit(false);
		}

		return bSteamworksClientInitialized;
	}
	// Otherwise initialize as normal
	else
	{
		// Steamworks needs to initialize as close to start as possible, so it can hook its overlay into Direct3D, etc.
		bSteamworksClientInitialized = (SteamAPI_Init() ? true : false);

		// Test all the Steam interfaces
#define GET_STEAMWORKS_INTERFACE(Interface) \
		if (Interface() == NULL) \
		{ \
			UE_LOG_ONLINE(Warning, TEXT("Steamworks: %s() failed!"), TEXT(#Interface)); \
			bSteamworksClientInitialized = false; \
		} \

		// GSteamUtils
		GET_STEAMWORKS_INTERFACE(SteamUtils);
		// GSteamUser
		GET_STEAMWORKS_INTERFACE(SteamUser);
		// GSteamFriends
		GET_STEAMWORKS_INTERFACE(SteamFriends);
		// GSteamRemoteStorage
		GET_STEAMWORKS_INTERFACE(SteamRemoteStorage);
		// GSteamUserStats
		GET_STEAMWORKS_INTERFACE(SteamUserStats);
		// GSteamMatchmakingServers
		GET_STEAMWORKS_INTERFACE(SteamMatchmakingServers);
		// GSteamApps
		GET_STEAMWORKS_INTERFACE(SteamApps);
		// GSteamNetworking
		GET_STEAMWORKS_INTERFACE(SteamNetworking);
		// GSteamMatchmaking
		GET_STEAMWORKS_INTERFACE(SteamMatchmaking);

#undef GET_STEAMWORKS_INTERFACE
	}

	if (bSteamworksClientInitialized)
	{
		bool bIsSubscribed = true;
		if (FPlatformProperties::IsGameOnly() || FPlatformProperties::IsServerOnly())
		{
			bIsSubscribed = SteamApps()->BIsSubscribed();
		}

		// Make sure the Steam user has valid access to the game
		if (bIsSubscribed)
		{
			UE_LOG_ONLINE(Log, TEXT("Steam User is subscribed %i"), bSteamworksClientInitialized);
			if (SteamUtils())
			{
				SteamAppID = SteamUtils()->GetAppID();
				SteamUtils()->SetWarningMessageHook(SteamworksWarningMessageHook);
			}
		}
		else
		{
			UE_LOG_ONLINE(Error, TEXT("Steam User is NOT subscribed, exiting."));
			bSteamworksClientInitialized = false;
			FPlatformMisc::RequestExit(false);
		}
	}

	UE_LOG_ONLINE(Log, TEXT("[AppId: %d] Client API initialized %i"), GetSteamAppId(), bSteamworksClientInitialized);
	return bSteamworksClientInitialized;
}

bool FOnlineSubsystemSteam::InitSteamworksServer()
{
	bSteamworksGameServerInitialized = false;

	// Initialize the Steam game server interfaces (done regardless of whether or not a server will be setup)
	// NOTE: The port values specified here, are not changeable once the interface is setup
	
	uint32 LocalServerIP = 0;
	FString MultiHome;
	if (FParse::Value(FCommandLine::Get(), TEXT("MULTIHOME="), MultiHome) && !MultiHome.IsEmpty())
	{
		TSharedRef<FInternetAddr> MultiHomeIP = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
		bool bIsValidIP = false;

		MultiHomeIP->SetIp(*MultiHome, bIsValidIP);
		if (bIsValidIP)
		{
			MultiHomeIP->GetIp(LocalServerIP);
		}
	}

	GConfig->GetInt(TEXT("URL"), TEXT("Port"), GameServerGamePort, GEngineIni);
	GameServerSteamPort = GameServerGamePort + 1;

	// Allow the command line to override the default query port
	if (FParse::Value(FCommandLine::Get(), TEXT("QueryPort="), GameServerQueryPort) == false)
	{
		if (!GConfig->GetInt(TEXT("OnlineSubsystemSteam"), TEXT("GameServerQueryPort"), GameServerQueryPort, GEngineIni))
		{
			GameServerQueryPort = 27015;
		}
	}

	bool bVACEnabled = false;
	GConfig->GetBool(TEXT("OnlineSubsystemSteam"), TEXT("bVACEnabled"), bVACEnabled, GEngineIni);

	FString GameVersion;
	GConfig->GetString(TEXT("OnlineSubsystemSteam"), TEXT("GameVersion"), GameVersion, GEngineIni);
	if (GameVersion.Len() == 0)
	{
		UE_LOG_ONLINE(Warning, TEXT("[OnlineSubsystemSteam].GameVersion is not set. Server advertising will fail"));
	}

	// NOTE: IP of 0 causes SteamGameServer_Init to automatically use the public (external) IP
	UE_LOG_ONLINE(Verbose, TEXT("Initializing Steam Game Server IP: 0x%08X Port: %d SteamPort: %d QueryPort: %d"), LocalServerIP, GameServerGamePort, GameServerSteamPort, GameServerQueryPort);
	bSteamworksGameServerInitialized = SteamGameServer_Init(LocalServerIP, GameServerSteamPort, GameServerGamePort, GameServerQueryPort,
		(bVACEnabled ? eServerModeAuthenticationAndSecure : eServerModeAuthentication),
		TCHAR_TO_UTF8(*GameVersion));

	if (bSteamworksGameServerInitialized)
	{
		// Test all the Steam interfaces
		#define GET_STEAMWORKS_INTERFACE(Interface) \
		if (Interface() == NULL) \
		{ \
			UE_LOG_ONLINE(Warning, TEXT("Steamworks: %s() failed!"), TEXT(#Interface)); \
			bSteamworksGameServerInitialized = false; \
		} \

		// NOTE: It's not possible for >some< of the interfaces to initialize, and others fail; it's all or none
		GET_STEAMWORKS_INTERFACE(SteamGameServer);
		GET_STEAMWORKS_INTERFACE(SteamGameServerStats);
		GET_STEAMWORKS_INTERFACE(SteamGameServerNetworking);
		GET_STEAMWORKS_INTERFACE(SteamGameServerUtils);

		#undef GET_STEAMWORKS_INTERFACE
	}

	if (SteamGameServerUtils() != NULL)
	{
		SteamAppID = SteamGameServerUtils()->GetAppID();
		SteamGameServerUtils()->SetWarningMessageHook(SteamworksWarningMessageHook);
	}

	UE_LOG_ONLINE(Log, TEXT("[AppId: %d] Game Server API initialized %i"), GetSteamAppId(), bSteamworksGameServerInitialized);
	return bSteamworksGameServerInitialized;
}

void FOnlineSubsystemSteam::ShutdownSteamworks()
{
	if (bSteamworksGameServerInitialized)
	{
		if (SteamGameServer() != NULL)
		{
			// Since SteamSDK 1.17, LogOff is required to stop the game server advertising after exit; ensure we don't miss this at shutdown
			if (SteamGameServer()->BLoggedOn())
			{
				SteamGameServer()->LogOff();
			}

			SteamGameServer_Shutdown();
			if (SessionInterface.IsValid())
			{
				SessionInterface->GameServerSteamId = NULL;
				SessionInterface->bSteamworksGameServerConnected = false;
			}	
		}
	}

	if (bSteamworksClientInitialized)
	{
		SteamAPI_Shutdown();
		bSteamworksClientInitialized = false;
	}
}

bool FOnlineSubsystemSteam::IsLocalPlayer(const FUniqueNetId& UniqueId) const
{
	ISteamUser* SteamUserPtr = SteamUser();
	return SteamUserPtr && SteamUserPtr->GetSteamID() == (const FUniqueNetIdSteam&)UniqueId;
}

FOnlineLeaderboardsSteam * FOnlineSubsystemSteam::GetInternalLeaderboardsInterface()
{
	return LeaderboardsInterface.Get();
}

FSteamUserCloudData* FOnlineSubsystemSteam::GetUserCloudEntry(const FUniqueNetId& UserId)
{
	FScopeLock ScopeLock(&UserCloudDataLock);
	for (int32 UserIdx=0; UserIdx < UserCloudData.Num(); UserIdx++)
	{
		FSteamUserCloudData* UserMetadata = UserCloudData[UserIdx];
		if (UserMetadata->UserId == UserId)
		{
			return UserMetadata;
		}
	}

	// Always create a new one if it doesn't exist
	FUniqueNetIdSteam SteamUserId(*(uint64*)UserId.GetBytes());
	FSteamUserCloudData* NewItem = new FSteamUserCloudData(SteamUserId);
	int32 UserIdx = UserCloudData.Add(NewItem);
	return UserCloudData[UserIdx];
}

bool FOnlineSubsystemSteam::ClearUserCloudMetadata(const FUniqueNetId& UserId, const FString& FileName)
{
	if (FileName.Len() > 0)
	{
		FScopeLock ScopeLock(&UserCloudDataLock);
		// Search for the specified file
		FSteamUserCloudData* UserCloud = GetUserCloudEntry(UserId);
		if (UserCloud)
		{
			UserCloud->ClearMetadata(FileName);
		}
	}

	return true;
}

void FOnlineSubsystemSteam::ClearUserCloudFiles()
{
	FScopeLock ScopeLock(&UserCloudDataLock);
	for (int32 UserIdx = 0; UserIdx < UserCloudData.Num(); UserIdx++)
	{
		FSteamUserCloudData* UserCloud = UserCloudData[UserIdx];
		delete UserCloud;
	}

	UserCloudData.Empty();
}

static void DeleteFromEnumerateUserFilesComplete(bool bWasSuccessful, const FUniqueNetId& UserId)
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	check(OnlineSub); 

	IOnlineUserCloudPtr UserCloud = OnlineSub->GetUserCloudInterface();

	FOnEnumerateUserFilesCompleteDelegate Delegate = FOnEnumerateUserFilesCompleteDelegate::CreateStatic(&DeleteFromEnumerateUserFilesComplete);
	UserCloud->ClearOnEnumerateUserFilesCompleteDelegate(Delegate);
	if (bWasSuccessful)
	{
		TArray<FCloudFileHeader> UserFiles;
		UserCloud->GetUserFileList(UserId, UserFiles);

		for (int32 Idx=0; Idx < UserFiles.Num(); Idx++)
		{
			UserCloud->DeleteUserFile(UserId, UserFiles[Idx].FileName, true, true);
		}
	}
}

bool FOnlineSubsystemSteam::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) 
{
	if (FParse::Command(&Cmd, TEXT("DELETECLOUDFILES")))
	{
		IOnlineUserCloudPtr UserCloud = GetUserCloudInterface();

		FUniqueNetIdSteam SteamId(SteamUser()->GetSteamID());

		FOnEnumerateUserFilesCompleteDelegate Delegate = FOnEnumerateUserFilesCompleteDelegate::CreateStatic(&DeleteFromEnumerateUserFilesComplete);
		UserCloud->AddOnEnumerateUserFilesCompleteDelegate(Delegate);
		UserCloud->EnumerateUserFiles(SteamId);
		return true;
	}
	else if (FParse::Command(&Cmd, TEXT("SYNCLOBBIES")))
	{
		if (SessionInterface.IsValid())
		{
			SessionInterface->SyncLobbies();
			return true;
		}
	}

	return false;
}

FString FOnlineSubsystemSteam::GetAppId() const
{
	return FString::Printf(TEXT("%d"),GetSteamAppId());
}
