#include "core/clock/MtcDecoder.h"

namespace spacer {

namespace {
TimecodeRate rateFromBits(int bits) {
    return static_cast<TimecodeRate>(bits & 0x3);
}
}  // namespace

void MtcDecoder::reset() {
    inSet_ = false;
    expected_ = 0;
}

MtcDecoder::Result MtcDecoder::handle(const std::uint8_t* data, int size) {
    if (data == nullptr || size < 1) return {};
    if (data[0] == 0xF1 && size >= 2) return quarterFrame(data[1]);
    if (data[0] == 0xF0) {
        if (auto tc = parseFullFrame(data, size)) {
            reset();
            Result r;
            r.kind = Result::Kind::FullFrame;
            r.timecode = *tc;
            return r;
        }
    }
    return {};
}

MtcDecoder::Result MtcDecoder::quarterFrame(std::uint8_t dataByte) {
    const int piece = (dataByte >> 4) & 0x7;
    const auto nibble = static_cast<std::uint8_t>(dataByte & 0x0F);
    Result r;
    r.kind = Result::Kind::QuarterFrame;
    r.piece = piece;

    if (piece == 0) {
        inSet_ = true;
        expected_ = 0;
    }
    if (!inSet_ || piece != expected_) {
        inSet_ = false;
        return r;
    }
    nibbles_[static_cast<std::size_t>(piece)] = nibble;
    expected_ = piece + 1;
    if (piece == 7) {
        inSet_ = false;
        const auto n = [this](int i) { return static_cast<int>(nibbles_[static_cast<std::size_t>(i)]); };
        r.kind = Result::Kind::FullSet;
        r.timecode.frames = n(0) | ((n(1) & 0x1) << 4);
        r.timecode.seconds = n(2) | ((n(3) & 0x3) << 4);
        r.timecode.minutes = n(4) | ((n(5) & 0x3) << 4);
        r.timecode.hours = n(6) | ((n(7) & 0x1) << 4);
        r.timecode.rate = rateFromBits(n(7) >> 1);
    }
    return r;
}

std::optional<Timecode> MtcDecoder::parseFullFrame(const std::uint8_t* d, int size) {
    // F0 7F <device> 01 01 hr mn sc fr F7
    if (size < 10 || d[0] != 0xF0 || d[1] != 0x7F || d[3] != 0x01 || d[4] != 0x01 || d[9] != 0xF7)
        return std::nullopt;
    Timecode tc;
    tc.rate = rateFromBits(d[5] >> 5);
    tc.hours = d[5] & 0x1F;
    tc.minutes = d[6] & 0x3F;
    tc.seconds = d[7] & 0x3F;
    tc.frames = d[8] & 0x1F;
    return tc;
}

}  // namespace spacer
