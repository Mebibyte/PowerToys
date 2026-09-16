#include "pch.h"

#include "../ModuleConstants.h"
#include "resource.h"

#include <common/SettingsAPI/settings_helpers.h>

namespace
{
    constexpr wchar_t WindowClassName[] = L"PowerToys.DEPiP.Window";
    constexpr wchar_t SelectorWindowClassName[] = L"PowerToys.DEPiP.DisplaySelector";
    constexpr int InitialClientWidth = 960;
    constexpr int MinimumClientWidth = 320;
    constexpr int SelectorMargin = 24;
    constexpr int SelectorHeaderHeight = 88;
    constexpr int SelectorFooterHeight = 64;
    constexpr int SelectorCardGap = 16;
    constexpr int SelectorCardWidth = 280;
    constexpr int SelectorCardHeight = 210;
    constexpr int SelectorMinimumCardWidth = 160;
    constexpr int PreviewBitmapWidth = 320;
    constexpr int PreviewBitmapHeight = 180;
    constexpr int DisplayButtonIdBase = 1000;
    constexpr UINT_PTR OpacityTimerId = 1;
    constexpr UINT OpacityRefreshMilliseconds = 150;
    constexpr int DefaultInactiveTransparency = 14;
    constexpr int MaximumInactiveTransparency = 90;
    constexpr bool DefaultLockAspectRatio = false;
    constexpr bool DefaultAlwaysOnTop = false;

    int LoadInactiveTransparency()
    {
        try
        {
            const auto properties =
                PTSettingsHelper::load_module_settings(L"DEPiP").GetNamedObject(L"properties");
            const int transparency = static_cast<int>(
                properties.GetNamedObject(L"inactiveTransparency").GetNamedNumber(L"value"));
            return std::clamp(transparency, 0, MaximumInactiveTransparency);
        }
        catch (const winrt::hresult_error& error)
        {
            OutputDebugStringW(error.message().c_str());
            return DefaultInactiveTransparency;
        }
        catch (const std::exception& error)
        {
            OutputDebugStringA(error.what());
            return DefaultInactiveTransparency;
        }
    }

    bool LoadLockAspectRatio()
    {
        try
        {
            const auto properties =
                PTSettingsHelper::load_module_settings(L"DEPiP").GetNamedObject(L"properties");
            return properties.GetNamedObject(L"lockAspectRatio").GetNamedBoolean(L"value");
        }

        catch (const winrt::hresult_error& error)
        {
            OutputDebugStringW(error.message().c_str());
            return DefaultLockAspectRatio;
        }
        catch (const std::exception& error)
        {
            OutputDebugStringA(error.what());
            return DefaultLockAspectRatio;
        }
    }

    bool LoadAlwaysOnTop()
    {
        try
        {
            const auto properties =
                PTSettingsHelper::load_module_settings(L"DEPiP").GetNamedObject(L"properties");
            return properties.GetNamedObject(L"alwaysOnTop").GetNamedBoolean(L"value");
        }
        catch (const winrt::hresult_error& error)
        {
            OutputDebugStringW(error.message().c_str());
            return DefaultAlwaysOnTop;
        }
        catch (const std::exception& error)
        {
            OutputDebugStringA(error.what());
            return DefaultAlwaysOnTop;
        }
    }

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

    struct PreviewBitmap
    {
        PreviewBitmap() = default;

        explicit PreviewBitmap(HBITMAP bitmap) :
            value{ bitmap }
        {
        }

        PreviewBitmap(const PreviewBitmap&) = delete;
        PreviewBitmap& operator=(const PreviewBitmap&) = delete;

        PreviewBitmap(PreviewBitmap&& other) noexcept :
            value{ std::exchange(other.value, nullptr) }
        {
        }

        PreviewBitmap& operator=(PreviewBitmap&& other) noexcept
        {
            if (this != &other)
            {
                if (value)
                {
                    DeleteObject(value);
                }
                value = std::exchange(other.value, nullptr);
            }
            return *this;
        }

        ~PreviewBitmap()
        {
            if (value)
            {
                DeleteObject(value);
            }
        }

        HBITMAP value = nullptr;
    };

