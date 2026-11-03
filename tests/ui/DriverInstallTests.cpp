#include <catch2/catch_test_macros.hpp>

#include "ui/logic/DriverInstall.h"

using namespace spacer::driverinstall;

TEST_CASE("DriverInstall: shell and AppleScript quoting") {
    CHECK(shellQuote("/Applications/Spacer.app") == "'/Applications/Spacer.app'");
    CHECK(shellQuote("it's") == "'it'\\''s'");
    CHECK(appleScriptQuote("a \"b\" \\c") == "\"a \\\"b\\\" \\\\c\"");
}

TEST_CASE("DriverInstall: install copies the bundle into the HAL directory and restarts coreaudiod") {
    const auto cmd = installShellCommand("/Apps/My Spacer.app/Contents/Resources/Spacer.driver");
    CHECK(cmd.find("/usr/bin/ditto '/Apps/My Spacer.app/Contents/Resources/Spacer.driver' "
                   "'/Library/Audio/Plug-Ins/HAL/Spacer.driver'") != std::string::npos);
    CHECK(cmd.find("/bin/rm -rf '/Library/Audio/Plug-Ins/HAL/Spacer.driver'") != std::string::npos);
    CHECK(cmd.find("chown -R root:wheel") != std::string::npos);
    CHECK(cmd.find("coreaudiod") != std::string::npos);
    // rm comes before the copy, all steps chained with &&.
    CHECK(cmd.find("rm -rf") < cmd.find("ditto"));

    const auto script = adminAppleScript(cmd);
    CHECK(script.rfind("do shell script \"", 0) == 0);
    CHECK(script.find("with administrator privileges") != std::string::npos);
    CHECK(script.find("'/Library/Audio/Plug-Ins/HAL/Spacer.driver'") != std::string::npos);

    CHECK(uninstallShellCommand().find("/bin/rm -rf '/Library/Audio/Plug-Ins/HAL/Spacer.driver'") == 0);
}

TEST_CASE("DriverInstall: reads CFBundleShortVersionString from Info.plist") {
    const std::string plist = R"(<?xml version="1.0"?><plist><dict>
        <key>CFBundleIdentifier</key><string>org.spacer.driver</string>
        <key>CFBundleShortVersionString</key>
        <string>0.1.0</string></dict></plist>)";
    CHECK(bundleShortVersion(plist) == std::optional<std::string>("0.1.0"));
    CHECK_FALSE(bundleShortVersion("<plist/>").has_value());
    CHECK_FALSE(bundleShortVersion("<key>CFBundleShortVersionString</key><key>x</key><string>1</string>"));
}
