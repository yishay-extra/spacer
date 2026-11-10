#include "ui/StageView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <vector>

#include "ui/logic/LayoutEditing.h"
#include "ui/logic/SourceGeometry.h"
#include "ui/logic/StageCloud.h"
#include "ui/logic/StageDepth.h"
#include "ui/logic/StageHitTest.h"

namespace spacer {

namespace {

const juce::Colour kBackground{0xff15171a};
const juce::Colour kGrid{0xff2a2e34};
const juce::Colour kGridText{0xff5c636e};
const juce::Colour kSpeaker{0xffc8ccd2};
const juce::Colour kSpeakerLfe{0xff8a7fd0};
const juce::Colour kSelected{0xff4fa3ff};
const juce::Colour kTesting{0xffff9f1c};
const juce::Colour kSourceMuted{0xff5a6068};
const juce::Colour kTrajectory{0x553ddc97};
const juce::Colour kListener{0xff7d8590};

constexpr float kSpeakerSize = 14.0f;
constexpr float kSourceRadius = 7.0f;
constexpr float kMeterRingMax = 16.0f;  // extra ring radius at 0 dBFS
constexpr float kCloudRadius = 0.20f;   // metres, per unit of the cloud table
constexpr float kGlowRadius = 0.13f;    // metres
constexpr float kShimmer = 0.12f;       // dot wobble, in cloud-table units
// Whole shimmer cycles for 1.7, 1.3 and 1.1 rad/s (their common period is 20 pi s).
constexpr double kAnimPeriodSeconds = 20.0 * 3.14159265358979323846;

juce::Colour sourceJuceColour(const StageSourcePoint& s) {
    return s.mute ? kSourceMuted : juce::Colour(s.colour.r, s.colour.g, s.colour.b);
}

bool isDrawable(const Cartesian& c) {
    return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
}

juce::Colour meterColour(float levelDb, float gainReductionDb) {
    if (gainReductionDb < -0.1f) return juce::Colour(0xffff4d4d);
    if (levelDb > -6.0f) return juce::Colour(0xffffd23f);
    return juce::Colour(0xff3ddc97);
}

}  // namespace

StageView::StageView(AppContext& context, Mode mode) : context_(context), mode_(mode) {
    const struct {
        StageSnap snap;
        const char* text;
        const char* tip;
    } specs[] = {
        {StageSnap::ThreeD, "3D", "Free 3D view"},
        {StageSnap::Top, "Top", "From above: front is up"},
        {StageSnap::Side, "Side", "From the right: front is right"},
        {StageSnap::Front, "Front", "From behind the listener: right is right"},
    };
    for (std::size_t i = 0; i < snapButtons_.size(); ++i) {
        auto& sb = snapButtons_[i];
        sb.snap = specs[i].snap;
        sb.button.setButtonText(specs[i].text);
        sb.button.setTooltip(specs[i].tip);
        sb.button.setColour(juce::TextButton::buttonOnColourId, kSelected);
        sb.button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        sb.button.onClick = [this, snap = specs[i].snap] { setSnap(snap); };
        addAndMakeVisible(sb.button);
    }
    showStats_ = juce::SystemStats::getEnvironmentVariable("SPACER_STAGE_STATS", "") == "1";
    dots_.ensureStorageAllocated(kCloudMaxDots);
    setSnap(StageSnap::ThreeD);
    context_.addListener(this);
    refreshScene(true);
}

StageView::~StageView() {
    context_.removeListener(this);
}

void StageView::setSnap(StageSnap snap) {
    transform_.setSnap(snap);
    refreshSnapButtons();
    repaint();
}

void StageView::refreshSnapButtons() {
    const auto current = transform_.snap();
    for (auto& sb : snapButtons_)
        sb.button.setToggleState(current && *current == sb.snap, juce::dontSendNotification);
}

void StageView::setSelectedSpeaker(int speakerId) {
    selectedSpeaker_ = speakerId;
    repaint();
}

void StageView::refreshScene(bool refit) {
    const Project& p = context_.project();
    scene_.setProject(p);
    scene_.update(p, context_.feedback());
    if (refit) {
        transform_.setExtent(StageTransform::fitExtent(p.layout));
        transform_.setZoom(1.0f);
        transform_.setPan({});
    }
    repaint();
}

void StageView::projectChanged(AppContext::Change change) {
    const bool refit = change == AppContext::Change::Loaded;
    refreshScene(refit);
}

void StageView::feedbackUpdated() {
    animTime_ = std::fmod(animTime_ + 1.0 / AppContext::kTickHz, kAnimPeriodSeconds);
    scene_.update(context_.project(), context_.feedback());
    repaint();
}

void StageView::resized() {
    transform_.setViewport({0.0f, 0.0f, static_cast<float>(getWidth()), static_cast<float>(getHeight())});
    constexpr int w = 46, gap = 4, h = 24;
    int x = getWidth() - 8 - static_cast<int>(snapButtons_.size()) * (w + gap) + gap;
    for (auto& sb : snapButtons_) {
        sb.button.setBounds(x, 8, w, h);
        x += w + gap;
    }
}

juce::Point<float> StageView::toJuce(const Cartesian& c) const {
    const StagePoint p = transform_.toScreen(c);
    return {p.x, p.y};
}

// ---- painting -----------------------------------------------------------------

void StageView::paint(juce::Graphics& g) {
    const double paintStart = juce::Time::getMillisecondCounterHiRes();
    g.fillAll(kBackground);
    paintFloor(g);
    paintMast(g);
    paintListener(g);
    paintTrajectories(g);
    paintShareLines(g);
    paintItems(g);
    paintGizmo(g);

    g.setColour(kGridText);
    g.setFont(juce::FontOptions(12.0f));
    juce::String caption = "3D view  -  drag empty space to rotate";
    if (const auto snap = transform_.snap()) {
        switch (*snap) {
            case StageSnap::ThreeD:
                break;
            case StageSnap::Top:
                caption = "Top view  -  front is up";
                break;
            case StageSnap::Side:
                caption = "Side view (from the right)  -  front is right, up is up";
                break;
            case StageSnap::Front:
                caption = "Front view (from behind the listener)  -  right is right, up is up";
                break;
        }
    }
    if (showStats_)
        caption << "   paint " << juce::String(paintStats_.meanMs(), 1) << " ms mean / "
                << juce::String(paintStats_.maxMs(), 1) << " ms max";
    g.drawText(caption, getLocalBounds().reduced(10).removeFromBottom(16), juce::Justification::bottomLeft);
    if (const auto test = context_.speakerTestId()) {
        g.setColour(kTesting);
        g.drawText("Speaker test: " + juce::String(*test) + "  (click the speaker again to stop)",
                   getLocalBounds().reduced(10).removeFromTop(20), juce::Justification::topLeft);
    }
    paintStats_.add(juce::Time::getMillisecondCounterHiRes() - paintStart);
}

void StageView::paintFloor(juce::Graphics& g) const {
    constexpr int kSegments = 72;
    constexpr int kMaxRings = 50;
    const float extent = transform_.extent();
    const int rings =
        std::clamp(static_cast<int>(std::ceil(std::max(extent * 1.5f, extent * 1.5f / transform_.zoom()))), 1,
                   kMaxRings);
    g.setFont(juce::FontOptions(10.0f));
    for (int r = 1; r <= rings; ++r) {
        const float radius = static_cast<float>(r);
        juce::Path ring;
        for (int i = 0; i <= kSegments; ++i) {
            const float a = static_cast<float>(i) * (2.0f * kPi) / static_cast<float>(kSegments);
            const auto p = toJuce({radius * std::cos(a), radius * std::sin(a), 0.0f});
            if (i == 0)
                ring.startNewSubPath(p);
            else
                ring.lineTo(p);
        }
        g.setColour(kGrid);
        g.strokePath(ring, juce::PathStrokeType(1.0f));
        const auto label = toJuce({radius, 0.0f, 0.0f});
        g.setColour(kGridText);
        g.drawText(juce::String(r) + " m",
                   juce::Rectangle<float>(label.x + 3.0f, label.y - 12.0f, 40.0f, 12.0f),
                   juce::Justification::topLeft);
    }
    const float reach = static_cast<float>(rings);
    g.setColour(kGrid.brighter(0.3f));
    g.drawLine(juce::Line<float>(toJuce({-reach, 0.0f, 0.0f}), toJuce({reach, 0.0f, 0.0f})), 1.0f);
    g.drawLine(juce::Line<float>(toJuce({0.0f, -reach, 0.0f}), toJuce({0.0f, reach, 0.0f})), 1.0f);
    constexpr float kLabelW = 60.0f, kLabelH = 14.0f;
    const auto from = toJuce({});
    const auto to = toJuce({0.0f, extent * 1.08f, 0.0f});
    const auto bounds = getLocalBounds().toFloat().reduced(kLabelW * 0.5f + 6.0f, kLabelH * 0.5f + 6.0f);
    const StageRect limit{bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()};
    if (!bounds.contains(from) || std::hypot(to.x - from.x, to.y - from.y) < 12.0f) return;
    const auto at = pullInside({from.x, from.y}, {to.x, to.y}, limit);
    g.setColour(kGridText);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("FRONT", juce::Rectangle<float>(kLabelW, kLabelH).withCentre({at.x, at.y}),
               juce::Justification::centred);
}

void StageView::paintMast(juce::Graphics& g) const {
    const int height = std::min(50, static_cast<int>(std::ceil(transform_.extent())));
    g.setColour(kListener.withAlpha(0.7f));
    g.drawLine(juce::Line<float>(toJuce({}), toJuce({0.0f, 0.0f, static_cast<float>(height)})), 1.0f);
    g.setFont(juce::FontOptions(10.0f));
    for (int m = 1; m <= height; ++m) {
        const auto p = toJuce({0.0f, 0.0f, static_cast<float>(m)});
        g.setColour(kListener.withAlpha(0.7f));
        g.drawLine(p.x - 3.0f, p.y, p.x + 3.0f, p.y, 1.0f);
        g.setColour(kGridText);
        g.drawText(juce::String(m), juce::Rectangle<float>(p.x + 5.0f, p.y - 6.0f, 24.0f, 12.0f),
                   juce::Justification::centredLeft);
    }
}

void StageView::paintListener(juce::Graphics& g) const {
    const auto origin = toJuce({});
    g.setColour(kListener);
    g.drawEllipse(origin.x - 8.0f, origin.y - 8.0f, 16.0f, 16.0f, 1.5f);
    const auto front = toJuce({0.0f, 1.0f, 0.0f}) - origin;
    const float len = std::hypot(front.x, front.y);
    if (len < 1.0f) return;
    const juce::Point<float> dir{front.x / len, front.y / len};
    const juce::Point<float> side{-dir.y, dir.x};
    juce::Path nose;
    nose.addTriangle(origin + dir * 7.0f + side * 4.0f, origin + dir * 7.0f - side * 4.0f,
                     origin + dir * 14.0f);
    g.fillPath(nose);
}

void StageView::paintGizmo(juce::Graphics& g) const {
    const juce::Point<float> centre{static_cast<float>(getWidth()) - 50.0f,
                                    static_cast<float>(getHeight()) - 50.0f};
    constexpr float kLength = 26.0f;
    const struct {
        Cartesian axis;
        const char* text;
        juce::uint32 colour;
    } axes[] = {
        {{1.0f, 0.0f, 0.0f}, "R", 0xffe5645c},
        {{0.0f, 1.0f, 0.0f}, "F", 0xff5cc98a},
        {{0.0f, 0.0f, 1.0f}, "Up", 0xff5c9be5},
    };
    g.setFont(juce::FontOptions(11.0f));
    for (const auto& a : axes) {
        const juce::Point<float> tip = centre + juce::Point<float>(transform_.screenRight().dot(a.axis),
                                                                   -transform_.screenUp().dot(a.axis)) *
                                                    kLength;
        g.setColour(juce::Colour(a.colour));
        g.drawArrow(juce::Line<float>(centre, tip), 1.5f, 6.0f, 6.0f);
        const auto out = tip - centre;
        const float len = std::max(1.0f, std::hypot(out.x, out.y));
        const auto labelAt = tip + juce::Point<float>(out.x / len, out.y / len) * 9.0f;
        g.drawText(a.text, juce::Rectangle<float>(24.0f, 12.0f).withCentre(labelAt),
                   juce::Justification::centred);
    }
}

void StageView::paintCloud(juce::Graphics& g, const Cartesian& centre, float radiusMetres, int dots,
                           juce::Colour colour, float cue) {
    const auto offsets = cloudOffsets();
    dots = std::min(dots, static_cast<int>(offsets.size()));
    if (dots <= 0) return;
    const auto c = toJuce(centre);
    const float scale = radiusMetres * transform_.pixelsPerMetre() * cue;
    const Cartesian right = transform_.screenRight();
    const Cartesian up = transform_.screenUp();
    const float size = cue > 0.88f ? 3.0f : 2.0f;
    const float t = static_cast<float>(animTime_);  // wrapped, so float keeps its precision
    dots_.clear();
    for (int k = 0; k < dots; ++k) {
        const CloudOffset& o = offsets[static_cast<std::size_t>(k)];
        const Cartesian s{o.x + kShimmer * std::sin(t * 1.7f + o.phase),
                          o.y + kShimmer * std::cos(t * 1.3f + o.phase),
                          o.z + kShimmer * std::sin(t * 1.1f + 2.0f * o.phase)};
        dots_.addWithoutMerging(
            {c.x + s.dot(right) * scale - size * 0.5f, c.y - s.dot(up) * scale - size * 0.5f, size, size});
    }
    g.setColour(colour.withAlpha(0.6f * cue));
    g.fillRectList(dots_);
}

void StageView::paintShareLines(juce::Graphics& g) const {
    if (mode_ == Mode::LayoutEdit) return;
    const auto& speakers = scene_.speakers();
    const std::size_t count = std::min(speakers.size(), static_cast<std::size_t>(kMaxSpeakers));
    const auto& points = scene_.sources();
    // Stereo pairs: a line between the two points.
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        if (points[i].id != points[i + 1].id) continue;
        if (!isDrawable(points[i].position) || !isDrawable(points[i + 1].position)) continue;
        g.setColour(sourceJuceColour(points[i]).withAlpha(0.5f));
        g.drawLine(juce::Line<float>(toJuce(points[i].position), toJuce(points[i + 1].position)), 1.5f);
    }
    for (const auto& s : points) {
        if (s.mute || s.level <= 0.0f || !s.hasShares || !isDrawable(s.position)) continue;
        const auto from = toJuce(s.position);
        const juce::Colour colour = sourceJuceColour(s);
        std::array<int, kMaxShareLines> strongest{};
        const int n = strongestShares(std::span<const float>(s.shares.data(), count), strongest);
        for (int k = 0; k < n; ++k) {
            const auto i = static_cast<std::size_t>(strongest[static_cast<std::size_t>(k)]);
            if (!isDrawable(speakers[i].position)) continue;
            g.setColour(colour.withAlpha(std::min(0.8f, 0.1f + 0.7f * s.shares[i])));
            g.drawLine(juce::Line<float>(from, toJuce(speakers[i].position)), 1.0f);
        }
    }
}

