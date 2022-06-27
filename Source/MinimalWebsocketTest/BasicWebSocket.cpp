#include "BasicWebSocket.h"

DEFINE_LOG_CATEGORY(MiniWebSocket);

void UBasicWebSocket::Initialise(const FString PlayerNameIn, const FString PlayerIDIn, const FString GameVersionIn)
{
    bWantToConnect = true;
    if(!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
         FModuleManager::Get().LoadModule("WebSockets");
        if(!FModuleManager::Get().IsModuleLoaded("WebSockets"))
        {
            OnInternalErrorMessage.Broadcast("WebSockets module is still not loaded");
        }
    }
    
    PlayerName = PlayerNameIn;
    PlayerID = PlayerIDIn;
    GameVersion = GameVersionIn;

    // NOTE: If we don't set this header, then Glitch will not accept the websocket connection.
    TMap<FString, FString> UpgradeHeaders;
    UpgradeHeaders.Add(TEXT("User-Agent"), FGenericPlatformHttp::GetDefaultUserAgent());
    
    UE_LOG(MiniWebSocket, Verbose, TEXT("About to create WebSocket connection to %s via %s"), *ServerURL, *ServerProtocol);
    Socket = FWebSocketsModule::Get().CreateWebSocket(ServerURL, ServerProtocol, UpgradeHeaders);
    
    if(!Socket)
    {
        OnInternalErrorMessage.Broadcast("Failed to create websocket object");
    }
    UE_LOG(MiniWebSocket, Verbose, TEXT("Created WebSocket"));
    
    // Request authentication as soon as a connection has been established
    Socket->OnConnected().AddLambda([this]() -> void {
        if (ShuttingDown)
        {
            UE_LOG(MiniWebSocket, Verbose, TEXT("Connected, but we're shutting down, so don't do anything"));
            return;
        }
        UE_LOG(MiniWebSocket, Verbose, TEXT("Connected, requesting authentication"));

        FRequestAuthenticationPayload Payload;
        Payload.PlayerName = PlayerName;
        Payload.PlayerID = PlayerID;
        Payload.GameVersion = GameVersion;
        
        UE_LOG(MiniWebSocket, Verbose, TEXT("Payload's \"PlayerName\" and \"PlayerID\" set"));
        
        if (Socket)
        {
            // Should bypass the message queue for this one
            UE_LOG(MiniWebSocket, Verbose, TEXT("Sending authentication request"));
            Socket->Send(ConvertMessageToString(EWebSocketMessageType::RequestAuthentication, Payload));
            UE_LOG(MiniWebSocket, Verbose, TEXT("Authentication request sent"));
        }
        UE_LOG(MiniWebSocket, Verbose, TEXT("Exiting \"OnConnected\" lambda function"));

    });

    Socket->OnConnectionError().AddLambda([this](const FString & Error) -> void {
        UE_LOG(MiniWebSocket, Warning, TEXT("Connection Error: %s"), *Error);
        OnInternalErrorMessage.Broadcast(FString::Printf(TEXT("Websocket connection Error: %s"), *Error));
        // This code will run if the connection failed. Check Error to see what happened.
    });

    OnSocketClosedLambdaFunctionHandle = Socket->OnClosed().AddLambda([this](int32 StatusCode, const FString& Reason, bool bWasClean) -> void {
        
        UE_LOG(MiniWebSocket, Verbose, TEXT("Closed (code: %d, reason: %s)"), StatusCode, *Reason);
        
        if (bIsAuthenticated)
        {
            bIsAuthenticated = false;
        }
        
        // This code will run when the connection to the server has been terminated.
        // Because of an error or a call to Socket->Close().
    });

    Socket->OnMessage().AddUObject(this, &UBasicWebSocket::HandleInboundMessage);
    OnPlayerAuthenticated.AddDynamic(this, &UBasicWebSocket::PlayerAuthenticatedDelegate);

    Socket->OnRawMessage().AddLambda([this](const void* Data, SIZE_T Size, SIZE_T BytesRemaining) -> void {
        UE_LOG(MiniWebSocket, VeryVerbose, TEXT("Raw Message received of size: %d with %d bytes remaining (last string message length was %d)"), Size, BytesRemaining, LastStringMessageLength);
        // This code will run when we receive a raw (binary) message from the server.
    });

    //Socket->OnMessageSent().AddLambda([](const FString& MessageString) -> void {
    //    // This code is called after we sent a message to the server.
    //});

    // And we finally connect to the server.
    UE_LOG(MiniWebSocket, Log, TEXT("Attempting to connect to WebSocket"));
    Socket->Connect();
    UE_LOG(MiniWebSocket, Verbose, TEXT("Connection request sent to WebSocket"));
    
    
};


