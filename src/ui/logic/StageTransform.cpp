#include "ui/logic/StageTransform.h"

#include <algorithm>
#include <cmath>

namespace spacer {

void StageTransform::setExtent(float metres) {
    extent_ = std::isfinite(metres) ? std::max(metres, kMinExtentMetres) : kMinExtentMetres;
}

void StageTransform::setZoom(float zoom) {
    zoom_ = std::isfinite(zoom) ? std::clamp(zoom, kMinZoom, kMaxZoom) : 1.0f;
}

float StageTransform::pixelsPerMetre() const {
    const float side = std::max(1.0f, std::min(viewport_.width, viewport_.height));
    return side * 0.5f / extent_ * zoom_;
}

StageCamera StageTransform::cameraFor(StageSnap snap) {
    switch (snap) {
        case StageSnap::ThreeD: return {-32.0f, 24.0f};
        case StageSnap::Top: return {0.0f, 90.0f};
        case StageSnap::Side: return {90.0f, 0.0f};
        case StageSnap::Front: return {0.0f, 0.0f};
    }
    return {0.0f, 90.0f};
}

void StageTransform::setCamera(const StageCamera& camera) {
    if (!std::isfinite(camera.yawDeg) || !std::isfinite(camera.pitchDeg)) return;
    camera_ = {Position::wrapAzimuth(camera.yawDeg), std::clamp(camera.pitchDeg, -90.0f, 90.0f)};
    updateBasis();
}

std::optional<StageSnap> StageTransform::snap() const {
    for (StageSnap s : {StageSnap::ThreeD, StageSnap::Top, StageSnap::Side, StageSnap::Front})
        if (camera_ == cameraFor(s)) return s;
    return std::nullopt;
}

void StageTransform::orbit(float dxPixels, float dyPixels) {
    setCamera({camera_.yawDeg - kOrbitDegPerPixel * dxPixels, camera_.pitchDeg + kOrbitDegPerPixel * dyPixels});
}

void StageTransform::updateBasis() {
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const double y = static_cast<double>(camera_.yawDeg) * kDegToRad;
    const double p = static_cast<double>(camera_.pitchDeg) * kDegToRad;
    const double d[3] = {std::sin(y) * std::cos(p), -std::cos(y) * std::cos(p), std::sin(p)};
    const double r[3] = {std::cos(y), std::sin(y), 0.0};
    // u = r x (-d)
    const double u[3] = {-(r[1] * d[2] - r[2] * d[1]), -(r[2] * d[0] - r[0] * d[2]),
                         -(r[0] * d[1] - r[1] * d[0])};
    auto snapped = [](const double v[3]) {
        auto f = [](double c) { return std::abs(c) < 1e-6 ? 0.0f : static_cast<float>(c); };
        return Cartesian{f(v[0]), f(v[1]), f(v[2])};
    };
    right_ = snapped(r);
    up_ = snapped(u);
    toward_ = snapped(d);
}

StagePoint StageTransform::toPlane(const Cartesian& c) const {
    return {c.dot(right_), c.dot(up_)};
}

float StageTransform::depthOf(const Cartesian& c) const {
    return c.dot(toward_);
}

StagePoint StageTransform::toScreen(const Cartesian& c) const {
    const StagePoint p = toPlane(c);
    const float s = pixelsPerMetre();
    const StagePoint o = centre();
    return {o.x + (p.x - pan_.x) * s, o.y - (p.y - pan_.y) * s};
}

Cartesian StageTransform::toWorld(const StagePoint& screen, const Cartesian& depthFrom) const {
    const float s = pixelsPerMetre();
    const StagePoint o = centre();
    const float h = pan_.x + (screen.x - o.x) / s;
    const float v = pan_.y - (screen.y - o.y) / s;
    return right_ * h + up_ * v + toward_ * depthOf(depthFrom);
}

Cartesian StageTransform::screenDeltaToWorld(float dxPixels, float dyPixels) const {
    const float s = pixelsPerMetre();
    return right_ * (dxPixels / s) + up_ * (-dyPixels / s);
}

void StageTransform::zoomAbout(const StagePoint& screenAnchor, float factor) {
    if (!std::isfinite(factor) || factor <= 0.0f) return;
    const StagePoint before = toPlane(toWorld(screenAnchor));
    setZoom(zoom_ * factor);
    const StagePoint after = toPlane(toWorld(screenAnchor));
    pan_.x += before.x - after.x;
    pan_.y += before.y - after.y;
}

float StageTransform::fitExtent(const Layout& layout, float margin) {
    float m = 0.0f;
    for (const auto& s : layout.speakers) {
        const Cartesian c = s.position.toCartesian();
        m = std::max(m, c.length());
    }
    return std::max(kMinExtentMetres, m * margin);
}

}  // namespace spacer
