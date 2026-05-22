# Critical Cadence Stabilization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stabilize the 33ms critical update loop so `PerformanceTest.exe --duration-ms 60000` returns to `criticalDeadlineMisses=0` while preserving 100ms Write-start and history/normal update coexistence.

**Architecture:** Keep the existing `UpdateCoordinator` thread split, but remove deadline-miss amplification caused by catch-up scheduling after an overdue critical cycle. Add small timing helpers and metrics so failures show whether the miss came from critical read time, snapshot lock time, or scheduler wake drift.

**Tech Stack:** Visual Studio 2022, MFC/ATL, C++17, native CoreTests, `PerformanceTest.exe`, existing mock bridge and delay-injection options.

---

## Current Evidence

- `docs/current-spec/verification.md` records `PerformanceTest.exe --duration-ms 60000` failing twice with `criticalDeadlineMisses=22` and `30`.
- The same run keeps `maxWriteStartDelayMs=0`, `writeStartDelayExceededCount=0`, and `historyErrorCount=0`, so the immediate regression surface is critical cadence rather than Write completion or history correctness.
- `docs/current-spec/threading.md` documents `CriticalLoop()` as `next += 33ms; sleep_until(next)`. If a cycle overruns or wakes late, `next` can remain in the past and the critical thread can run catch-up cycles without sleeping. That can starve below-normal normal/history work and makes long runs more fragile.

## File Map

- Modify `src/Core/UpdateScheduler.h`
  - Add timing metrics to `SchedulerMetrics`.
  - Declare a pure `ComputeNextPeriodicWake()` helper for tests.
- Modify `src/Core/UpdateScheduler.cpp`
  - Implement `ComputeNextPeriodicWake()`.
  - Use it in `CriticalLoop()` to prevent catch-up spinning after overdue cycles.
  - Record critical cycle timing metrics with atomics.
- Modify `tests/CoreTests/main.cpp`
  - Add deterministic tests for `ComputeNextPeriodicWake()`.
  - Add a coordinator smoke that critical cycles continue while normal/history run.
- Modify `tests/PerformanceTest/main.cpp`
  - Print the new critical timing metrics.
  - Keep existing pass/fail gates unless the new metrics prove a gate is measuring the wrong thing.
- Modify `docs/current-spec/threading.md`
  - Document the no-catch-up scheduling rule and new timing metrics.
- Modify `docs/current-spec/verification.md`
  - Replace the recorded 60s failure with the new result after rerun, or keep the failure with the new timing evidence if still failing.
- Modify `docs/unimplemented-from-overview.md`
  - Update U-015 with the cadence stabilization status and remaining external COM risk.

---

### Task 1: Add deterministic scheduling helper tests

**Files:**
- Modify: `src/Core/UpdateScheduler.h`
- Modify: `src/Core/UpdateScheduler.cpp`
- Modify: `tests/CoreTests/main.cpp`

- [ ] **Step 1: Declare the helper and metrics**

Add this near the existing scheduler declarations in `src/Core/UpdateScheduler.h`:

```cpp
std::chrono::steady_clock::time_point ComputeNextPeriodicWake(
    std::chrono::steady_clock::time_point previousNext,
    std::chrono::steady_clock::time_point finished,
    std::chrono::milliseconds period) noexcept;
```

Extend `SchedulerMetrics` with:

```cpp
long long criticalLastCycleMs{};
long long criticalMaxCycleMs{};
long long criticalMaxSnapshotLockMs{};
```

Add matching private atomics to `UpdateCoordinator`:

```cpp
std::atomic<long long> criticalLastCycleMs_{0};
std::atomic<long long> criticalMaxCycleMs_{0};
std::atomic<long long> criticalMaxSnapshotLockMs_{0};
```

- [ ] **Step 2: Add failing tests**

Add tests to `tests/CoreTests/main.cpp`:

```cpp
void TestComputeNextPeriodicWakeKeepsNormalCadence()
{
    using clock = std::chrono::steady_clock;
    const auto base = clock::time_point{};
    const auto next = ComputeNextPeriodicWake(base, base + std::chrono::milliseconds(10), std::chrono::milliseconds(33));
    Check(next == base + std::chrono::milliseconds(33), "on-time critical cycle should keep the next scheduled tick");
}

void TestComputeNextPeriodicWakeSkipsCatchUpWhenOverdue()
{
    using clock = std::chrono::steady_clock;
    const auto base = clock::time_point{};
    const auto next = ComputeNextPeriodicWake(base, base + std::chrono::milliseconds(80), std::chrono::milliseconds(33));
    Check(next == base + std::chrono::milliseconds(113), "overdue critical cycle should schedule from finish time instead of catching up");
}
```

Register both tests in the CoreTests test vector.

