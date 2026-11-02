#include "control/MidiMapper.h"

#include <algorithm>
#include <cmath>

#include "control/ControlPath.h"

namespace spacer {

namespace {

float apply(const MidiMapping& m, float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    if (m.curve == MidiCurve::Log && m.min > 0.0f && m.max > 0.0f) return m.min * std::pow(m.max / m.min, x);
    return m.min + x * (m.max - m.min);
}

}  // namespace

void MidiMapper::setConfig(const MidiConfig& config) {
    config_ = config;
    channels_ = {};
    learn_.reset();
}

void MidiMapper::startLearn(std::string parameterPath, LearnDefaults defaults) {
    learn_ = Learn{std::move(parameterPath), defaults};
}

void MidiMapper::emit(const MidiMapping& m, float normalised, std::vector<ControlEvent>& out) {
    if (auto e = controlpath::makeEvent(m.parameterPath, apply(m, normalised)))
        out.push_back(std::move(*e));
    else
        ++stats_.invalidPath;
}

bool MidiMapper::capture(MidiMessageType type, int channel, int number) {
    if (!learn_) return false;
    MidiMapping m;
    m.type = type;
    m.channel = channel;
    m.number = number;
    m.parameterPath = learn_->path;
    m.min = learn_->defaults.min;
    m.max = learn_->defaults.max;
    m.curve = learn_->defaults.curve;
    std::erase_if(config_.mappings, [&](const MidiMapping& o) { return o.parameterPath == m.parameterPath; });
    config_.mappings.push_back(m);
    learn_.reset();
    if (onLearned) onLearned(m);
    return true;
}

/// Applies every mapping of `type` on `channel` / `number` (the MSB number for
/// Cc14). True if at least one matched.
bool MidiMapper::control(MidiMessageType type, int channel, int number, float normalised,
                         std::vector<ControlEvent>& out) {
    bool mapped = false;
    for (const auto& m : config_.mappings) {
        if (m.type != type || m.number != number) continue;
        if (m.channel != 0 && m.channel != channel) continue;
        mapped = true;
        emit(m, normalised, out);
    }
    return mapped;
}

void MidiMapper::handleController(int channel, int number, int value, std::vector<ControlEvent>& out) {
    auto& st = channels_[static_cast<std::size_t>(channel - 1)];
    auto& n = st.nrpn;

    // NRPN selection and data entry.
    if (number == 99) {
        n = Nrpn{value, -1, false, 0, 0};
        return;
    }
    if (number == 98) {
        n.lsb = value;
        n.selected = n.msb >= 0;
        n.dataMsb = n.dataLsb = 0;
        return;
    }
    if (number == 101 || number == 100) {  // RPN selection cancels NRPN
        n = Nrpn{};
        return;
    }
    if ((number == 6 || number == 38) && n.selected) {
        (number == 6 ? n.dataMsb : n.dataLsb) = value;
        const int id = (n.msb << 7) | n.lsb;
        if (capture(MidiMessageType::Nrpn, channel, id)) return;
        const float x = static_cast<float>((n.dataMsb << 7) | n.dataLsb) / 16383.0f;
        if (!control(MidiMessageType::Nrpn, channel, id, x, out)) ++stats_.unmapped;
        return;
    }

    if (capture(MidiMessageType::Cc, channel, number)) return;

    bool mapped = control(MidiMessageType::Cc, channel, number, static_cast<float>(value) / 127.0f, out);
    if (number < 64) {
        const int msbNumber = number < 32 ? number : number - 32;
        const auto k = static_cast<std::size_t>(msbNumber);
        (number < 32 ? st.msb : st.lsb)[k] = value;
        const int v14 = (st.msb[k] << 7) | st.lsb[k];
        mapped |= control(MidiMessageType::Cc14, channel, msbNumber, static_cast<float>(v14) / 16383.0f, out);
    }
    if (!mapped) ++stats_.unmapped;
    if (value > 0) out.push_back(ev::MidiTrigger{ev::MidiTrigger::Type::Cc, number, channel});
}

void MidiMapper::handle(const juce::MidiMessage& m, std::vector<ControlEvent>& out) {
    ++stats_.messages;
    const int channel = m.getChannel();
    if (channel < 1 || channel > 16) return;  // system messages

    if (m.isNoteOn()) {
        out.push_back(ev::MidiTrigger{ev::MidiTrigger::Type::Note, m.getNoteNumber(), channel});
    } else if (m.isProgramChange()) {
        if (config_.programChangeChannel == channel)
            out.push_back(ev::RecallSceneIndex{m.getProgramChangeNumber(), 0.0f});
    } else if (m.isPitchWheel()) {
        if (capture(MidiMessageType::PitchBend, channel, 0)) return;
        if (!control(MidiMessageType::PitchBend, channel, 0,
                     static_cast<float>(m.getPitchWheelValue()) / 16383.0f, out))
            ++stats_.unmapped;
    } else if (m.isController()) {
        handleController(channel, m.getControllerNumber(), m.getControllerValue(), out);
    }
}

}  // namespace spacer
