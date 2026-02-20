#pragma once

#include "../../common/common_types.hpp"
#include "../3ds/memory.hpp"
#include "gsp_types.hpp"
#include <memory>
#include <string>
#include <unordered_map>

class ArmInterpreter;
namespace VideoCore { class Gpu; }

/// GSP GPU service HLE. Handles ConnectToPort("gsp::Gpu") and SendSyncRequest.
/// Processes SetBufferSwap, TriggerCmdReqQueue (command buffer), etc.
class GspHle {
public:
    GspHle(MemorySystem& memory, VideoCore::Gpu& gpu);
    ~GspHle();

    bool handleConnectToPort(ArmInterpreter& cpu);
    bool handleSendSyncRequest(ArmInterpreter& cpu);

    static constexpr u32 GSP_GPU_HANDLE = 1;
    static constexpr u32 SRV_HANDLE = 0x20;  // srv: service (ConnectToService returns this handle)

private:
    void processGspRequest(ArmInterpreter& cpu);
    void processCommandBuffer(VAddr shared_vaddr);
    u32 getOrCreateServiceHandle(const std::string& name);

    MemorySystem& memory_;
    VideoCore::Gpu& gpu_;
    VAddr gsp_shared_vaddr_{0};
    std::unordered_map<u32, std::string> service_by_handle_{};
    std::unordered_map<std::string, u32> handle_by_service_{};
    u32 next_service_handle_{0x40};
};
