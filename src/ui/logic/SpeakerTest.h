#pragma once

// Speaker test ("click a speaker to solo pink noise", SPEC §16 items 1 and
// 4). The test never touches the model: the app builds the engine graph from
// speakerTestProject() instead of the project while a test is running, and
// from the project again when it stops. Plain C++, unit-tested on Linux.

#include "core/Project.h"

namespace spacer {

namespace speakertest {

/// Level of the test noise (source gain, dB).
inline constexpr float kNoiseGainDb = -18.0f;
inline constexpr const char* kSourceName = "Speaker test";

/// `project` with every source muted and a pink-noise Generator source
/// routed Direct to `speakerId` (SPEC §6.4, §8). If the project already has
/// kMaxSources sources the last one is dropped to make room (it is muted
/// anyway). Returns `project` unchanged if the speaker does not exist.
Project speakerTestProject(const Project& project, int speakerId);

/// The id the test source gets in speakerTestProject().
int testSourceId(const Project& project);

}  // namespace speakertest

}  // namespace spacer
