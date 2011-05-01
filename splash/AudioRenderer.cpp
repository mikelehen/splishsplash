#include "StdAfx.h"
#include "AudioRenderer.h"

AudioRenderer::AudioRenderer(int latencyInMilliseconds) :
_audioClient(NULL),
    _renderClient(NULL),
    _pWaveFormat(NULL),
    _renderThread(NULL),
    _shutDown(false),
    _audioBufferReadyEvent(NULL),
    _latencyInMilliseconds(latencyInMilliseconds)
{
}

AudioRenderer::~AudioRenderer(void)
{
    Cleanup();


}

HRESULT AudioRenderer::Initialize(void)
{
    HRESULT hr;

    // Initialize Audio client
    CComPtr<IMMDevice> device;
    IfFailGo(GetDefaultRenderDevice(&device));

    IfFailGo(device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, NULL, reinterpret_cast<void **>(&_audioClient)));

    IfFailGo(_audioClient->GetMixFormat(&_pWaveFormat));
    if (_pWaveFormat->wBitsPerSample != 32 || _pWaveFormat->nChannels != 2 || _pWaveFormat->nSamplesPerSec != 44100)
    {
        fprintf(stderr, "Unexpected wave format: %d bits per sample, %d channels, %d samples / sec.\n", 
            _pWaveFormat->wBitsPerSample, _pWaveFormat->nChannels, _pWaveFormat->nSamplesPerSec);
        fprintf(stderr, "\nYou need to change the default sample rate in Windows.\n");
        fprintf(stderr, "See: http://splishsplash.codeplex.com/wikipage?title=Configuring%%20Windows%%20Default%%20Audio%%20Format\n\n");
        hr = E_FAIL;
        goto Error;
    }

    IfFailGo(_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        /* hnsBufferDuration=*/ _latencyInMilliseconds*10000, 
        /* hnsPeriodicity=*/ 0, 
        _pWaveFormat, 
        /*AudioSessionGuid=*/ NULL));

    // Create and set up event to notify us when the audio buffer is ready for more data.
    _audioBufferReadyEvent = CreateEvent(
        /*lpEventAttributes=*/ NULL, 
        /*bManualReset=*/ FALSE,
        /*bInitialState=*/ FALSE,
        /*lpName=*/ NULL);
    IfFailGo(_audioClient->SetEventHandle(_audioBufferReadyEvent));

    IfFailGo(_audioClient->GetService(IID_PPV_ARGS(&_renderClient)));

Error:
    assert(SUCCEEDED(hr));
    return hr;
}

HRESULT AudioRenderer::Start(const NetworkReceiver &receiver)
{
    HRESULT hr;

    _networkReceiver = &receiver;

    IfFailGo(_audioClient->Start());

    _renderThread = CreateThread(NULL, 0, RenderThreadStart, this, 0, NULL);
    hr = _renderThread != NULL;

Error:
    assert(SUCCEEDED(hr));
    return hr;
}

DWORD AudioRenderer::RenderThreadStart(LPVOID context)
{
    AudioRenderer *renderer= static_cast<AudioRenderer *>(context);
    return renderer->RenderThread();
}

