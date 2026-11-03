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

StagePoint StageTransform::toPlane(const Cartesian& c) const {
    return view_ == StageProjection::Top ? StagePoint{c.x, c.y} : StagePoint{c.y, c.z};
}

float StageTransform::depthOf(const Cartesian& c) const {
    return view_ == StageProjection::Top ? c.z : c.x;
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
    if (view_ == StageProjection::Top) return {h, v, depthFrom.z};
    return {depthFrom.x, h, v};
}

Cartesian StageTransform::screenDeltaToWorld(float dxPixels, float dyPixels) const {
    const float s = pixelsPerMetre();
    const float h = dxPixels / s;
    const float v = -dyPixels / s;
    if (view_ == StageProjection::Top) return {h, v, 0.0f};
    return {0.0f, h, v};
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
        m = std::max({m, std::abs(c.x), std::abs(c.y), std::abs(c.z)});
    }
    return std::max(kMinExtentMetres, m * margin);
}

}  // namespace spacer
