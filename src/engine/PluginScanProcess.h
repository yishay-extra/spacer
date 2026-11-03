#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include "engine/PluginScanner.h"

namespace spacer {

/// The child-process side of out-of-process scanning, shared by the real
/// scanner tool (tools/PluginScanner.cpp) and the test scanner.
///
/// Arguments: `--scan <formatName> <identifier> <resultFile>`. Scans the
/// identifier with the named format of `formats` and writes the result file
/// (see writeScanResult). Returns the process exit code: 0 when a result was
/// written, 2 for bad arguments. A crash or hang writes nothing.
int runPluginScanProcess(juce::AudioPluginFormatManager& formats, const juce::StringArray& args);

/// Result file format (XML): <SCAN_RESULT status="ok|failed" message="...">
/// with one <PLUGIN .../> (PluginDescription::createXml) per plugin found.
bool writeScanResult(const PluginScanResult& result, const juce::File& file);
/// Parses a result file. `present` = the file exists; an unreadable file is Failed.
PluginScanResult readScanResult(const juce::File& file, bool& present);

/// Scans in this process (what the child does): findAllTypesForFile.
PluginScanResult scanInProcess(juce::AudioPluginFormatManager& formats, const juce::String& formatName,
                               const juce::String& identifier);

}  // namespace spacer
