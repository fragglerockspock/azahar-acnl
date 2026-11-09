#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include "core/leaf_acnl/tool_ring.h"
// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <cstring>

#include "audio_core/audio_types.h"
#include "audio_core/libretro_sink.h"
#include "common/scm_rev.h"
#include "core/3ds.h"
#include "core_settings.h"
#include "emu_window/libretro_window.h"
#include "environment.h"

#ifdef HAVE_LIBRETRO_VFS
#include "streams/file_stream.h"
#endif

using namespace LibRetro;

namespace LibRetro {

namespace {

static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_hw_get_current_framebuffer_t framebuffer_cb;

} // namespace

bool GetSoftwareFramebuffer(retro_framebuffer* fb, int width, int height) {
    fb->data = nullptr;
    fb->width = width;
    fb->height = height;
    fb->pitch = 0;
    fb->format = RETRO_PIXEL_FORMAT_XRGB8888;
    fb->access_flags = RETRO_MEMORY_ACCESS_WRITE;
    fb->memory_flags = 0;
    return environ_cb(RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER, fb);
}

void UploadVideoFrame(const void* data, unsigned width, unsigned height, size_t pitch) {
    return video_cb(data, width, height, pitch);
}

bool SetHWSharedContext() {
    return environ_cb(RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT, NULL);
}

namespace {
bool tool_ring_open = false;
bool tool_ring_capture = false;
std::array<bool, 16> previous_tool_buttons{};
std::array<bool,16> tool_release_block{};
int tool_selected = 0;
std::chrono::steady_clock::time_point tool_last_toggle{};
std::string tool_last_message;
std::chrono::steady_clock::time_point tool_diagnostic_until{};
void PollToolRing() {
    std::array<bool, 16> buttons{};
    for (unsigned i=0;i<buttons.size();++i) buttons[i]=input_state_cb(0,RETRO_DEVICE_JOYPAD,0,i)>0;
    for(unsigned i=0;i<buttons.size();++i)if(!buttons[i])tool_release_block[i]=false;
    const auto edge=[&](unsigned i){return buttons[i] && !previous_tool_buttons[i];};
    auto tools=LeafACNL::Tools();
    const auto now=std::chrono::steady_clock::now();
    tool_ring_capture=tool_ring_open && !tools.empty();
    if (tools.empty() && now>=tool_diagnostic_until) tool_ring_open=false;
    if (edge(RETRO_DEVICE_ID_JOYPAD_R2) && now-tool_last_toggle>std::chrono::milliseconds(200) && !LeafACNL::ToolRingStatus().empty()) {
        tool_release_block[RETRO_DEVICE_ID_JOYPAD_R2]=true;
        tool_last_toggle=now;
        tool_ring_open=!tool_ring_open;
        tool_ring_capture=!tools.empty();
        tool_selected=0;
        tool_diagnostic_until=now+std::chrono::seconds(4);
    }
    if (tool_ring_open) {
        tool_ring_capture=!tools.empty();
        if (edge(RETRO_DEVICE_ID_JOYPAD_LEFT) || edge(RETRO_DEVICE_ID_JOYPAD_UP)) tool_selected=(tool_selected+7)%8;
        if (edge(RETRO_DEVICE_ID_JOYPAD_RIGHT) || edge(RETRO_DEVICE_ID_JOYPAD_DOWN)) tool_selected=(tool_selected+1)%8;
        const double x=input_state_cb(0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_X)/32767.0;
        const double y=input_state_cb(0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_Y)/32767.0;
        if (std::hypot(x,y)>=0.5) tool_selected=(static_cast<int>(std::lround((std::atan2(y,x)*180.0/3.141592653589793+90)/45))+8)%8;
        if (edge(RETRO_DEVICE_ID_JOYPAD_A) && static_cast<size_t>(tool_selected)<tools.size()) {
            tool_release_block[RETRO_DEVICE_ID_JOYPAD_A]=true;
            LeafACNL::Select(tools[tool_selected].slot);
            tool_ring_open=false;
        }
        if (edge(RETRO_DEVICE_ID_JOYPAD_B)) {tool_release_block[RETRO_DEVICE_ID_JOYPAD_B]=true;tool_ring_open=false;}
        if (tool_ring_open) {
            std::string message="Tool Ring: ";
            for (size_t i=0;i<tools.size();++i) {
                const auto& tool=tools[i];
                const char* name="Tool";
                switch(tool.kind) {
                case 14:name="Axe";break;
                case 15:name="Net";break;
                case 16:name="Rod";break;
                case 17:name="Shovel";break;
                case 18:name="Slingshot";break;
                case 19:name="Can";break;
                case 21:name="Megaphone";break;
                }
                message+=(static_cast<int>(i)==tool_selected ? " [" : " ");
                message+=name;
                message+=" #"+std::to_string(tool.variant);
                if (tool.slot<0) message+=" (held)";
                if (static_cast<int>(i)==tool_selected) message+="]";
            }
            if (message!=tool_last_message) {DisplayMessage(message.c_str());tool_last_message=message;}
        }
    }
    if (!tool_ring_open && !tool_last_message.empty()) {DisplayMessage(" ");tool_last_message.clear();}
    previous_tool_buttons=buttons;
}
}
bool ToolRingVisible() {return tool_ring_open;}
int ToolRingSelected() {return tool_selected;}

void PollInput() {
    input_poll_cb();
    PollToolRing();
}

bool GetSensorInterface(struct retro_sensor_interface* sensor_interface) {
    return environ_cb(RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE, sensor_interface);
}

bool GetMicrophoneInterface(struct retro_microphone_interface* mic_interface) {
    return environ_cb(RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE, mic_interface);
}

Settings::GraphicsAPI GetPreferredRenderer() {
    // On Android, we really want to default to Vulkan if we can, so we'll ignore the frontend's
    // recommendation if possible...
#if defined(ANDROID) && defined(ENABLE_VULKAN)
    return Settings::GraphicsAPI::Vulkan;
#endif
    // ...Otherwise negotiate with the RetroArch frontend as usual

    // Attempt to use the renderer recommended by the frontend if possible
    retro_hw_context_type context_type = RETRO_HW_CONTEXT_OPENGL;
    environ_cb(RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER, &context_type);
    switch (context_type) {
#ifdef ENABLE_OPENGL
    case RETRO_HW_CONTEXT_OPENGL:
    case RETRO_HW_CONTEXT_OPENGL_CORE:
    case RETRO_HW_CONTEXT_OPENGLES2:
    case RETRO_HW_CONTEXT_OPENGLES3:
    case RETRO_HW_CONTEXT_OPENGLES_VERSION:
        return Settings::GraphicsAPI::OpenGL;
#endif
#ifdef ENABLE_VULKAN
    case RETRO_HW_CONTEXT_VULKAN:
        return Settings::GraphicsAPI::Vulkan;
#endif
    default:
        break;
    }
    // We can't get a recommendation from the frontend, so fall back to whatever's available
#if defined(ENABLE_VULKAN)
    return Settings::GraphicsAPI::Vulkan;
#elif defined(ENABLE_OPENGL)
    return Settings::GraphicsAPI::OpenGL;
#else
    return Settings::GraphicsAPI::Software;
#endif
}

bool SetVariables(const retro_variable vars[]) {
    return environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
}

bool SetCoreOptionsV2(const retro_core_options_v2* options) {
    return environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, (void*)options);
}