    PreviewBitmap CaptureDisplayPreview(const DisplayInfo& display)
    {
        const HDC screenDc = GetDC(nullptr);
        winrt::check_pointer(screenDc);
        const HDC previewDc = CreateCompatibleDC(screenDc);
        if (!previewDc)
        {
            ReleaseDC(nullptr, screenDc);
            winrt::throw_last_error();
        }

        const HBITMAP bitmap = CreateCompatibleBitmap(screenDc, PreviewBitmapWidth, PreviewBitmapHeight);
        if (!bitmap)
        {
            DeleteDC(previewDc);
            ReleaseDC(nullptr, screenDc);
            winrt::throw_last_error();
        }

        const auto previousBitmap = SelectObject(previewDc, bitmap);
        RECT previewBounds{ 0, 0, PreviewBitmapWidth, PreviewBitmapHeight };
        FillRect(previewDc, &previewBounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        const RECT sourceBounds = display.info.rcMonitor;
        const int sourceWidth = sourceBounds.right - sourceBounds.left;
        const int sourceHeight = sourceBounds.bottom - sourceBounds.top;
        int targetWidth = PreviewBitmapWidth;
        int targetHeight = MulDiv(sourceHeight, targetWidth, sourceWidth);
        if (targetHeight > PreviewBitmapHeight)
        {
            targetHeight = PreviewBitmapHeight;
            targetWidth = MulDiv(sourceWidth, targetHeight, sourceHeight);
        }
        const int targetX = (PreviewBitmapWidth - targetWidth) / 2;
        const int targetY = (PreviewBitmapHeight - targetHeight) / 2;

        SetStretchBltMode(previewDc, HALFTONE);
        SetBrushOrgEx(previewDc, 0, 0, nullptr);
        StretchBlt(
            previewDc,
            targetX,
            targetY,
            targetWidth,
            targetHeight,
            screenDc,
            sourceBounds.left,
            sourceBounds.top,
            sourceWidth,
            sourceHeight,
            SRCCOPY | CAPTUREBLT);

        SelectObject(previewDc, previousBitmap);
        DeleteDC(previewDc);
        ReleaseDC(nullptr, screenDc);
        return PreviewBitmap{ bitmap };
    }

    class DisplaySelector
    {
    public:
        DisplaySelector(HINSTANCE instance, const std::vector<DisplayInfo>& displays) :
            m_instance{ instance },
            m_instruction{ LoadResourceString(instance, IDS_DISPLAY_SELECTOR_INSTRUCTION) },
            m_description{ LoadResourceString(instance, IDS_DISPLAY_SELECTOR_DESCRIPTION) },
            m_cancel{ LoadResourceString(instance, IDS_CANCEL) }
        {
            const auto displayLabel = LoadResourceString(instance, IDS_DISPLAY_LABEL);
            const auto primaryLabel = LoadResourceString(instance, IDS_PRIMARY_DISPLAY);
            m_cards.reserve(displays.size());
            for (size_t index = 0; index < displays.size(); ++index)
            {
                const auto& display = displays[index];
                const RECT bounds = display.info.rcMonitor;
                std::wstring label = displayLabel + L" " + std::to_wstring(index + 1);
                if ((display.info.dwFlags & MONITORINFOF_PRIMARY) != 0)
                {
                    label.append(L" (").append(primaryLabel).append(L")");
                }
                label.append(L"\n")
                    .append(std::to_wstring(bounds.right - bounds.left))
                    .append(L" x ")
                    .append(std::to_wstring(bounds.bottom - bounds.top));
                m_cards.push_back({ display, CaptureDisplayPreview(display), nullptr, std::move(label), false });
            }
        }

        DisplayInfo Run()
        {
            CreateSelectorWindow();
            ShowWindow(m_window, SW_SHOW);
            UpdateWindow(m_window);
            BringWindowToTop(m_window);
            SetForegroundWindow(m_window);
            SetActiveWindow(m_window);
            SetFocus(m_cards.front().button);

            MSG message{};
            while (true)
            {
                const int result = GetMessageW(&message, nullptr, 0, 0);
                if (result == -1)
                {
                    winrt::throw_last_error();
                }
                if (result == 0)
                {
                    break;
                }
                if (!IsDialogMessageW(m_window, &message))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }

            if (!m_selection)
            {
                throw winrt::hresult_canceled();
            }
            return m_cards.at(*m_selection).display;
        }

    private:
        struct DisplayCard
        {
            DisplayInfo display;
            PreviewBitmap preview;
            HWND button;
            std::wstring label;
            bool hovered = false;
        };

        static LRESULT CALLBACK CardButtonProc(
            HWND button,
            UINT message,
            WPARAM wordParam,
            LPARAM longParam,
            UINT_PTR subclassId,
            DWORD_PTR referenceData)
        {
            const auto self = reinterpret_cast<DisplaySelector*>(referenceData);
            const size_t index = static_cast<size_t>(subclassId);
            switch (message)
            {
            case WM_SETCURSOR:
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            case WM_MOUSEMOVE:
                if (!self->m_cards[index].hovered)
                {
                    self->m_cards[index].hovered = true;
                    TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, button, 0 };
                    TrackMouseEvent(&tracking);
                    InvalidateRect(button, nullptr, TRUE);
                }
                break;
            case WM_MOUSELEAVE:
                self->m_cards[index].hovered = false;
                InvalidateRect(button, nullptr, TRUE);
                break;
            case WM_NCDESTROY:
                RemoveWindowSubclass(button, CardButtonProc, subclassId);
                break;
            }
            return DefSubclassProc(button, message, wordParam, longParam);
        }

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wordParam, LPARAM longParam)
        {
            if (message == WM_NCCREATE)
            {
                const auto create = reinterpret_cast<CREATESTRUCTW*>(longParam);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            }

            const auto self = reinterpret_cast<DisplaySelector*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (!self)
            {
                return DefWindowProcW(window, message, wordParam, longParam);
            }

            switch (message)
            {
            case WM_COMMAND:
            {
                const int controlId = LOWORD(wordParam);
                if (controlId == IDCANCEL)
                {
                    DestroyWindow(window);
                    return 0;
                }
                if (controlId >= DisplayButtonIdBase &&
                    controlId < DisplayButtonIdBase + static_cast<int>(self->m_cards.size()) &&
                    HIWORD(wordParam) == BN_CLICKED)
                {
                    self->m_selection =
                        static_cast<size_t>(controlId) - static_cast<size_t>(DisplayButtonIdBase);
                    DestroyWindow(window);
                    return 0;
                }
                break;
            }
            case WM_DRAWITEM:
                self->DrawDisplayCard(*reinterpret_cast<DRAWITEMSTRUCT*>(longParam));
                return TRUE;
            case WM_PAINT:
                self->PaintWindow();
                return 0;
            case WM_CLOSE:
                DestroyWindow(window);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcW(window, message, wordParam, longParam);
        }

        void CreateSelectorWindow()
        {
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = WindowProc;
            windowClass.hInstance = m_instance;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
            windowClass.lpszClassName = SelectorWindowClassName;
            winrt::check_bool(RegisterClassW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);

            const auto primary = std::find_if(m_cards.begin(), m_cards.end(), [](const auto& card) {
                return (card.display.info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            });
            const RECT workArea = primary != m_cards.end() ? primary->display.info.rcWork : m_cards.front().display.info.rcWork;
            const int maximumClientWidth = (workArea.right - workArea.left) * 9 / 10;
            const int maximumColumns = std::max(
                1,
                (maximumClientWidth - (2 * SelectorMargin) + SelectorCardGap) /
                    (SelectorMinimumCardWidth + SelectorCardGap));
            m_columnCount = std::min(static_cast<int>(m_cards.size()), maximumColumns);
            m_rowCount = (static_cast<int>(m_cards.size()) + m_columnCount - 1) / m_columnCount;
            m_cardWidth = std::min(
                SelectorCardWidth,
                (maximumClientWidth - (2 * SelectorMargin) -
                 ((m_columnCount - 1) * SelectorCardGap)) /
                    m_columnCount);

            const int clientWidth = (2 * SelectorMargin) + (m_columnCount * m_cardWidth) +
                                    ((m_columnCount - 1) * SelectorCardGap);
            const int clientHeight = SelectorHeaderHeight + (m_rowCount * SelectorCardHeight) +
                                     ((m_rowCount - 1) * SelectorCardGap) + SelectorFooterHeight;
            RECT windowRect{ 0, 0, clientWidth, clientHeight };
            winrt::check_bool(AdjustWindowRectEx(&windowRect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0));
            const int windowWidth = windowRect.right - windowRect.left;
            const int windowHeight = windowRect.bottom - windowRect.top;
            const int x = workArea.left + (workArea.right - workArea.left - windowWidth) / 2;
            const int y = workArea.top + (workArea.bottom - workArea.top - windowHeight) / 2;

            const auto title = LoadResourceString(m_instance, IDS_DISPLAY_SELECTOR_TITLE);
            m_window = CreateWindowExW(
                WS_EX_DLGMODALFRAME,
                SelectorWindowClassName,
                title.c_str(),
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                x,
                y,
                windowWidth,
                windowHeight,
                nullptr,
                nullptr,
                m_instance,
                this);
            winrt::check_pointer(m_window);

            const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            for (size_t index = 0; index < m_cards.size(); ++index)
            {
                const int column = static_cast<int>(index) % m_columnCount;
                const int row = static_cast<int>(index) / m_columnCount;
                const int cardX = SelectorMargin + column * (m_cardWidth + SelectorCardGap);
                const int cardY = SelectorHeaderHeight + row * (SelectorCardHeight + SelectorCardGap);
                const DWORD groupStyle = index == 0 ? WS_GROUP : 0;
                auto& card = m_cards[index];
                card.button = CreateWindowExW(
                    0,
                    L"BUTTON",
                    card.label.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_OWNERDRAW | groupStyle,
                    cardX,
                    cardY,
                    m_cardWidth,
                    SelectorCardHeight,
                    m_window,
                    reinterpret_cast<HMENU>(DisplayButtonIdBase + index),
                    m_instance,
                    nullptr);
                winrt::check_pointer(card.button);
                SendMessageW(card.button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                winrt::check_bool(SetWindowSubclass(
                    card.button,
                    CardButtonProc,
                    static_cast<UINT_PTR>(index),
                    reinterpret_cast<DWORD_PTR>(this)));
            }

            const int cancelWidth = 96;
            const int cancelHeight = 32;
            const HWND cancelButton = CreateWindowExW(
                0,
                L"BUTTON",
                m_cancel.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                clientWidth - SelectorMargin - cancelWidth,
                clientHeight - SelectorMargin - cancelHeight,
                cancelWidth,
                cancelHeight,
                m_window,
                reinterpret_cast<HMENU>(IDCANCEL),
                m_instance,
                nullptr);
            winrt::check_pointer(cancelButton);
            SendMessageW(cancelButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }

        void PaintWindow()
        {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(m_window, &paint);
            RECT clientRect{};
            GetClientRect(m_window, &clientRect);
            FillRect(dc, &clientRect, GetSysColorBrush(COLOR_WINDOW));

            RECT instructionRect{ SelectorMargin, 16, clientRect.right - SelectorMargin, 44 };
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const auto previousFont = SelectObject(dc, font);
            DrawTextW(dc, m_instruction.c_str(), -1, &instructionRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            RECT descriptionRect{ SelectorMargin, 44, clientRect.right - SelectorMargin, SelectorHeaderHeight };
            SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
            DrawTextW(dc, m_description.c_str(), -1, &descriptionRect, DT_LEFT | DT_WORDBREAK);
            SelectObject(dc, previousFont);
            EndPaint(m_window, &paint);
        }

        void DrawDisplayCard(const DRAWITEMSTRUCT& drawItem)
        {
            const size_t index = static_cast<size_t>(drawItem.CtlID - DisplayButtonIdBase);
            if (index >= m_cards.size())
            {
                return;
            }

            const bool focused = (drawItem.itemState & ODS_FOCUS) != 0;
            const bool pressed = (drawItem.itemState & ODS_SELECTED) != 0;
            const bool hovered = m_cards[index].hovered;

            FillRect(drawItem.hDC, &drawItem.rcItem, GetSysColorBrush(COLOR_WINDOW));

            RECT faceRect = drawItem.rcItem;
            faceRect.right -= 3;
            faceRect.bottom -= 3;
            RECT shadowRect = faceRect;
            OffsetRect(&shadowRect, 3, 3);
            const HRGN shadowRegion = CreateRoundRectRgn(
                shadowRect.left,
                shadowRect.top,
                shadowRect.right + 1,
                shadowRect.bottom + 1,
                16,
                16);
            const HBRUSH shadowBrush = GetSysColorBrush(pressed ? COLOR_3DHILIGHT : COLOR_3DSHADOW);
            FillRgn(drawItem.hDC, shadowRegion, shadowBrush);
            DeleteObject(shadowRegion);

            if (pressed)
            {
                OffsetRect(&faceRect, 2, 2);
            }

            const HRGN faceRegion = CreateRoundRectRgn(
                faceRect.left,
                faceRect.top,
                faceRect.right + 1,
                faceRect.bottom + 1,
                16,
                16);
            FillRgn(drawItem.hDC, faceRegion, GetSysColorBrush(hovered ? COLOR_3DLIGHT : COLOR_3DFACE));
            const HBRUSH borderBrush = GetSysColorBrush(
                focused || hovered ? COLOR_HIGHLIGHT : (pressed ? COLOR_3DSHADOW : COLOR_3DHILIGHT));
            FrameRgn(drawItem.hDC, faceRegion, borderBrush, focused || hovered ? 2 : 1, focused || hovered ? 2 : 1);
            DeleteObject(faceRegion);

            constexpr int cardPadding = 10;
            constexpr int labelHeight = 42;
            RECT imageRect{
                faceRect.left + cardPadding,
                faceRect.top + cardPadding,
                faceRect.right - cardPadding,
                faceRect.bottom - labelHeight - cardPadding,
            };
            FillRect(drawItem.hDC, &imageRect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

            const HDC previewDc = CreateCompatibleDC(drawItem.hDC);
            if (previewDc)
            {
                const auto previousBitmap = SelectObject(previewDc, m_cards[index].preview.value);
                SetStretchBltMode(drawItem.hDC, HALFTONE);
                SetBrushOrgEx(drawItem.hDC, 0, 0, nullptr);
                StretchBlt(
                    drawItem.hDC,
                    imageRect.left,
                    imageRect.top,
                    imageRect.right - imageRect.left,
                    imageRect.bottom - imageRect.top,
                    previewDc,
                    0,
                    0,
                    PreviewBitmapWidth,
                    PreviewBitmapHeight,
                    SRCCOPY);
                SelectObject(previewDc, previousBitmap);
                DeleteDC(previewDc);
            }

            RECT labelRect{
                faceRect.left + cardPadding,
                faceRect.bottom - labelHeight,
                faceRect.right - cardPadding,
                faceRect.bottom - 4,
            };
            SetBkMode(drawItem.hDC, TRANSPARENT);
            SetTextColor(drawItem.hDC, GetSysColor(COLOR_WINDOWTEXT));
            DrawTextW(
                drawItem.hDC,
                m_cards[index].label.c_str(),
                -1,
                &labelRect,
                DT_CENTER | DT_WORDBREAK | DT_VCENTER);
        }

        HINSTANCE m_instance;
        HWND m_window = nullptr;
        std::wstring m_instruction;
        std::wstring m_description;
        std::wstring m_cancel;
        std::vector<DisplayCard> m_cards;
        std::optional<size_t> m_selection;
        int m_columnCount = 1;
        int m_rowCount = 1;
        int m_cardWidth = SelectorCardWidth;
    };

    DisplayInfo SelectDisplay(HINSTANCE instance, const std::vector<DisplayInfo>& displays)
    {
        return DisplaySelector{ instance, displays }.Run();
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
        MirrorWindow(
            DisplayInfo primaryDisplay,
            DisplayInfo sourceDisplay,
            std::wstring title,
            int inactiveTransparency,
            bool lockAspectRatio,
            bool alwaysOnTop) :
            m_primaryDisplay{ primaryDisplay },
            m_sourceDisplay{ sourceDisplay },
            m_title{ std::move(title) },
            m_inactiveTransparency{ inactiveTransparency },
            m_lockAspectRatio{ lockAspectRatio },
            m_alwaysOnTop{ alwaysOnTop }
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

        void ReloadSettings()
        {
            m_inactiveTransparency = LoadInactiveTransparency();
            m_lockAspectRatio = LoadLockAspectRatio();
            m_alwaysOnTop = LoadAlwaysOnTop();
            UpdateOpacity();
            ApplyAlwaysOnTop();
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
            case WM_SIZING:
                if (self && self->m_lockAspectRatio)
                {
                    self->ApplyAspectRatio(wordParam, *reinterpret_cast<RECT*>(longParam));
                    return TRUE;
                }
                break;
            case WM_TIMER:
                if (self && wordParam == OpacityTimerId)
                {
                    self->UpdateOpacity();
                    return 0;
                }
                break;
            case WM_DESTROY:
                KillTimer(window, OpacityTimerId);
                PostQuitMessage(0);
                return 0;
            default:
                break;
            }
            return DefWindowProcW(window, message, wordParam, longParam);
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
                WS_EX_LAYERED,
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

            SetLayeredWindowAttributes(m_window, 0, 255, LWA_ALPHA);
            SetTimer(m_window, OpacityTimerId, OpacityRefreshMilliseconds, nullptr);
            UpdateOpacity();
            ApplyAlwaysOnTop();
        }

        void ApplyAlwaysOnTop()
        {
            SetWindowPos(
                m_window,
                m_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }

        void ApplyAspectRatio(WPARAM sizingEdge, RECT& proposedWindowRect)
        {
            RECT currentClientRect{};
            RECT currentWindowRect{};
            if (!GetClientRect(m_window, &currentClientRect) ||
                !GetWindowRect(m_window, &currentWindowRect))
            {
                return;
            }

            const int sourceWidth = m_sourceDisplay.info.rcMonitor.right - m_sourceDisplay.info.rcMonitor.left;
            const int sourceHeight = m_sourceDisplay.info.rcMonitor.bottom - m_sourceDisplay.info.rcMonitor.top;
            if (sourceWidth <= 0 || sourceHeight <= 0)
            {
                return;
            }

            const int nonClientWidth =
                (currentWindowRect.right - currentWindowRect.left) -
                (currentClientRect.right - currentClientRect.left);
            const int nonClientHeight =
                (currentWindowRect.bottom - currentWindowRect.top) -
                (currentClientRect.bottom - currentClientRect.top);
            const int proposedClientWidth = std::max(
                1,
                static_cast<int>(proposedWindowRect.right - proposedWindowRect.left) - nonClientWidth);
            const int proposedClientHeight = std::max(
                1,
                static_cast<int>(proposedWindowRect.bottom - proposedWindowRect.top) - nonClientHeight);

            int clientWidth = proposedClientWidth;
            int clientHeight = proposedClientHeight;
            if (sizingEdge == WMSZ_LEFT || sizingEdge == WMSZ_RIGHT)
            {
                clientHeight = MulDiv(clientWidth, sourceHeight, sourceWidth);
            }
            else if (sizingEdge == WMSZ_TOP || sizingEdge == WMSZ_BOTTOM)
            {
                clientWidth = MulDiv(clientHeight, sourceWidth, sourceHeight);
            }
            else
            {
                const int widthDrivenHeight = MulDiv(clientWidth, sourceHeight, sourceWidth);
                const int heightDrivenWidth = MulDiv(clientHeight, sourceWidth, sourceHeight);
                if (std::abs(widthDrivenHeight - clientHeight) <=
                    std::abs(heightDrivenWidth - clientWidth))
                {
                    clientHeight = widthDrivenHeight;
                }
                else
                {
                    clientWidth = heightDrivenWidth;
                }
            }

            const int windowWidth = clientWidth + nonClientWidth;
            const int windowHeight = clientHeight + nonClientHeight;
            const LONG horizontalCenter =
                proposedWindowRect.left + (proposedWindowRect.right - proposedWindowRect.left) / 2;
            const LONG verticalCenter =
                proposedWindowRect.top + (proposedWindowRect.bottom - proposedWindowRect.top) / 2;

            switch (sizingEdge)
            {
            case WMSZ_LEFT:
                proposedWindowRect.left = proposedWindowRect.right - windowWidth;
                proposedWindowRect.top = verticalCenter - windowHeight / 2;
                proposedWindowRect.bottom = proposedWindowRect.top + windowHeight;
                break;
            case WMSZ_RIGHT:
                proposedWindowRect.right = proposedWindowRect.left + windowWidth;
                proposedWindowRect.top = verticalCenter - windowHeight / 2;
                proposedWindowRect.bottom = proposedWindowRect.top + windowHeight;
                break;
            case WMSZ_TOP:
                proposedWindowRect.top = proposedWindowRect.bottom - windowHeight;
                proposedWindowRect.left = horizontalCenter - windowWidth / 2;
                proposedWindowRect.right = proposedWindowRect.left + windowWidth;
                break;
            case WMSZ_BOTTOM:
                proposedWindowRect.bottom = proposedWindowRect.top + windowHeight;
                proposedWindowRect.left = horizontalCenter - windowWidth / 2;
                proposedWindowRect.right = proposedWindowRect.left + windowWidth;
                break;
            case WMSZ_TOPLEFT:
                proposedWindowRect.left = proposedWindowRect.right - windowWidth;
                proposedWindowRect.top = proposedWindowRect.bottom - windowHeight;
                break;
            case WMSZ_TOPRIGHT:
                proposedWindowRect.right = proposedWindowRect.left + windowWidth;
                proposedWindowRect.top = proposedWindowRect.bottom - windowHeight;
                break;
            case WMSZ_BOTTOMLEFT:
                proposedWindowRect.left = proposedWindowRect.right - windowWidth;
                proposedWindowRect.bottom = proposedWindowRect.top + windowHeight;
                break;
            case WMSZ_BOTTOMRIGHT:
                proposedWindowRect.right = proposedWindowRect.left + windowWidth;
                proposedWindowRect.bottom = proposedWindowRect.top + windowHeight;
                break;
            }
        }

        void UpdateOpacity()
        {
            POINT cursor{};
            if (!GetCursorPos(&cursor))
            {
                return;
            }

            const LONG_PTR extendedStyle = GetWindowLongPtrW(m_window, GWL_EXSTYLE);
            const bool opacityStyleMissing = (extendedStyle & WS_EX_LAYERED) == 0;
            if (opacityStyleMissing)
            {
                SetWindowLongPtrW(m_window, GWL_EXSTYLE, extendedStyle | WS_EX_LAYERED);
            }

            const HMONITOR cursorMonitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
            const BYTE inactiveOpacity = static_cast<BYTE>(
                MulDiv(255, 100 - m_inactiveTransparency, 100));
            const BYTE opacity = cursorMonitor == m_sourceDisplay.monitor ? 255 : inactiveOpacity;
            if (opacityStyleMissing || opacity != m_opacity)
            {
                SetLayeredWindowAttributes(m_window, 0, opacity, LWA_ALPHA);
                m_opacity = opacity;
            }
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
        BYTE m_opacity = 255;
        int m_inactiveTransparency = DefaultInactiveTransparency;
        bool m_lockAspectRatio = DefaultLockAspectRatio;
        bool m_alwaysOnTop = DefaultAlwaysOnTop;
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
        MirrorWindow mirror{
            primary != displays.end() ? *primary : displays.front(),
            source,
            title,
            LoadInactiveTransparency(),
            LoadLockAspectRatio(),
            LoadAlwaysOnTop()
        };
        mirror.Initialize(instance);
        ShowWindow(mirror.Window(), showCommand);
        UpdateWindow(mirror.Window());

        winrt::handle exitEvent{ CreateEventW(nullptr, FALSE, FALSE, DEPiPConstants::ExitEvent) };
        winrt::check_pointer(exitEvent.get());
        winrt::handle reloadSettingsEvent{
            CreateEventW(nullptr, FALSE, FALSE, DEPiPConstants::ReloadSettingsEvent)
        };
        winrt::check_pointer(reloadSettingsEvent.get());
        const HANDLE waitHandles[]{ exitEvent.get(), reloadSettingsEvent.get() };

        bool running = true;
        while (running)
        {
            const DWORD waitResult = MsgWaitForMultipleObjects(
                static_cast<DWORD>(std::size(waitHandles)),
                waitHandles,
                FALSE,
                INFINITE,
                QS_ALLINPUT);
            if (waitResult == WAIT_OBJECT_0)
            {
                DestroyWindow(mirror.Window());
                break;
            }
            if (waitResult == WAIT_OBJECT_0 + 1)
            {
                mirror.ReloadSettings();
                continue;
            }
            if (waitResult != WAIT_OBJECT_0 + static_cast<DWORD>(std::size(waitHandles)))
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