void UBasicWebSocket::FlushMessageOutQueue()
{
    if (!bWantToConnect)
    {
        DisconnectFromServer();
        return;
    }
    UE_LOG(MiniWebSocket, VeryVerbose, TEXT("Attempting to flush the message out queue..."));
    // check if our socket even exists yet
    if (!Socket)
    {
        UE_LOG(MiniWebSocket, Log, TEXT("Socket didn't exist for some reason, initialising now..."));
        if (ServerURL != "" && PlayerName != "" && PlayerID != "")
        {
            Initialise(PlayerName, PlayerID, GameVersion);
        }
        else
        {
            UE_LOG(MiniWebSocket, Warning, TEXT("We don't have a server URL (or could be the player name...)!"));
        }
        return;
    }
    // If our socket isn't connected, then we should attempt to connect and return.
    // We will attempt to flush the messages again after connecting to the game
    if (!Socket->IsConnected())
    {
        UE_LOG(MiniWebSocket, Log, TEXT("... socket is not connected, returning."));
        Socket->Connect();
        return;
    }
    
    if (!ConnectionIsLive)
    {
        UE_LOG(MiniWebSocket, Log, TEXT("... connection is not live, returning."));
        // We're waiting for a server to respond to our ping with a pong, at which point this function will be called again
        return;
    }
    if (MessageOutQueue.IsEmpty())
    {
        UE_LOG(MiniWebSocket, VeryVerbose, TEXT("... no messages to flush."));
    }
    
    // Finally, actually go through the queue and send messages.
    FString MessageOut;
    while (MessageOutQueue.Dequeue(MessageOut))
    {
        //SendMessage(MessageOut);
        UE_LOG(MiniWebSocket, Log, TEXT("... sending message: %s"), *MessageOut);
        OnMessageSent.Broadcast(MessageOut, FDateTime::Now());
        Socket->Send(MessageOut);
    }
    
    
};


void UBasicWebSocket::PingServer()
{
    if (!bWantToConnect)
    {
        DisconnectFromServer();
        return;
    }
    UE_LOG(MiniWebSocket, VeryVerbose, TEXT("Pinging server..."));
    ConnectionIsLive = false;
    // check if our socket even exists yet
    if (!Socket)
    {
        UE_LOG(MiniWebSocket, Log, TEXT("Socket didn't exist for some reason, initialising now..."));
        if (ServerURL != "" && PlayerName != "" && PlayerID != "")
        {
            Initialise(PlayerName, PlayerID, GameVersion);
        }
        else
        {
            UE_LOG(MiniWebSocket, Warning, TEXT("We don't have a server URL (or could be the player name...)!"));
        }
        return;
    }
    // If our socket isn't connected, then we should attempt to connect and return.
    // We will attempt to flush the messages again after connecting
    if (!Socket->IsConnected())
    {
        UE_LOG(MiniWebSocket, Log, TEXT("... socket is not connected, returning."));
        Socket->Connect();
        return;
    }
    FPingPayload PingPayload;

    FDateTime CurrentTime = FDateTime::Now();
    PingPayload.PingTime = CurrentTime;
    PingPayload.PingMs   = CurrentTime.GetMillisecond();
    PingPayload.CurrentLatencyEstimate = LatencyEstimate;
    PingPayload.CurrentServerTimeOffsetEstimate = ServerClockOffset;
    Socket->Send(ConvertMessageToString(EWebSocketMessageType::Ping, PingPayload));
    
    
};



FString UBasicWebSocket::WSMessageTypeEnumToString(const EWebSocketMessageType MessageType)
{
    return StaticEnum<EWebSocketMessageType>()->GetNameStringByValue((int64)MessageType);
};

EWebSocketMessageType UBasicWebSocket::WSMessageTypeStringToEnum(const FString MessageTypeString)
{
    return (EWebSocketMessageType)StaticEnum<EWebSocketMessageType>()->GetValueByNameString(MessageTypeString);
};


// Stringify and send a message from enum and payload
template<typename MessageDataType>
void UBasicWebSocket::SendMessage(EWebSocketMessageType MessageType, MessageDataType MessageData)
{
    SendMessage(ConvertMessageToString(MessageType, MessageData));
};

template<typename MessageDataType>
FString UBasicWebSocket::ConvertMessageToString(EWebSocketMessageType MessageType, MessageDataType MessageData)
{
    FString MessageString;
    // Convert the struct part to json
    FJsonObjectConverter::UStructToJsonObjectString(MessageData, MessageString, 0, 0, 2);

    // Add the message type on the front
    MessageString = WSMessageTypeEnumToString(MessageType) + "\n" + MessageString;
    return MessageString;
}

void UBasicWebSocket::SendMessage(EWebSocketMessageType MessageType)
{
    SendMessage(WSMessageTypeEnumToString(MessageType) + "\n{}");
};

// Send a message that has already been stringified. Safe to call from BP. Just adds the message to the queue then tries to flush it.
void UBasicWebSocket::SendMessage(FString MessageString)
{
    if (!bWantToConnect)
    {
        DisconnectFromServer();
        return;
    }
    MessageOutQueue.Enqueue(MessageString);
    FlushMessageOutQueue();
};

