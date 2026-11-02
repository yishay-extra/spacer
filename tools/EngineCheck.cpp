// spacer-engine-check: manual validation tool for WP3 on a real Mac (see
// docs/mac-validation/WP3.md, WP4.md). Runs the real AudioEngine on a real device.
//
//   spacer-engine-check devices
//   spacer-engine-check tone  [--device NAME] [--seconds S] [--az DEG] [--rate HZ] [--buffer N]
//   spacer-engine-check pan   [--device NAME] [--seconds S]
//   spacer-engine-check delay [--device NAME] [--seconds S]
//   spacer-engine-check file PATH [--loop] [--seek-at S] [--device NAME] [--seconds S]     (WP4)
//   spacer-engine-check generator sine|pink|white|impulse [--az DEG] [--device NAME] [--seconds S]
//   spacer-engine-check input --channel N [--az DEG] [--device NAME] [--seconds S]
//   spacer-engine-check orbit [--period S] [--device NAME] [--seconds S]
//   spacer-engine-check aggregate-create NAME UID SUB_UID [SUB_UID...]
//   spacer-engine-check aggregate-destroy UID

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "core/Project.h"
#include "engine/BufferInput.h"
#include "engine/device/AggregateDevice.h"
#include "engine/device/AudioEngine.h"

using namespace spacer;

namespace {

struct Args {
    std::vector<std::string> positional;
    std::string device;
    double seconds{10.0};
    float az{-90.0f};
    double rate{0.0};
    int buffer{0};
    bool loop{false};
    double seekAt{-1.0};
    int channel{0};
    double period{4.0};
};

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        const bool hasValue = i + 1 < argc;
        if (s == "--device" && hasValue)
            a.device = argv[++i];
        else if (s == "--seconds" && hasValue)
            a.seconds = std::stod(argv[++i]);
        else if (s == "--az" && hasValue)
            a.az = std::stof(argv[++i]);
        else if (s == "--rate" && hasValue)
            a.rate = std::stod(argv[++i]);
        else if (s == "--buffer" && hasValue)
            a.buffer = std::stoi(argv[++i]);
        else if (s == "--loop")
            a.loop = true;
        else if (s == "--seek-at" && hasValue)
            a.seekAt = std::stod(argv[++i]);
        else if (s == "--channel" && hasValue)
            a.channel = std::stoi(argv[++i]);
        else if (s == "--period" && hasValue)
            a.period = std::stod(argv[++i]);
        else
            a.positional.push_back(s);
    }
    return a;
}

Speaker speaker(int id, float az, float dist, int channel) {
    Speaker s;
    s.id = id;
    s.name = id == 1 ? "L" : "R";
    s.position = {az, 0.0f, dist};
    s.outputChannel = channel;
    return s;
}

/// Speakers L at -90 / R at +90 on device channels 0 / 1, one sine source
/// at -20 dBFS. `nearLeft` puts L at 1 m (R at 2 m), so L is delayed 2.915 ms.
Project tonePlan(float az, bool nearLeft) {
    Project p;
    p.layout.name = "engine-check";
    p.layout.speakers = {speaker(1, -90.0f, nearLeft ? 1.0f : 2.0f, 0), speaker(2, 90.0f, 2.0f, 1)};
    SourceSpec s;
    s.id = 1;
    s.name = "tone";
    s.input.value = GeneratorSpec{};
    s.spatializer = SpatializerKind::Vbap;
    s.distance.k = 0.0f;
    s.motion.value = ManualMotion{{az, 0.0f, 2.0f}, 20.0f};
    if (nearLeft) {
        s.spatializer = SpatializerKind::Direct;
        s.directSpeakers = {1, 2};
    }
    p.sources = {s};
    return p;
}

