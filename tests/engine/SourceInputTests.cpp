#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "EngineTestUtils.h"
#include "engine/FilePlayer.h"
#include "engine/GeneratorInput.h"
#include "engine/HardwareInput.h"
#include "engine/SourceInputFactory.h"
#include "engine/VirtualInput.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {
constexpr double kSr = 48000.0;

std::vector<float> generate(GeneratorInput& g, int n, int channel = 0, int channels = 1) {
    std::vector<float> a(static_cast<std::size_t>(n)), b(static_cast<std::size_t>(n));
    float* d[] = {a.data(), b.data()};
    g.read({}, d, channels, n);
    return channel == 0 ? a : b;
}

GeneratorSpec gen(GeneratorKind k) {
    GeneratorSpec s;
    s.type = k;
    return s;
}
}  // namespace

TEST_CASE("Generator: sine has the given frequency, unit peak and starts at phase 0") {
    GeneratorSpec s = gen(GeneratorKind::Sine);
    s.hz = 1000.0f;
    GeneratorInput g(s);
    g.prepare(kSr, 512);
    std::vector<float> x;
    for (int b = 0; b < 100; ++b) {
        auto blk = generate(g, 480);
        x.insert(x.end(), blk.begin(), blk.end());
    }
    for (std::size_t i = 0; i < x.size(); i += 37)
        REQUIRE(x[i] == Approx(std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 *
                                        static_cast<double>(i) / kSr))
                            .margin(1e-5));
    CHECK(peakAbs(x.data(), static_cast<int>(x.size())) == Approx(1.0f).margin(1e-4));
}

TEST_CASE("Generator: impulse fires every periodSeconds") {
    GeneratorSpec s = gen(GeneratorKind::Impulse);
    s.periodSeconds = 0.01f;
    GeneratorInput g(s);
    g.prepare(kSr, 512);
    const auto x = generate(g, 2000);
    for (int i = 0; i < 2000; ++i) REQUIRE(x[static_cast<std::size_t>(i)] == (i % 480 == 0 ? 1.0f : 0.0f));
}

TEST_CASE("Generator: white noise is bounded, zero-mean and deterministic") {
    GeneratorInput a(gen(GeneratorKind::White)), b(gen(GeneratorKind::White));
    a.prepare(kSr, 48000);
    b.prepare(kSr, 48000);
    const auto x = generate(a, 48000), y = generate(b, 48000);
    CHECK(x == y);
    double mean = 0.0;
    for (float v : x) {
        REQUIRE(std::abs(v) <= 1.0f);
        mean += v;
    }
    CHECK(std::abs(mean / 48000.0) < 0.02);
    CHECK(rms(x.data(), 48000) == Approx(1.0 / std::sqrt(3.0)).epsilon(0.02));
}

TEST_CASE("Generator: pink noise is bounded and has much less high-frequency energy than white") {
    GeneratorInput p(gen(GeneratorKind::Pink));
    p.prepare(kSr, 96000);
    const auto x = generate(p, 96000);
    for (float v : x) REQUIRE(std::abs(v) <= 1.0f);
    // White noise: var(diff) / var = 2. Pink: concentrated at low frequencies.
    double var = 0.0, dvar = 0.0;
    for (std::size_t i = 1; i < x.size(); ++i) {
        var += static_cast<double>(x[i]) * x[i];
        const double d = x[i] - x[i - 1];
        dvar += d * d;
    }
    CHECK(dvar / var < 0.5);
    CHECK(rms(x.data(), 96000) > 0.05f);
}

TEST_CASE("Generator: a stereo source gets the same signal on both channels") {
    GeneratorInput g(gen(GeneratorKind::White));
    g.prepare(kSr, 256);
    std::vector<float> l(256), r(256);
    float* d[] = {l.data(), r.data()};
    g.read({}, d, 2, 256);
    CHECK(l == r);
}

