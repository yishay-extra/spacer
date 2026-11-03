// spacer-plugin-scanner: scans one plugin out of process for the Spacer app
// (SPEC §13, WP10). Bundled next to the app's executable; PluginHost runs
//   spacer-plugin-scanner --scan AudioUnit <identifier> <resultFile>
// for each plugin to scan. A plugin that crashes or hangs here only takes
// this process down; the app blacklists it (see PluginScanCache).

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_events/juce_events.h>

#include "engine/PluginScanProcess.h"

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::AudioPluginFormatManager formats;
#if JUCE_PLUGINHOST_AU && JUCE_MAC
    formats.addFormat(std::make_unique<juce::AudioUnitPluginFormatHeadless>());
#endif
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add(juce::String::fromUTF8(argv[i]));
    return spacer::runPluginScanProcess(formats, args);
}
