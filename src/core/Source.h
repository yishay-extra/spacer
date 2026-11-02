#pragma once

// Source model (SPEC §4, §8). Pure data; JSON in Json.h.

#include <array>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/MotionSpec.h"
#include "core/PluginSlot.h"
#include "core/Position.h"

namespace spacer {

enum class SourceKind { VirtualInput, HardwareInput, File, Generator };
enum class SpatializerKind { Vbap, Dbap, Ambisonic, Direct };
enum class GeneratorKind { Sine, Pink, White, Impulse };

/// Channel selection for VirtualInput (0-63) and HardwareInput (device channel).
/// right < 0 means mono (JSON `{"channel": n}`); otherwise `{"left","right"}`.
struct ChannelInput {
    int left{0};
    int right{-1};
    bool isStereo() const { return right >= 0; }
    bool operator==(const ChannelInput&) const = default;
};

struct VirtualInputSpec : ChannelInput {
    bool operator==(const VirtualInputSpec&) const = default;
};
struct HardwareInputSpec : ChannelInput {
    bool operator==(const HardwareInputSpec&) const = default;
};

struct FileInputSpec {
    std::string path;
    bool relativeToProject{false};  // JSON `"relativeTo": "project"`
    bool loop{false};
    float gainDb{0.0f};
    float startSeconds{0.0f};
    bool playOnSceneRecall{false};
    bool operator==(const FileInputSpec&) const = default;
};

struct GeneratorSpec {
    GeneratorKind type{GeneratorKind::Sine};
    float hz{440.0f};           // Sine only
    float periodSeconds{1.0f};  // Impulse only
    bool operator==(const GeneratorSpec&) const = default;
};

struct InputSpec {
    std::variant<VirtualInputSpec, HardwareInputSpec, FileInputSpec, GeneratorSpec> value{VirtualInputSpec{}};
    SourceKind kind() const { return static_cast<SourceKind>(value.index()); }
    bool operator==(const InputSpec&) const = default;
};

/// SPEC §6.5. r_ref is fixed at 1 m.
struct DistanceModel {
    float k{1.0f};     // 0 disables distance attenuation
    float rMin{0.5f};  // metres
    float q{0.0f};     // low-pass exponent, 0 = off
    bool operator==(const DistanceModel&) const = default;
};

inline constexpr int kMaxInserts = 4;
inline constexpr int kMaxModulators = 4;

struct SourceSpec {
    int id{0};  // unique within a project (and within a scene), >= 1
    std::string name;
    InputSpec input;
    bool stereo{false};
    float width{60.0f};        // degrees between L/R when stereo (SPEC §8)
    bool linkedPoints{false};  // stereo: each channel has own motion
    float gainDb{0.0f};
    bool mute{false};
    SpatializerKind spatializer{SpatializerKind::Vbap};
    float spread{0.0f};               // 0..1
    std::vector<int> directSpeakers;  // SpatializerKind::Direct targets (speaker ids)
    DistanceModel distance;
    MotionSpec motion;
    std::optional<MotionSpec> motionRight;  // only used when linkedPoints
    std::vector<ModulatorSpec> modulators;  // <= kMaxModulators
    std::vector<PluginSlot> inserts;        // <= kMaxInserts

    SourceKind kind() const { return input.kind(); }
    bool operator==(const SourceSpec&) const = default;
};

}  // namespace spacer
