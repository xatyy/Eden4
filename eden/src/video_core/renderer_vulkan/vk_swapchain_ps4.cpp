// SPDX-License-Identifier: GPL-3.0-or-later
// VideoOut presentation follows DolphinPS4's RADV integration (GPL-2.0-or-later).

#include <algorithm>
#include <memory>
#include <array>
#include <limits>
#include <orbis/VideoOut.h>
#include <unistd.h>
#include "common/alignment.h"
#include "common/logging.h"
#include "common/ps4_diagnostics.h"
#include "common/settings.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"

// RADV's PS4 winsys (ac_ps4_drm.c): queues a VideoOut flip behind all GPU work submitted so far.
extern "C" int ac_ps4_submit_flip(int video, unsigned buffer, unsigned mode, int64_t arg);

namespace Vulkan {
namespace {
constexpr u32 BufferCount = 3;
constexpr VkDeviceSize BufferAlignment = 2 * 1024 * 1024;
}

struct Swapchain::PS4State {
    explicit PS4State(const vk::Device& logical_) : logical{logical_} {}
    ~PS4State() {
        void(logical.WaitIdle());
        if (video >= 0) sceVideoOutClose(video);
        const auto& api = logical.Dispatch();
        for (u32 i = 0; i < BufferCount; ++i) {
            if (images[i]) api.vkDestroyImage(*logical, images[i], nullptr);
            if (mapped[i]) api.vkUnmapMemory(*logical, memories[i]);
            if (memories[i]) api.vkFreeMemory(*logical, memories[i], nullptr);
        }
    }
    const vk::Device& logical;
    int video{-1};
    std::array<VkImage, BufferCount> images{};
    std::array<VkDeviceMemory, BufferCount> memories{};
    std::array<bool, BufferCount> mapped{};
    std::array<s64, BufferCount> last_flip{};
    s64 submitted{};
    bool gpu_flips{true};
};

Swapchain::Swapchain(VkSurfaceKHR_T* surface_, const Device& device_, Scheduler& scheduler_,
                     u32 width_, u32 height_)
    : surface{surface_}, device{device_}, scheduler{scheduler_} {
    Create(surface_, width_, height_);
}

Swapchain::~Swapchain() {
    Destroy();
}

void Swapchain::Create(VkSurfaceKHR_T* surface_, u32 width_, u32 height_) {
    Destroy();
    surface = surface_;
    width = width_;
    height = height_;
    const bool full_hd = Common::PS4Diagnostics::output_1080 || Common::PS4Diagnostics::videoout_needs_1080;
    const bool argb = Common::PS4Diagnostics::videoout_argb;
    extent = full_hd ? VkExtent2D{1920, 1080} : VkExtent2D{1280, 720};
    surface_format = {argb ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM,
                      VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    image_view_format = surface_format.format;
    is_outdated = false;
    is_suboptimal = false;
    ps4 = std::make_unique<PS4State>(device.GetLogical());
    ps4->video = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (ps4->video < 0) {
        LOG_ERROR(Render_Vulkan, "PS4 VideoOut open failed: {:#x}", u32(ps4->video));
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
    const auto& logical = device.GetLogical();
    const auto& api = logical.Dispatch();
    const auto memory_properties = device.GetPhysical().GetMemoryProperties().memoryProperties;
    std::array<void*, BufferCount> addresses{};
    u32 pitch{};
    for (u32 i = 0; i < BufferCount; ++i) {
        const VkImageCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = surface_format.format,
            .extent = {extent.width, extent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_LINEAR,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        vk::Check(api.vkCreateImage(*logical, &info, nullptr, &ps4->images[i]));
        const auto requirements = logical.GetImageMemoryRequirements(ps4->images[i]);
        constexpr auto wanted = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        u32 type = std::numeric_limits<u32>::max();
        for (u32 candidate = 0; candidate < memory_properties.memoryTypeCount; ++candidate) {
            if ((requirements.memoryTypeBits & (1u << candidate)) &&
                (memory_properties.memoryTypes[candidate].propertyFlags & wanted) == wanted) {
                type = candidate;
                break;
            }
        }
        if (type == std::numeric_limits<u32>::max())
            throw vk::Exception(VK_ERROR_FEATURE_NOT_PRESENT);
        const VkMemoryAllocateInfo allocate{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size + BufferAlignment,
            .memoryTypeIndex = type,
        };
        vk::Check(api.vkAllocateMemory(*logical, &allocate, nullptr, &ps4->memories[i]));
        void* base = nullptr;
        vk::Check(api.vkMapMemory(*logical, ps4->memories[i], 0, VK_WHOLE_SIZE, 0, &base));
        ps4->mapped[i] = true;
        const auto pointer = reinterpret_cast<uintptr_t>(base);
        const VkDeviceSize offset = Common::AlignUp(pointer, BufferAlignment) - pointer;
        if (offset % requirements.alignment != 0)
            throw vk::Exception(VK_ERROR_MEMORY_MAP_FAILED);
        vk::Check(api.vkBindImageMemory(*logical, ps4->images[i], ps4->memories[i], offset));
        const VkImageSubresource subresource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
        VkSubresourceLayout layout{};
        api.vkGetImageSubresourceLayout(*logical, ps4->images[i], &subresource, &layout);
        if (i && pitch != layout.rowPitch / 4)
            throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
        pitch = static_cast<u32>(layout.rowPitch / 4);
        addresses[i] = static_cast<u8*>(base) + offset + layout.offset;
    }
    OrbisVideoOutBufferAttribute attributes{};
    sceVideoOutSetBufferAttribute(&attributes, argb ? 0x80000000 /* A8R8G8B8_SRGB */
                                                    : ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
        ORBIS_VIDEO_OUT_TILING_MODE_LINEAR, 0, extent.width, extent.height, pitch);
    const int result = sceVideoOutRegisterBuffers(ps4->video, 0, addresses.data(), BufferCount,
                                                 &attributes);
    if (result < 0) {
        LOG_ERROR(Render_Vulkan, "PS4 VideoOut buffer registration failed: {:#x}", u32(result));
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
    images.assign(ps4->images.begin(), ps4->images.end());
    image_count = BufferCount;
    if (const std::unique_ptr<std::vector<u32>> picture{Common::PS4Diagnostics::loading_picture.exchange(nullptr)};
        picture && picture->size() == size_t(1920) * 1080) {
        // The launcher's loading screen until the game's first frame: box-filtered into buffer 0.
        auto* out = static_cast<u32*>(addresses[0]);
        for (u32 y = 0; y < extent.height; ++y) {
            const u32 y0 = y * 1080 / extent.height, y1 = std::max(y0 + 1, (y + 1) * 1080 / extent.height);
            for (u32 x = 0; x < extent.width; ++x) {
                const u32 x0 = x * 1920 / extent.width, x1 = std::max(x0 + 1, (x + 1) * 1920 / extent.width);
                u32 sum[3]{}, count = 0;
                for (u32 sy = y0; sy < y1; ++sy)
                    for (u32 sx = x0; sx < x1; ++sx, ++count) {
                        const u32 pixel = (*picture)[size_t(sy) * 1920 + sx];
                        for (int c = 0; c < 3; ++c) sum[c] += (pixel >> (c * 8)) & 255;
                    }
                u32 r = sum[0] / count, g = sum[1] / count, b = sum[2] / count;
                if (argb) std::swap(r, b);
                out[size_t(y) * pitch + x] = 0xff000000u | (b << 16) | (g << 8) | r;
            }
        }
        __builtin_ia32_sfence();
        if (sceVideoOutSubmitFlip(ps4->video, 0, ORBIS_VIDEO_OUT_FLIP_VSYNC, 1) >= 0) {
            ps4->submitted = 1;
            ps4->last_flip[0] = 1;
            frame_index = 1;
            LOG_INFO(Render_Vulkan, "PS4 display: loading screen shown until the first game frame");
        }
    }
    LOG_INFO(Render_Vulkan, "PS4 RADV display ready: {}x{}, {} buffers, pitch {}",
             extent.width, extent.height, image_count, pitch);
}

bool Swapchain::AcquireNextImage() {
    image_index = frame_index;
    const s64 previous = ps4->last_flip[image_index];
    if (previous != 0) {
        bool available = false;
        for (u32 attempt = 0; attempt < 1000; ++attempt) {
            OrbisVideoOutFlipStatus status{};
            if (sceVideoOutGetFlipStatus(ps4->video, &status) < 0)
                throw vk::Exception(VK_ERROR_DEVICE_LOST);
            if (status.flipArg > previous) { available = true; break; }
            usleep(1000);
        }
        if (!available) {
            LOG_ERROR(Render_Vulkan, "PS4 display did not release buffer {}", image_index);
            throw vk::Exception(VK_TIMEOUT);
        }
    }
    return false;
}

void Swapchain::Present(VkSemaphore) {
    const int mode = Settings::values.vsync_mode.GetValue() == Settings::VSyncMode::Immediate ?
        ORBIS_VIDEO_OUT_FLIP_HSYNC : ORBIS_VIDEO_OUT_FLIP_VSYNC;
    const s64 argument = ps4->submitted + 1;
    // The copy into the VideoOut buffer was submitted just before this (PresentManager::
    // CopyToSwapchain): queue the flip behind it on the GPU instead of waiting for the GPU to go
    // idle first, which stalled this thread (and the emulated GPU) every frame.
    int result = -1;
    if (ps4->gpu_flips) {
        Common::PS4Diagnostics::stage = "queueing VideoOut flip";
        std::scoped_lock lock{scheduler.submit_mutex};
        result = ac_ps4_submit_flip(ps4->video, image_index, static_cast<unsigned>(mode), argument);
        if (result != 0) {
            LOG_WARNING(Render_Vulkan, "PS4 GPU-queued flip failed ({:#x}); waiting for the GPU instead",
                        u32(result));
            ps4->gpu_flips = false;
        }
    }
    if (result != 0) {
        {
            Common::PS4Diagnostics::stage = "waiting for GPU display copy";
            std::scoped_lock lock{scheduler.submit_mutex};
            vk::Check(device.GetLogical().WaitIdle());
        }
        Common::PS4Diagnostics::stage = "submitting VideoOut flip";
        result = sceVideoOutSubmitFlip(ps4->video, static_cast<int>(image_index), mode, argument);
    }
    if (result < 0) {
        LOG_ERROR(Render_Vulkan, "PS4 display flip failed: {:#x}", u32(result));
        throw vk::Exception(VK_ERROR_DEVICE_LOST);
    }
    ps4->submitted = argument;
    ps4->last_flip[image_index] = argument;
    const auto flip = ++Common::PS4Diagnostics::flips;
    if (flip <= 3) {
        LOG_INFO(Render_Vulkan, "PS4 VideoOut flip {} submitted: buffer={}, argument={}, {}",
                 flip, image_index, argument, ps4->gpu_flips ? "queued on the GPU" : "after GPU idle");
        Common::PS4Diagnostics::LogDirectMemory("display flip");
    }
    frame_index = (frame_index + 1) % BufferCount;
}

void Swapchain::Destroy() {
    if (ps4) {
        std::scoped_lock lock{scheduler.submit_mutex};
        ps4.reset();
    }
    images.clear();
    image_count = 0;
    frame_index = 0;
    image_index = 0;
}

bool Swapchain::NeedsPresentModeUpdate() const {
    return false;
}
} // namespace Vulkan
