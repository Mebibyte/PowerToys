#include "pch.h"

#include "../ModuleConstants.h"
#include "resource.h"

namespace
{
    constexpr wchar_t WindowClassName[] = L"PowerToys.DEPiP.Window";
    constexpr int InitialClientWidth = 960;
    constexpr int MinimumClientWidth = 320;

    struct DisplayInfo
    {
        HMONITOR monitor = nullptr;
        MONITORINFOEXW info{ sizeof(MONITORINFOEXW) };
    };

    BOOL CALLBACK FindMonitors(HMONITOR monitor, HDC, LPRECT, LPARAM data)
    {
        auto displays = reinterpret_cast<std::vector<DisplayInfo>*>(data);
        MONITORINFOEXW info{ sizeof(MONITORINFOEXW) };
        if (!GetMonitorInfoW(monitor, &info))
        {
            return TRUE;
        }

        displays->push_back({ monitor, info });
        return TRUE;
    }

    std::vector<DisplayInfo> EnumerateDisplays()
    {
        std::vector<DisplayInfo> displays;
        EnumDisplayMonitors(nullptr, nullptr, FindMonitors, reinterpret_cast<LPARAM>(&displays));
        return displays;
    }

    std::wstring LoadResourceString(HINSTANCE instance, UINT id)
    {
        const wchar_t* value = nullptr;
        const int length = LoadStringW(instance, id, reinterpret_cast<wchar_t*>(&value), 0);
        winrt::check_bool(length > 0);
        return { value, static_cast<size_t>(length) };
    }

    DisplayInfo SelectDisplay(HINSTANCE instance, const std::vector<DisplayInfo>& displays)
    {
        std::vector<std::wstring> labels;
        std::vector<TASKDIALOG_BUTTON> buttons;
        labels.reserve(displays.size());
        buttons.reserve(displays.size());

        const auto primaryLabel = LoadResourceString(instance, IDS_PRIMARY_DISPLAY);
        for (size_t index = 0; index < displays.size(); ++index)
        {
            const auto& display = displays[index];
            const RECT bounds = display.info.rcMonitor;
            std::wstring label = std::to_wstring(index + 1);
            if ((display.info.dwFlags & MONITORINFOF_PRIMARY) != 0)
            {
                label.append(L" (").append(primaryLabel).append(L")");
            }
            label.append(L" - ")
                .append(std::to_wstring(bounds.right - bounds.left))
                .append(L" x ")
                .append(std::to_wstring(bounds.bottom - bounds.top))
                .append(L" - ")
                .append(display.info.szDevice);
            labels.push_back(std::move(label));
        }

        for (size_t index = 0; index < labels.size(); ++index)
        {
            buttons.push_back({ static_cast<int>(index + 1), labels[index].c_str() });
        }

        const auto title = LoadResourceString(instance, IDS_DISPLAY_SELECTOR_TITLE);
        const auto instruction = LoadResourceString(instance, IDS_DISPLAY_SELECTOR_INSTRUCTION);
        const auto description = LoadResourceString(instance, IDS_DISPLAY_SELECTOR_DESCRIPTION);
        TASKDIALOGCONFIG config{ sizeof(config) };
        config.hInstance = instance;
        config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
        config.dwCommonButtons = TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON;
        config.pszWindowTitle = title.c_str();
        config.pszMainInstruction = instruction.c_str();
        config.pszContent = description.c_str();
        config.cRadioButtons = static_cast<UINT>(buttons.size());
        config.pRadioButtons = buttons.data();
        config.nDefaultRadioButton = buttons.front().nButtonID;

        int selectedButton = 0;
        int selectedDisplay = 0;
        winrt::check_hresult(TaskDialogIndirect(&config, &selectedButton, &selectedDisplay, nullptr));
        if (selectedButton == IDCANCEL || selectedDisplay <= 0)
        {
            throw winrt::hresult_canceled();
        }
        return displays.at(static_cast<size_t>(selectedDisplay) - 1);
    }

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateCaptureItem(HMONITOR monitor)
    {
        using namespace winrt::Windows::Graphics::Capture;

        const auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem item{ nullptr };
        winrt::check_hresult(interop->CreateForMonitor(
            monitor,
            winrt::guid_of<GraphicsCaptureItem>(),
            winrt::put_abi(item)));
        return item;
    }

    template<typename T>
    winrt::com_ptr<T> GetDXGIInterface(const winrt::Windows::Foundation::IInspectable& object)
    {
        const auto access = object.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<T> result;
        winrt::check_hresult(access->GetInterface(winrt::guid_of<T>(), result.put_void()));
        return result;
    }

    class MirrorWindow
    {
    public:
        MirrorWindow(DisplayInfo primaryDisplay, DisplayInfo sourceDisplay, std::wstring title) :
            m_primaryDisplay{ primaryDisplay },
            m_sourceDisplay{ sourceDisplay },
            m_title{ std::move(title) }
        {
        }

        ~MirrorWindow()
        {
            StopCapture();
        }

