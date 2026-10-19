// SPDX-License-Identifier: GPL-3.0-or-later
// PS4 platform validation, using the runtime and VideoOut approach from DolphinPS4.

#include <atomic>
#include <cerrno>
#include <cpuid.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>

extern "C" int sceSystemServiceLoadExec(const char*, char* const[]);
extern "C" const char* ps4rt_heap_source();
extern "C" size_t ps4rt_heap_size();
// PacBrew's declarations incorrectly pass these output arguments by value.
extern "C" int QueryDirectMemory(off_t, off_t, size_t, off_t*, size_t*)
    asm("sceKernelAvailableDirectMemorySize");
extern "C" int QueryFlexibleMemory(size_t*) asm("sceKernelAvailableFlexibleMemorySize");

namespace {
constexpr size_t MiB = 1024 * 1024;
int log_fd = -1;
int usb_fd = -1;

void WriteAll(int fd, const char* data, size_t size) {
    while (fd >= 0 && size > 0) {
        const ssize_t written = write(fd, data, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return;
        data += written;
        size -= static_cast<size_t>(written);
    }
}

__attribute__((format(printf, 1, 2))) void Log(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (length <= 0) return;
    const size_t size = static_cast<size_t>(length) < sizeof(text) ?
        static_cast<size_t>(length) : sizeof(text) - 1;
    for (const int fd : {log_fd, usb_fd}) {
        if (fd < 0) continue;
        WriteAll(fd, text, size);
        fsync(fd);
    }
}

bool CheckThreads() {
    Log("BEGIN C++ thread test\n");
    try {
        std::atomic<int> result{0};
        std::thread worker([&] { result.store(42); });
        worker.join();
        Log("Thread result: %d (expected 42)\n", result.load());
        return result.load() == 42;
    } catch (const std::exception& error) {
        Log("Thread failure: %s\n", error.what());
        return false;
    }
}

bool CheckJit() {
    Log("BEGIN JIT test: writable mapping, then executable mapping\n");
    constexpr size_t size = 64 * 1024;
    void* code = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) {
        Log("JIT mmap failed: errno=%d\n", errno);
        return false;
    }
    const unsigned char program[] = {0xb8, 42, 0, 0, 0, 0xc3};
    memcpy(code, program, sizeof(program));
    const int status = sceKernelMprotect(code, size, PROT_READ | PROT_EXEC);
    Log("JIT mprotect: %#x\n", status);
    bool passed = false;
    if (status == 0) {
        Log("Calling generated function\n");
        const auto result = reinterpret_cast<int (*)()>(code)();
        Log("JIT result: %d (expected 42)\n", result);
        passed = result == 42;
    }
    munmap(code, size);
    return passed;
}

void CheckMemory() {
    Log("BEGIN memory budget queries; no multi-gigabyte allocation\n");
    Log("Runtime heap: %s, %zu MiB\n", ps4rt_heap_source(), ps4rt_heap_size() / MiB);
    const size_t total = sceKernelGetDirectMemorySize();
    off_t start{};
    size_t available{};
    const int direct = QueryDirectMemory(0, total, 64 * 1024, &start, &available);
    Log("Direct total: %zu MiB; query=%#x; largest free block=%zu MiB\n",
        total / MiB, direct, available / MiB);
    available = 0;
    const int flexible = QueryFlexibleMemory(&available);
    Log("Flexible query=%#x; free=%zu MiB\n", flexible, available / MiB);
    void* base = nullptr;
    constexpr size_t reserve = size_t{1} << 39;
    const int status = sceKernelReserveVirtualRange(&base, reserve, 0, 64 * 1024);
    Log("Eden 512 GiB virtual reservation: status=%#x, address=%p\n", status, base);
    if (status == 0) Log("Release reservation: %#x\n", sceKernelMunmap(base, reserve));
}

bool ShowResult(bool passed) {
    Log("BEGIN VideoOut test\n");
    constexpr uint32_t width = 1280, height = 720;
    constexpr size_t alignment = 2 * MiB;
    constexpr size_t bytes = (width * height * 4 + alignment - 1) & ~(alignment - 1);
    const int video = sceVideoOutOpen(0xff, 0, 0, nullptr);
    Log("VideoOut open: %#x\n", video);
    if (video < 0) return false;
    off_t physical{};
    int status = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), bytes,
        alignment, ORBIS_KERNEL_WC_GARLIC, &physical);
    Log("Framebuffer allocation: %#x\n", status);
    if (status != 0) { sceVideoOutClose(video); return false; }
    void* frame = nullptr;
    status = sceKernelMapDirectMemory(&frame, bytes,
        ORBIS_KERNEL_PROT_CPU_RW | ORBIS_KERNEL_PROT_GPU_READ | ORBIS_KERNEL_PROT_GPU_WRITE,
        0, physical, alignment);
    Log("Framebuffer map: %#x\n", status);
    if (status != 0) {
        sceKernelReleaseDirectMemory(physical, bytes);
        sceVideoOutClose(video);
        return false;
    }
    auto* pixels = static_cast<uint32_t*>(frame);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const bool border = x < 32 || y < 32 || x >= width - 32 || y >= height - 32;
            pixels[y * width + x] = border ? 0xffffffff : passed ? 0xff208020 : 0xff2020c0;
        }
    }
    OrbisVideoOutBufferAttribute attributes{};
    sceVideoOutSetBufferAttribute(&attributes, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
        ORBIS_VIDEO_OUT_TILING_MODE_LINEAR, 0, width, height, width);
    const int buffer_set = sceVideoOutRegisterBuffers(video, 0, &frame, 1, &attributes);
    Log("Framebuffer registration: %#x\n", buffer_set);
    bool displayed = false;
    if (buffer_set >= 0) {
        status = sceVideoOutSubmitFlip(video, 0, 1, 1);
        Log("Display flip: %#x; expected screen=%s with white border\n",
            status, passed ? "green" : "red");
        displayed = status == 0;
        sleep(12);
    }
    // Close the output before releasing the buffer it may still be scanning out.
    sceVideoOutClose(video);
    sceKernelMunmap(frame, bytes);
    sceKernelReleaseDirectMemory(physical, bytes);
    return displayed;
}
} // namespace

