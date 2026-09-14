#include "pch.h"

#include <interface/powertoy_module_interface.h>

#include <common/interop/shared_constants.h>
#include <common/utils/EventWaiter.h>

#include "../ModuleConstants.h"
#include "trace.h"

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        Trace::RegisterProvider();
        break;
    case DLL_PROCESS_DETACH:
        Trace::UnregisterProvider();
        break;
    }

    return TRUE;
}

namespace
{
    constexpr wchar_t MODULE_NAME[] = L"DEPiP";
}

class DEPiPModuleInterface : public PowertoyModuleIface
{
public:
    DEPiPModuleInterface()
    {
        m_exitEvent = CreateDefaultEvent(DEPiPConstants::ExitEvent);
        m_showEvent = CreateDefaultEvent(CommonSharedConstants::SHOW_DEPIP_EVENT);
        m_showEventWaiter.start(CommonSharedConstants::SHOW_DEPIP_EVENT, [this](DWORD error) {
            if (m_enabled && error == ERROR_SUCCESS)
            {
                launch();
            }
        });
    }

    void destroy() override
    {
        disable();
        if (m_exitEvent)
        {
            CloseHandle(m_exitEvent);
        }
        if (m_showEvent)
        {
            CloseHandle(m_showEvent);
        }
        delete this;
    }

    const wchar_t* get_name() override
    {
        return MODULE_NAME;
    }

    const wchar_t* get_key() override
    {
        return MODULE_NAME;
    }

    bool get_config(wchar_t*, int*) override
    {
        return false;
    }

    void set_config(const wchar_t*) override
    {
    }

    void enable() override
    {
        m_enabled = true;
        Trace::Enable(true);
    }

    void disable() override
    {
        if (m_exitEvent)
        {
            SetEvent(m_exitEvent);
        }

        if (m_process)
        {
            WaitForSingleObject(m_process, 3000);
            CloseHandle(m_process);
            m_process = nullptr;
        }

        m_enabled = false;
        Trace::Enable(false);
    }

    bool is_enabled() override
    {
        if (m_process && WaitForSingleObject(m_process, 0) != WAIT_TIMEOUT)
        {
            CloseHandle(m_process);
            m_process = nullptr;
        }
        return m_enabled;
    }

    bool is_enabled_by_default() const override
    {
        return false;
    }

private:
    bool is_process_running()
    {
        return m_process && WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT;
    }

    void bring_process_to_front()
    {
        auto enumWindows = [](HWND window, LPARAM parameter) -> BOOL {
            const HANDLE process = reinterpret_cast<HANDLE>(parameter);
            DWORD windowProcessId = 0;
            GetWindowThreadProcessId(window, &windowProcessId);
            if (GetProcessId(process) == windowProcessId)
            {
                SetForegroundWindow(window);
                return FALSE;
            }
            return TRUE;
        };

        EnumWindows(enumWindows, reinterpret_cast<LPARAM>(m_process));
    }

    void launch()
    {
        if (is_process_running())
        {
            bring_process_to_front();
            return;
        }

        if (m_process)
        {
            CloseHandle(m_process);
            m_process = nullptr;
        }

        if (m_exitEvent)
        {
            ResetEvent(m_exitEvent);
        }

        std::wstring executablePath(MAX_PATH, L'\0');
        const DWORD pathLength = SearchPathW(
            nullptr,
            DEPiPConstants::ExecutableName,
            nullptr,
            static_cast<DWORD>(executablePath.size()),
            executablePath.data(),
            nullptr);
        if (pathLength == 0 || pathLength >= executablePath.size())
        {
            return;
        }
        executablePath.resize(pathLength);

        std::wstring commandLine = L"\"" + executablePath + L"\"";
        STARTUPINFOW startupInfo{ sizeof(startupInfo) };
        PROCESS_INFORMATION processInfo{};
        if (!CreateProcessW(
                executablePath.c_str(),
                commandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                nullptr,
                &startupInfo,
                &processInfo))
        {
            return;
        }

        CloseHandle(processInfo.hThread);
        m_process = processInfo.hProcess;
    }

    bool m_enabled = false;
    HANDLE m_exitEvent = nullptr;
    HANDLE m_showEvent = nullptr;
    HANDLE m_process = nullptr;
    EventWaiter m_showEventWaiter;
};

extern "C" __declspec(dllexport) PowertoyModuleIface* __cdecl powertoy_create()
{
    return new DEPiPModuleInterface();
}
