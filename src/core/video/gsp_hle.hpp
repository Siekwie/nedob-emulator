#pragma once

#include "../../common/common_types.hpp"
#include "../3ds/memory.hpp"
#include "gsp_types.hpp"
#include <memory>

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

    MemorySystem& memory_;
    VideoCore::Gpu& gpu_;
    VAddr gsp_shared_vaddr_{0};
};