bool SetCoreOptionsV1(const retro_core_option_definition* options) {
    return environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, (void*)options);
}

bool GetCoreOptionsVersion(unsigned* version) {
    return environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, version);
}

bool SetMemoryMaps(const retro_memory_map* map) {
    return environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, (void*)map);
}

bool SetControllerInfo(const retro_controller_info info[]) {
    return environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)info);
}

bool SetPixelFormat(const retro_pixel_format fmt) {
    return environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, (void*)&fmt);
}

bool SetHWRenderer(retro_hw_render_callback* cb) {
    return environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, cb);
}

bool GetHWRenderInterface(void** interface) {
    return environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, interface) && !!*interface;
}

bool SetHWRenderContextNegotiationInterface(void** interface) {
    return environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, interface) &&
           !!*interface;
}

bool SetAudioCallback(retro_audio_callback* cb) {
    return environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK, cb);
}

bool SetFrameTimeCallback(retro_frame_time_callback* cb) {
    return environ_cb(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, cb);
}

bool SetGeometry(retro_system_av_info* cb) {
    return environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, cb);
}

bool SetInputDescriptors(const retro_input_descriptor desc[]) {
    return environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*)desc);
}

bool HasUpdatedConfig() {
    bool updated = false;
    return environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated;
}

bool Shutdown() {
    return environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

/// Displays the specified message to the screen.
bool DisplayMessage(const char* sg) {
    retro_message msg;
    msg.msg = sg;
    msg.frames = 60 * 10;
    return environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
}

bool SetSerializationQuirks(uint64_t quirks) {
    return environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &quirks);
}

