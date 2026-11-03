#include "engine/PluginScanProcess.h"

namespace spacer {

namespace {

juce::AudioPluginFormat* findFormat(juce::AudioPluginFormatManager& formats, const juce::String& name) {
    for (auto* f : formats.getFormats())
        if (f->getName() == name) return f;
    return nullptr;
}

}  // namespace

PluginScanResult scanInProcess(juce::AudioPluginFormatManager& formats, const juce::String& formatName,
                               const juce::String& identifier) {
    PluginScanResult r;
    auto* format = findFormat(formats, formatName);
    if (format == nullptr) {
        r.message = "unknown plugin format '" + formatName.toStdString() + "'";
        return r;
    }
    juce::OwnedArray<juce::PluginDescription> found;
    format->findAllTypesForFile(found, identifier);
    r.status = PluginScanResult::Status::Ok;
    for (auto* d : found) r.found.push_back(*d);
    if (r.found.empty()) r.message = "no plugin found";
    return r;
}

bool writeScanResult(const PluginScanResult& result, const juce::File& file) {
    juce::XmlElement root("SCAN_RESULT");
    root.setAttribute("status", result.status == PluginScanResult::Status::Ok ? "ok" : "failed");
    root.setAttribute("message", juce::String(result.message));
    for (const auto& d : result.found) root.addChildElement(d.createXml().release());
    return root.writeTo(file);
}

PluginScanResult readScanResult(const juce::File& file, bool& present) {
    PluginScanResult r;
    present = file.existsAsFile();
    if (!present) return r;
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("SCAN_RESULT")) {
        r.message = "unreadable scan result";
        return r;
    }
    r.message = xml->getStringAttribute("message").toStdString();
    if (xml->getStringAttribute("status") != "ok") return r;
    r.status = PluginScanResult::Status::Ok;
    for (const auto* e : xml->getChildIterator()) {
        juce::PluginDescription d;
        if (d.loadFromXml(*e)) r.found.push_back(d);
    }
    return r;
}

int runPluginScanProcess(juce::AudioPluginFormatManager& formats, const juce::StringArray& args) {
    const int at = args.indexOf("--scan");
    if (at < 0 || at + 3 >= args.size()) return 2;
    const juce::String formatName = args[at + 1];
    const juce::String identifier = args[at + 2];
    const juce::File resultFile(args[at + 3]);

    const PluginScanResult result = scanInProcess(formats, formatName, identifier);
    // Write to a temporary file and move it into place, so a crash while
    // writing never leaves a half result that reads as "ok".
    const juce::File temp = resultFile.getSiblingFile(resultFile.getFileName() + ".part");
    if (!writeScanResult(result, temp)) return 3;
    return temp.moveFileTo(resultFile) ? 0 : 3;
}

}  // namespace spacer
