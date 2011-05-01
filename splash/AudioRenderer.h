#pragma once

#include "NetworkReceiver.h"

/// Responsible for creating a thread that waits for data from the network and then 
/// plays the received audio data to the audio render device.
class AudioRenderer
{
public:
    AudioRenderer(int latencyInMilliseconds);
    ~AudioRenderer(void);

    HRESULT Initialize(void);
    HRESULT Start(const NetworkReceiver &receiver);
    HRESULT GetIsRunning(bool *fRunning);

private:
    static const int TargetFrameSize = 8; // 2 channels at 32 bits per sample.

    int _latencyInMilliseconds;

    CComPtr<IAudioClient> _audioClient;
    CComPtr<IAudioRenderClient> _renderClient;
    const NetworkReceiver *_networkReceiver;
    WAVEFORMATEX *_pWaveFormat;
    HANDLE _renderThread;
    volatile bool _shutDown;
    HANDLE _audioBufferReadyEvent;

    // helpers
    HRESULT GetDefaultRenderDevice(IMMDevice **device);
    HRESULT Cleanup(void);

    static DWORD __stdcall RenderThreadStart(LPVOID Context);
    HRESULT RenderThread(void);
};
