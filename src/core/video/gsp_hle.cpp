#include "gsp_hle.hpp"
#include "../arm/interpreter.hpp"
#include "../3ds/memory.hpp"
#include "gpu.hpp"
#include "../../common/logger.hpp"
#include <cstdio>
#include <cstring>

namespace {

constexpr u32 CMD_REGISTER_INTERRUPT_RELAY_QUEUE = 0x00010040;
constexpr u32 CMD_SET_LCD_FORCE_BLACK = 0x00060082;
constexpr u32 CMD_SET_BUFFER_SWAP = 0x00050082;
constexpr u32 CMD_TRIGGER_CMD_REQ_QUEUE = 0x00070082;

constexpr u32 FB_UPDATE_OFFSET = 0x200;
constexpr u32 CMD_BUFFER_OFFSET = 0x800;

}  // namespace

GspHle::GspHle(MemorySystem& memory, VideoCore::Gpu& gpu) : memory_(memory), gpu_(gpu) {}

GspHle::~GspHle() = default;

bool GspHle::handleConnectToPort(ArmInterpreter& cpu) {
    const u32 out_handle_ptr = cpu.state().r[0];
    const u32 port_name_ptr = cpu.state().r[1];
    if (port_name_ptr == 0) {
        cpu.state().r[0] = 0xE0E01BF4;  // invalid address
        return true;
    }
    char name[32]{};
    for (int i = 0; i < 31; ++i) {
        name[i] = static_cast<char>(memory_.read8(port_name_ptr + i));
        if (name[i] == '\0') break;
    }
    if (std::strcmp(name, "gsp::Gpu") == 0) {
        cpu.state().r[1] = GSP_GPU_HANDLE;  // 3DS ABI: handle returned in R1
        cpu.state().r[0] = 0;  // success
        Logger::logInfo("GSP: ConnectToPort gsp::Gpu -> success (R1=0x%08X, out=0x%08X)\n",
                        cpu.state().r[1], out_handle_ptr);
    } else if (std::strcmp(name, "srv:") == 0 || name[0] == '\0' || std::strstr(name, "srv") != nullptr) {
        cpu.state().r[1] = SRV_HANDLE;  // returned in R1; caller stores it
        cpu.state().r[0] = 0;  // success (early boot: return srv: even if name hard to read)
        Logger::logInfo("GSP: ConnectToPort %s -> srv: success (R1=0x%08X, out=0x%08X)\n",
                        name[0] ? name : "(empty)", cpu.state().r[1], out_handle_ptr);
    } else {
        cpu.state().r[1] = SRV_HANDLE;  // default to srv handle for bringup
        cpu.state().r[0] = 0;  // success: always return valid handle so boot continues
        Logger::logInfo("GSP: ConnectToPort %s -> default srv: handle (R1=0x%08X, out=0x%08X)\n",
                        name, cpu.state().r[1], out_handle_ptr);
    }
    return true;
}

namespace {
constexpr u32 CMD_BUF_TLS_VADDR = 0x0FFFF080;  // Thread command buffer at TLS + 0x80
constexpr u32 IPC_HEADER_SUCCESS = 0x00010040;
}  // namespace

u32 GspHle::getOrCreateServiceHandle(const std::string& name) {
    if (name == "gsp::Gpu") return GSP_GPU_HANDLE;
    if (name == "srv:") return SRV_HANDLE;
    const auto it = handle_by_service_.find(name);
    if (it != handle_by_service_.end()) return it->second;
    const u32 h = next_service_handle_++;
    handle_by_service_[name] = h;
    service_by_handle_[h] = name;
    return h;
}

