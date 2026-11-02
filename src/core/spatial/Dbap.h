#pragma once

// Distance-based amplitude panning (SPEC §6.2, Lossius & Neukom). Works with
// any layout: no hull, no triangulation. Speakers with `lfe: true` are never
// panned (SPEC §7): they take no part in the gains or the normalisation and
// are never written to by render().

#include <span>
#include <string_view>
#include <vector>

#include "core/Position.h"
#include "core/Spatializer.h"

namespace spacer {

class Dbap final : public Spatializer {
public:
    /// `rolloffDb` is the rolloff `a` of SPEC §6.2 (default 6).
    explicit Dbap(float rolloffDb = 6.0f);

    std::string_view name() const override { return "DBAP"; }

    /// Copies the speaker positions and LFE flags; allocates the per-speaker
    /// gain buffers. Not real-time safe.
    void prepare(const Layout& layout, double sampleRate, int maxBlockSize) override;

    /// Gains are computed at the start and end positions and interpolated
    /// linearly per sample (SPEC §6). Allocation-free.
    void render(const float* mono, int numSamples, const Position& start, const Position& end, float spread,
                SpeakerBus& bus) override;

    void gainsFor(const Position& pos, float spread, std::span<float> outGains) const override;

    /// Spatial blur r_s in metres for a spread value: 0.2 + 3.0·spread (SPEC §6.2).
    static float blurFor(float spread);

    float rolloffDb() const { return rolloffDb_; }

private:
    struct Target {
        Cartesian position;  // metres, listener at the origin
        bool lfe{false};
    };

    /// Writes one gain per speaker index into `out` (zero for LFE speakers and
    /// for any index beyond `out.size()`). Allocation-free.
    void computeGains(const Position& pos, float spread, std::span<float> out) const;

    float rolloffDb_;
    std::vector<Target> targets_;
    std::vector<float> startGains_;
    std::vector<float> endGains_;
};

}  // namespace spacer
