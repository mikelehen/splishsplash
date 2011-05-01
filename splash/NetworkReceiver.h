#pragma once

/// Responsible for listening for network connections and streaming audio data from the network
/// into a buffer.
class NetworkReceiver
{
public:
    NetworkReceiver(int bufferSize, int port);
    ~NetworkReceiver(void);

    BYTE *GetBuffer() const;
    UINT32 GetBufferSize() const;
    UINT32 GetBufferPosition() const;
    HANDLE GetDataReceivedEvent() const;

    HRESULT WaitForConnection();
    HRESULT StreamData();

private:
    UINT32 _bufferSize;
    UINT32 _bufferPosition;
    BYTE *_buffer;
    HANDLE _dataReceivedEvent;

    int _port;
    SOCKET _socket;
};

