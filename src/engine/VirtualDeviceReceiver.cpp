#include "engine/VirtualDeviceReceiver.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace spacer {

namespace {
double steadySeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace

VirtualDeviceReceiver::VirtualDeviceReceiver(std::string sharedMemoryName)
    : name_(std::move(sharedMemoryName)) {
    prepare(vdev::kDefaultSampleRate, 512);
    message_ = "Spacer virtual device not checked yet";
}

VirtualDeviceReceiver::~VirtualDeviceReceiver() {
    retire();
    retired_.clear();  // the audio thread must not be running any more
}

void VirtualDeviceReceiver::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : vdev::kDefaultSampleRate;
    maxBlock_ = std::max(1, maxBlockSize);
    scratchFrames_ = static_cast<int>(std::ceil(maxBlock_ * kMaxNominalRatio * (1.0 + kMaxAdjust))) + 8;
    out_.assign(static_cast<std::size_t>(kNumChannels) * static_cast<std::size_t>(maxBlock_), 0.0f);
    scratch_.assign(static_cast<std::size_t>(kNumChannels) * static_cast<std::size_t>(scratchFrames_), 0.0f);
    for (std::size_t c = 0; c < static_cast<std::size_t>(kNumChannels); ++c) {
        outPtrs_[c] = out_.data() + c * static_cast<std::size_t>(maxBlock_);
        scratchPtrs_[c] = scratch_.data() + c * static_cast<std::size_t>(scratchFrames_);
    }
    for (auto& r : resamplers_) r.reset();
    current_ = nullptr;  // re-prime on the next pull
    priming_ = true;
    fillError_ = 0.0;
    integral_ = 0.0;
    playing_ = false;
    if (owned_) requestRate(*owned_);
}

void VirtualDeviceReceiver::setLatencyFrames(int frames) noexcept {
    latency_.store(std::clamp(frames, 16, static_cast<int>(vdev::kRingFrames / 4)),
                   std::memory_order_relaxed);
}

void VirtualDeviceReceiver::requestRate(Connection& c) const noexcept {
    c.reader.header()->requestedSampleRate.store(sampleRate_, std::memory_order_relaxed);
}

std::unique_ptr<VirtualDeviceReceiver::Connection> VirtualDeviceReceiver::openConnection(
    std::string& message, State& failState) const {
    std::string error;
    auto memory = vdev::SharedMemory::open(name_.c_str(), &error);
    if (!memory.valid()) {
        failState = State::Absent;
        message = "Spacer virtual device not found (driver not installed or not loaded)";
        return nullptr;
    }
    const auto check = vdev::checkHeader(memory.data(), memory.size());
    if (check == vdev::HeaderCheck::NotInitialised || check == vdev::HeaderCheck::TooSmall) {
        failState = State::Absent;
        message = "Spacer virtual device is starting";
        return nullptr;
    }
    if (check != vdev::HeaderCheck::Ok) {
        // magic and protocolVersion are at fixed offsets in every protocol version.
        const auto* h = vdev::headerOf(memory.data());
        failState = State::Incompatible;
        message = "Spacer driver speaks protocol " + std::to_string(h->protocolVersion) + ", this app " +
                  std::to_string(vdev::kProtocolVersion) + " (" + vdev::toString(check) +
                  "): install the matching driver";
        return nullptr;
    }
    auto c = std::make_unique<Connection>();
    c->reader = vdev::RingReader(memory.data());
    c->sessionId = vdev::headerOf(memory.data())->sessionId;
    c->memory = std::move(memory);
    return c;
}

void VirtualDeviceReceiver::install(std::unique_ptr<Connection> connection) {
    retire();
    auto* h = connection->reader.header();
    requestRate(*connection);
    h->consumerProtocolVersion.store(vdev::kProtocolVersion, std::memory_order_relaxed);
    lastHeartbeat_ = h->heartbeat.load(std::memory_order_relaxed);
    owned_ = std::move(connection);
    active_.store(owned_.get());
    state_ = State::Connected;
    message_ = "Spacer driver " + std::string(h->driverVersion);
}

