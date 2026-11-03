#include "app/VirtualDeviceInstaller.h"

#include <juce_events/juce_events.h>

#include "ui/logic/DriverInstall.h"

namespace spacer {

juce::File VirtualDeviceInstaller::bundledDriver() {
    return juce::File::getSpecialLocation(juce::File::currentApplicationFile)
        .getChildFile("Contents/Resources")
        .getChildFile(driverinstall::kBundleName);
}

juce::File VirtualDeviceInstaller::installedDriver() {
    return juce::File(driverinstall::kHalPlugInDirectory).getChildFile(driverinstall::kBundleName);
}

juce::String VirtualDeviceInstaller::bundleVersion(const juce::File& bundle) {
    const auto plist = bundle.getChildFile("Contents/Info.plist");
    if (!plist.existsAsFile()) return {};
    const auto version = driverinstall::bundleShortVersion(plist.loadFileAsString().toStdString());
    return version ? juce::String(*version) : juce::String();
}

void VirtualDeviceInstaller::install(std::function<void(juce::String error)> done) {
    const auto source = bundledDriver();
    if (!source.isDirectory()) {
        done("This build of Spacer does not contain Spacer.driver (" + source.getFullPathName() + ")");
        return;
    }
    const auto script = driverinstall::adminAppleScript(
        driverinstall::installShellCommand(source.getFullPathName().toStdString()));
    juce::Thread::launch([script, done = std::move(done)] {
        juce::ChildProcess process;
        juce::String error;
        if (!process.start(juce::StringArray{"/usr/bin/osascript", "-e", juce::String(script)},
                           juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut)) {
            error = "Could not run osascript";
        } else {
            const auto output = process.readAllProcessOutput();
            if (process.getExitCode() != 0)
                error =
                    output.isNotEmpty() ? output.trim() : juce::String("Installation cancelled or failed");
        }
        juce::MessageManager::callAsync([done, error] { done(error); });
    });
}

}  // namespace spacer
