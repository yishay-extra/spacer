// Real AudioUnits (macOS only): Apple's built-in effects are present on every
// Mac, including CI runners, so scanning and hosting are tested for real.
// AUDelay: type aufx, subtype dely, manufacturer appl.

#include <juce_core/juce_core.h>  // defines JUCE_MAC

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#if JUCE_MAC && JUCE_PLUGINHOST_AU

#include <cmath>

#include "EngineTestUtils.h"
#include "core/Json.h"
#include "engine/OfflineRenderer.h"
#include "engine/PluginHost.h"
#include "engine/PluginState.h"

using Catch::Approx;
using namespace spacer;
using namespace spacer::testing;

namespace {

const char* const kAUDelay = "AudioUnit:Effects/aufx,dely,appl";

struct TempDir {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getNonexistentChildFile("spacer-au-test", "", false);
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

PluginHost::Options options(const juce::File& dir, bool outOfProcess) {
    PluginHost::Options o;
    o.cacheDirectory = dir;
#ifdef SPACER_AU_PLUGIN_SCANNER
    if (outOfProcess) o.scannerExecutable = juce::File(SPACER_AU_PLUGIN_SCANNER);
#else
    juce::ignoreUnused(outOfProcess);
#endif
    return o;
}

/// Index of the AUDelay parameter whose name contains `word`, or -1.
int paramIndex(juce::AudioProcessor& p, const juce::String& word) {
    const auto& params = p.getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (params[i]->getName(64).containsIgnoreCase(word)) return i;
    return -1;
}

}  // namespace

TEST_CASE("AudioUnit: the AU format lists Apple's AUDelay") {
    juce::AudioPluginFormatManager formats;
    PluginHost::addPlatformFormats(formats);
    REQUIRE(formats.getNumFormats() == 1);
    TempDir tmp;
    PluginScanCache cache(tmp.dir);
    InProcessScanWorker worker(formats);
    PluginScanner scanner(formats, cache, worker);
    const auto ids = scanner.candidates(*formats.getFormat(0));
    CHECK(std::find(ids.begin(), ids.end(), juce::String(kAUDelay)) != ids.end());
}

#ifdef SPACER_AU_PLUGIN_SCANNER
TEST_CASE("AudioUnit: AUDelay scans out of process with spacer-plugin-scanner") {
    TempDir tmp;
    PluginHost host(options(tmp.dir, true));
    REQUIRE(host.scansOutOfProcess());
    const auto report = host.scanner().scanIdentifiers("AudioUnit", {kAUDelay});
    CHECK(report.scanned == 1);
    CHECK(report.blacklisted.empty());
    const auto d = host.cache().find(kAUDelay);
    REQUIRE(d);
    CHECK(d->manufacturerName.containsIgnoreCase("Apple"));
}
#endif

TEST_CASE("AudioUnit: AUDelay in a source slot renders aligned and its state round-trips") {
    TempDir tmp;
    PluginHost host(options(tmp.dir, false));
    Project p = makeProject({makeSpeaker(1, -90.0f, 2.0f, 0), makeSpeaker(2, 90.0f, 2.0f, 1)});
    for (int id : {1, 2}) {
        SourceSpec s = makeSource(id, {}, SpatializerKind::Direct);
        s.directSpeakers = {id};
        p.sources.push_back(s);
    }
    PluginSlot slot;
    slot.pluginId = kAUDelay;
    p.sources[0].inserts = {slot};

    // Load it once to find the parameters and set the delay to dry only.
    auto chain = host.buildChain(InsertOwner::Source, 1, p.sources[0].inserts, 1, 48000.0, 128);
    REQUIRE(chain->numSlots() == 1);
    REQUIRE_FALSE(chain->isMissing(0));
    auto& au = chain->plugin(0)->instance();
    const int mix = paramIndex(au, "wet");
    REQUIRE(mix >= 0);
    INFO("AUDelay latency " << chain->latencySamples());
    p.sources[0].inserts[0].params[mix] = 0.0f;  // 0 % wet: the input, after the AU's own latency

    // Offline render: both sources' impulses land on the same sample, whatever
    // AUDelay reports as its latency.
    juce::AudioBuffer<float> imp(1, 4800);
    imp.clear();
    imp.setSample(0, 2000, 0.5f);
    OfflineRenderOptions o;
    o.pluginHost = &host;
    const auto r = OfflineRenderer::render(p, {{1, imp}, {2, imp}}, 0.2, o);
    REQUIRE(r);
    const auto peak = [&](int ch) {
        int best = 0;
        for (int i = 1; i < r->audio.getNumSamples(); ++i)
            if (std::abs(r->audio.getSample(ch, i)) > std::abs(r->audio.getSample(ch, best))) best = i;
        return best;
    };
    CHECK(peak(0) == peak(1));
    CHECK(peak(1) == 2000 + r->latencySamples);
    CHECK(std::abs(r->audio.getSample(0, peak(0))) == Approx(0.5f).margin(0.01));

    // State: change the delay time, capture into the model, through JSON, and
    // into a new instance (another host = another session).
    const int time = paramIndex(au, "time");
    REQUIRE(time >= 0);
    au.getParameters()[time]->setValueNotifyingHost(0.37f);
    REQUIRE(host.captureState(p) >= 1);
    const Scene scene = Json(captureScene(p, "A")).get<Scene>();
    REQUIRE_FALSE(scene.sources[0].inserts[0].stateBase64.empty());

    TempDir tmp2;
    PluginHost other(options(tmp2.dir, false));
    PluginSlot restored = scene.sources[0].inserts[0];
    restored.params.clear();  // the state alone must carry the delay time
    auto again = other.buildChain(InsertOwner::Source, 1, {restored}, 1, 48000.0, 128);
    REQUIRE_FALSE(again->isMissing(0));
    CHECK(again->plugin(0)->instance().getParameters()[time]->getValue() == Approx(0.37f).margin(0.01));
}

TEST_CASE("AudioUnit: a project with an AudioUnit that is not installed still renders") {
    TempDir tmp;
    PluginHost host(options(tmp.dir, false));
    Project p = makeProject({makeSpeaker(1, 0.0f, 2.0f, 0)}, {makeSource(1, {0, 0, 2})});
    PluginSlot missing;
    missing.pluginId = "AudioUnit:Effects/aufx,zzzz,zzzz";
    p.sources[0].inserts = {missing};
    OfflineRenderOptions o;
    o.pluginHost = &host;
    REQUIRE(OfflineRenderer::render(p, {}, 0.05, o));
    const auto warnings = host.takeWarnings();
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("not found") != std::string::npos);
}

#endif
