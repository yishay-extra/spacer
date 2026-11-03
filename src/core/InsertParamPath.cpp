#include "core/InsertParamPath.h"

#include <charconv>

#include "core/Source.h"

namespace spacer {

namespace {

/// Consumes a non-negative decimal integer up to the next '.' (or the end).
bool takeInt(std::string_view& s, int& out) {
    const auto dot = s.find('.');
    const std::string_view digits = s.substr(0, dot);
    if (digits.empty() || digits.size() > 9) return false;
    for (char c : digits)
        if (c < '0' || c > '9') return false;
    int v = 0;
    std::from_chars(digits.data(), digits.data() + digits.size(), v);
    out = v;
    s = dot == std::string_view::npos ? std::string_view{} : s.substr(dot + 1);
    return true;
}

bool takeWord(std::string_view& s, std::string_view word) {
    if (s.substr(0, word.size()) != word) return false;
    s.remove_prefix(word.size());
    return true;
}

}  // namespace

std::optional<InsertParamPath> InsertParamPath::parseUnscoped(std::string_view s) {
    InsertParamPath p;
    if (s.empty() || s.back() == '.') return std::nullopt;
    if (!takeWord(s, "insert.") || !takeInt(s, p.slot) || !takeWord(s, "param.") || !takeInt(s, p.index))
        return std::nullopt;
    // takeInt leaves the rest after a '.', so a trailing component means "not ours".
    if (!s.empty() || p.slot >= kMaxInserts) return std::nullopt;
    return p;
}

std::optional<InsertParamPath> InsertParamPath::parse(std::string_view s) {
    InsertOwner owner{};
    if (takeWord(s, "source."))
        owner = InsertOwner::Source;
    else if (takeWord(s, "speaker."))
        owner = InsertOwner::Speaker;
    else
        return std::nullopt;
    int id = 0;
    if (s.find('.') == std::string_view::npos || !takeInt(s, id) || id < 1) return std::nullopt;
    auto p = parseUnscoped(s);
    if (!p) return std::nullopt;
    p->owner = owner;
    p->ownerId = id;
    return p;
}

std::string InsertParamPath::toString() const {
    std::string out = owner == InsertOwner::Source ? "source." : "speaker.";
    out += std::to_string(ownerId) + ".insert." + std::to_string(slot) + ".param." + std::to_string(index);
    return out;
}

}  // namespace spacer
