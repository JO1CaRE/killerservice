# ProcessWatcherSvc

## Назначение

`ProcessWatcherSvc` — user-mode Windows-служба, которая циклически просматривает список процессов и при обнаружении `EDR_service.exe` пытается завершить его через `TerminateProcess`.

## Основные параметры

| Параметр | Значение |
|---|---|
| Имя службы | `ProcessWatcherSvc` |
| Отображаемое имя | `Process Watcher Service` |
| Целевой процесс | `EDR_service.exe` |
| Лог-файл | `C:\Windows\Temp\process_watcher.log` |
| Тип | `SERVICE_WIN32_OWN_PROCESS` |

## Логика работы

1. `wmain()` запускает службу через `StartServiceCtrlDispatcherW`.
2. `ServiceMain()` регистрирует обработчик управления через `RegisterServiceCtrlHandlerExW`.
3. Создается событие остановки `g_hStopEvent`.
4. Создается рабочий поток `WorkerThread`.
5. Поток вызывает `ScanProcesses()`.
6. `ScanProcesses()` получает снимок процессов через `CreateToolhelp32Snapshot`.
7. Процессы перебираются через `Process32FirstW` и `Process32NextW`.
8. Если имя процесса совпадает с `EDR_service.exe`, вызывается `TryHandleTargetProcess()`.
9. `TryHandleTargetProcess()` открывает процесс с правом `PROCESS_TERMINATE`.
10. При успешном открытии процесса вызывается `TerminateProcess()`.

## Вредоносная логика

Код предназначен для принудительного завершения процесса `EDR_service.exe`. Такая функциональность характерна для EDR-killer/defense evasion компонентов, так как нарушает работу защитного программного обеспечения.

## Ошибка в коде

В `WorkerThread` указано:

```c
while (WaitForSingleObject(g_hStopEvent, 1 == WAIT_TIMEOUT)) {
```

Из-за ошибки приоритета операций в `WaitForSingleObject` передается `0`, а не `1000`. В результате цикл может выполняться почти без задержки и создавать повышенную нагрузку на CPU.

Корректный вариант:

```c
while (WaitForSingleObject(g_hStopEvent, 1000) == WAIT_TIMEOUT) {
```

## Проверка наличия службы

```cmd
sc.exe queryex ProcessWatcherSvc
sc.exe qc ProcessWatcherSvc
reg.exe query HKLM\SYSTEM\CurrentControlSet\Services\ProcessWatcherSvc /s
```

## Проверка логов

```cmd
type C:\Windows\Temp\process_watcher.log
```

## Остановка и удаление

```cmd
sc.exe stop ProcessWatcherSvc
sc.exe config ProcessWatcherSvc start= disabled
sc.exe delete ProcessWatcherSvc
del C:\Windows\Temp\process_watcher.log
```

## Вывод

Это не драйвер, не руткит и не буткит. Это обычная Windows-служба пользовательского режима. Основная опасность — попытка подавления защитного процесса `EDR_service.exe`.
