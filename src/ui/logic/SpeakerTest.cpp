#include "ui/logic/SpeakerTest.h"

#include <algorithm>

namespace spacer::speakertest {

int testSourceId(const Project& project) {
    int m = 0;
    for (const auto& s : project.sources) m = std::max(m, s.id);
    return m + 1;
}

Project speakerTestProject(const Project& project, int speakerId) {
    if (project.layout.findSpeaker(speakerId) == nullptr) return project;
    Project p = project;
    const int id = testSourceId(project);
    for (auto& s : p.sources) s.mute = true;
    if (p.sources.size() >= static_cast<std::size_t>(kMaxSources))
        p.sources.resize(static_cast<std::size_t>(kMaxSources - 1));

    SourceSpec test;
    test.id = id;
    test.name = kSourceName;
    test.input.value = GeneratorSpec{GeneratorKind::Pink, 440.0f, 1.0f};
    test.gainDb = kNoiseGainDb;
    test.spatializer = SpatializerKind::Direct;
    test.directSpeakers = {speakerId};
    test.distance.k = 0.0f;  // Direct ignores position anyway (SPEC §6.4, §6.5)
    if (const Speaker* sp = project.layout.findSpeaker(speakerId))
        test.motion.value = ManualMotion{sp->position};
    p.sources.push_back(test);
    return p;
}

}  // namespace spacer::speakertest
