#pragma once

#include <cstdint>

namespace spacer {

/// Small deterministic pseudo-random generator (SplitMix64) for motions and
/// modulators (SPEC §10 determinism rule). The output sequence for a given
/// seed is identical on every platform and compiler, which the std::
/// distributions do not guarantee. Real-time safe: no allocation, no state
/// beyond one 64-bit word.
class Rng {
public:
    explicit Rng(std::uint64_t seed = 1) : state_(seed) {}

    void reseed(std::uint64_t seed) { state_ = seed; }

    std::uint64_t next() {
        state_ += kGolden;
        return mix(state_);
    }

    /// Uniform in [0, 1).
    double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

    /// Uniform in [lo, hi).
    float uniform(float lo, float hi) { return lo + (hi - lo) * static_cast<float>(uniform()); }

    /// Uniform integer in [0, n); 0 when n == 0.
    std::uint32_t below(std::uint32_t n) {
        if (n == 0) return 0;
        const auto r = static_cast<std::uint32_t>(uniform() * static_cast<double>(n));
        return r < n ? r : n - 1;
    }

    /// Stateless hash of (seed, index) to a uniform value in [0, 1). Used where
    /// a random value must depend only on an absolute position in time.
    static double hashUniform(std::uint64_t seed, std::int64_t index) {
        const std::uint64_t h = mix(mix(seed + kGolden) ^ (static_cast<std::uint64_t>(index) * kGolden));
        return static_cast<double>(h >> 11) * 0x1.0p-53;
    }

private:
    static constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ull;

    static std::uint64_t mix(std::uint64_t z) {
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    std::uint64_t state_;
};

}  // namespace spacer