template<typename MessageDataType, typename MessageEventType>
void UBasicWebSocket::BroadcastMessageEvent(const MessageEventType MessageEvent, const FString MessageDataString)
{
    MessageDataType MessageData;
    FJsonObjectConverter::JsonObjectStringToUStruct(MessageDataString, &MessageData, 0, 0);
    MessageEvent.Broadcast(MessageData);
};

void UBasicWebSocket::HandlePongMessage(const FString MessageDataString)
{
    if (!bWantToConnect)
    {
        DisconnectFromServer();
        return;
    }
    // Parse MessageDataString into a PongPayload
    FPongPayload PongData;
    FJsonObjectConverter::JsonObjectStringToUStruct(MessageDataString, &PongData, 0, 0);
    
    FDateTime CurrentTime = FDateTime::Now();
    // This is a round trip, so we should be able to divide by 2
    LatencyEstimate = (CurrentTime - PongData.PingTime) / 2;
    
    // The time difference between client and server: How far ahead or behind the server is compared to the client
    ServerClockOffset = PongData.PongTime - (PongData.PingTime + LatencyEstimate);
    UE_LOG(MiniWebSocket, VeryVerbose, TEXT("Pong received, latency estimate is %s, server clock offset estimate is %s"), *LatencyEstimate.ToString(), *ServerClockOffset.ToString());
    
};


void UBasicWebSocket::PlayerAuthenticatedDelegate(const FPlayerAuthenticatedPayload Payload)
{
    if (!bWantToConnect)
    {
        DisconnectFromServer();
        return;
    }
    // Mark the socket as authenticated?
    bIsAuthenticated = true;
    
    UE_LOG(MiniWebSocket, Log, TEXT("Player authenticated, PlayerName: %s\n  PlayerId: %s"), *Payload.PlayerName, *Payload.PlayerID);
    // Ping the server as soon as we're authenticated to measure the clock offsets
    PingServer();
    
    // Flush the message queue
    FlushMessageOutQueue();
};


void UBasicWebSocket::DisconnectFromServer()
{
    UE_LOG(MiniWebSocket, Log, TEXT("Disconnecting websocket..."));
    bWantToConnect = false;
    bIsAuthenticated = false;
    
    if (Socket)
    {
        if (OnSocketClosedLambdaFunctionHandle.IsValid())
        {
            Socket->OnClosed().Remove(OnSocketClosedLambdaFunctionHandle);
        }
        
        if(Socket->IsConnected())
        {
            Socket->Close();
        }
    }
}

void UBasicWebSocket::BeginDestroy()
{
    DisconnectFromServer();
    UE_LOG(MiniWebSocket, Log, TEXT("Destroying websocket, if it's open, we should close it too!"));
    ShuttingDown = true;
    
    Super::BeginDestroy();
};

void UBasicWebSocket::RequestAuthentication(const FRequestAuthenticationPayload Payload )
{
    SendMessage(EWebSocketMessageType::RequestAuthentication, Payload);
};


FTimespan UBasicWebSocket::GetServerTimeElapsedSoFar(const FDateTime StartTime)
{
    return GetEstimatedServerTime() - StartTime;
};

FDateTime UBasicWebSocket::GetEstimatedServerTime()
{
    return FDateTime::Now() + ServerClockOffset;
};


void UBasicWebSocket::HandleInboundMessage(const FString & Message)
{
    if (!bWantToConnect)
    {
        DisconnectFromServer();
        return;
    }
    // If we get any message from the server, that means it's live
    ConnectionIsLive = true;
    LastStringMessageLength = Message.Len();
    

    FString MessageTypeString, MessageDataString;
    
    // First line of the message tells us what kind of message it is
    Message.Split("\n", &MessageTypeString, &MessageDataString);
  
    EWebSocketMessageType MessageType = WSMessageTypeStringToEnum(MessageTypeString);

    if (MessageType != EWebSocketMessageType::Pong)
    {
        UE_LOG(MiniWebSocket, Verbose, TEXT("String Message received of length %d: %s"), LastStringMessageLength, *Message);
    }
    
    OnMessageReceived.Broadcast(Message, FDateTime::Now());
    
    switch (MessageType)
    {
        case EWebSocketMessageType::PlayerAuthenticated:
            UE_LOG(MiniWebSocket, Verbose, TEXT("Player authenticated"));        

            BroadcastMessageEvent<FPlayerAuthenticatedPayload>(OnPlayerAuthenticated, MessageDataString);
            break;
        case EWebSocketMessageType::Pong:
            HandlePongMessage(MessageDataString);
            // if we got a pong, chances are we sent a ping and may have blocked some messages from being sent while waiting for it
            FlushMessageOutQueue();
            break;
            
        case EWebSocketMessageType::WarningMessage:
            UE_LOG(MiniWebSocket, Warning, TEXT("Received a warning message from server:\n %s"), *MessageDataString);
            OnWarningMessage.Broadcast(MessageDataString);
            break;
        case EWebSocketMessageType::ErrorMessage:
            UE_LOG(MiniWebSocket, Warning, TEXT("Received an error message from server:\n %s?"), *MessageDataString);
            OnErrorMessage.Broadcast(MessageDataString);
            break;
                   
    }
}
