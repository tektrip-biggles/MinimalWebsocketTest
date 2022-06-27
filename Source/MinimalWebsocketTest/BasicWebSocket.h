// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"

#include "GenericPlatformHttp.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Containers/Queue.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/DateTime.h"
#include "Misc/Timespan.h"
#include "Containers/UnrealString.h"
#include "Modules/ModuleManager.h"

#include "BasicWebSocket.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(MiniWebSocket, Verbose, All);

UENUM(BlueprintType)
enum class EWebSocketMessageType : uint8
{
    RequestAuthentication,
    PlayerAuthenticated,
    PlayerNotAuthenticated,
    WarningMessage,
    ErrorMessage,
    Ping,
    Pong,

    INVALID
};

// Client -> Server messages

USTRUCT(BlueprintType)
struct FRequestAuthenticationPayload
{
    GENERATED_BODY()
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString PlayerName;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString PlayerID;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString GameVersion;
};

USTRUCT(BlueprintType)
struct FPingPayload
{
    GENERATED_BODY();
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FDateTime PingTime;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 PingMs;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FTimespan CurrentLatencyEstimate;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FTimespan CurrentServerTimeOffsetEstimate;
};


// Server -> Client messages

USTRUCT(BlueprintType)
struct FPlayerAuthenticatedPayload
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString PlayerName;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString PlayerID;

};

USTRUCT(BlueprintType)
struct FPongPayload
{
    GENERATED_BODY();
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FDateTime PingTime;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FDateTime PongTime;
    
};


// Delegate definitions

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMessageSent, FString, MessageString, FDateTime, TimeStamp);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMessageReceived, FString, MessageString, FDateTime, TimeStamp);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerAuthenticated, FPlayerAuthenticatedPayload, MessageData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWarningMessage, FString, WarningMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnErrorMessage, FString, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInternalErrorMessage, FString, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnConnectionAuthorised);

/**
 * 
 */
UCLASS(BlueprintType, Blueprintable)
class MINIMALWEBSOCKETTEST_API UBasicWebSocket : public UObject
{
    GENERATED_BODY()
    
public:
    // ------ Event dispatchers -------
    
    UPROPERTY(BlueprintAssignable)
    FOnMessageSent OnMessageSent;
    UPROPERTY(BlueprintAssignable)
    FOnMessageReceived OnMessageReceived;

    UPROPERTY(BlueprintAssignable)
    FOnPlayerAuthenticated OnPlayerAuthenticated;

    UPROPERTY(BlueprintAssignable)
    FOnWarningMessage OnWarningMessage;
    UPROPERTY(BlueprintAssignable)
    FOnErrorMessage OnErrorMessage;
    
    UPROPERTY(BlueprintAssignable)
    FOnInternalErrorMessage OnInternalErrorMessage;

    // ------- Server settings --------
    UPROPERTY(BlueprintReadWrite)
    FString ServerURL;
    
    UPROPERTY(BlueprintReadWrite)
    FString FriendlyServerName;
    
    UPROPERTY(BlueprintReadWrite)
    FString ServerProtocol = TEXT("ws");
    
    UPROPERTY(BlueprintReadWrite)
    bool bIsAuthenticated = false;
    
    /// Pointer to the actual underlying websocket object
    TSharedPtr<IWebSocket> Socket;
    
    // ------- Requests --------

    UFUNCTION(BlueprintCallable)
    void PlayerAuthenticatedDelegate(const FPlayerAuthenticatedPayload Payload);
    
    UFUNCTION(BlueprintCallable)
    void RequestAuthentication(const FRequestAuthenticationPayload Payload );

    // ------- Connection settings --------
    
    UPROPERTY(BlueprintReadWrite)
    FString PlayerName;
    
    UPROPERTY(BlueprintReadWrite)
    FString PlayerID;
    
    UPROPERTY(BlueprintReadWrite)
    FString GameVersion;  

    UPROPERTY(BlueprintReadWrite)
    bool ConnectionIsLive = true;

    UFUNCTION(BlueprintCallable)
    void Initialise(const FString PlayerNameIn, const FString PlayerIDIn, const FString GameVersionIn);
    
    UPROPERTY(BlueprintReadWrite)
    bool bWantToConnect = true;
    
    UFUNCTION(BlueprintCallable)
    void DisconnectFromServer();

    UFUNCTION(BlueprintCallable)
    void PingServer();

    void HandlePongMessage(const FString MessageDataString);

    UPROPERTY(BlueprintReadWrite)
    FTimespan LatencyEstimate;
    
    UPROPERTY(BlueprintReadWrite)
    FTimespan ServerClockOffset;

    int32 LastStringMessageLength = 0;
    
    FDelegateHandle OnSocketClosedLambdaFunctionHandle;
    
    bool ShuttingDown = false;

    // ------- Message Routing --------

    void HandleInboundMessage(const FString & Message);

    // ------- Event delegates --------
    
    template<typename MessageDataType, typename MessageEventType>
    void BroadcastMessageEvent(const MessageEventType MessageEvent, const FString MessageDataString);

    // ------- Message queue --------

    TQueue<FString> MessageOutQueue;

    /// If the connection is open, send messages from the queue in order. Otherwise, open the connection (and flush messages when the connection has been established.
    UFUNCTION(BlueprintCallable)
    void FlushMessageOutQueue();

    template<typename MessageDataType>
    FString ConvertMessageToString(EWebSocketMessageType MessageType, MessageDataType MessageData);
    
    // Stringify and send a message from enum and payload
    template<typename MessageDataType>
    void SendMessage(EWebSocketMessageType MessageType, MessageDataType MessageData);
    
    // Send a message without a payload
    void SendMessage(EWebSocketMessageType MessageType);
    
    // Stringify and send a message that has already been packaged up into enum and payload
    //void SendMessage(TSharedRef<FJsonObject> MessageJson);
    
    // Send a message that has already been stringified. Safe to call from BP?
    UFUNCTION(BlueprintCallable)
    void SendMessage(FString MessageString);
    
    virtual void BeginDestroy() override;

    // --------- Enum to/from String --------
    
    UFUNCTION(BlueprintPure)
    static FString WSMessageTypeEnumToString(const EWebSocketMessageType MessageType);
    
    UFUNCTION(BlueprintPure)
    static EWebSocketMessageType WSMessageTypeStringToEnum(const FString MessageTypeString);    

    // --------- Timer helper functions --------
    
    UFUNCTION(BlueprintCallable)
    FTimespan GetServerTimeElapsedSoFar(const FDateTime StartTime); // Turn (or game) start time, server offset, current time
    
    // Get an estimate for the current time on the server
    UFUNCTION(BlueprintCallable)
    FDateTime GetEstimatedServerTime();

};