void runFor(double seconds, AudioEngine& engine, const std::function<void(double)>& everyTick = {}) {
    const auto start = juce::Time::getMillisecondCounterHiRes();
    double nextPrint = 0.0;
    for (;;) {
        const double t = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
        if (t >= seconds) break;
        if (everyTick) everyTick(t);
        if (t >= nextPrint) {
            const auto s = engine.status();
            std::printf(
                "t=%5.1fs  %s  %.0f Hz / %d  out=%d  latency=%d+%d smp  cpu=%.1f%%  xruns=%d  dropped=%llu\n",
                t, s.deviceName.toRawUTF8(), s.sampleRate, s.bufferSize, s.outputChannels,
                s.deviceLatencySamples, s.graphLatencySamples, s.cpuLoad * 100.0, s.xruns,
                static_cast<unsigned long long>(s.droppedCommands));
            std::fflush(stdout);
            nextPrint += 1.0;
        }
#if JUCE_MODAL_LOOPS_PERMITTED
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
#else
        juce::Thread::sleep(20);
#endif
    }
}

int playTone(const Args& a, const std::string& mode) {
    AudioEngine engine;
    const bool nearLeft = mode == "delay";
    Project p = tonePlan(mode == "pan" ? -90.0f : a.az, nearLeft);
    DeviceMapping devices;
    devices.outputDeviceName = a.device;
    devices.sampleRate = a.rate;
    devices.bufferSize = a.buffer;

    const auto error = engine.openDevice(devices);
    if (error.isNotEmpty()) {
        std::fprintf(stderr, "cannot open device: %s\n", error.toRawUTF8());
        return 1;
    }
    // The tone is generated at the rate the device actually opened at.
    const double sr = engine.status().sampleRate > 0.0 ? engine.status().sampleRate : 48000.0;
    const int total = static_cast<int>(sr * (a.seconds + 1.0));
    engine.setInputFactory([sr, total](const SourceSpec&) -> std::unique_ptr<SourceInput> {
        juce::AudioBuffer<float> b(1, total);
        const float amp = juce::Decibels::decibelsToGain(-20.0f);
        for (int i = 0; i < total; ++i)
            b.setSample(
                0, i,
                amp * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr)));
        return std::make_unique<BufferInput>(std::move(b));
    });
    engine.setProject(p);

    std::printf("mode=%s: %s\n", mode.c_str(),
                mode == "pan"     ? "tone sweeps L -> R -> L, 4 s per cycle"
                : mode == "delay" ? "same tone on L (1 m, delayed 2.915 ms, -6 dB) and R (2 m)"
                                  : "tone at the given azimuth (default -90 = left only)");
    if (mode == "pan") {
        runFor(a.seconds, engine, [&engine](double t) {
            const float az =
                -90.0f * static_cast<float>(std::cos(2.0 * juce::MathConstants<double>::pi * t / 4.0));
            engine.send(cmd::SetSourcePosition{1, -1, {az, 0.0f, 2.0f}, 20.0f});
        });
    } else {
        runFor(a.seconds, engine);
    }
    return 0;
}

