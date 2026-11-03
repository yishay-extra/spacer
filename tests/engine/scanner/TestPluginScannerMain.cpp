// Out-of-process scanner for the test plugin format (WP10 tests): the same
// child-side code as tools/PluginScanner.cpp, with the SpacerTest format.

#include <juce_events/juce_events.h>

#include "TestPlugins.h"
#include "engine/PluginScanProcess.h"

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::AudioPluginFormatManager formats;
    spacer::testing::addTestFormat(formats);
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add(juce::String::fromUTF8(argv[i]));
    return spacer::runPluginScanProcess(formats, args);
}