void StageView::paintItems(juce::Graphics& g) {
    const auto& speakers = scene_.speakers();
    const auto& points = scene_.sources();
    const bool withSources = mode_ == Mode::Perform;
    const std::size_t speakerCount = speakers.size();
    const int cloudLimit = cloudDotLimit(scene_.loudPoints());
    const int glowLimit = glowDotLimit(cloudLimit, scene_.sharePairs());

    itemPositions_.clear();
    for (const auto& s : speakers) itemPositions_.push_back(s.position);
    if (withSources)
        for (const auto& s : points) itemPositions_.push_back(s.position);

    for (const std::size_t index : farToNear(itemPositions_, transform_)) {
        if (index < speakerCount)
            paintSpeaker(g, index, glowLimit);
        else
            paintSourcePoint(g, points[index - speakerCount], cloudLimit);
    }
}

void StageView::paintStem(juce::Graphics& g, const Cartesian& position, juce::Colour colour,
                          float cue) const {
    if (std::abs(position.z) <= 0.02f) return;
    const auto p = toJuce(position);
    const auto foot = toJuce({position.x, position.y, 0.0f});
    g.setColour(colour.withAlpha(0.4f * cue));
    const float dash[] = {3.0f, 3.0f};
    g.drawDashedLine(juce::Line<float>(p, foot), dash, 2, 1.0f);
    g.drawEllipse(foot.x - 2.5f, foot.y - 2.5f, 5.0f, 5.0f, 1.0f);
}

