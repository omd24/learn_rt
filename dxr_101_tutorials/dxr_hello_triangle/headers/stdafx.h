#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

#include <stdlib.h>
#include <sstream>
#include <iomanip>

#include <list>
#include <string>
#include <wrl.h>
#include <shellapi.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include <atlbase.h>
#include <assert.h>

#include <dxgi1_6.h>
#include <d3d12.h>
#include "d3dx12.h"

#ifdef _DEBUG
#include <dxgidebug.h>
#endif

#include "dx_sample_helper.h"
#include "device_resources.h"

