#include "ui/logic/ValueFormat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace spacer::fmt {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// Strips one of `suffixes` (already lower case) from the end of `s`.
std::string stripSuffix(std::string s, std::initializer_list<const char*> suffixes) {
    for (const char* suffix : suffixes) {
        const std::string suf(suffix);
        if (endsWith(s, suf)) {
            s.erase(s.size() - suf.size());
            return trim(s);
        }
    }
    return s;
}

std::optional<double> parseNumber(const std::string& text) {
    if (text.empty()) return std::nullopt;
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    double v = 0.0;
    in >> v;
    if (in.fail()) return std::nullopt;
    in >> std::ws;
    if (!in.eof()) return std::nullopt;  // trailing garbage
    if (!std::isfinite(v)) return std::nullopt;
    return v;
}

const char* suffix(Unit unit) {
    switch (unit) {
        case Unit::Db:
            return " dB";
        case Unit::Degrees:
            return "\xC2\xB0";  // U+00B0 DEGREE SIGN, UTF-8
        case Unit::Metres:
            return " m";
        case Unit::Percent:
            return " %";
        case Unit::Seconds:
            return " s";
        case Unit::Millis:
            return " ms";
        case Unit::Hertz:
            return " Hz";
        case Unit::None:
        case Unit::Count:
            return "";
    }
    return "";
}

int decimalsFor(Unit unit) {
    switch (unit) {
        case Unit::Count:
            return 0;
        case Unit::Percent:
        case Unit::Millis:
            return 1;
        default:
            return 2;
    }
}

}  // namespace

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string number(double v, int decimals) {
    if (!std::isfinite(v)) return "0";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(decimals) << v;
    std::string s = out.str();
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0" || s.empty()) s = "0";
    return s;
}

std::string formatBare(float v, Unit unit) {
    if (unit == Unit::Percent) return number(static_cast<double>(v) * 100.0, decimalsFor(unit));
    if (unit == Unit::Db && v <= kGainFloorDb) return "-inf";
    return number(static_cast<double>(v), decimalsFor(unit));
}

std::string format(float v, Unit unit) {
    return formatBare(v, unit) + suffix(unit);
}

std::optional<float> parse(const std::string& text, Unit unit) {
    std::string s = lower(trim(text));
    if (s.empty()) return std::nullopt;
    switch (unit) {
        case Unit::Db:
            if (s == "-inf" || s == "off" || s == "-infinity") return kGainFloorDb;
            s = stripSuffix(s, {"db"});
            break;
        case Unit::Degrees:
            s = stripSuffix(s, {"\xC2\xB0", "deg", "\xC2\xBA"});
            break;
        case Unit::Metres:
            s = stripSuffix(s, {"m"});
            break;
        case Unit::Percent:
            s = stripSuffix(s, {"%"});
            break;
        case Unit::Seconds:
            s = stripSuffix(s, {"sec", "s"});
            break;
        case Unit::Millis:
            s = stripSuffix(s, {"ms"});
            break;
        case Unit::Hertz:
            s = stripSuffix(s, {"hz"});
            break;
        case Unit::None:
        case Unit::Count:
            break;
    }
    const auto v = parseNumber(s);
    if (!v) return std::nullopt;
    const double out = unit == Unit::Percent ? *v / 100.0 : *v;
    if (std::abs(out) > 1.0e9) return std::nullopt;
    return static_cast<float>(out);
}

std::optional<float> parseClamped(const std::string& text, Unit unit, float min, float max) {
    const auto v = parse(text, unit);
    if (!v) return std::nullopt;
    return std::clamp(*v, min, max);
}

std::string formatPeriod(const Period& period) {
    return number(period.value, 3) + (period.unit == Period::Unit::Beats ? " beats" : " s");
}

std::optional<Period> parsePeriod(const std::string& text, const Period& current) {
    struct Suffix {
        const char* text;
        Period::Unit unit;
    };
    // Longest first so "beats" is not read as "s".
    static constexpr Suffix kSuffixes[] = {
        {"beats", Period::Unit::Beats},    {"beat", Period::Unit::Beats},  {"seconds", Period::Unit::Seconds},
        {"second", Period::Unit::Seconds}, {"sec", Period::Unit::Seconds}, {"b", Period::Unit::Beats},
        {"s", Period::Unit::Seconds},
    };
    std::string s = lower(trim(text));
    Period::Unit unit = current.unit;
    for (const auto& suf : kSuffixes) {
        if (endsWith(s, suf.text)) {
            s = trim(s.substr(0, s.size() - std::string(suf.text).size()));
            unit = suf.unit;
            break;
        }
    }
    const auto v = parseNumber(s);
    if (!v || *v <= 0.0 || *v > 1.0e6) return std::nullopt;
    return Period{unit, *v};
}

}  // namespace spacer::fmt