void StageView::paintSpeaker(juce::Graphics& g, std::size_t index, int glowLimit) {
    const auto& s = scene_.speakers()[index];
    const float cue = depthCue(s.position, transform_);
    const auto p = toJuce(s.position);
    paintStem(g, s.position, kSpeaker, cue);
    if (mode_ == Mode::Perform && index < static_cast<std::size_t>(kMaxSpeakers)) {
        for (const auto& point : scene_.sources()) {
            if (point.mute || !point.hasShares || point.level <= 0.0f) continue;
            const float share = point.shares[index];
            if (share <= 0.0f) continue;
            paintCloud(g, s.position, kGlowRadius, cloudDots(share, glowLimit), sourceJuceColour(point), cue);
        }
    }
    // Meter ring (post-limiter peak; red while the limiter reduces gain).
    const float size = kSpeakerSize * cue;
    const float fraction = FeedbackState::meterFraction(s.levelDb);
    if (fraction > 0.0f) {
        const float r = (kSpeakerSize * 0.6f + fraction * kMeterRingMax) * cue;
        g.setColour(meterColour(s.levelDb, s.gainReductionDb).withAlpha((0.35f + 0.65f * fraction) * cue));
        g.drawEllipse(p.x - r, p.y - r, r * 2.0f, r * 2.0f, 2.5f);
    }
    const auto box = juce::Rectangle<float>(size, size).withCentre(p);
    const auto test = context_.speakerTestId();
    const bool selected = s.id == selectedSpeaker_;
    const bool hovered = hover_ && hover_->kind == StageItem::Kind::Speaker && hover_->id == s.id;
    g.setColour((s.lfe ? kSpeakerLfe : kSpeaker).withAlpha(cue));
    if (s.lfe)
        g.fillEllipse(box);
    else
        g.fillRoundedRectangle(box, 2.0f);
    if (test && *test == s.id) {
        g.setColour(kTesting);
        g.drawRoundedRectangle(box.expanded(4.0f), 3.0f, 2.5f);
    } else if (selected || hovered) {
        g.setColour(selected ? kSelected : kSpeaker.withAlpha(0.6f));
        g.drawRoundedRectangle(box.expanded(3.0f), 3.0f, 1.5f);
    }
    g.setFont(juce::FontOptions(11.0f));
    g.setColour(kSpeaker.withAlpha(0.85f * cue));
    g.drawText(juce::String(s.name), juce::Rectangle<float>(p.x - 40.0f, p.y + 9.0f, 80.0f, 14.0f),
               juce::Justification::centredTop);
}

