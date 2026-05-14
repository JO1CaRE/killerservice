// service_template.c
// Windows Service skeleton for controlled lab usage
// Циклическое завершение целевого процесса через TerminateProcess

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <wchar.h>

#define SERVICE_NAME          L"ProcessWatcherSvc"
#define SERVICE_DISPLAY_NAME  L"Process Watcher Service"
#define TARGET_PROCESS_NAME   L"EDR_service.exe"
#define LOG_FILE_PATH         L"C:\\Windows\\Temp\\process_watcher.log"

static SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
static SERVICE_STATUS        g_ServiceStatus = {0};
static HANDLE                g_hStopEvent = NULL;
static HANDLE                g_hThread  = NULL;

static void WriteLog(const char* text)
{
    HANDLE hFile = CreateFileW(
        LOG_FILE_PATH,
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    char buffer[1024];
    int len = snprintf(
        buffer, sizeof(buffer),
        "[%04u-%02u-%02u %02u:%02u:%02u] %s\r\n",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond,
        text
    );

    if (len > 0) {
        DWORD written = 0;
        WriteFile(hFile, buffer, (DWORD)len, &written, NULL);
    }
    CloseHandle(hFile);
}

// ---------------------------------------------------------
// Логика работы с процессами (без изменений)
// ---------------------------------------------------------
static void TryHandleTargetProcess(DWORD pid, const wchar_t* name)
{
    char msg[512];
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess == NULL) {
        snprintf(msg, sizeof(msg), "OpenProcess failed for PID=%lu, Error=%lu", pid, GetLastError());
        WriteLog(msg);
        return;
    }

    if (TerminateProcess(hProcess, 1)) {
        snprintf(msg, sizeof(msg), "Successfully terminated PID=%lu (%S)", pid, name);
    } else {
        snprintf(msg, sizeof(msg), "TerminateProcess failed for PID=%lu, Error=%lu", pid, GetLastError());
    }
    WriteLog(msg);
    CloseHandle(hProcess);
}

static void ScanProcesses(void)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        WriteLog("CreateToolhelp32Snapshot failed");
        return;
    }

    PROCESSENTRY32W pe;
    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);

    if (!Process32FirstW(snapshot, &pe)) {
        WriteLog("Process32FirstW failed");
        CloseHandle(snapshot);
        return;
    }

    do {
        if (_wcsicmp(pe.szExeFile, TARGET_PROCESS_NAME) == 0) {
            TryHandleTargetProcess(pe.th32ProcessID, pe.szExeFile);
        }
    } while (Process32NextW(snapshot, &pe));

    CloseHandle(snapshot);
}

static DWORD WINAPI WorkerThread(LPVOID param)
{
    (void)param;
    WriteLog("Worker thread started");

    // Цикл сканирования каждые 1000 мс с возможностью graceful shutdown
    while (WaitForSingleObject(g_hStopEvent, 1 == WAIT_TIMEOUT)) {
        ScanProcesses();
    }

    WriteLog("Worker thread stopped");
    return 0;
}

static DWORD WINAPI ServiceCtrlHandler(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext)
{
    switch (dwControl)
    {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
            if (g_hStopEvent) SetEvent(g_hStopEvent);
            return NO_ERROR;

        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;

        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static VOID WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    g_StatusHandle = RegisterServiceCtrlHandlerExW(SERVICE_NAME, ServiceCtrlHandler, NULL);
    if (!g_StatusHandle) {
        WriteLog("RegisterServiceCtrlHandlerEx failed");
        return;
    }

    // Инициализация статуса
    g_ServiceStatus.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState            = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted        = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_ServiceStatus.dwWin32ExitCode           = NO_ERROR;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint              = 0;
    g_ServiceStatus.dwWaitHint                = 3000;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    // Создание события остановки
    g_hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_hStopEvent) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    // Запуск рабочего потока
    g_hThread = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
    if (!g_hThread) {
        CloseHandle(g_hStopEvent);
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    // Служба запущена
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwCheckPoint   = 0;
    g_ServiceStatus.dwWaitHint     = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    WriteLog("Service started successfully");

    // Ожидание сигнала остановки
    WaitForSingleObject(g_hStopEvent, INFINITE);

    // Корректное завершение
    if (g_hThread) {
        WaitForSingleObject(g_hThread, 3000);
        CloseHandle(g_hThread);
    }
    CloseHandle(g_hStopEvent);

    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    WriteLog("Service stopped cleanly");
}

int wmain(int argc, wchar_t* argv[])
{
    SERVICE_TABLE_ENTRYW DispatchTable[] = {
        { SERVICE_NAME, ServiceMain },
        { NULL, NULL }
    };

    if (!StartServiceCtrlDispatcherW(DispatchTable)) {
        DWORD err = GetLastError();
        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            wprintf(L"[!] Запустите этот файл как службу или используйте sc.exe для установки.\n");
        } else {
            wprintf(L"[!] StartServiceCtrlDispatcher failed. Error: %lu\n", err);
        }
        return (int)err;
    }
    return 0;
}