TEST_CASE("Generator: graph swap continues the phase of the same generator") {
    GeneratorSpec s = gen(GeneratorKind::Sine);
    s.hz = 100.0f;
    GeneratorInput a(s), b(s);
    a.prepare(kSr, 512);
    b.prepare(kSr, 512);
    generate(a, 100);
    b.takeStateFrom(a);
    CHECK(generate(b, 1)[0] ==
          Approx(std::sin(2.0 * juce::MathConstants<double>::pi * 100.0 * 100 / kSr)).margin(1e-6));
}

TEST_CASE("HardwareInput: reads the selected device channels") {
    std::vector<float> c0(64, 0.1f), c1(64, 0.2f), c2(64, 0.3f);
    const float* ins[] = {c0.data(), c1.data(), c2.data()};
    const InputContext ctx{ins, 3};
    std::vector<float> l(64), r(64);
    float* d[] = {l.data(), r.data()};

    SECTION("mono") {
        HardwareInput in(ChannelInput{2, -1});
        in.read(ctx, d, 1, 64);
        CHECK(l[5] == 0.3f);
    }
    SECTION("stereo pair") {
        HardwareInput in(ChannelInput{1, 0});
        in.read(ctx, d, 2, 64);
        CHECK(l[5] == 0.2f);
        CHECK(r[5] == 0.1f);
    }
    SECTION("stereo selection into a mono source is averaged") {
        HardwareInput in(ChannelInput{0, 2});
        in.read(ctx, d, 1, 64);
        CHECK(l[5] == Approx(0.2f));
    }
    SECTION("mono selection into a stereo source goes to both sides") {
        HardwareInput in(ChannelInput{1, -1});
        in.read(ctx, d, 2, 64);
        CHECK(r[5] == 0.2f);
    }
    SECTION("a channel the device does not have is silent") {
        HardwareInput in(ChannelInput{7, -1});
        l.assign(64, 1.0f);
        in.read(ctx, d, 1, 64);
        CHECK(l[5] == 0.0f);
    }
}

TEST_CASE("SourceInputFactory: one input per kind; missing files are silent with a warning") {
    SourceInputFactory f;
    SourceSpec s;
    s.id = 3;
    s.input.value = GeneratorSpec{};
    CHECK(dynamic_cast<GeneratorInput*>(f.create(s).get()) != nullptr);
    s.input.value = HardwareInputSpec{};
    CHECK(dynamic_cast<HardwareInput*>(f.create(s).get()) != nullptr);
    s.input.value = VirtualInputSpec{};
    CHECK(dynamic_cast<VirtualInput*>(f.create(s).get()) != nullptr);

    FileInputSpec file;
    file.path = "/definitely/not/here.wav";
    s.input.value = file;
    std::vector<std::string> warnings;
    auto in = f.create(s, &warnings);
    REQUIRE(dynamic_cast<FilePlayer*>(in.get()) != nullptr);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("source 3") != std::string::npos);
    in->prepare(kSr, 128);
    in->play();
    std::vector<float> x(128, 1.0f);
    float* d[] = {x.data()};
    in->read({}, d, 1, 128);
    CHECK(x[0] == 0.0f);
}

TEST_CASE("SourceInputFactory: project-relative paths resolve against the project directory") {
    const auto dir = juce::File::createTempFile("spacer-paths");
    REQUIRE(dir.createDirectory());
    REQUIRE(dir.getChildFile("a.wav").create().wasOk());
    SourceInputFactory f;
    f.setProjectDirectory(dir);
    FileInputSpec spec;
    spec.path = "a.wav";
    spec.relativeToProject = true;
    CHECK(f.resolve(spec) == dir.getChildFile("a.wav"));
    // An absolute path that no longer exists falls back to the same name in the project directory.
    spec.path = "/moved/away/a.wav";
    spec.relativeToProject = false;
    CHECK(f.resolve(spec) == dir.getChildFile("a.wav"));
    dir.deleteRecursively();
}
