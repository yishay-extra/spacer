#include "ui/logic/DriverInstall.h"

namespace spacer::driverinstall {

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

std::string appleScriptQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += "\"";
    return out;
}

namespace {
std::string restartCoreAudio() {
    // kickstart is the supported way; killall is the fallback (launchd restarts it).
    return "(/bin/launchctl kickstart -k system/com.apple.audio.coreaudiod || /usr/bin/killall coreaudiod)";
}
}  // namespace

std::string installShellCommand(const std::string& bundle, const std::string& halDirectory) {
    const std::string target = shellQuote(halDirectory + "/" + kBundleName);
    return "/bin/mkdir -p " + shellQuote(halDirectory) + " && /bin/rm -rf " + target + " && /usr/bin/ditto " +
           shellQuote(bundle) + " " + target + " && /usr/sbin/chown -R root:wheel " + target + " && " +
           restartCoreAudio();
}

std::string uninstallShellCommand(const std::string& halDirectory) {
    return "/bin/rm -rf " + shellQuote(halDirectory + "/" + kBundleName) + " && " + restartCoreAudio();
}

std::string adminAppleScript(const std::string& shellCommand) {
    return "do shell script " + appleScriptQuote(shellCommand) + " with administrator privileges";
}

std::optional<std::string> bundleShortVersion(const std::string& xml) {
    const std::string key = "<key>CFBundleShortVersionString</key>";
    auto k = xml.find(key);
    if (k == std::string::npos) return std::nullopt;
    const auto open = xml.find("<string>", k + key.size());
    if (open == std::string::npos) return std::nullopt;
    // Only whitespace may separate the key from its value.
    for (auto i = k + key.size(); i < open; ++i)
        if (xml[i] != ' ' && xml[i] != '\t' && xml[i] != '\n' && xml[i] != '\r') return std::nullopt;
    const auto begin = open + 8;
    const auto close = xml.find("</string>", begin);
    if (close == std::string::npos) return std::nullopt;
    return xml.substr(begin, close - begin);
}

}  // namespace spacer::driverinstall