void StageView::paintSourcePoint(juce::Graphics& g, const StageSourcePoint& s, int cloudLimit) {
    const float cue = depthCue(s.position, transform_);
    const auto p = toJuce(s.position);
    const juce::Colour colour = sourceJuceColour(s);
    paintStem(g, s.position, colour, cue);
    if (!s.mute && s.level > 0.0f)
        paintCloud(g, s.position, kCloudRadius, cloudDots(s.level, cloudLimit), colour, cue);
    const bool selected = context_.selection().isSelected(s.id);
    const bool active = (drag_ && drag_->item.kind == StageItem::Kind::Source && drag_->item.id == s.id) ||
                        (hover_ && hover_->kind == StageItem::Kind::Source && hover_->id == s.id);
    const float radius = kSourceRadius * cue;
    g.setColour(colour.withAlpha(cue));
    g.fillEllipse(p.x - radius, p.y - radius, radius * 2.0f, radius * 2.0f);
    if (active || selected) {
        g.setColour(active ? juce::Colours::white : kSelected);
        g.drawEllipse(p.x - radius - 3.0f, p.y - radius - 3.0f, radius * 2.0f + 6.0f, radius * 2.0f + 6.0f,
                      1.5f);
    }
    juce::String label(s.name);
    if (s.channel == 1) label << " R";
    g.setFont(juce::FontOptions(12.0f));
    g.setColour(juce::Colours::white.withAlpha(0.85f * cue));
    g.drawText(label, juce::Rectangle<float>(p.x + 9.0f, p.y - 8.0f, 120.0f, 16.0f),
               juce::Justification::centredLeft);
}