        void Initialize(HINSTANCE instance)
        {
            m_captureItem = CreateCaptureItem(m_sourceDisplay.monitor);
            CreateMirrorWindow(instance);
            CreateGraphicsResources();
            StartCapture();
        }

        HWND Window() const
        {
            return m_window;
        }

    private:
        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wordParam, LPARAM longParam)
        {
            if (message == WM_NCCREATE)
            {
                const auto create = reinterpret_cast<CREATESTRUCTW*>(longParam);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            }

            const auto self = reinterpret_cast<MirrorWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            switch (message)
            {
            case WM_ERASEBKGND:
                return 1;
            case WM_GETMINMAXINFO:
                if (self)
                {
                    const auto minMax = reinterpret_cast<MINMAXINFO*>(longParam);
                    const auto sourceSize = self->m_captureItem.Size();
                    minMax->ptMinTrackSize.x = MinimumClientWidth;
                    minMax->ptMinTrackSize.y = std::max(
                        180,
                        MinimumClientWidth * sourceSize.Height / std::max(1, sourceSize.Width));
                }
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(window, message, wordParam, longParam);
            }
        }

        void CreateMirrorWindow(HINSTANCE instance)
        {
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = WindowProc;
            windowClass.hInstance = instance;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            windowClass.lpszClassName = WindowClassName;
            winrt::check_bool(RegisterClassW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);

            const auto sourceSize = m_captureItem.Size();
            const RECT workArea = m_primaryDisplay.info.rcWork;
            const int maxClientWidth = (workArea.right - workArea.left) * 3 / 4;
            int clientWidth = std::min(InitialClientWidth, maxClientWidth);
            int clientHeight = clientWidth * sourceSize.Height / std::max(1, sourceSize.Width);
            const int maxClientHeight = (workArea.bottom - workArea.top) * 3 / 4;
            if (clientHeight > maxClientHeight)
            {
                clientHeight = maxClientHeight;
                clientWidth = clientHeight * sourceSize.Width / std::max(1, sourceSize.Height);
            }

            RECT windowRect{ 0, 0, clientWidth, clientHeight };
            winrt::check_bool(AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0));
            const int windowWidth = windowRect.right - windowRect.left;
            const int windowHeight = windowRect.bottom - windowRect.top;
            const int x = workArea.left + (workArea.right - workArea.left - windowWidth) / 2;
            const int y = workArea.top + (workArea.bottom - workArea.top - windowHeight) / 2;

            std::wstring title = m_title;
            title.append(L" - ");
            title.append(m_sourceDisplay.info.szDevice);

