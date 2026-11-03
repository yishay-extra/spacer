#pragma once

// Internal to src/ui/logic: builds a table of editable fields bound to the
// members of a model struct (MotionFields.cpp, SourceFields.cpp). The same
// builder yields the descriptors for display (bound to a private copy) and the
// setters for editing (bound to the real spec), so each table exists once.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/Layout.h"
#include "ui/logic/MotionFields.h"
#include "ui/logic/ValueFormat.h"

namespace spacer::fields {

using fmt::Unit;

/// One field plus how to write it. Built against a MotionSpec the caller
/// owns: for reads a private copy, for writes the spec being edited, so the
/// table of fields exists exactly once.
struct Binding {
    MotionField field;
    std::function<bool(float)> setNumber;
    std::function<bool(const Period&)> setPeriod;
    std::function<bool(int)> setChoice;
    std::function<bool(bool)> setToggle;
    std::function<bool(const std::string&)> setText;
};

/// Applies `text` through the binding's setter for its type. False if invalid.
bool applyText(Binding& b, const std::string& text);

class Builder {
public:
    explicit Builder(const Layout* layout) : layout_(layout) {}

    std::vector<Binding> take() { return std::move(out_); }
    void group(std::string g, int item = -1) {
        group_ = std::move(g);
        item_ = item;
    }

    void number(const std::string& id, const std::string& label, Unit unit, float min, float max, float& ref,
                std::optional<MotionParam> param = std::nullopt) {
        Binding b;
        b.field.id = prefix_ + id;
        b.field.label = label;
        b.field.group = group_;
        b.field.item = item_;
        b.field.type = FieldType::Number;
        b.field.unit = unit;
        b.field.value = ref;
        b.field.min = min;
        b.field.max = max;
        b.field.decimals = unit == Unit::Count ? 0 : 2;
        b.field.text = fmt::format(ref, unit);
        b.field.param = param;
        b.setNumber = [&ref, min, max](float v) {
            if (!std::isfinite(v)) return false;
            ref = std::clamp(v, min, max);
            return true;
        };
        out_.push_back(std::move(b));
    }

    void integer(const std::string& id, const std::string& label, int min, int max, int& ref) {
        float shadow = static_cast<float>(ref);
        number(id, label, Unit::Count, static_cast<float>(min), static_cast<float>(max), shadow);
        out_.back().setNumber = [&ref, min, max](float v) {
            if (!std::isfinite(v)) return false;
            ref = std::clamp(static_cast<int>(std::lround(v)), min, max);
            return true;
        };
    }

    void seed(const std::string& id, std::uint32_t& ref) {
        float shadow = static_cast<float>(ref);
        number(id, "Seed", Unit::Count, 0.0f, 1.0e6f, shadow);
        out_.back().setNumber = [&ref](float v) {
            if (!std::isfinite(v)) return false;
            ref = static_cast<std::uint32_t>(std::clamp(std::lround(v), 0L, 1000000L));
            return true;
        };
    }

    void text(const std::string& id, const std::string& label, std::string& ref) {
        Binding b;
        b.field.id = prefix_ + id;
        b.field.label = label;
        b.field.group = group_;
        b.field.item = item_;
        b.field.type = FieldType::Text;
        b.field.text = ref;
        b.setText = [&ref](const std::string& t) {
            ref = t;
            return true;
        };
        out_.push_back(std::move(b));
    }

    void period(const std::string& id, const std::string& label, Period& ref) {
        Binding b;
        b.field.id = prefix_ + id;
        b.field.label = label;
        b.field.group = group_;
        b.field.item = item_;
        b.field.type = FieldType::Period;
        b.field.period = ref;
        b.field.text = fmt::formatPeriod(ref);
        b.setPeriod = [&ref](const Period& p) {
            if (!std::isfinite(p.value) || p.value <= 0.0) return false;
            ref = p;
            return true;
        };
        out_.push_back(std::move(b));
    }

    void choice(const std::string& id, const std::string& label, std::vector<std::string> names, int current,
                std::function<bool(int)> set) {
        Binding b;
        b.field.id = prefix_ + id;
        b.field.label = label;
        b.field.group = group_;
        b.field.item = item_;
        b.field.type = FieldType::Choice;
        b.field.choices = std::move(names);
        b.field.choice = std::clamp(current, 0, static_cast<int>(b.field.choices.size()) - 1);
        b.field.text = b.field.choices[static_cast<std::size_t>(b.field.choice)];
        b.setChoice = [n = static_cast<int>(b.field.choices.size()), set = std::move(set)](int i) {
            return i >= 0 && i < n && set(i);
        };
        out_.push_back(std::move(b));
    }

    template <typename E>
    void enumChoice(const std::string& id, const std::string& label, std::vector<std::string> names, E& ref) {
        choice(id, label, std::move(names), static_cast<int>(ref), [&ref](int i) {
            ref = static_cast<E>(i);
            return true;
        });
    }

    void toggle(const std::string& id, const std::string& label, bool& ref) {
        Binding b;
        b.field.id = prefix_ + id;
        b.field.label = label;
        b.field.group = group_;
        b.field.item = item_;
        b.field.type = FieldType::Toggle;
        b.field.on = ref;
        b.field.text = ref ? "on" : "off";
        b.setToggle = [&ref](bool v) {
            ref = v;
            return true;
        };
        out_.push_back(std::move(b));
    }

    /// az / el / dist of a Position under `<id>.az` … with live parameters.
    /// `ownGroup`: put the three numbers under a heading `label` (else they stay in
    /// the current group).
    void position(const std::string& id, const std::string& label, Position& ref,
                  std::optional<std::array<MotionParam, 3>> params, bool ownGroup = true) {
        const auto saved = group_;
        if (ownGroup) group_ = label;
        const std::string sub = id.empty() ? "" : id + ".";
        number(sub + "az", "Azimuth", Unit::Degrees, -180.0f, 180.0f, ref.azimuth,
               params ? std::optional<MotionParam>((*params)[0]) : std::nullopt);
        number(sub + "el", "Elevation", Unit::Degrees, -90.0f, 90.0f, ref.elevation,
               params ? std::optional<MotionParam>((*params)[1]) : std::nullopt);
        number(sub + "dist", "Distance", Unit::Metres, 0.0f, 100.0f, ref.distance,
               params ? std::optional<MotionParam>((*params)[2]) : std::nullopt);
        group_ = saved;
    }

    void cartesian(const std::string& id, const std::string& label, Cartesian& ref) {
        const auto saved = group_;
        group_ = label;
        const std::string sub = id.empty() ? "" : id + ".";
        number(sub + "x", "X (right)", Unit::Metres, -100.0f, 100.0f, ref.x);
        number(sub + "y", "Y (front)", Unit::Metres, -100.0f, 100.0f, ref.y);
        number(sub + "z", "Z (up)", Unit::Metres, -100.0f, 100.0f, ref.z);
        group_ = saved;
    }

    /// Replace the setter of the field added last (for fields whose write has
    /// side effects, or that show a different number than they store).
    void lastSetNumber(std::function<bool(float)> f) { out_.back().setNumber = std::move(f); }
    void lastSetToggle(std::function<bool(bool)> f) { out_.back().setToggle = std::move(f); }
    void lastSetText(std::function<bool(const std::string&)> f) { out_.back().setText = std::move(f); }

    void setPrefix(std::string p) { prefix_ = std::move(p); }
    const Layout* layout() const { return layout_; }

private:
    const Layout* layout_;
    std::vector<Binding> out_;
    std::string group_;
    std::string prefix_;
    int item_{-1};
};

}  // namespace spacer::fields
