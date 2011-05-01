// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#include <atlbase.h>

#include <assert.h>
#include <string>

#include <winsock2.h>
#include <Ws2tcpip.h>

// WASAPI includes
#include <mmdeviceapi.h>
#include <audioclient.h>

// For AvSetMmThreadCharacteristics.
#include <avrt.h>

#define IfFailGo(hresult)  if(FAILED(hr = (hresult))) goto Error;