int main() {
    mkdir("/data/EdenPS4", 0777);
    log_fd = open("/data/EdenPS4/hardware-check.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    for (const char* path : {"/mnt/usb0/EdenPS4-hardware-check.log",
                              "/mnt/usb1/EdenPS4-hardware-check.log"}) {
        usb_fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (usb_fd >= 0) break;
    }
    Log("\nEden4 Diagnostics 00.01 - platform checks only, no Switch emulation\n");
    Log("Build: " __DATE__ " " __TIME__ "; USB logging=%s\n", usb_fd >= 0 ? "yes" : "no");
    Log("Page size=%ld; CPUs=%ld; Pro mode=%d\n", sysconf(_SC_PAGESIZE),
        sysconf(_SC_NPROCESSORS_ONLN), sceKernelIsNeoMode());
    unsigned eax{}, ebx{}, ecx{}, edx{};
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
        Log("CPU feature bits: ecx=%#x, edx=%#x\n", ecx, edx);
    const int system = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    const int user = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    const int video = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT);
    Log("Module load: system=%#x user=%#x video=%#x\n", system, user, video);
    CheckMemory();
    const bool threads = CheckThreads();
    const bool jit = CheckJit();
    bool display = false;
    if (user >= 0 && video >= 0) {
        Log("UserService init: %#x\n", sceUserServiceInitialize(nullptr));
        display = ShowResult(threads && jit);
    }
    Log("COMPLETE: threads=%s JIT=%s video-submit=%s\n", threads ? "PASS" : "FAIL",
        jit ? "PASS" : "FAIL", display ? "PASS" : "FAIL");
    Log("These checks do not test Eden, Vulkan, game loading, or game performance.\n");
    if (usb_fd >= 0) close(usb_fd);
    if (log_fd >= 0) close(log_fd);
    if (system >= 0) sceSystemServiceLoadExec("exit", nullptr);
    return 0;
}