void VirtualDeviceReceiver::retire() {
    if (!owned_) return;
    owned_->reader.header()->consumerProtocolVersion.store(0, std::memory_order_relaxed);
    active_.store(nullptr);
    retired_.push_back(std::move(owned_));
}

void VirtualDeviceReceiver::freeRetired() noexcept {
    const Connection* busy = inUse_.load();
    std::erase_if(retired_, [busy](const auto& c) { return c.get() != busy; });
}

bool VirtualDeviceReceiver::poll() {
    return poll(steadySeconds());
}

bool VirtualDeviceReceiver::poll(double now) {
    freeRetired();
    const State before = state_;
    std::string message;
    State failState = State::Absent;

    if (owned_) {
        auto* h = owned_->reader.header();
        h->consumerHeartbeat.fetch_add(1, std::memory_order_relaxed);
        const auto beat = h->heartbeat.load(std::memory_order_relaxed);
        if (beat != lastHeartbeat_) {
            lastHeartbeat_ = beat;
            heartbeatChanged_ = now;
            if (state_ == State::NotResponding) {
                state_ = State::Connected;
                message_ = "Spacer driver " + std::string(h->driverVersion);
            }
        } else if (state_ == State::Connected &&
                   now - heartbeatChanged_ > vdev::kHeartbeatTimeoutMs / 1000.0) {
            state_ = State::NotResponding;
            message_ = "Spacer driver not responding";
            nextRetry_ = now;
        }
        // While the driver is silent, look for a new instance (coreaudiod restart).
        if (state_ == State::NotResponding && now >= nextRetry_) {
            nextRetry_ = now + kRetrySeconds;
            auto fresh = openConnection(message, failState);
            if (!fresh) {
                if (failState == State::Absent) {
                    retire();
                    state_ = State::Absent;
                    message_ = message;
                }
            } else if (fresh->sessionId != owned_->sessionId) {
                install(std::move(fresh));
                heartbeatChanged_ = now;
            }
        }
    } else if (now >= nextRetry_) {
        nextRetry_ = now + kRetrySeconds;
        if (auto fresh = openConnection(message, failState)) {
            install(std::move(fresh));
            heartbeatChanged_ = now;
        } else {
            state_ = failState;
            message_ = message;
        }
    }
    return state_ != before;
}

VirtualDeviceReceiver::Status VirtualDeviceReceiver::status() const {
    Status s;
    s.state = state_;
    s.message = message_;
    s.latencyFrames = latency_.load(std::memory_order_relaxed);
    s.underrunFrames = underruns_.load(std::memory_order_relaxed);
    s.resyncs = resyncs_.load(std::memory_order_relaxed);
    s.playing = playing_.load(std::memory_order_relaxed);
    s.ratio = ratio_.load(std::memory_order_relaxed);
    s.fillFrames = fill_.load(std::memory_order_relaxed);
    if (owned_) {
        const auto* h = owned_->reader.header();
        s.driverVersion = h->driverVersion;
        s.protocolVersion = h->protocolVersion;
        s.driverSampleRate = h->sampleRate.load(std::memory_order_relaxed);
        s.driverRunning = h->ioRunning.load(std::memory_order_relaxed) != 0;
        s.overrunFrames = h->overruns.load(std::memory_order_relaxed);
    }
    return s;
}

void VirtualDeviceReceiver::silence(int from, int numSamples) noexcept {
    if (numSamples <= from) return;
    for (auto* p : outPtrs_) std::fill(p + from, p + numSamples, 0.0f);
}

void VirtualDeviceReceiver::pull(int numSamples) noexcept {
    pull(numSamples, vdev::monotonicNowNs());
}

