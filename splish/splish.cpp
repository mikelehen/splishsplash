// splish.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "AudioCapturer.h"
#include "NetworkTransmitter.h"

#define DEFAULT_PORT 8265

int wmain(int argc, wchar_t* argv[])
{
    HRESULT hr;

    // Initialize COM
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    // Initialize this thread as a multimedia thread.
    HANDLE mmcssHandle = NULL;
    DWORD mmcssTaskIndex = 0;
    mmcssHandle = AvSetMmThreadCharacteristics(L"Audio", &mmcssTaskIndex);

    if (argc < 2 || argc > 3 || (argc == 3 && _wtoi(argv[2]) == 0))
    {
        fprintf(stderr, "Usage: splish <splash server IP> [port]\n");
        return -1;
    }
    std::wstring host = argv[1];
    int port = (argc == 3) ? _wtoi(argv[2]) : DEFAULT_PORT;

    // Start up audio capture thread.
    AudioCapturer capturer(/*bufferSizeInSeconds=*/ 2, /*latencyInMilliseconds=*/ 20);
    hr = capturer.Initialize();
    if (FAILED(hr))
    {
        fprintf(stderr, "Failed to initialize audio capturing: %d\n", hr);
        goto Error;
    }

    hr = capturer.Start();
    if (FAILED(hr))
    {
        fprintf(stderr, "Failed to start capturing: %d\n", hr);
        goto Error;
    }

    while (true)
    {
        NetworkTransmitter transmitter(host, port);

        printf("Connecting to %S:%d...\n", host.c_str(), port);
        hr = transmitter.Connect();
        if (FAILED(hr))
        {
            fprintf(stderr, "Failed to connect.  Retrying in 1 second...\n");
            Sleep(1000);
            continue;
        }

        printf("Successfully connected.  Streaming audio...\n");

        hr = transmitter.StreamAudio(capturer);
        if (FAILED(hr))
        {
            // Non-fatal (hopefully); so continue capturing / streaming.
            fprintf(stderr, "Error while streaming audio: %d\n", hr);
        }
    }

Error:
    CoUninitialize();
    AvRevertMmThreadCharacteristics(mmcssHandle);

    assert(SUCCEEDED(hr));
    return 0;
}