HRESULT AudioRenderer::RenderThread(void)
{
    HRESULT hr;
    int index = 0;

    HANDLE mmcssHandle = NULL;
    DWORD mmcssTaskIndex = 0;
    mmcssHandle = AvSetMmThreadCharacteristics(L"Audio", &mmcssTaskIndex);

    BYTE *networkBuffer = _networkReceiver->GetBuffer();

    // TODO: Make this configurable.
    // We always try to stay at least 1/2 a second (22050 samples at 44.1khz) away from the beginning or end of 
    // the buffer, to avoid glitching. (this causes at least 1/2 second latency though)
    UINT32 networkBufferPadding = 22050 * TargetFrameSize;

    UINT32 audioBufferSize;
    IfFailGo(_audioClient->GetBufferSize(&audioBufferSize));

    while (!_shutDown)
    {
        int bytesAvailableToPlay = _networkReceiver->GetBufferPosition() - index;
        // Need to loop if network receiver is "behind" us in the buffer.
        if (bytesAvailableToPlay < 0)
            bytesAvailableToPlay += _networkReceiver->GetBufferSize();

        if ((UINT32)bytesAvailableToPlay > _networkReceiver->GetBufferSize() - networkBufferPadding)
        {
            // Running out of buffer.  Let's skip ahead.
            fprintf(stderr, "Network buffer is almost full.  Skipping audio frames to catch up.\n");
            index = _networkReceiver->GetBufferPosition() - networkBufferPadding;
            if (index < 0)
                index += _networkReceiver->GetBufferSize();
        }

        // Calculate # of bytes to play.  If we are looping, just play to end of buffer.
        int bytesToPlay = _networkReceiver->GetBufferPosition() - index;
        if (bytesToPlay < 0)
            bytesToPlay = _networkReceiver->GetBufferSize() - index;

        UINT32 framesToPlay = bytesToPlay / TargetFrameSize;
        if (framesToPlay > 0)
        {
            // padding is the amount of data already in the buffer (waiting to be played).
            UINT32 padding;
            IfFailGo(_audioClient->GetCurrentPadding(&padding));

            // audioBufferSize - padding is the amount of space in the buffer for us to fill.
            framesToPlay = min(framesToPlay, audioBufferSize - padding);
            if (framesToPlay > 0)
            {
                BYTE *pData = NULL;
                IfFailGo(_renderClient->GetBuffer(framesToPlay, &pData));

                CopyMemory(pData, networkBuffer + index, framesToPlay * TargetFrameSize);

                IfFailGo(_renderClient->ReleaseBuffer(framesToPlay, /*dwFlags=*/ 0));

                // Advance position and loop if necessary.
                index += framesToPlay * TargetFrameSize;
                if ((UINT32)index >= _networkReceiver->GetBufferSize())
                {
                    assert(index == _networkReceiver->GetBufferSize());
                    index = 0;
                }
            }
            else
            {
                // Audio buffer is full.  Wait for it to be ready for more writing.
                WaitForSingleObject(_audioBufferReadyEvent, INFINITE);
            }
        }
        else
        {
            // We've emptied our network buffer. :-(  Wait until we have networkBufferPadding bytes buffered again.
            // TODO: Instead of waiting, we could just move our index backwards and re-play the last bit of audio.
            // fprintf(stderr, "Network buffer is empty.  Waiting for %d bytes.\n", networkBufferPadding);
            int bytesInBuffer;
            do
            {
                WaitForSingleObject(_networkReceiver->GetDataReceivedEvent(), INFINITE);

                bytesInBuffer = _networkReceiver->GetBufferPosition() - index;
                if (bytesInBuffer < 0)
                    bytesInBuffer += _networkReceiver->GetBufferSize();
            }
            while ((UINT32)bytesInBuffer < networkBufferPadding);
        }
    }

Error:
    assert(SUCCEEDED(hr));

    if (FAILED(hr))
    {
        fprintf(stderr, "RenderThread failed: %d\n", hr);
    }

    AvRevertMmThreadCharacteristics(mmcssHandle);
    _shutDown = true;
    Cleanup();


    return hr;
}

HRESULT AudioRenderer::GetDefaultRenderDevice(IMMDevice **device)
{
    HRESULT hr;
    CComPtr<IMMDeviceEnumerator> deviceEnumerator;

    IfFailGo(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&deviceEnumerator)));
    IfFailGo(deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, device));

Error:
    assert(SUCCEEDED(hr));
    return hr;
}

HRESULT AudioRenderer::Cleanup(void)
{
    assert(_shutDown);

    if (_pWaveFormat != NULL)
    {
        CoTaskMemFree(_pWaveFormat);
        _pWaveFormat = NULL;
    }

    if (_audioBufferReadyEvent != NULL)
    {
        CloseHandle(_audioBufferReadyEvent);
        _audioBufferReadyEvent = NULL;
    }

    if (_audioClient != NULL)
    {
        _audioClient->Stop();
        _audioClient = NULL;
    }

    _renderThread = NULL;
    _renderClient = NULL;
    _networkReceiver = NULL;

    return S_OK;
}

HRESULT AudioRenderer::GetIsRunning(bool *fRunning)
{
    *fRunning = !_shutDown;
    return S_OK;
}