void StageView::paintTrajectories(juce::Graphics& g) const {
    for (const auto& t : scene_.trajectories()) {
        if (t.points.size() < 2) continue;
        juce::Colour colour = kTrajectory;
        for (const auto& point : scene_.sources())
            if (point.id == t.sourceId) {
                colour = sourceJuceColour(point).withAlpha(0.33f);
                break;
            }
        g.setColour(colour);
        juce::Path path;
        path.startNewSubPath(toJuce(t.points.front()));
        for (std::size_t i = 1; i < t.points.size(); ++i) path.lineTo(toJuce(t.points[i]));
        g.strokePath(path, juce::PathStrokeType(1.2f));
    }
}

// ---- mouse --------------------------------------------------------------------

void StageView::mouseMove(const juce::MouseEvent& e) {
    StageHitOptions options;
    if (mode_ == Mode::LayoutEdit) options.only = StageItem::Kind::Speaker;
    const auto hit = spacer::hitTest(scene_.items(), transform_, toStage(e.position), options);
    const bool changed = hit.has_value() != hover_.has_value() ||
                         (hit && hover_ && (hit->id != hover_->id || hit->kind != hover_->kind));
    hover_ = hit;
    setMouseCursor(hit ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    if (changed) repaint();
}

void StageView::mouseDown(const juce::MouseEvent& e) {
    drag_.reset();
    orbiting_ = false;
    StageHitOptions options;
    if (mode_ == Mode::LayoutEdit) options.only = StageItem::Kind::Speaker;
    const auto hit = spacer::hitTest(scene_.items(), transform_, toStage(e.position), options);
    if (!hit) {
        orbiting_ = true;
        orbitStarted_ = false;
        return;
    }
    Drag d;
    d.item = *hit;
    if (hit->kind == StageItem::Kind::Source) {
        if (mode_ == Mode::Perform) context_.selectSource(hit->id);  // the Sources panel follows
        bool found = false;
        for (const auto& s : context_.project().sources)
            if (s.id == hit->id) {
                d.startSource = s;
                found = true;
            }
        if (!found) return;
    } else {
        const Speaker* sp = context_.project().layout.findSpeaker(hit->id);
        if (sp == nullptr) return;
        d.startSpeaker = *sp;
    }
    drag_ = d;
}

void StageView::mouseDrag(const juce::MouseEvent& e) {
    if (orbiting_) {
        if (!orbitStarted_) {
            if (e.getDistanceFromDragStart() < 3) return;  // a click, not an orbit (yet)
            orbitStarted_ = true;
            lastOrbitPosition_ = e.position;
            hover_.reset();
            setMouseCursor(juce::MouseCursor::NormalCursor);
            return;
        }
        const auto movement = e.position - lastOrbitPosition_;
        lastOrbitPosition_ = e.position;
        transform_.orbit(movement.x, movement.y);
        refreshSnapButtons();
        repaint();
        return;
    }
    if (!drag_) return;
    if (!drag_->moved) {
        if (e.getDistanceFromDragStart() < 3) return;  // a click, not a drag (yet)
        drag_->moved = true;
        context_.beginGesture();
    }
    const Cartesian delta = transform_.screenDeltaToWorld(static_cast<float>(e.getDistanceFromDragStartX()),
                                                          static_cast<float>(e.getDistanceFromDragStartY()));
    if (drag_->item.kind == StageItem::Kind::Source) {
        if (mode_ != Mode::Perform) return;
        auto edit =
            edits::setSource(context_.project(), dragSource(drag_->startSource, drag_->item.channel, delta),
                             "Move " + drag_->startSource.name);
        edit.mergeKey = "stage.source." + std::to_string(drag_->item.id);
        context_.perform(std::move(edit));
    } else {
        if (mode_ != Mode::LayoutEdit) return;  // speakers move only in the layout editor
        Layout layout = context_.project().layout;
        const Position target = Position::fromCartesian(drag_->startSpeaker.position.toCartesian() + delta);
        if (!layoutedit::moveSpeaker(layout, drag_->item.id, target)) return;
        auto edit = edits::setLayout(context_.project(), std::move(layout),
                                     "Move Speaker " + drag_->startSpeaker.name);
        edit.mergeKey = "stage.speaker." + std::to_string(drag_->item.id);
        context_.perform(std::move(edit));
    }
}

void StageView::mouseUp(const juce::MouseEvent&) {
    orbiting_ = false;
    orbitStarted_ = false;
    if (!drag_) return;
    const Drag d = *drag_;
    drag_.reset();
    if (d.moved) {
        context_.endGesture();
        repaint();
        return;
    }
    // A click.
    if (d.item.kind != StageItem::Kind::Speaker) return;
    if (mode_ == Mode::Perform) {
        const auto test = context_.speakerTestId();
        if (test && *test == d.item.id)
            context_.stopSpeakerTest();
        else
            context_.startSpeakerTest(d.item.id);
    } else {
        setSelectedSpeaker(d.item.id);
        if (onSpeakerClicked) onSpeakerClicked(d.item.id);
    }
}

void StageView::mouseDoubleClick(const juce::MouseEvent& e) {
    StageHitOptions options;
    if (spacer::hitTest(scene_.items(), transform_, toStage(e.position), options)) return;
    refreshScene(true);  // double-click on empty stage: fit the layout again
}

void StageView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    const float factor = std::pow(1.15f, wheel.deltaY * 4.0f);
    transform_.zoomAbout(toStage(e.position), factor);
    repaint();
}

}  // namespace spacer