std::string FetchVariable(std::string key, std::string def) {
    struct retro_variable var = {nullptr};
    var.key = key.c_str();
    if (!environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) || var.value == nullptr) {
        // Fetching variable failed.
        LOG_ERROR(Frontend, "Fetching variable {} failed.", key);
        return def;
    }
    return std::string(var.value);
}

std::string GetSaveDir() {
    char* var = nullptr;
    if (!environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &var) || var == nullptr) {
        // Fetching variable failed.
        LOG_ERROR(Frontend, "No save directory provided by LibRetro.");
        return std::string();
    }
    return std::string(var);
}

std::string GetSystemDir() {
    char* var = nullptr;
    if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &var) || var == nullptr) {
        // Fetching variable failed.
        LOG_ERROR(Frontend, "No system directory provided by LibRetro.");
        return std::string();
    }
    return std::string(var);
}

retro_log_printf_t GetLoggingBackend() {
    retro_log_callback callback{};
    if (!environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &callback)) {
        return nullptr;
    }
    return callback.log;
}

int16_t CheckInput(unsigned port, unsigned device, unsigned index, unsigned id) {
    if(port==0 && device==RETRO_DEVICE_JOYPAD && id<tool_release_block.size() && tool_release_block[id])return 0;
    if (port == 0 && tool_ring_capture && (device == RETRO_DEVICE_JOYPAD || device == RETRO_DEVICE_ANALOG)) return 0;
    return input_state_cb(port, device, index, id);
}

#ifdef HAVE_LIBRETRO_VFS
void SetVFSCallback(struct retro_vfs_interface_info* vfs_iface_info) {
    if (environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, vfs_iface_info))
        filestream_vfs_init(vfs_iface_info);
}
#endif

#ifdef IOS
bool CanUseJIT() {
    bool can_jit = false;
    return environ_cb(RETRO_ENVIRONMENT_GET_JIT_CAPABLE, &can_jit) && can_jit;
}
#endif

void OnConfigureEnvironment() {
#ifdef HAVE_LIBRETRO_VFS
    struct retro_vfs_interface_info vfs_iface_info{1, nullptr};
    SetVFSCallback(&vfs_iface_info);
#endif

    RegisterCoreOptions();

    static const struct retro_controller_description controllers[] = {
        {"Nintendo 3DS", RETRO_DEVICE_JOYPAD},
    };

    static const struct retro_controller_info ports[] = {
        {controllers, 1},
        {nullptr, 0},
    };

    SetControllerInfo(ports);
}

void SetFramebufferCallback(retro_hw_get_current_framebuffer_t cb) {
    framebuffer_cb = cb;
}

uintptr_t GetFramebuffer() {
    return framebuffer_cb ? framebuffer_cb() : 0;
}

}; // namespace LibRetro

void retro_get_system_info(struct retro_system_info* info) {
    memset(info, 0, sizeof(*info));
    info->library_name = "Azahar";
    info->library_version = Common::g_build_fullname;
    info->need_fullpath = true;
    info->valid_extensions = "3ds|3dsx|z3dsx|elf|axf|cci|zcci|cxi|zcxi|app|bcci|bcxi";
}

void LibRetro::SubmitAudio(const int16_t* data, size_t frames) {
    audio_batch_cb(data, frames);
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
    // We don't need single audio sample callbacks.
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    LibRetro::audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb) {
    LibRetro::input_poll_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb) {
    LibRetro::video_cb = cb;
}
void retro_set_environment(retro_environment_t cb) {
    LibRetro::environ_cb = cb;
    LibRetro::OnConfigureEnvironment();
}

void retro_set_controller_port_device(unsigned port, unsigned device) {}

void retro_set_input_state(retro_input_state_t cb) {
    input_state_cb = cb;
}

void retro_get_system_av_info(struct retro_system_av_info* info) {
    info->timing.fps = 60.0;
    info->timing.sample_rate = AudioCore::native_sample_rate;

    // Compute geometry from current settings so the frontend allocates the
    // correct framebuffer on first use.
    auto geom = ComputeLayoutGeometry();
    info->geometry.base_width = geom.width;
    info->geometry.base_height = geom.height;
    // Max must cover the largest possible layout (SideScreen at 10x = 7200).
    info->geometry.max_width = (Core::kScreenBottomWidth + Core::kScreenTopWidth) * 10;
    info->geometry.max_height = (Core::kScreenTopHeight + Core::kScreenBottomHeight) * 10;
    info->geometry.aspect_ratio = (float)geom.width / (float)geom.height;
}
