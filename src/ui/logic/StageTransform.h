#pragma once

// World <-> screen mapping for the stage view (SPEC §16 item 1). Plain C++,
// no JUCE, so it is unit-tested on Linux.
//
// World coordinates are the SPEC §2 Cartesian frame (x right, y front, z up,
// metres; see core/Position.h). Screen coordinates are pixels with y growing
// downwards, as in juce::Graphics.
//
//   Top view  (seen from above):         screen right = +x, screen up = +y (front)
//   Side view (seen from the right, +x): screen right = +y (front), screen up = +z
//
// The coordinate that a view does not show (z in the top view, x in the side
// view) is its "depth"; screen -> world keeps it from a reference point.

#include "core/Layout.h"
#include "core/Position.h"

namespace spacer {

enum class StageProjection { Top, Side };

struct StagePoint {
    float x{0.0f};
    float y{0.0f};
};

struct StageRect {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
};

class StageTransform {
public:
    static constexpr float kMinExtentMetres = 1.0f;
    static constexpr float kMinZoom = 0.1f;
    static constexpr float kMaxZoom = 20.0f;

    void setViewport(const StageRect& viewport) { viewport_ = viewport; }
    const StageRect& viewport() const { return viewport_; }

    void setView(StageProjection view) { view_ = view; }
    StageProjection view() const { return view_; }

    /// Half the visible world span (metres) along the viewport's shorter side
    /// at zoom 1. Clamped to >= kMinExtentMetres.
    void setExtent(float metres);
    float extent() const { return extent_; }

    /// Zoom factor (> 1 = closer), clamped to [kMinZoom, kMaxZoom].
    void setZoom(float zoom);
    float zoom() const { return zoom_; }

    /// World point (in the view plane) shown at the viewport centre.
    void setPan(const StagePoint& planeCentre) { pan_ = planeCentre; }
    StagePoint pan() const { return pan_; }

    float pixelsPerMetre() const;

    /// The two coordinates of `c` that this view shows (horizontal, vertical).
    StagePoint toPlane(const Cartesian& c) const;
    /// The coordinate this view does not show (z for Top, x for Side).
    float depthOf(const Cartesian& c) const;

    StagePoint toScreen(const Cartesian& c) const;
    /// The world point under `screen`, with the hidden coordinate taken from `depthFrom`.
    Cartesian toWorld(const StagePoint& screen, const Cartesian& depthFrom = {}) const;

    /// Screen-space movement -> world-space movement (only the two shown axes change).
    Cartesian screenDeltaToWorld(float dxPixels, float dyPixels) const;

    /// Zooms by `factor` keeping the world point under `screenAnchor` fixed.
    void zoomAbout(const StagePoint& screenAnchor, float factor);

    /// Extent that fits every speaker of `layout` (in both views) with a margin.
    static float fitExtent(const Layout& layout, float margin = 1.15f);

private:
    StagePoint centre() const {
        return {viewport_.x + viewport_.width * 0.5f, viewport_.y + viewport_.height * 0.5f};
    }

    StageRect viewport_{0.0f, 0.0f, 100.0f, 100.0f};
    StageProjection view_{StageProjection::Top};
    float extent_{3.0f};
    float zoom_{1.0f};
    StagePoint pan_{};
};

}  // namespace spacer
