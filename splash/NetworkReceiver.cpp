#include "StdAfx.h"
#include "NetworkReceiver.h"


NetworkReceiver::NetworkReceiver(int bufferSize, int port) :
_bufferPosition(0),
    _dataReceivedEvent(NULL),
    _socket(INVALID_SOCKET)
{
    _port = port;

    _bufferSize = bufferSize;
    _buffer = new BYTE[bufferSize];
    ZeroMemory(_buffer, bufferSize);

    // Create event used to signal when we've received data.
    _dataReceivedEvent = CreateEvent(
        /*lpEventAttributes=*/ NULL, 
        /*bManualReset=*/ FALSE,
        /*bInitialState=*/ FALSE,
        /*lpName=*/ NULL);
}

NetworkReceiver::~NetworkReceiver(void)
{
    if (_buffer != NULL)
    {
        delete[] _buffer;
        _buffer = NULL;
    }

    if (_socket != INVALID_SOCKET)
    {
        closesocket(_socket);
        _socket = INVALID_SOCKET;
    }

    if (_dataReceivedEvent != NULL)
    {
        CloseHandle(_dataReceivedEvent);
        _dataReceivedEvent = NULL;
    }
}

BYTE *NetworkReceiver::GetBuffer() const
{
    return _buffer;
}

UINT32 NetworkReceiver::GetBufferSize() const
{
    return _bufferSize;
}

UINT32 NetworkReceiver::GetBufferPosition() const
{
    return _bufferPosition;
}

HANDLE NetworkReceiver::GetDataReceivedEvent() const
{
    return _dataReceivedEvent;
}

HRESULT NetworkReceiver::WaitForConnection()
{
    static bool initializedWinSock = false;
    if (!initializedWinSock)
    {
        // Initialize Winsock with version 2.2
        WSADATA wsd;
        if (WSAStartup(MAKEWORD(2,2), &wsd) != 0)
            return E_FAIL;
        initializedWinSock = true;
    }

    // Do any cleanup from previous connections.
    if (_socket != INVALID_SOCKET)
    {
        closesocket(_socket);
        _socket = INVALID_SOCKET;
    }

    // Create a listen socket.
    SOCKET listenSocket;
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET)
    {
        return E_FAIL;
    }

    // Set up listening address / port.
    sockaddr_in listenAddr;
    listenAddr.sin_family = AF_INET;
    listenAddr.sin_addr.s_addr = INADDR_ANY;
    listenAddr.sin_port = htons(_port);

    // Bind listenSocket to our address / port.
    int result = bind( listenSocket, (SOCKADDR*) &listenAddr,  sizeof(listenAddr));
    if (result == SOCKET_ERROR)
    {
        printf("%d\n", WSAGetLastError());
        closesocket(listenSocket);
        return E_FAIL;
    }

    // Listen for incoming connections.
    result = listen(listenSocket, /*backlog=*/ 1);
    if (result == SOCKET_ERROR)
    {
        closesocket(listenSocket);
        return E_FAIL;
    }

    // Now wait for and accept the first connection we get.
    _socket = accept(listenSocket, NULL, NULL);
    if (_socket == INVALID_SOCKET)
    {
        closesocket(listenSocket);
        return E_FAIL;
    }

    // Done with listenSocket.
    closesocket(listenSocket);
    return S_OK;
}

HRESULT NetworkReceiver::StreamData()
{
    while(true)
    {
        int bytesReceived = recv(_socket, (char*)(_buffer + _bufferPosition), _bufferSize - _bufferPosition, /*flags=*/ 0);
        if (bytesReceived == 0)
        {
            // socket was closed.
            closesocket(_socket);
            return S_OK;
        }
        else if (bytesReceived < 0)
        {
            int error = WSAGetLastError();
            if (error == WSAEMSGSIZE)
            {
                // didn't have enough space.  That's okay.  We'll get the rest next time.
                assert (_bufferPosition != 0); // entire buffer wasn't enough space?
                _bufferPosition = 0;
            }
            else
            {
                printf("Error receiving: %d\n", error);
                closesocket(_socket);
                return E_FAIL;
            }
        }
        else
        {
            // Everything was successful.  Advance buffer and loop if necessary.
            _bufferPosition += bytesReceived;
            if (_bufferPosition >= _bufferSize)
            {
                _bufferPosition = 0;
            }
        }

        // Signal new data available.
        SetEvent(_dataReceivedEvent);
    }
}