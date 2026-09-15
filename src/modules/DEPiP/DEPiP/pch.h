#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>

#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <algorithm>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>
