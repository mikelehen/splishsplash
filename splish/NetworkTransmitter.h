#pragma once

#include "audiocapturer.h"

/// Responsible for grabbing audio data captured by the AudioCapturer and sending it
/// over the network to the remote client.
class NetworkTransmitter
{
public:
    NetworkTransmitter(std::wstring host, int port);
    ~NetworkTransmitter(void);

    HRESULT Connect(void);
    HRESULT StreamAudio(const AudioCapturer &capturer);

private:
    std::wstring _host;
    int _port;
    SOCKET _socket;

};

