#pragma once

#include <optional>
#include <string>

namespace spacer::driverinstall {

/// "Install virtual device…" (SPEC §14): the testable part, no JUCE, no OS
/// calls. The app runs the resulting script with /usr/bin/osascript, which
/// shows the macOS administrator prompt.

/// Where the HAL loads plug-ins from.
inline constexpr const char* kHalPlugInDirectory = "/Library/Audio/Plug-Ins/HAL";
inline constexpr const char* kBundleName = "Spacer.driver";

/// `s` as one single-quoted POSIX shell word.
std::string shellQuote(const std::string& s);

/// `s` as an AppleScript string literal (with the quotes).
std::string appleScriptQuote(const std::string& s);

/// Shell command that replaces `<halDirectory>/Spacer.driver` with a copy of
/// `bundle` (owned by root:wheel) and restarts coreaudiod so it loads it.
std::string installShellCommand(const std::string& bundle,
                                const std::string& halDirectory = kHalPlugInDirectory);

/// Same for removal.
std::string uninstallShellCommand(const std::string& halDirectory = kHalPlugInDirectory);

/// AppleScript that runs `shellCommand` as root after the admin prompt.
std::string adminAppleScript(const std::string& shellCommand);

/// CFBundleShortVersionString from an XML Info.plist, if present.
std::optional<std::string> bundleShortVersion(const std::string& infoPlistXml);

}  // namespace spacer::driverinstall
