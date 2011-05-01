#include "StdAfx.h"
#include "NetworkTransmitter.h"


NetworkTransmitter::NetworkTransmitter(std::wstring host, int port) :
_socket(INVALID_SOCKET)
{
    _host = host;
    _port = port;
}

NetworkTransmitter::~NetworkTransmitter(void)
{
    if (_socket != INVALID_SOCKET)
    {
        closesocket(_socket);
        _socket = INVALID_SOCKET;
    }
}

HRESULT NetworkTransmitter::Connect(void)
{
    BOOL bSuccess;
    SOCKADDR_STORAGE LocalAddr = {0};
    SOCKADDR_STORAGE RemoteAddr = {0};
    DWORD dwLocalAddr = sizeof(LocalAddr);
    DWORD dwRemoteAddr = sizeof(RemoteAddr);

    static bool initializedWinSock = false;
    if (!initializedWinSock)
    {
        // Initialize Winsock with version 2.2
        WSADATA wsd;
        if (WSAStartup(MAKEWORD(2,2), &wsd) != 0)
            return E_FAIL;
        initializedWinSock = true;
    }

    // Create socket.
    _socket = socket(AF_INET, SOCK_STREAM, 0);
    if (_socket == INVALID_SOCKET)
        return E_FAIL;

    WCHAR port[10];
    swprintf(port, _countof(port), L"%d", _port);

    // Connect to destination.
    bSuccess = WSAConnectByName(_socket, (LPWSTR) _host.c_str(), 
        port, &dwLocalAddr, (SOCKADDR*)&LocalAddr,
        &dwRemoteAddr, (SOCKADDR*)&RemoteAddr,
        NULL, NULL);

    if (!bSuccess)
    {
        closesocket(_socket);
        _socket = INVALID_SOCKET;
        return E_FAIL;
    }

    return S_OK;
}

HRESULT NetworkTransmitter::StreamAudio(const AudioCapturer &capturer)
{
    // Just stream data as fast as possible
    int index = capturer.GetBufferPosition();
    const char *buffer = (const char *)capturer.GetBuffer();
    HANDLE audioCapturedEvent = capturer.GetAudioCapturedEvent();

    while (true)
    {
        int bytesToSend = capturer.GetBufferPosition() - index;

        // Buffer possition has looped.  Just send to end of the buffer.
        if (bytesToSend < 0)
            bytesToSend = capturer.GetBufferSize() - index;

        assert(bytesToSend >= 0);
        if (bytesToSend > 0)
        {
            UINT32 bytesSent = send(_socket, buffer + index, bytesToSend, 0);
            if (bytesSent == SOCKET_ERROR)
            {
                printf("Error sending: %d\n", WSAGetLastError());
                return E_FAIL;
            }

            // Advance index and loop if necessary.
            index += bytesSent;
            if (index == capturer.GetBufferSize())
                index = 0;
        }
        else
        {
            // Wait for more data.
            DWORD result = WaitForSingleObject(audioCapturedEvent, INFINITE);
            assert(result == WAIT_OBJECT_0);
        }
    }

    return S_OK;
}