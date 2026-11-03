#include "ui/StageView.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "ui/logic/LayoutEditing.h"
#include "ui/logic/SourceGeometry.h"
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
const juce::Colour kSource{0xff3ddc97};
const juce::Colour kSourceMuted{0xff5a6068};
const juce::Colour kTrajectory{0x553ddc97};
const juce::Colour kListener{0xff7d8590};

constexpr float kSpeakerSize = 14.0f;
constexpr float kSourceRadius = 7.0f;
constexpr float kMeterRingMax = 16.0f;  // extra ring radius at 0 dBFS

juce::Colour meterColour(float levelDb, float gainReductionDb) {
    if (gainReductionDb < -0.1f) return juce::Colour(0xffff4d4d);
    if (levelDb > -6.0f) return juce::Colour(0xffffd23f);
    return juce::Colour(0xff3ddc97);
}

}  // namespace

StageView::StageView(AppContext& context, Mode mode) : context_(context), mode_(mode) {
    viewButton_.setButtonText("Side view");
    viewButton_.setClickingTogglesState(true);
    viewButton_.setTooltip("Toggle between the top-down and the side view");
    viewButton_.onClick = [this] { toggleView(); };
    addAndMakeVisible(viewButton_);
    context_.addListener(this);
    refreshScene(true);
}

StageView::~StageView() {
    context_.removeListener(this);
}

void StageView::setProjection(StageProjection projection) {
    transform_.setView(projection);
    viewButton_.setToggleState(projection == StageProjection::Side, juce::dontSendNotification);
    repaint();
}

void StageView::toggleView() {
    setProjection(viewButton_.getToggleState() ? StageProjection::Side : StageProjection::Top);
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
    scene_.update(context_.project(), context_.feedback());
    repaint();
}

void StageView::resized() {
    transform_.setViewport({0.0f, 0.0f, static_cast<float>(getWidth()), static_cast<float>(getHeight())});
    viewButton_.setBounds(getWidth() - 100, 8, 92, 24);
}

juce::Point<float> StageView::toJuce(const Cartesian& c) const {
    const StagePoint p = transform_.toScreen(c);
    return {p.x, p.y};
}

// ---- painting -----------------------------------------------------------------

void StageView::paint(juce::Graphics& g) {
    g.fillAll(kBackground);
    paintGrid(g);
    paintTrajectories(g);
    paintSpeakers(g);
    paintSources(g);

    g.setColour(kGridText);
    g.setFont(juce::FontOptions(12.0f));
    const bool top = transform_.view() == StageProjection::Top;
    g.drawText(top ? "Top view  -  front is up" : "Side view (from the right)  -  front is right, up is up",
               getLocalBounds().reduced(10).removeFromBottom(16), juce::Justification::bottomLeft);
    if (const auto test = context_.speakerTestId()) {
        g.setColour(kTesting);
        g.drawText("Speaker test: " + juce::String(*test) + "  (click the speaker again to stop)",
                   getLocalBounds().reduced(10).removeFromTop(20), juce::Justification::topLeft);
    }
}

void StageView::paintGrid(juce::Graphics& g) const {
    const auto origin = toJuce({});
    const float ppm = transform_.pixelsPerMetre();
    const float maxR = std::hypot(static_cast<float>(getWidth()), static_cast<float>(getHeight())) / ppm +
                       std::hypot(transform_.pan().x, transform_.pan().y);
    const int rings = std::min(50, static_cast<int>(std::ceil(maxR)));
    g.setFont(juce::FontOptions(10.0f));
    if (transform_.view() == StageProjection::Top) {
        for (int r = 1; r <= rings; ++r) {
            const float rp = static_cast<float>(r) * ppm;
            g.setColour(kGrid);
            g.drawEllipse(origin.x - rp, origin.y - rp, rp * 2.0f, rp * 2.0f, 1.0f);
            g.setColour(kGridText);
            g.drawText(juce::String(r) + " m",
                       juce::Rectangle<float>(origin.x + 3.0f, origin.y - rp, 40.0f, 12.0f),
                       juce::Justification::topLeft);
        }
        g.setColour(kGrid);
        g.drawLine(0.0f, origin.y, static_cast<float>(getWidth()), origin.y, 1.0f);
        g.drawLine(origin.x, 0.0f, origin.x, static_cast<float>(getHeight()), 1.0f);
        // Listener: a head with its nose pointing front (+y = screen up).
        g.setColour(kListener);
        g.drawEllipse(origin.x - 8.0f, origin.y - 8.0f, 16.0f, 16.0f, 1.5f);
        juce::Path nose;
        nose.addTriangle(origin.x - 4.0f, origin.y - 7.0f, origin.x + 4.0f, origin.y - 7.0f, origin.x,
                         origin.y - 14.0f);
        g.fillPath(nose);
    } else {
        for (int r = -rings; r <= rings; ++r) {
            const float y = origin.y - static_cast<float>(r) * ppm;
            const float x = origin.x + static_cast<float>(r) * ppm;
            g.setColour(r == 0 ? kGrid.brighter(0.4f) : kGrid);
            g.drawLine(0.0f, y, static_cast<float>(getWidth()), y, r == 0 ? 1.5f : 1.0f);
            g.setColour(kGrid);
            g.drawLine(x, 0.0f, x, static_cast<float>(getHeight()), 1.0f);
        }
        g.setColour(kListener);
        g.drawEllipse(origin.x - 8.0f, origin.y - 8.0f, 16.0f, 16.0f, 1.5f);
        juce::Path nose;  // front = screen right
        nose.addTriangle(origin.x + 7.0f, origin.y - 4.0f, origin.x + 7.0f, origin.y + 4.0f, origin.x + 14.0f,
                         origin.y);
        g.fillPath(nose);
    }
}

