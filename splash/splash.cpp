// splash.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "NetworkReceiver.h"
#include "AudioRenderer.h"

#define DEFAULT_PORT 8265

int wmain(int argc, WCHAR* argv[])
{
    HRESULT hr;

    if (argc > 2 || (argc == 2 && _wtoi(argv[1]) == 0))
    {
        fprintf(stderr, "Usage: splash [port]\n");
        return -1;
    }

    // Initialize COM
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    // Initialize this thread as a multimedia thread.
    HANDLE mmcssHandle = NULL;
    DWORD mmcssTaskIndex = 0;
    mmcssHandle = AvSetMmThreadCharacteristics(L"Audio", &mmcssTaskIndex);

    int port = (argc == 2) ? _wtoi(argv[1]) : DEFAULT_PORT;

    // Allow for up to 2 seconds of network buffer (at 44.1khz, 2 channel, 32bits per sample).
    // This affects the *maximum* latency.
    int bufferSize = 2 * 44100 * 2 * 4;
    NetworkReceiver receiver(bufferSize, port);

    // Start the audio thread (it will wait until there's data from the network to play).
    AudioRenderer renderer(/*latencyInMilliseconds=*/40);
    hr = renderer.Initialize();
    if (FAILED(hr))
    {
        fprintf(stderr, "Failed to initialize audio.  Error hresult=%d.\n", hr);
        goto Error;
    }

    hr = renderer.Start(receiver);
    if (FAILED(hr))
    {
        fprintf(stderr, "Failed to start playing audio.  Error hresult=%d.\n", hr);
        goto Error;
    }

    // Repeatedly wait for a connection and stream audio data from network to the audio renderer.
    while (true)
    {
        printf("Waiting for connection on port %d.\n", port);
        int hr = receiver.WaitForConnection();
        if (FAILED(hr))
        {
            fprintf(stderr, "Failed to listen on port %d.  Retry in 1 second...\n", port);
            Sleep(1000);
            continue;
        }

        printf("Received connection.  Streaming audio...\n");

        hr = receiver.StreamData();
        if (FAILED(hr))
        {
            fprintf(stderr, "Error while streaming data: %d\n", hr);
            continue;
        }
    }

Error:
    CoUninitialize();
    AvRevertMmThreadCharacteristics(mmcssHandle);

    assert(SUCCEEDED(hr));
    return 0;
}
