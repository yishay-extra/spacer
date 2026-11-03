#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace spacer {

/// "Install virtual device…" (SPEC §14): copies the Spacer.driver bundled in
/// Spacer.app/Contents/Resources into /Library/Audio/Plug-Ins/HAL and
/// restarts coreaudiod, behind the macOS administrator prompt (osascript
/// "with administrator privileges"). Message thread; the copy runs on a
/// background thread and `done` is called back on the message thread.
///
/// A signed SMJobBless privileged helper (PLAN WP5) needs code signing, which
/// is WP13; until then the admin prompt comes from osascript.
class VirtualDeviceInstaller {
public:
    /// The driver shipped inside the app bundle (may not exist in dev builds).
    static juce::File bundledDriver();
    static juce::File installedDriver();
    /// CFBundleShortVersionString of a driver bundle, empty if unreadable.
    static juce::String bundleVersion(const juce::File& bundle);

    /// `done(error)`: error is empty on success.
    static void install(std::function<void(juce::String error)> done);
};

}  // namespace spacer
