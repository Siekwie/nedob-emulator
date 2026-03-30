#pragma once

#include "../../common/common_types.hpp"
#include <functional>
#include <map>
#include <string>
#include <vector>

class ArmInterpreter;
class MemorySystem;

/// SVC call handler: receives SVC number and CPU. Returns true to continue, false to stop.
using SvcDispatchFn = std::function<bool(u32, ArmInterpreter&)>;

/// SVC dispatcher - routes SVC #N to handlers. Stub returns true (continue).
/// Call from ARM interpreter when SVC is executed.
class SvcDispatcher {
public:
    SvcDispatcher();
    ~SvcDispatcher() = default;

    /// Handle SVC call. Returns true to continue execution, false to stop (e.g. ExitProcess).
    bool call(u32 svc_num, ArmInterpreter& cpu);

    /// Register custom handler for SVC number (optional, for tests).
    void setHandler(u32 svc_num, SvcDispatchFn fn);

    /// Allocate a new handle with the given type label (for ConnectToPort/ConnectToService stubs).
    u32 allocHandle(const std::string& type);

    /// Execute bounded bootstrap work for queued threads.
    void runPendingThreadBootstraps(ArmInterpreter& cpu, u32 max_threads);

private:
    struct PendingThreadBootstrap {
        u32 handle{0};
        u32 entry{0};
        u32 arg{0};
        u32 stack_top{0};
    };

    static constexpr u32 kMaxSvc = 256;
    SvcDispatchFn handlers_[kMaxSvc];
    std::map<u32, std::string> handle_table_;
    u32 next_handle_id_{1};
    std::vector<PendingThreadBootstrap> pending_thread_bootstraps_;
};