            m_window = CreateWindowExW(
                0,
                WindowClassName,
                title.c_str(),
                WS_OVERLAPPEDWINDOW,
                x,
                y,
                windowWidth,
                windowHeight,
                nullptr,
                nullptr,
                instance,
                this);
            winrt::check_pointer(m_window);
        }

        void CreateGraphicsResources()
        {
            constexpr D3D_FEATURE_LEVEL featureLevels[]{
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
            };

            D3D_FEATURE_LEVEL selectedFeatureLevel{};
            HRESULT result = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                featureLevels,
                static_cast<UINT>(std::size(featureLevels)),
                D3D11_SDK_VERSION,
                m_device.put(),
                &selectedFeatureLevel,
                m_context.put());
            if (FAILED(result))
            {
                winrt::check_hresult(D3D11CreateDevice(
                    nullptr,
                    D3D_DRIVER_TYPE_WARP,
                    nullptr,
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                    featureLevels,
                    static_cast<UINT>(std::size(featureLevels)),
                    D3D11_SDK_VERSION,
                    m_device.put(),
                    &selectedFeatureLevel,
                    m_context.put()));
            }

            if (const auto multithread = m_context.try_as<ID3D11Multithread>())
            {
                multithread->SetMultithreadProtected(TRUE);
            }

            const auto dxgiDevice = m_device.as<IDXGIDevice>();
            winrt::com_ptr<IInspectable> inspectableDevice;
            winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectableDevice.put()));
            m_winrtDevice = inspectableDevice.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

            winrt::com_ptr<IDXGIAdapter> adapter;
            winrt::check_hresult(dxgiDevice->GetAdapter(adapter.put()));
            winrt::com_ptr<IDXGIFactory2> factory;
            winrt::check_hresult(adapter->GetParent(winrt::guid_of<IDXGIFactory2>(), factory.put_void()));

            const auto sourceSize = m_captureItem.Size();
            DXGI_SWAP_CHAIN_DESC1 description{};
            description.Width = sourceSize.Width;
            description.Height = sourceSize.Height;
            description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            description.BufferCount = 2;
            description.Scaling = DXGI_SCALING_STRETCH;
            description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

            winrt::com_ptr<IDXGISwapChain1> swapChain;
            winrt::check_hresult(factory->CreateSwapChainForHwnd(
                m_device.get(),
                m_window,
                &description,
                nullptr,
                nullptr,
                swapChain.put()));
            m_swapChain = swapChain.as<IDXGISwapChain2>();
            factory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER);
        }

        void StartCapture()
        {
            using namespace winrt::Windows::Graphics;
            using namespace winrt::Windows::Graphics::Capture;
            using namespace winrt::Windows::Graphics::DirectX;

            m_framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
                m_winrtDevice,
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                2,
                m_captureItem.Size());
            m_session = m_framePool.CreateCaptureSession(m_captureItem);
            m_session.IsCursorCaptureEnabled(true);
            m_frameArrivedToken = m_framePool.FrameArrived({ this, &MirrorWindow::OnFrameArrived });
            m_closedToken = m_captureItem.Closed([this](auto&&, auto&&) {
                PostMessageW(m_window, WM_CLOSE, 0, 0);
            });
            m_session.StartCapture();
        }

        void StopCapture()
        {
            std::scoped_lock lock{ m_captureMutex };
            if (m_captureItem && m_closedToken.value)
            {
                m_captureItem.Closed(m_closedToken);
                m_closedToken = {};
            }
            if (m_framePool && m_frameArrivedToken.value)
            {
                m_framePool.FrameArrived(m_frameArrivedToken);
                m_frameArrivedToken = {};
            }
            if (m_session)
            {
                m_session.Close();
                m_session = nullptr;
            }
            if (m_framePool)
            {
                m_framePool.Close();
                m_framePool = nullptr;
            }
        }

        void OnFrameArrived(
            const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
            const winrt::Windows::Foundation::IInspectable&)
        {
            std::scoped_lock lock{ m_captureMutex };
            try
            {
                const auto frame = sender.TryGetNextFrame();
                if (!frame)
                {
                    return;
                }

                const auto sourceTexture = GetDXGIInterface<ID3D11Texture2D>(frame.Surface());
                winrt::com_ptr<ID3D11Texture2D> targetTexture;
                winrt::check_hresult(m_swapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), targetTexture.put_void()));

                D3D11_TEXTURE2D_DESC sourceDescription{};
                D3D11_TEXTURE2D_DESC targetDescription{};
                sourceTexture->GetDesc(&sourceDescription);
                targetTexture->GetDesc(&targetDescription);
                D3D11_BOX sourceBox{
                    0,
                    0,
                    0,
                    std::min(sourceDescription.Width, targetDescription.Width),
                    std::min(sourceDescription.Height, targetDescription.Height),
                    1,
                };
                m_context->CopySubresourceRegion(targetTexture.get(), 0, 0, 0, 0, sourceTexture.get(), 0, &sourceBox);
                winrt::check_hresult(m_swapChain->Present(1, 0));
            }
            catch (...)
            {
                PostMessageW(m_window, WM_CLOSE, 0, 0);
            }
        }

        DisplayInfo m_primaryDisplay;
        DisplayInfo m_sourceDisplay;
        std::wstring m_title;
        HWND m_window = nullptr;
        std::mutex m_captureMutex;
        winrt::com_ptr<ID3D11Device> m_device;
        winrt::com_ptr<ID3D11DeviceContext> m_context;
        winrt::com_ptr<IDXGISwapChain2> m_swapChain;
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_winrtDevice{ nullptr };
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_captureItem{ nullptr };
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
        winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
        winrt::event_token m_frameArrivedToken{};
        winrt::event_token m_closedToken{};
    };
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    const auto title = LoadResourceString(instance, IDS_APP_TITLE);
    const auto displays = EnumerateDisplays();
    if (displays.empty())
    {
        return 1;
    }

    try
    {
        const auto primary = std::find_if(displays.begin(), displays.end(), [](const auto& display) {
            return (display.info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        });
        const auto source = SelectDisplay(instance, displays);
        MirrorWindow mirror{ primary != displays.end() ? *primary : displays.front(), source, title };
        mirror.Initialize(instance);
        ShowWindow(mirror.Window(), showCommand);
        UpdateWindow(mirror.Window());

        winrt::handle exitEvent{ CreateEventW(nullptr, FALSE, FALSE, DEPiPConstants::ExitEvent) };
        winrt::check_pointer(exitEvent.get());
        const HANDLE waitHandles[]{ exitEvent.get() };

        bool running = true;
        while (running)
        {
            const DWORD waitResult = MsgWaitForMultipleObjects(1, waitHandles, FALSE, INFINITE, QS_ALLINPUT);
            if (waitResult == WAIT_OBJECT_0)
            {
                DestroyWindow(mirror.Window());
                break;
            }
            if (waitResult != WAIT_OBJECT_0 + 1)
            {
                winrt::throw_last_error();
            }

            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    running = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }
    catch (const winrt::hresult_canceled&)
    {
        return 0;
    }
    catch (const winrt::hresult_error& error)
    {
        std::wstring message = LoadResourceString(instance, IDS_CAPTURE_ERROR);
        message.append(L"\n\n");
        message.append(error.message());
        MessageBoxW(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }

    return 0;
}