void VirtualDeviceReceiver::pull(int numSamples, std::uint64_t nowNs) noexcept {
    const int n = std::clamp(numSamples, 0, maxBlock_);
    if (n == 0) return;

    // Hazard pointer: announce the connection, then check it is still active.
    Connection* c = active_.load();
    inUse_.store(c);
    if (c != active_.load()) c = nullptr;

    auto finish = [&](bool playing) {
        playing_.store(playing, std::memory_order_relaxed);
        inUse_.store(nullptr);
    };

    if (c == nullptr) {
        current_ = nullptr;
        silence(0, n);
        return finish(false);
    }
    if (c != current_) {
        current_ = c;
        priming_ = true;
    }

    auto& reader = c->reader;
    auto& h = *reader.header();
    const double driverRate = h.sampleRate.load(std::memory_order_relaxed);
    const double nominal = driverRate > 0.0 ? driverRate / sampleRate_ : 1.0;
    if (!(nominal >= 1.0 / kMaxNominalRatio && nominal <= kMaxNominalRatio)) {
        silence(0, n);
        return finish(false);
    }

    // The sender writes whole buffers, so the ring's fill moves in steps of
    // its buffer size. Frames it has produced since its last write (by the
    // protocol clock) are counted too, which makes the fill continuous and the
    // controller insensitive to where the writes fall relative to our reads.
    std::uint32_t avail = reader.available();
    const auto chunk = h.lastWriteFrames.load(std::memory_order_relaxed);
    const std::uint64_t writtenAt = h.lastWriteHostTimeNs.load(std::memory_order_relaxed);
    double pending = 0.0;
    if (writtenAt == 0)
        pending = 0.5 * chunk;
    else if (nowNs > writtenAt)
        pending =
            std::min(static_cast<double>(chunk), static_cast<double>(nowNs - writtenAt) * 1e-9 * driverRate);
    fill_.store(static_cast<int>(avail), std::memory_order_relaxed);
    const int latency = std::max(latency_.load(std::memory_order_relaxed), static_cast<int>(chunk));
    const double target = n * nominal + latency + kSafetyFrames;
    auto keepFrames = [&] { return static_cast<std::uint32_t>(std::max(0.0, std::ceil(target - pending))); };

    if (priming_) {
        if (avail + pending < target) {
            silence(0, n);
            return finish(false);
        }
        reader.skipToLatest(keepFrames());
        avail = reader.available();
        priming_ = false;
        fillError_ = 0.0;
        integral_ = 0.0;
        for (auto& r : resamplers_) r.reset();
    } else if (avail + pending > target + vdev::kRingFrames / 2) {
        // Far behind (the callback stalled): drop the backlog.
        reader.skipToLatest(keepFrames());
        avail = reader.available();
        fillError_ = 0.0;
        resyncs_.fetch_add(1, std::memory_order_relaxed);
    }

    // PI controller on the smoothed fill error (frames). The integral removes
    // the steady-state offset a constant drift would otherwise leave, so the
    // fill sits on target and the safety margin stays intact.
    const double dt = n / sampleRate_;
    const double alpha = 1.0 - std::exp(-dt / kFillSmoothingSeconds);
    fillError_ += alpha * ((avail + pending - target) - fillError_);
    const double scale = 1.0 / (sampleRate_ * kCorrectionSeconds);
    integral_ = std::clamp(integral_ + fillError_ * scale * dt / kIntegralSeconds, -kMaxAdjust, kMaxAdjust);
    const double adjust = std::clamp(fillError_ * scale + integral_, -kMaxAdjust, kMaxAdjust);
    const double ratio = nominal * (1.0 + adjust);
    const auto need = static_cast<std::uint32_t>(std::ceil(n * ratio)) + 2;

    if (need > avail) {
        if (h.ioRunning.load(std::memory_order_relaxed) != 0) {
            reader.addUnderrun(static_cast<std::uint32_t>(n));
            underruns_.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);
        }
        priming_ = true;
        silence(0, n);
        return finish(false);
    }

    const auto frames = std::min(need, static_cast<std::uint32_t>(scratchFrames_));
    reader.peek(frames, scratchPtrs_.data(), static_cast<std::uint32_t>(kNumChannels));
    int consumed = 0;
    for (std::size_t ch = 0; ch < resamplers_.size(); ++ch)
        consumed = resamplers_[ch].process(ratio, scratchPtrs_[ch], outPtrs_[ch], n);
    reader.consume(static_cast<std::uint32_t>(std::clamp(consumed, 0, static_cast<int>(frames))));
    ratio_.store(ratio, std::memory_order_relaxed);
    finish(true);
}

}  // namespace spacer
