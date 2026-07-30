#include "context_impl.hpp"
#include "internal.hpp"

#include "../utils.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

namespace infinicore {

thread_local Runtime *ContextImpl::current_runtime_ = nullptr;

Runtime *ContextImpl::getCurrentRuntime() {
    if (current_runtime_ == nullptr) {
        spdlog::debug("current_runtime_ is null, performing lazy initialization");
        // Lazy initialization: use the first available runtime
        // Try to find the first non-CPU device, fallback to CPU
        for (int i = int(Device::Type::COUNT) - 1; i > 0; i--) {
            if (!runtime_table_[i].empty() && runtime_table_[i][0] != nullptr) {
                current_runtime_ = runtime_table_[i][0].get()->activate();
                spdlog::debug("Lazy init: Set current_runtime_ to {} (ptr={})", current_runtime_->device().toString(), static_cast<void *>(current_runtime_));
                return current_runtime_;
            }
        }
        // Fallback to CPU runtime
        if (!runtime_table_[0].empty() && runtime_table_[0][0] != nullptr) {
            current_runtime_ = runtime_table_[0][0].get()->activate();
            spdlog::debug("Lazy init: Set current_runtime_ to {} (ptr={})", current_runtime_->device().toString(), static_cast<void *>(current_runtime_));
        }
    } else {
        spdlog::debug("getCurrentRuntime() returning {} (ptr={})", current_runtime_->device().toString(), static_cast<void *>(current_runtime_));
    }
    return current_runtime_;
}

void ContextImpl::setDevice(Device device) {
    if (device == getCurrentRuntime()->device()) {
        // Do nothing if the device is already set.
        return;
    }

    const bool recording = getCurrentRuntime()->isGraphRecording();
    thread_local bool warn_switch_runtime = false;
    if (recording && !warn_switch_runtime) {
        spdlog::warn("Switching device runtime during graph recording may break the graph!");
        warn_switch_runtime = true;
    }

    if (runtime_table_[int(device.getType())][device.getIndex()] == nullptr) {
        // Lazy initialization of runtime if never set before.
        runtime_table_[int(device.getType())][device.getIndex()] = std::unique_ptr<Runtime>(new Runtime(device));
        current_runtime_ = runtime_table_[int(device.getType())][device.getIndex()].get();
    } else {
        current_runtime_ = runtime_table_[int(device.getType())][device.getIndex()].get()->activate();
    }
    current_runtime_->flushDeferredPinnedHostFrees();
}

size_t ContextImpl::getDeviceCount(Device::Type type) {
    return runtime_table_[int(type)].size();
}

ContextImpl &ContextImpl::singleton() {
    static ContextImpl instance;
    return instance;
}

ContextImpl::ContextImpl() {
    std::vector<int> device_counter(static_cast<size_t>(Device::Type::COUNT));
    INFINICORE_CHECK_ERROR(infinirtGetAllDeviceCount(device_counter.data()));

    // Reserve runtime slot for all devices.
    runtime_table_[0].resize(device_counter[0]);
    runtime_table_[0][0] = std::unique_ptr<Runtime>(new Runtime(Device(Device::Type::CPU, 0)));

    // Context will try to use the first non-cpu available device as the default runtime.
    for (int i = int(Device::Type::COUNT) - 1; i > 0; i--) {
        if (device_counter[i] > 0) {
            runtime_table_[i].resize(device_counter[i]);
            if (current_runtime_ == nullptr) {
                runtime_table_[i][0] = std::unique_ptr<Runtime>(new Runtime(Device(Device::Type(i), 0)));
                current_runtime_ = runtime_table_[i][0].get();
            }
        }
    }

    if (current_runtime_ == nullptr) {
        current_runtime_ = runtime_table_[0][0].get();
    }
}

namespace context {

void setDevice(Device device) {
    ContextImpl::singleton().setDevice(device);
}

Device getDevice() {
    return ContextImpl::singleton().getCurrentRuntime()->device();
}

size_t getDeviceCount(Device::Type type) {
    return ContextImpl::singleton().getDeviceCount(type);
}

infinirtStream_t getStream() {
    return ContextImpl::singleton().getCurrentRuntime()->stream();
}

infiniopHandle_t getInfiniopHandle(Device device) {
    if (device != getDevice()) {
        setDevice(device);
    }
    return ContextImpl::singleton().getCurrentRuntime()->infiniopHandle();
}

void syncStream() {
    return ContextImpl::singleton().getCurrentRuntime()->syncStream();
}

void syncDevice() {
    return ContextImpl::singleton().getCurrentRuntime()->syncDevice();
}

void trimDeviceMemory() {
    return ContextImpl::singleton().getCurrentRuntime()->trimDeviceMemory();
}

void flushDeferredPinnedHostFrees() {
    return ContextImpl::singleton().getCurrentRuntime()->flushDeferredPinnedHostFrees();
}

std::shared_ptr<Memory> allocateMemory(size_t size) {
    return ContextImpl::singleton().getCurrentRuntime()->allocateMemory(size);
}

std::shared_ptr<Memory> allocateHostMemory(size_t size) {
    setDevice(Device::cpu());
    return allocateMemory(size);
}

std::shared_ptr<Memory> allocatePinnedHostMemory(size_t size) {
    return ContextImpl::singleton().getCurrentRuntime()->allocatePinnedHostMemory(size);
}

void memcpyH2D(void *dst, const void *src, size_t size, bool async) {
    return ContextImpl::singleton().getCurrentRuntime()->memcpyH2D(dst, src, size, async);
}

void memcpyD2H(void *dst, const void *src, size_t size) {
    return ContextImpl::singleton().getCurrentRuntime()->memcpyD2H(dst, src, size);
}

void memcpyD2D(void *dst, const void *src, size_t size, bool async) {
    return ContextImpl::singleton().getCurrentRuntime()->memcpyD2D(dst, src, size, async);
}

void memcpyH2H(void *dst, const void *src, size_t size) {
    setDevice(Device::cpu());
    return ContextImpl::singleton().getCurrentRuntime()->memcpyD2D(dst, src, size);
}

// Timing API implementations
infinirtEvent_t createEvent() {
    return ContextImpl::singleton().getCurrentRuntime()->createEvent();
}

infinirtEvent_t createEventWithFlags(uint32_t flags) {
    return ContextImpl::singleton().getCurrentRuntime()->createEventWithFlags(flags);
}

void recordEvent(infinirtEvent_t event, infinirtStream_t stream) {
    ContextImpl::singleton().getCurrentRuntime()->recordEvent(event, stream);
}

bool queryEvent(infinirtEvent_t event) {
    return ContextImpl::singleton().getCurrentRuntime()->queryEvent(event);
}

void synchronizeEvent(infinirtEvent_t event) {
    ContextImpl::singleton().getCurrentRuntime()->synchronizeEvent(event);
}

void destroyEvent(infinirtEvent_t event) {
    ContextImpl::singleton().getCurrentRuntime()->destroyEvent(event);
}

float elapsedTime(infinirtEvent_t start, infinirtEvent_t end) {
    return ContextImpl::singleton().getCurrentRuntime()->elapsedTime(start, end);
}

void streamWaitEvent(infinirtStream_t stream, infinirtEvent_t event) {
    ContextImpl::singleton().getCurrentRuntime()->streamWaitEvent(stream, event);
}

bool isGraphRecording() {
    return ContextImpl::singleton().getCurrentRuntime()->isGraphRecording();
}

void startGraphRecording() {
    ContextImpl::singleton().getCurrentRuntime()->startGraphRecording();
}

void addGraphOperator(std::shared_ptr<graph::GraphOperator> op) {
    ContextImpl::singleton().getCurrentRuntime()->addGraphOperator(op);
}

std::shared_ptr<graph::Graph> stopGraphRecording() {
    return ContextImpl::singleton().getCurrentRuntime()->stopGraphRecording();
}

namespace {
thread_local bool g_device_stream_capturing = false;
thread_local graph::CaptureArena *g_capture_arena = nullptr;
thread_local InferencePhase g_inference_phase = InferencePhase::Unknown;

bool env_truthy_(const char *name) {
    const char *v = std::getenv(name);
    return v != nullptr && v[0] != '\0' && std::string(v) != "0"
        && std::strcmp(v, "false") != 0 && std::strcmp(v, "no") != 0
        && std::strcmp(v, "off") != 0;
}

void warn_once_(bool &flag, const char *msg) {
    if (flag) {
        return;
    }
    flag = true;
    spdlog::warn("{}", msg);
}

/// Canonical ``INFINI_MOE_INGRAPH``: off | decode | prefill | both.
/// Legacy FORCE_CAPTURE / TRITON_CAPTURE → decode (one release shim).
enum class MoeIngraphMode : uint8_t { Off = 0, Decode, Prefill, Both };

MoeIngraphMode moeIngraphMode_() {
    const char *raw = std::getenv("INFINI_MOE_INGRAPH");
    if (raw != nullptr && raw[0] != '\0') {
        std::string s(raw);
        for (char &c : s) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (s == "off" || s == "0" || s == "false" || s == "no") {
            return MoeIngraphMode::Off;
        }
        if (s == "decode") {
            return MoeIngraphMode::Decode;
        }
        if (s == "prefill") {
            return MoeIngraphMode::Prefill;
        }
        if (s == "both") {
            return MoeIngraphMode::Both;
        }
        static bool unknown_warned = false;
        warn_once_(unknown_warned,
                   "INFINI_MOE_INGRAPH has unknown value (expected "
                   "off|decode|prefill|both); treating as off");
        return MoeIngraphMode::Off;
    }
    // Legacy: FORCE_CAPTURE / TRITON_CAPTURE → Decode-only (preserve old semantics).
    if (env_truthy_("INFINI_MOE_FORCE_CAPTURE")
        || env_truthy_("INFINI_MOE_TRITON_CAPTURE")) {
        static bool legacy_warned = false;
        warn_once_(
            legacy_warned,
            "INFINI_MOE_FORCE_CAPTURE / INFINI_MOE_TRITON_CAPTURE are deprecated; "
            "shim to INFINI_MOE_INGRAPH=decode. Prefer INFINI_MOE_INGRAPH="
            "decode|prefill|both (+ INFINI_MOE_METAX_INGRAPH_UNSAFE=1 on MetaX)");
        return MoeIngraphMode::Decode;
    }
    return MoeIngraphMode::Off;
}

bool metaxMoeIngraphUnsafe_() {
#if defined(ENABLE_METAX_API)
    if (env_truthy_("INFINI_MOE_METAX_INGRAPH_UNSAFE")) {
        return true;
    }
    if (env_truthy_("INFINI_MOE_METAX_CAPTURE_UNSAFE")) {
        static bool warned = false;
        warn_once_(
            warned,
            "INFINI_MOE_METAX_CAPTURE_UNSAFE is deprecated; use "
            "INFINI_MOE_METAX_INGRAPH_UNSAFE=1");
        return true;
    }
    return false;
#else
    return true;
#endif
}

void warnCaptureSafeIfSet_() {
    if (!env_truthy_("INFINI_MOE_CAPTURE_SAFE")) {
        return;
    }
    static bool warned = false;
    warn_once_(
        warned,
        "INFINI_MOE_CAPTURE_SAFE is hard-removed (aten MoE body deleted Phase 1); "
        "ignored. Use INFINI_MOE_INGRAPH + Triton, or host-break");
}
} // namespace

void setInferencePhase(InferencePhase phase) {
    g_inference_phase = phase;
}

InferencePhase getInferencePhase() {
    return g_inference_phase;
}

const char *cudagraphPolicy() {
    const char *v = std::getenv("INFINI_CUDAGRAPH_POLICY");
    if (v == nullptr || v[0] == '\0') {
        return "";
    }
    const std::string s(v);
    if (s == "eager") {
        return "eager";
    }
    if (s == "full_and_piecewise") {
        return "full_and_piecewise";
    }
    // Reject unknown / track_b — fall back to legacy (empty).
    return "";
}

bool faInGraphAllowed() {
    // Phase-agnostic diagnose GraphOp. Prod keeps FA host-break.
    // Canonical: INFINI_FA_INGRAPH; legacy INFINI_FA_FORCE_CAPTURE shims one release.
    if (env_truthy_("INFINI_FA_INGRAPH")) {
        return true;
    }
    if (env_truthy_("INFINI_FA_FORCE_CAPTURE")) {
        static bool warned = false;
        warn_once_(
            warned,
            "INFINI_FA_FORCE_CAPTURE is deprecated; use INFINI_FA_INGRAPH=1");
        return true;
    }
    return false;
}

bool moeTritonCaptureAllowed() {
    warnCaptureSafeIfSet_();
    // Bisect escape: force MoE host-break even under Decode FULL capture.
    if (env_truthy_("INFINI_MOE_FORCE_HOST_BREAK")) {
        return false;
    }
    // Eager policy never captures MoE into device graphs.
    if (std::strcmp(cudagraphPolicy(), "eager") == 0) {
        return false;
    }
    // MetaX wall: MoE-in-graph remains unsafe-by-default (hcGraph garble history).
    if (!metaxMoeIngraphUnsafe_()) {
        return false;
    }
    const MoeIngraphMode mode = moeIngraphMode_();
    const InferencePhase phase = getInferencePhase();
    switch (mode) {
    case MoeIngraphMode::Off:
        return false;
    case MoeIngraphMode::Decode:
        return phase == InferencePhase::Decode;
    case MoeIngraphMode::Prefill:
        return phase == InferencePhase::Prefill;
    case MoeIngraphMode::Both:
        return phase == InferencePhase::Prefill || phase == InferencePhase::Decode;
    }
    return false;
}

bool isDeviceStreamCapturing() {
    return g_device_stream_capturing;
}

void setDeviceStreamCapturing(bool capturing) {
    g_device_stream_capturing = capturing;
    // Mirror to env so Infiniop (separate DSO) can force capture-safe PagedAttention
    // dispatch (no split-kv multi-launch / hcGetDevice) while the stream is capturing.
    setenv("INFINI_DEVICE_STREAM_CAPTURING", capturing ? "1" : "0", 1);
}

graph::CaptureArena *currentCaptureArena() {
    return g_capture_arena;
}

void setCurrentCaptureArena(graph::CaptureArena *arena) {
    g_capture_arena = arena;
}

std::shared_ptr<Memory> reinstantiateBlob(std::shared_ptr<Memory> blob) {
    setDevice(blob->device());
    return ContextImpl::singleton().getCurrentRuntime()->reinstantiateBlob(blob);
}

} // namespace context

} // namespace infinicore