- [ ] **Step 3: Run the targeted build and confirm RED**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\MFCApplication7.sln /t:CoreTests /p:Configuration=Debug /p:Platform=x64
```

Expected before implementation: compile fails because `ComputeNextPeriodicWake()` is declared or referenced but not implemented, or tests fail if a placeholder implementation still catches up.

- [ ] **Step 4: Implement helper**

Add to `src/Core/UpdateScheduler.cpp` outside the anonymous namespace:

```cpp
std::chrono::steady_clock::time_point ComputeNextPeriodicWake(
    std::chrono::steady_clock::time_point previousNext,
    std::chrono::steady_clock::time_point finished,
    std::chrono::milliseconds period) noexcept
{
    const auto scheduled = previousNext + period;
    if (scheduled <= finished) {
        return finished + period;
    }
    return scheduled;
}
```

- [ ] **Step 5: Run CoreTests and confirm GREEN for the helper**

Run:

```powershell
.\bin\x64\Debug\CoreTests.exe
```

Expected: all CoreTests pass, including the two new scheduling helper tests.

---

### Task 2: Apply no-catch-up scheduling and critical metrics

**Files:**
- Modify: `src/Core/UpdateScheduler.cpp`
- Modify: `tests/CoreTests/main.cpp`

- [ ] **Step 1: Add a max-atomic helper**

In the anonymous namespace of `src/Core/UpdateScheduler.cpp`, add:

```cpp
void StoreMax(std::atomic<long long>& target, long long value) noexcept
{
    auto current = target.load();
    while (value > current && !target.compare_exchange_weak(current, value)) {
    }
}
```

- [ ] **Step 2: Populate new metrics**

Update `UpdateCoordinator::Metrics()`:

```cpp
metrics.criticalLastCycleMs = criticalLastCycleMs_.load();
metrics.criticalMaxCycleMs = criticalMaxCycleMs_.load();
metrics.criticalMaxSnapshotLockMs = criticalMaxSnapshotLockMs_.load();
```

- [ ] **Step 3: Replace critical wake scheduling**

In `UpdateCoordinator::CriticalLoop()`, keep the 33ms period constant and replace raw `next += milliseconds(33)` with the helper:

```cpp
constexpr auto period = milliseconds(33);
auto next = steady_clock::now();
while (running_) {
    const auto started = steady_clock::now();
    const auto values = gateway_.ReadMany(catalog_.CriticalKeys());
    const auto beforeSnapshotLock = steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        snapshot_.criticalValues = values;
    }
    const auto finished = steady_clock::now();
    const auto cycleMs = duration_cast<milliseconds>(finished - started).count();
    const auto snapshotLockMs = duration_cast<milliseconds>(finished - beforeSnapshotLock).count();
    criticalLastCycleMs_ = cycleMs;
    StoreMax(criticalMaxCycleMs_, cycleMs);
    StoreMax(criticalMaxSnapshotLockMs_, snapshotLockMs);
    ++criticalCycles_;
    if (cycleMs > period.count()) {
        ++criticalDeadlineMisses_;
    }
    next = ComputeNextPeriodicWake(next, finished, period);
    std::this_thread::sleep_until(next);
}
```

- [ ] **Step 4: Add a coordinator metrics smoke**

Add a CoreTests smoke that starts the coordinator for a short period with history running and asserts the new metrics are populated:

```cpp
void TestCriticalTimingMetricsAreRecorded()
{
    auto catalog = DataCatalog::CreateDefault();
    auto bridge = std::make_shared<MockBackendBridge>(catalog);
    DataGateway gateway(bridge);
    Check(gateway.Connect(L"127.0.0.1") == BridgeError::Ok, "gateway connect should succeed");

    UpdateCoordinator coordinator(catalog, gateway);
    coordinator.Start();
    Check(coordinator.StartHistoryLoad({1}), "history should start");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    coordinator.CancelHistoryLoad();
    coordinator.Stop();

    const auto metrics = coordinator.Metrics();
    Check(metrics.criticalCycles > 0, "critical cycles should run");
    Check(metrics.criticalMaxCycleMs >= metrics.criticalLastCycleMs, "max critical cycle should include last cycle");
    Check(metrics.criticalMaxSnapshotLockMs >= 0, "snapshot lock metric should be recorded");
}
```

Add needed includes only if absent:

```cpp
#include <chrono>
#include <thread>
```

- [ ] **Step 5: Run CoreTests**

Run:

```powershell
.\bin\x64\Debug\CoreTests.exe
```

Expected: all tests pass.

---

### Task 3: Extend PerformanceTest diagnostics without changing the main gate

**Files:**
- Modify: `tests/PerformanceTest/main.cpp`
- Modify: `docs/current-spec/verification.md`

- [ ] **Step 1: Print the new metrics**

Add these lines to the metrics output in `tests/PerformanceTest/main.cpp`:

```cpp
               << L"criticalLastCycleMs=" << metrics.criticalLastCycleMs << L"\n"
               << L"criticalMaxCycleMs=" << metrics.criticalMaxCycleMs << L"\n"
               << L"criticalMaxSnapshotLockMs=" << metrics.criticalMaxSnapshotLockMs << L"\n"
