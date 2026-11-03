#include "ui/logic/FieldBuilder.h"

#include <cctype>

namespace spacer::fields {

namespace {

std::string lowerCase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::optional<int> parseIndex(const std::string& text) {
    const std::string t = fmt::trim(text);
    if (t.empty() || t.size() > 6 ||
        !std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return std::nullopt;
    return std::stoi(t);
}

std::optional<bool> parseBool(const std::string& text) {
    const std::string t = lowerCase(fmt::trim(text));
    if (t == "1" || t == "on" || t == "true" || t == "yes") return true;
    if (t == "0" || t == "off" || t == "false" || t == "no") return false;
    return std::nullopt;
}

}  // namespace

bool applyText(Binding& b, const std::string& text) {
    switch (b.field.type) {
        case FieldType::Number: {
            const auto v = fmt::parse(text, b.field.unit);
            return v && b.setNumber(*v);
        }
        case FieldType::Period: {
            const auto p = fmt::parsePeriod(text, b.field.period);
            return p && b.setPeriod(*p);
        }
        case FieldType::Choice: {
            int index = -1;
            const std::string t = lowerCase(fmt::trim(text));
            for (std::size_t i = 0; i < b.field.choices.size(); ++i)
                if (lowerCase(b.field.choices[i]) == t) index = static_cast<int>(i);
            if (index < 0)
                if (const auto i = parseIndex(text)) index = *i;
            return index >= 0 && b.setChoice(index);
        }
        case FieldType::Toggle: {
            const auto v = parseBool(text);
            return v && b.setToggle(*v);
        }
        case FieldType::Text:
            return b.setText && b.setText(fmt::trim(text));
    }
    return false;
}

}  // namespace spacer::fields
