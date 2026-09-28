#include "Autostart.h"

#include <windows.h>
#include <taskschd.h>
#include <comdef.h>

#include <string>

namespace {

constexpr const wchar_t *kTaskName = L"ArduSysMonitor";

std::wstring exePath() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::wstring(buf) : std::wstring();
}

std::wstring hrText(const wchar_t *what, HRESULT hr) {
    wchar_t buf[160]{};
    swprintf(buf, 160, L"%ls failed (0x%08lX)", what, static_cast<unsigned long>(hr));
    return buf;
}

// Small RAII so every early return does not need a matching Release.
template <typename T>
class ComPtr {
public:
    ~ComPtr() { if (p) p->Release(); }
    T **operator&() { return &p; }
    T *operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    T *get() const { return p; }

private:
    T *p = nullptr;
};

// COM may already be initialised on this thread by the shell; treat
// RPC_E_CHANGED_MODE as success and simply do not uninitialise in that case.
class ComScope {
public:
    ComScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_owned = SUCCEEDED(hr);
        m_ok = m_owned || hr == RPC_E_CHANGED_MODE;
    }
    ~ComScope() { if (m_owned) CoUninitialize(); }
    bool ok() const { return m_ok; }

private:
    bool m_owned = false;
    bool m_ok = false;
};

bool connectRoot(ComScope &com, ComPtr<ITaskService> &service,
                 ComPtr<ITaskFolder> &root, std::wstring &errorOut) {
    if (!com.ok()) {
        errorOut = L"COM could not be initialised";
        return false;
    }

    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITaskService, reinterpret_cast<void **>(&service));
    if (FAILED(hr)) {
        errorOut = hrText(L"Connecting to Task Scheduler", hr);
        return false;
    }

    VARIANT empty{};
    empty.vt = VT_EMPTY;
    hr = service->Connect(empty, empty, empty, empty);
    if (FAILED(hr)) {
        errorOut = hrText(L"Task Scheduler Connect", hr);
        return false;
    }

    _bstr_t rootPath(L"\\");
    hr = service->GetFolder(rootPath, &root);
    if (FAILED(hr)) {
        errorOut = hrText(L"Opening the task folder", hr);
        return false;
    }
    return true;
}

}  // namespace

namespace autostart {

bool isEnabled() {
    ComScope com;
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> root;
    std::wstring ignored;
    if (!connectRoot(com, service, root, ignored)) return false;

    ComPtr<IRegisteredTask> task;
    if (FAILED(root->GetTask(_bstr_t(kTaskName), &task)) || !task) return false;

    VARIANT_BOOL enabled = VARIANT_FALSE;
    if (FAILED(task->get_Enabled(&enabled))) return false;
    return enabled != VARIANT_FALSE;
}

bool enable(std::wstring &errorOut) {
    const std::wstring exe = exePath();
    if (exe.empty()) {
        errorOut = L"Could not determine this executable's path";
        return false;
    }

    ComScope com;
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> root;
    if (!connectRoot(com, service, root, errorOut)) return false;

    ComPtr<ITaskDefinition> task;
    HRESULT hr = service->NewTask(0, &task);
    if (FAILED(hr)) {
        errorOut = hrText(L"Creating the task definition", hr);
        return false;
    }

    // --- registration info ---
    {
        ComPtr<IRegistrationInfo> reg;
        if (SUCCEEDED(task->get_RegistrationInfo(&reg)) && reg) {
            reg->put_Author(_bstr_t(L"ARDU-SYS-MONITOR"));
            reg->put_Description(
                _bstr_t(L"Starts the Multi Screen System Monitor agent, elevated, "
                        L"at logon."));
        }
    }

    // --- run elevated, as the interactive user ---
    {
        ComPtr<IPrincipal> principal;
        if (SUCCEEDED(task->get_Principal(&principal)) && principal) {
            principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
            // The whole point: without HIGHEST the task starts unelevated and the
            // app exits on its own admin check.
            principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
        }
    }

    // --- settings suited to a tray agent ---
    {
        ComPtr<ITaskSettings> settings;
        if (SUCCEEDED(task->get_Settings(&settings)) && settings) {
            settings->put_StartWhenAvailable(VARIANT_TRUE);
            settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
            settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
            settings->put_AllowDemandStart(VARIANT_TRUE);
            settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
            // A monitor is meant to run for as long as the session does.
            settings->put_ExecutionTimeLimit(_bstr_t(L"PT0S"));
            ComPtr<IIdleSettings> idle;
            if (SUCCEEDED(settings->get_IdleSettings(&idle)) && idle) {
                idle->put_StopOnIdleEnd(VARIANT_FALSE);
            }
        }
    }

    // --- logon trigger ---
    {
        ComPtr<ITriggerCollection> triggers;
        if (FAILED(task->get_Triggers(&triggers)) || !triggers) {
            errorOut = L"Could not access the trigger collection";
            return false;
        }
        ComPtr<ITrigger> trigger;
        hr = triggers->Create(TASK_TRIGGER_LOGON, &trigger);
        if (FAILED(hr)) {
            errorOut = hrText(L"Creating the logon trigger", hr);
            return false;
        }
        // A few seconds of delay keeps it out of the logon stampede, so the port
        // enumeration does not race the USB stack coming up.
        ILogonTrigger *logon = nullptr;
        if (SUCCEEDED(trigger->QueryInterface(IID_ILogonTrigger,
                                              reinterpret_cast<void **>(&logon))) &&
            logon) {
            logon->put_Delay(_bstr_t(L"PT10S"));
            logon->Release();
        }
    }

    // --- the action ---
    {
        ComPtr<IActionCollection> actions;
        if (FAILED(task->get_Actions(&actions)) || !actions) {
            errorOut = L"Could not access the action collection";
            return false;
        }
        ComPtr<IAction> action;
        hr = actions->Create(TASK_ACTION_EXEC, &action);
        if (FAILED(hr)) {
            errorOut = hrText(L"Creating the task action", hr);
            return false;
        }
        IExecAction *exec = nullptr;
        if (SUCCEEDED(action->QueryInterface(IID_IExecAction,
                                             reinterpret_cast<void **>(&exec))) &&
            exec) {
            exec->put_Path(_bstr_t(exe.c_str()));
            exec->Release();
        }
    }

    // TASK_CREATE_OR_UPDATE so toggling autostart after moving the .exe fixes the
    // stored path instead of leaving a task pointing at nothing.
    VARIANT empty{};
    empty.vt = VT_EMPTY;
    ComPtr<IRegisteredTask> registered;
    hr = root->RegisterTaskDefinition(_bstr_t(kTaskName), task.get(),
                                      TASK_CREATE_OR_UPDATE, empty, empty,
                                      TASK_LOGON_INTERACTIVE_TOKEN, empty, &registered);
    if (FAILED(hr)) {
        errorOut = hrText(L"Registering the task", hr);
        return false;
    }
    return true;
}

bool disable(std::wstring &errorOut) {
    ComScope com;
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> root;
    if (!connectRoot(com, service, root, errorOut)) return false;

    const HRESULT hr = root->DeleteTask(_bstr_t(kTaskName), 0);
    // Already gone is the desired end state, not a failure.
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        errorOut = hrText(L"Deleting the task", hr);
        return false;
    }
    return true;
}

}  // namespace autostart
