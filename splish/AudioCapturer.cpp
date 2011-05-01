#include "StdAfx.h"
#include "AudioCapturer.h"


AudioCapturer::AudioCapturer(int bufferSizeInSeconds, int latencyInMilliseconds) :
_buffer(NULL),
    _bufferIndex(0),
    _audioClient(NULL),
    _captureClient(NULL),
    _pWaveFormat(NULL),
    _captureThread(NULL),
    _shutDown(false),
    _latencyInMilliseconds(latencyInMilliseconds)
{
    _bufferSize = bufferSizeInSeconds * 44100 * TargetFrameSize;
}

AudioCapturer::~AudioCapturer(void)
{
    Stop();

    if (_pWaveFormat != NULL)
    {
        CoTaskMemFree(_pWaveFormat);
        _pWaveFormat = NULL;
    }

    if (_buffer != NULL)
    {
        delete[] _buffer;
        _buffer = NULL;
    }

    if (_audioCapturedEvent != NULL)
    {
        CloseHandle(_audioCapturedEvent);
        _audioCapturedEvent = NULL;
    }
}

HRESULT AudioCapturer::Initialize(void)
{
    HRESULT hr;

    // We need to get the default rendering (playback) device and then initialize our audio client
    // as a *loopback* capture device, in order to capture the sound output.

    CComPtr<IMMDevice> device;
    IfFailGo(GetDefaultRenderDevice(&device));

    IfFailGo(device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, NULL, reinterpret_cast<void **>(&_audioClient)));

    IfFailGo(_audioClient->GetMixFormat(&_pWaveFormat));
    if (_pWaveFormat->wBitsPerSample != 32 || _pWaveFormat->nChannels != 2 || _pWaveFormat->nSamplesPerSec != 44100)
    {
        fprintf(stderr, "Unsupported wave format: %d bits per sample, %d channels, %d samples / sec.\n", 
            _pWaveFormat->wBitsPerSample, _pWaveFormat->nChannels, _pWaveFormat->nSamplesPerSec);
        fprintf(stderr, "\nYou need to change the default sample rate in Windows.\n");
        fprintf(stderr, "See: http://splishsplash.codeplex.com/wikipage?title=Configuring%%20Windows%%20Default%%20Audio%%20Format\n\n");
        hr = E_FAIL;
        goto Error;
    }

    // AUDCLNT_STREAMFLAGS_LOOPBACK is the critical flag so we can *capture* from this rendering device.
    IfFailGo(_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED, 
        AUDCLNT_STREAMFLAGS_LOOPBACK, 
        /* hnsBufferDuration=*/ _latencyInMilliseconds*10000, 
        /* hnsPeriodicity=*/ 0, 
        _pWaveFormat, 
        /*AudioSessionGuid=*/ NULL));

    IfFailGo(_audioClient->GetService(IID_PPV_ARGS(&_captureClient)));

    // Allocate buffer for captured samples.
    _buffer = new BYTE[_bufferSize];

    // Create event used to signal when we've captured data.
    _audioCapturedEvent = CreateEvent(
        /*lpEventAttributes=*/ NULL, 
        /*bManualReset=*/ FALSE,
        /*bInitialState=*/ FALSE,
        /*lpName=*/ NULL);

Error:
    assert(SUCCEEDED(hr));
    return hr;
}

HRESULT AudioCapturer::Start(void)
{
    HRESULT hr = S_OK;

    _captureThread = CreateThread(NULL, 0, CaptureThreadStart, this, 0, NULL);

    IfFailGo(_audioClient->Start());

Error:
    assert(SUCCEEDED(hr));
    return hr;
}

HRESULT AudioCapturer::Stop(void)
{
    int hr;

    IfFailGo(_audioClient->Stop());

    _shutDown = true;

    // wait for thread to shut down.
    if (_captureThread != NULL)
    {
        WaitForSingleObject(_captureThread, INFINITE);

        CloseHandle(_captureThread);
        _captureThread = NULL;
    }

Error:
    assert(SUCCEEDED(hr));
    return hr;
}

BYTE *AudioCapturer::GetBuffer(void) const
{
    return _buffer;
}

UINT32 AudioCapturer::GetBufferSize(void) const
{
    return _bufferSize;
}

UINT32 AudioCapturer::GetBufferPosition(void) const
{
    return _bufferIndex;
}

HANDLE AudioCapturer::GetAudioCapturedEvent(void) const
{
    return _audioCapturedEvent;
}

DWORD AudioCapturer::CaptureThreadStart(LPVOID context)
{
    AudioCapturer *capturer = static_cast<AudioCapturer *>(context);
    return capturer->CaptureThread();
}

HRESULT AudioCapturer::CaptureThread(void)
{
    HRESULT hr = S_OK;

    // Initialize this thread as a multimedia thread.
    HANDLE mmcssHandle = NULL;
    DWORD mmcssTaskIndex = 0;
    mmcssHandle = AvSetMmThreadCharacteristics(L"Audio", &mmcssTaskIndex);

    while (!_shutDown)
    {
        Sleep(_latencyInMilliseconds / 2);

        // Retrieve samples from input buffer.
        BYTE *pData;
        UINT32 framesAvailable;
        DWORD  flags;

        IfFailGo(_captureClient->GetBuffer(&pData, &framesAvailable, &flags, NULL, NULL));

        // Useful for detecting glitches.
        //if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0)
        //	printf("AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY\n");

        // Time to copy frames from the capture buffer to *our* buffer.  We'll execute through the 
        // while loop once (normal) or twice (if we're looping around in our cyclic buffer).
        UINT32 framesLeft = framesAvailable;
        while (framesLeft > 0)
        {
            UINT32 framesToCopy = min(framesLeft, static_cast<UINT32>((_bufferSize - _bufferIndex) / TargetFrameSize));

            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
            {
                //  Fill the output buffer with 0's.
                ZeroMemory(&_buffer[_bufferIndex], framesToCopy * TargetFrameSize);
            }
            else
            {
                // Copy audio data to the output buffer

                // TODO: We ASSUME input / target formats are the same.  Else, need to do conversion.
                CopyMemory(&_buffer[_bufferIndex], pData, framesToCopy * TargetFrameSize);
            }

            // Advance buffer, looping if necessary.
            _bufferIndex += framesToCopy * TargetFrameSize;
            if (_bufferIndex >= _bufferSize)
                _bufferIndex = 0;

            // At this point, there's new data available.  Signal event.
            SetEvent(_audioCapturedEvent);

            // TODO: Again, assuming input / target formats are the same.
            pData += framesToCopy * TargetFrameSize;

            // Subtract copied frames.
            framesLeft -= framesToCopy;
        }

        IfFailGo(_captureClient->ReleaseBuffer(framesAvailable));
    }

    AvRevertMmThreadCharacteristics(mmcssHandle);

Error:
    assert(SUCCEEDED(hr));
    return hr;
}

HRESULT AudioCapturer::GetDefaultRenderDevice(IMMDevice **device)
{
    HRESULT hr;
    CComPtr<IMMDeviceEnumerator> deviceEnumerator;

    IfFailGo(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&deviceEnumerator)));
    IfFailGo(deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, device));

Error:
    assert(SUCCEEDED(hr));
    return hr;
}