void StageView::paintSpeakers(juce::Graphics& g) const {
    const auto test = context_.speakerTestId();
    g.setFont(juce::FontOptions(11.0f));
    for (const auto& s : scene_.speakers()) {
        const auto p = toJuce(s.position);
        // Meter ring (post-limiter peak; red while the limiter reduces gain).
        const float fraction = FeedbackState::meterFraction(s.levelDb);
        if (fraction > 0.0f) {
            const float r = kSpeakerSize * 0.6f + fraction * kMeterRingMax;
            g.setColour(meterColour(s.levelDb, s.gainReductionDb).withAlpha(0.35f + 0.65f * fraction));
            g.drawEllipse(p.x - r, p.y - r, r * 2.0f, r * 2.0f, 2.5f);
        }
        const auto box = juce::Rectangle<float>(kSpeakerSize, kSpeakerSize).withCentre(p);
        const bool selected = s.id == selectedSpeaker_;
        const bool hovered = hover_ && hover_->kind == StageItem::Kind::Speaker && hover_->id == s.id;
        g.setColour(s.lfe ? kSpeakerLfe : kSpeaker);
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
        g.setColour(kSpeaker.withAlpha(0.85f));
        g.drawText(juce::String(s.name), juce::Rectangle<float>(p.x - 40.0f, p.y + 9.0f, 80.0f, 14.0f),
                   juce::Justification::centredTop);
    }
}

void StageView::paintTrajectories(juce::Graphics& g) const {
    g.setColour(kTrajectory);
    for (const auto& t : scene_.trajectories()) {
        if (t.points.size() < 2) continue;
        juce::Path path;
        path.startNewSubPath(toJuce(t.points.front()));
        for (std::size_t i = 1; i < t.points.size(); ++i) path.lineTo(toJuce(t.points[i]));
        g.strokePath(path, juce::PathStrokeType(1.2f));
    }
}

void StageView::paintSources(juce::Graphics& g) const {
    if (mode_ == Mode::LayoutEdit) return;  // the layout editor shows speakers only
    g.setFont(juce::FontOptions(12.0f));
    const auto& points = scene_.sources();
    // Stereo pairs: a line between the two points.
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        if (points[i].id != points[i + 1].id) continue;
        g.setColour((points[i].mute ? kSourceMuted : kSource).withAlpha(0.5f));
        g.drawLine(juce::Line<float>(toJuce(points[i].position), toJuce(points[i + 1].position)), 1.5f);
    }
    for (const auto& s : points) {
        const auto p = toJuce(s.position);
        const bool selected = context_.selection().isSelected(s.id);
        const bool active =
            (drag_ && drag_->item.kind == StageItem::Kind::Source && drag_->item.id == s.id) ||
            (hover_ && hover_->kind == StageItem::Kind::Source && hover_->id == s.id);
        g.setColour(s.mute ? kSourceMuted : kSource);
        g.fillEllipse(p.x - kSourceRadius, p.y - kSourceRadius, kSourceRadius * 2.0f, kSourceRadius * 2.0f);
        if (active || selected) {
            g.setColour(active ? juce::Colours::white : kSelected);
            g.drawEllipse(p.x - kSourceRadius - 3.0f, p.y - kSourceRadius - 3.0f, kSourceRadius * 2.0f + 6.0f,
                          kSourceRadius * 2.0f + 6.0f, 1.5f);
        }
        juce::String label(s.name);
        if (s.channel == 1) label << " R";
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.drawText(label, juce::Rectangle<float>(p.x + 9.0f, p.y - 8.0f, 120.0f, 16.0f),
                   juce::Justification::centredLeft);
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
    StageHitOptions options;
    if (mode_ == Mode::LayoutEdit) options.only = StageItem::Kind::Speaker;
    const auto hit = spacer::hitTest(scene_.items(), transform_, toStage(e.position), options);
    if (!hit) return;
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