```

Keep the existing failure condition:

```cpp
if (strictCadence && metrics.criticalDeadlineMisses != 0) {
    std::wcerr << L"critical refresh deadline missed\n";
    return 3;
}
```

- [ ] **Step 2: Build and run a short performance smoke**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\MFCApplication7.sln /p:Configuration=Debug /p:Platform=x64 /m
.\bin\x64\Debug\PerformanceTest.exe --duration-ms 3000
```

Expected: exit 0 and output includes `criticalMaxCycleMs=...`.

- [ ] **Step 3: Run the failing 60s gate**

Run serially, with no parallel build/test commands:

```powershell
.\bin\x64\Debug\PerformanceTest.exe --duration-ms 60000
```

Expected after the scheduling fix: exit 0 with `criticalDeadlineMisses=0`. If it still fails, do not relax the gate. Record `criticalMaxCycleMs` and `criticalMaxSnapshotLockMs` in `docs/current-spec/verification.md` and treat the largest metric as the next root-cause target.

---

### Task 4: Update docs and U-015 status

**Files:**
- Modify: `docs/current-spec/threading.md`
- Modify: `docs/current-spec/verification.md`
- Modify: `docs/unimplemented-from-overview.md`

- [ ] **Step 1: Document the scheduling rule**

In `docs/current-spec/threading.md`, update the critical loop section:

```markdown
6. 次回起床時刻は `ComputeNextPeriodicWake()` で計算する。通常は前回予定時刻 + 33ms だが、処理完了時刻が予定時刻を過ぎている場合は、完了時刻 + 33ms へリセットし、過去時刻への catch-up スピンを行わない。
```

Add the metrics:

```markdown
- `criticalLastCycleMs`: 直近 critical cycle の処理時間。
- `criticalMaxCycleMs`: 起動後に観測した critical cycle 処理時間の最大値。
- `criticalMaxSnapshotLockMs`: critical 値を snapshot へ反映する区間の最大値。
```

- [ ] **Step 2: Update verification result**

In `docs/current-spec/verification.md`, replace the latest `PerformanceTest.exe --duration-ms 60000` row with the actual post-fix output. If the command passes, mark it `成功`. If it fails, mark it `失敗` and include the new timing metrics.

- [ ] **Step 3: Update U-015**

In `docs/unimplemented-from-overview.md`, update U-015:

```markdown
通常60秒性能ゲートは critical no-catch-up scheduling と timing metrics を持つ。
```

Keep remaining risks:

```markdown
正式COM経由、実ネットワーク、実設備最大データ量、長時間運用での実測は未完了。
```

---

### Task 5: Full verification gate

**Files:**
- No source changes unless failures identify a root cause.

- [ ] **Step 1: Build**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\MFCApplication7.sln /p:Configuration=Debug /p:Platform=x64 /m
```

Expected: `ビルドに成功しました。0 個の警告 0 エラー`

- [ ] **Step 2: Core and smoke tests**

Run:

```powershell
.\bin\x64\Debug\CoreTests.exe
.\bin\x64\Debug\BackendBridgeMock.exe /SelfTest
.\bin\x64\Debug\BackendBridgeMock.exe /ComSelfTest
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /WriteSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /HistorySmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ScheduleMutationSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ScheduleOrderSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /StatusSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /StationLayoutSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /ContainerListLayoutSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /MaintenanceDetailSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /GridEditSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /DetailSmoke /Bridge:Mock
.\bin\x64\Debug\MFCApplication7.exe /SelfTest /MaxLoadSmoke /Bridge:Mock /MockProfile:MaxLoad
```

Expected: all exit 0.

- [ ] **Step 3: Performance gates**

Run serially:

```powershell
.\bin\x64\Debug\PerformanceTest.exe --duration-ms 3000
.\bin\x64\Debug\PerformanceTest.exe --duration-ms 60000
.\bin\x64\Debug\PerformanceTest.exe --duration-ms 3000 --max-load /Bridge:Mock /MockProfile:MaxLoad
```

Expected:

- `--duration-ms 3000`: exit 0, `criticalDeadlineMisses=0`.
- `--duration-ms 60000`: exit 0, `criticalDeadlineMisses=0`.
- `--max-load`: exit 0, `writeStartDelayExceededCount=0`, `scheduleGridLastRows=1000`.

- [ ] **Step 4: Whitespace check**

Run:

```powershell
git diff --check
```

Expected: exit 0. CRLF conversion warnings are non-blocking if there are no whitespace errors.

---

## Self-Review

- Spec coverage: Targets U-015 and the current 60s critical deadline miss recorded in `verification.md`.
- Placeholder scan: No TBD/TODO placeholders are used; all tasks include exact file paths and commands.
- Type consistency: `ComputeNextPeriodicWake`, `criticalLastCycleMs`, `criticalMaxCycleMs`, and `criticalMaxSnapshotLockMs` are consistently named across declarations, implementation, tests, metrics, and docs.
