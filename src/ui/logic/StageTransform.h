#pragma once

// World <-> screen mapping for the stage view (SPEC §16 item 1). Plain C++,
// no JUCE, so it is unit-tested on Linux.
//
// World coordinates are the SPEC §2 Cartesian frame (x right, y front, z up,
// metres; see core/Position.h). Screen coordinates are pixels with y growing
// downwards, as in juce::Graphics. The view is an orthographic orbit camera
// (yaw, pitch; never rolls). Depth is the distance along the direction towards
// the viewer, larger = nearer. Top {0, 90}, Side {90, 0} and Front {0, 0}
// are axis-aligned exactly. Screen -> world keeps the depth of a reference point.

#include <optional>

#include "core/Layout.h"
#include "core/Position.h"

namespace spacer {

struct StageCamera {
    float yawDeg{0.0f};     // (-180, 180]
    float pitchDeg{90.0f};  // [-90, 90]
    bool operator==(const StageCamera&) const = default;
};

enum class StageSnap { ThreeD, Top, Side, Front };

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

/// The point of the segment from → to that is farthest from `from` and still inside `bounds`:
/// `to` when it is inside; `from` when `from` itself is outside.
StagePoint pullInside(const StagePoint& from, const StagePoint& to, const StageRect& bounds);

class StageTransform {
public:
    static constexpr float kMinExtentMetres = 1.0f;
    static constexpr float kMinZoom = 0.1f;
    static constexpr float kMaxZoom = 20.0f;
    static constexpr float kOrbitDegPerPixel = 0.4f;

    /// ThreeD {-32, 24}, Top {0, 90}, Side {90, 0}, Front {0, 0}.
    static StageCamera cameraFor(StageSnap snap);

    void setViewport(const StageRect& viewport) { viewport_ = viewport; }
    const StageRect& viewport() const { return viewport_; }

    /// Wraps yaw into (-180, 180], clamps pitch to [-90, 90]; ignored if either is not finite.
    void setCamera(const StageCamera& camera);
    const StageCamera& camera() const { return camera_; }
    void setSnap(StageSnap snap) { setCamera(cameraFor(snap)); }
    /// The snap the camera equals exactly, if any.
    std::optional<StageSnap> snap() const;
    /// yaw -= kOrbitDegPerPixel * dx; pitch += kOrbitDegPerPixel * dy.
    void orbit(float dxPixels, float dyPixels);

    Cartesian screenRight() const { return right_; }
    Cartesian screenUp() const { return up_; }
    Cartesian towardViewer() const { return toward_; }

    /// Half the visible world span (metres) along the viewport's shorter side
    /// at zoom 1. Clamped to >= kMinExtentMetres.
    void setExtent(float metres);
    float extent() const { return extent_; }

    /// Zoom factor (> 1 = closer), clamped to [kMinZoom, kMaxZoom].
    void setZoom(float zoom);
    float zoom() const { return zoom_; }

    /// Point of the screen plane (see toPlane) shown at the viewport centre.
    void setPan(const StagePoint& planeCentre) { pan_ = planeCentre; }
    StagePoint pan() const { return pan_; }

    float pixelsPerMetre() const;

    /// The screen-plane coordinates of `c` (along screenRight, along screenUp).
    StagePoint toPlane(const Cartesian& c) const;
    /// Distance of `c` along towardViewer; larger is nearer the viewer.
    float depthOf(const Cartesian& c) const;

    StagePoint toScreen(const Cartesian& c) const;
    /// The world point under `screen`, with the depth taken from `depthFrom`.
    Cartesian toWorld(const StagePoint& screen, const Cartesian& depthFrom = {}) const;

    /// Screen-space movement -> world-space movement (in the screen plane, depth unchanged).
    Cartesian screenDeltaToWorld(float dxPixels, float dyPixels) const;

    /// Zooms by `factor` keeping the world point under `screenAnchor` fixed.
    void zoomAbout(const StagePoint& screenAnchor, float factor);

    /// Extent that fits every speaker of `layout` (from any angle) with a margin.
    static float fitExtent(const Layout& layout, float margin = 1.15f);

private:
    void updateBasis();

    StagePoint centre() const {
        return {viewport_.x + viewport_.width * 0.5f, viewport_.y + viewport_.height * 0.5f};
    }

    StageRect viewport_{0.0f, 0.0f, 100.0f, 100.0f};
    StageCamera camera_{0.0f, 90.0f};
    Cartesian right_{1.0f, 0.0f, 0.0f};
    Cartesian up_{0.0f, 1.0f, 0.0f};
    Cartesian toward_{0.0f, 0.0f, 1.0f};
    float extent_{3.0f};
    float zoom_{1.0f};
    StagePoint pan_{};
};

}  // namespace spacer