/// WP4: the engine's own live inputs (no input override): file player with
/// the disk thread, generators, hardware input, and an Orbit motion.
int playLive(const Args& a, const std::string& mode) {
    AudioEngine engine;
    Project p = tonePlan(a.az, false);
    SourceSpec& s = p.sources.front();
    int inputs = 0;
    if (mode == "file") {
        if (a.positional.size() < 2) {
            std::fprintf(stderr, "usage: file PATH [--loop] [--seek-at S]\n");
            return 2;
        }
        FileInputSpec f;
        f.path = juce::File::getCurrentWorkingDirectory()
                     .getChildFile(a.positional[1])
                     .getFullPathName()
                     .toStdString();
        f.loop = a.loop;
        s.input.value = f;
        s.stereo = true;
        s.width = 180.0f;  // file L / R on speakers L / R
        s.motion.value = ManualMotion{{0.0f, 0.0f, 2.0f}, 20.0f};
    } else if (mode == "generator") {
        const std::string kind = a.positional.size() > 1 ? a.positional[1] : "sine";
        GeneratorSpec g;
        g.type = kind == "pink"      ? GeneratorKind::Pink
                 : kind == "white"   ? GeneratorKind::White
                 : kind == "impulse" ? GeneratorKind::Impulse
                                     : GeneratorKind::Sine;
        g.periodSeconds = 0.5f;
        s.input.value = g;
    } else if (mode == "input") {
        HardwareInputSpec in;
        in.left = a.channel;
        s.input.value = in;
        s.gainDb = 0.0f;
        inputs = a.channel + 1;
    } else {  // orbit
        GeneratorSpec g;
        g.type = GeneratorKind::Pink;
        s.input.value = g;
        OrbitMotion o;
        o.centre = {0.0f, 0.0f, 0.0f};
        o.radius = 2.0f;
        o.period = Period::seconds(a.period);
        s.motion.value = o;
    }
    if (mode != "input") s.gainDb = -20.0f;

    DeviceMapping devices;
    devices.outputDeviceName = a.device;
    devices.sampleRate = a.rate;
    devices.bufferSize = a.buffer;
    const auto error = engine.openDevice(devices, inputs);
    if (error.isNotEmpty()) {
        std::fprintf(stderr, "cannot open device: %s\n", error.toRawUTF8());
        return 1;
    }
    double position = 0.0;
    unsigned underruns = 0;
    engine.onFeedback = [&](const Feedback& f) {
        if (const auto* t = std::get_if<fb::FileTransport>(&f)) {
            position = t->positionSeconds;
            underruns = t->underruns;
        }
    };
    engine.setProject(p);
    for (const auto& w : engine.buildWarnings()) std::fprintf(stderr, "warning: %s\n", w.c_str());
    if (mode == "file") engine.send(cmd::FilePlay{1});

    std::printf("mode=%s\n", mode.c_str());
    bool seeked = false;
    double nextPrint = 0.0;
    runFor(a.seconds, engine, [&](double t) {
        if (mode != "file") return;
        if (a.seekAt >= 0.0 && !seeked && t >= a.seekAt) {
            seeked = true;
            engine.send(cmd::FileSeek{1, 0.0});
            std::printf("t=%5.1fs  seek to 0\n", t);
        }
        if (t >= nextPrint) {
            std::printf("t=%5.1fs  file position %.3f s  underruns %u\n", t, position, underruns);
            nextPrint += 1.0;
        }
    });
    return 0;
}

int listDevices() {
    for (const auto& d : AggregateDevice::listDevices())
        std::printf("%5u  in=%-3d out=%-3d  %-40s  uid=%s\n", d.id, d.numInputs, d.numOutputs, d.name.c_str(),
                    d.uid.c_str());
    return 0;
}

int createAggregate(const Args& a) {
    if (a.positional.size() < 4) {
        std::fprintf(stderr, "usage: aggregate-create NAME UID SUB_UID [SUB_UID...]\n");
        return 2;
    }
    const std::vector<std::string> subs(a.positional.begin() + 3, a.positional.end());
    auto r = AggregateDevice::create(a.positional[1], a.positional[2], subs);
    if (!r) {
        std::fprintf(stderr, "%s\n", r.error().c_str());
        return 1;
    }
    std::printf("created aggregate device id %u\n", r.value());
    return 0;
}

int destroyAggregate(const Args& a) {
    if (a.positional.size() < 2) {
        std::fprintf(stderr, "usage: aggregate-destroy UID\n");
        return 2;
    }
    for (const auto& d : AggregateDevice::listDevices()) {
        if (d.uid != a.positional[1]) continue;
        const bool ok = AggregateDevice::destroy(d.id);
        std::printf("%s\n", ok ? "destroyed" : "destroy failed");
        return ok ? 0 : 1;
    }
    std::fprintf(stderr, "no device with uid %s\n", a.positional[1].c_str());
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const Args a = parse(argc, argv);
    const std::string command = a.positional.empty() ? "" : a.positional.front();
    if (command == "devices") return listDevices();
    if (command == "tone" || command == "pan" || command == "delay") return playTone(a, command);
    if (command == "file" || command == "generator" || command == "input" || command == "orbit")
        return playLive(a, command);
    if (command == "aggregate-create") return createAggregate(a);
    if (command == "aggregate-destroy") return destroyAggregate(a);
    std::fprintf(
        stderr,
        "usage: spacer-engine-check devices | tone | pan | delay | file | generator | input | orbit |\n"
        "                           aggregate-create | aggregate-destroy\n"
        "       options: --device NAME --seconds S --az DEG --rate HZ --buffer N --loop --seek-at S\n"
        "                --channel N --period S\n");
    return 2;
}
