#pragma once

/// Responsible for launching a thread that continually captures the audio output and makes
/// it available in a cyclic buffer.  An event is raised every time new audio data is 
/// available.
class AudioCapturer
{
public:
    AudioCapturer(int bufferSizeInSeconds, int latencyInMilliseconds);
    ~AudioCapturer(void);

    HRESULT Initialize(void);
    HRESULT Start(void);
    HRESULT Stop(void);

    BYTE *GetBuffer() const;
    UINT32 GetBufferSize() const;
    UINT32 GetBufferPosition() const;
    HANDLE GetAudioCapturedEvent() const;

private:
    static const int TargetFrameSize = 8; // 2 channels at 32 bits per sample.

    int _latencyInMilliseconds;
    UINT32 _bufferSize;
    BYTE *_buffer;
    UINT32 _bufferIndex;
    HANDLE _audioCapturedEvent;

    CComPtr<IAudioClient> _audioClient;
    CComPtr<IAudioCaptureClient> _captureClient;
    WAVEFORMATEX *_pWaveFormat;
    HANDLE _captureThread;
    volatile bool _shutDown;

    // helpers
    HRESULT GetDefaultRenderDevice(IMMDevice **device);

    static DWORD __stdcall CaptureThreadStart(LPVOID Context);
    HRESULT CaptureThread(void);
};

