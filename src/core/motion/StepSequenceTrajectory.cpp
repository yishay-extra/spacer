#include "core/motion/StepSequenceTrajectory.h"

#include <algorithm>

namespace spacer {

namespace {
constexpr double kBoundaryEpsilon = 1e-9;
}

StepSequenceTrajectory::StepSequenceTrajectory(const StepSequenceMotion& spec, const Layout* layout)
    : spec_(spec) {
    steps_.reserve(spec_.steps.size());
    for (const Step& s : spec_.steps) {
        ResolvedStep r;
        if (const auto* p = std::get_if<Position>(&s.target)) {
            r.target = p->toCartesian();
            r.usable = true;
        } else if (const Speaker* spk = layout ? layout->findSpeaker(std::get<int>(s.target)) : nullptr) {
            r.target = spk->position.toCartesian();
            r.usable = true;
        }
        r.usable = r.usable && s.probability > 0.0f;
        r.duration = s.duration;
        r.glide = s.transition == StepTransition::Glide;
        r.glideTime = s.glideTime;
        r.probability = s.probability;
        anyUsable_ = anyUsable_ || r.usable;
        steps_.push_back(r);
    }
    // Before the first evaluation the source sits on the first usable target.
    for (const auto& r : steps_)
        if (r.usable) {
            pos_ = r.target;
            break;
        }
    restart();
}

void StepSequenceTrajectory::restart() {
    rng_.reseed(spec_.seed);
    cur_ = 0;
    dir_ = 1;
    taken_ = 0;
    finished_ = !anyUsable_;
    elapsedBeats_ = elapsedSeconds_ = 0.0;
    stepStart_ = pos_;  // a glide into step 0 starts from where the source is
    if (anyUsable_) {
        if (steps_[0].usable) {
            taken_ = 1;
        } else {
            advance();
        }
    }
    if (!finished_) pos_ = positionInStep();
    current_ = Position::fromCartesian(pos_).normalised();
}

int StepSequenceTrajectory::nextCandidate() {
    const int n = static_cast<int>(steps_.size());
    switch (spec_.order) {
        case StepOrder::Forward:
            if (cur_ + 1 < n) return cur_ + 1;
            return spec_.loop ? 0 : -1;
        case StepOrder::PingPong: {
            if (n == 1) return spec_.loop ? 0 : -1;
            int next = cur_ + dir_;
            if (next >= n) {
                dir_ = -1;
                next = n - 2;
            } else if (next < 0) {
                if (!spec_.loop) return -1;
                dir_ = 1;
                next = 1;
            }
            return next;
        }
        case StepOrder::Random: {
            if (!spec_.loop && taken_ >= n) return -1;
            if (n == 1) return 0;
            const auto r = static_cast<int>(rng_.below(static_cast<std::uint32_t>(n - 1)));
            return r >= cur_ ? r + 1 : r;
        }
    }
    return -1;
}

void StepSequenceTrajectory::advance() {
    const int n = static_cast<int>(steps_.size());
    int lastUsable = -1;
    for (int attempt = 0; attempt < n; ++attempt) {
        const int next = nextCandidate();
        if (next < 0) {
            finished_ = true;
            return;
        }
        cur_ = next;
        const ResolvedStep& s = steps_[static_cast<std::size_t>(cur_)];
        // One draw per candidate, always, so the random stream does not depend
        // on which probabilities happen to be 1.
        const double roll = rng_.uniform();
        if (!s.usable) continue;
        lastUsable = cur_;
        if (roll < static_cast<double>(s.probability)) break;
        if (attempt == n - 1) break;  // n skips in a row: take this one anyway
    }
    if (lastUsable < 0) {
        finished_ = true;
        return;
    }
    cur_ = lastUsable;
    ++taken_;
}

Cartesian StepSequenceTrajectory::positionInStep() const {
    const ResolvedStep& s = steps_[static_cast<std::size_t>(cur_)];
    if (!s.glide || s.glideTime.value <= 0.0) return s.target;
    const double e = s.glideTime.unit == Period::Unit::Beats ? elapsedBeats_ : elapsedSeconds_;
    const auto f = static_cast<float>(std::clamp(e / s.glideTime.value, 0.0, 1.0));
    return stepStart_ + (s.target - stepStart_) * f;
}

Position StepSequenceTrajectory::compute(const MotionTime& t) {
    if (finished_) return Position::fromCartesian(pos_);

    double dB = std::max(0.0, t.deltaBeats);
    double dS = std::max(0.0, t.deltaSeconds);
    for (int i = 0; i < kMaxAdvances && !finished_; ++i) {
        const ResolvedStep& s = steps_[static_cast<std::size_t>(cur_)];
        const bool beats = s.duration.unit == Period::Unit::Beats;
        const double elapsed = beats ? elapsedBeats_ : elapsedSeconds_;
        const double remaining = std::max(0.0, s.duration.value - elapsed);
        const double d = beats ? dB : dS;
        // The tolerance keeps a boundary that falls exactly on a block end (up
        // to rounding in the accumulated deltas) in this block, not the next.
        const double tolerance = kBoundaryEpsilon * std::max(1.0, s.duration.value);
        if (d < remaining - tolerance) {
            elapsedBeats_ += dB;
            elapsedSeconds_ += dS;
            break;
        }
        // The step ends inside this block: consume the part of both deltas up
        // to the boundary, then move on with the rest.
        const double f = d > 0.0 ? std::min(1.0, remaining / d) : 0.0;
        elapsedBeats_ += dB * f;
        elapsedSeconds_ += dS * f;
        dB -= dB * f;
        dS -= dS * f;
        stepStart_ = positionInStep();
        elapsedBeats_ = elapsedSeconds_ = 0.0;
        advance();
        if (finished_) {
            pos_ = stepStart_;
            return Position::fromCartesian(pos_);
        }
    }
    pos_ = positionInStep();
    return Position::fromCartesian(pos_);
}

}  // namespace spacer