bool GspHle::handleSendSyncRequest(ArmInterpreter& cpu) {
    const u32 handle = cpu.state().r[0];
    constexpr int CMD_BUF_WORDS = 16;
    char buf[256];
    int len = std::snprintf(buf, sizeof(buf), "Service Request on Handle 0x%08X  CmdBuf[0..15]=", handle);
    for (int i = 0; i < CMD_BUF_WORDS && len < static_cast<int>(sizeof(buf)) - 24; ++i) {
        const u32 w = memory_.read32(CMD_BUF_TLS_VADDR + static_cast<u32>(i) * 4);
        len += std::snprintf(buf + len, sizeof(buf) - static_cast<std::size_t>(len), " 0x%08X", w);
    }
    Logger::log("%s\n", buf);
    if (handle == SRV_HANDLE) {
        const u32 req_header = memory_.read32(CMD_BUF_TLS_VADDR + 0);
        const u32 cmd_id = (req_header >> 16) & 0xFFFFu;
        const u32 resp_header = (cmd_id << 16) | 0x40u;  // normal response shape
        memory_.write32(CMD_BUF_TLS_VADDR + 0, resp_header);
        memory_.write32(CMD_BUF_TLS_VADDR + 1 * 4, 0);  // ResultCode = 0 (success)

        // SRV basic commands needed by early runtime init.
        if (cmd_id == 0x1) {
            // RegisterClient
            Logger::logInfo("SRV: RegisterClient\n");
        } else if (cmd_id == 0x5) {
            // GetServiceHandleDirect
            char name_buf[9]{};
            const u32 w1 = memory_.read32(CMD_BUF_TLS_VADDR + 1 * 4);
            const u32 w2 = memory_.read32(CMD_BUF_TLS_VADDR + 2 * 4);
            std::memcpy(&name_buf[0], &w1, 4);
            std::memcpy(&name_buf[4], &w2, 4);
            name_buf[8] = '\0';
            std::string service_name(name_buf);
            // Trim at first NUL if any.
            const auto nul = service_name.find('\0');
            if (nul != std::string::npos) service_name.resize(nul);
            const u32 out_handle = getOrCreateServiceHandle(service_name);
            memory_.write32(CMD_BUF_TLS_VADDR + 3 * 4, out_handle);
            Logger::logInfo("SRV: GetServiceHandle('%s') -> 0x%08X\n", service_name.c_str(), out_handle);
        } else {
            Logger::logInfo("SRV: unhandled cmd_id=0x%X (stub success)\n", cmd_id);
        }

        cpu.state().r[0] = 0;
        return true;
    }
    if (handle == GSP_GPU_HANDLE || (service_by_handle_.count(handle) && service_by_handle_[handle] == "gsp::Gpu")) {
        const u32 cmd = memory_.read32(CMD_BUF_TLS_VADDR + 0);
        static u32 log_count = 0;
        if (log_count++ < 50) {
            Logger::logInfo("GSP: SendSyncRequest cmd=0x%08X\n", cmd);
        }
        processGspRequest(cpu);
        cpu.state().r[0] = 0;
        return true;
    }

    // Generic HLE response for unknown service handles (early bringup):
    // return success so titles can continue probing services.
    memory_.write32(CMD_BUF_TLS_VADDR + 0, IPC_HEADER_SUCCESS);
    memory_.write32(CMD_BUF_TLS_VADDR + 1 * 4, 0);
    cpu.state().r[0] = 0;
    return true;
}

void GspHle::processGspRequest(ArmInterpreter& cpu) {
    const u32 cmd = cpu.state().r[1];
    switch (cmd) {
        case CMD_REGISTER_INTERRUPT_RELAY_QUEUE: {
            const u32 flags = cpu.state().r[2];
            const u32 vaddr = cpu.state().r[3];
            const u32 size = cpu.state().r[4];
            (void)flags;
            (void)size;
            if (vaddr != 0 && vaddr >= 0x08000000 && vaddr < 0x1C000000) {
                gsp_shared_vaddr_ = vaddr;
                Logger::logInfo("GSP: RegisterInterruptRelayQueue vaddr=0x%08X size=%u\n", vaddr, size);
            }
            break;
        }
        case CMD_SET_BUFFER_SWAP: {
            const u32 screen_id = cpu.state().r[2];
            const u32 fb_info_ptr = cpu.state().r[3];
            if (fb_info_ptr != 0) {
                Gsp::FrameBufferInfo info{};
                info.active_fb = memory_.read32(fb_info_ptr + 0);
                info.address_left = memory_.read32(fb_info_ptr + 4);
                info.address_right = memory_.read32(fb_info_ptr + 8);
                info.stride = memory_.read32(fb_info_ptr + 12);
                info.format = memory_.read32(fb_info_ptr + 16);
                info.shown_fb = memory_.read32(fb_info_ptr + 20);
                gpu_.setBufferSwap(screen_id, info);
                Logger::logInfo("GSP: SetBufferSwap screen=%u addr=0x%08X stride=%u fmt=%u\n",
                               screen_id, info.address_left, info.stride, info.format);
            }
            break;
        }
        case CMD_TRIGGER_CMD_REQ_QUEUE: {
            if (gsp_shared_vaddr_ != 0) {
                Logger::logInfo("GSP: TriggerCmdReqQueue processing...\n");
                processCommandBuffer(gsp_shared_vaddr_);
            } else {
                Logger::logInfo("GSP: TriggerCmdReqQueue skipped (no shared vaddr)\n");
            }
            break;
        }
        case CMD_SET_LCD_FORCE_BLACK:
            break;
        default:
            break;
    }
}

void GspHle::processCommandBuffer(VAddr shared_vaddr) {
    const VAddr cmd_buf_addr = shared_vaddr + CMD_BUFFER_OFFSET;
    const u32 num_cmds = memory_.read32(cmd_buf_addr + 4);
    if (num_cmds == 0) return;
    static u32 cmd_buf_log_count = 0;
    if (cmd_buf_log_count++ < 30) {
        Logger::logInfo("GSP: CommandBuffer %u commands\n", num_cmds);
    }
    for (u32 i = 0; i < num_cmds && i < 15; ++i) {
        const VAddr cmd_addr = cmd_buf_addr + 0x20 + i * 0x20;
        Gsp::Command cmd{};
        cmd.id = memory_.read32(cmd_addr + 0);
        for (size_t j = 0; j < 7; ++j) {
            const u32 v = memory_.read32(cmd_addr + 4 + j * 4);
            cmd.raw_data[j * 4] = static_cast<u8>(v);
            cmd.raw_data[j * 4 + 1] = static_cast<u8>(v >> 8);
            cmd.raw_data[j * 4 + 2] = static_cast<u8>(v >> 16);
            cmd.raw_data[j * 4 + 3] = static_cast<u8>(v >> 24);
        }
        gpu_.executeCommand(cmd);
    }
    memory_.write32(cmd_buf_addr + 8, 0);  // status = idle
}
