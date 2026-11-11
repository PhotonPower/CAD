#pragma once

#include <QMatrix4x4>
#include <QPointF>
#include <QSizeF>
#include <QVector3D>
#include <optional>

namespace nfcad::viewport {

struct Ray {
  QVector3D origin;
  QVector3D direction;  ///< unit length
};

/// View camera of one viewport (docs/plan.md, M1). Qt Gui value types only, no widgets.
///
/// The visible scale is stored as `halfHeight()`: half the world height visible in the plane
/// through the target perpendicular to the view direction. The perspective camera stands at the
/// distance where its frustum shows the same height, so switching the projection keeps the scale.
/// Screen coordinates are device-independent pixels, origin top left, y down.
class Camera {
 public:
  enum class Projection { Orthographic, Perspective };

  /// Sets the viewing direction (from eye to target) and the screen-up direction.
  /// `up` is made orthogonal to `forward`.
  void setView(const QVector3D& forward, const QVector3D& up);
  void setTarget(const QVector3D& target) { target_ = target; }
  void setProjection(Projection p) { projection_ = p; }
  void setViewportSize(const QSizeF& size);
  void setHalfHeight(float h);
  /// Radius of the region that must stay inside the near/far planes.
  void setSceneRadius(float r);

  [[nodiscard]] QVector3D forward() const { return forward_; }
  [[nodiscard]] QVector3D up() const { return up_; }
  [[nodiscard]] QVector3D right() const { return QVector3D::crossProduct(forward_, up_); }
  [[nodiscard]] QVector3D target() const { return target_; }
  [[nodiscard]] QVector3D eye() const;
  [[nodiscard]] Projection projection() const { return projection_; }
  [[nodiscard]] float halfHeight() const { return half_height_; }
  [[nodiscard]] QSizeF viewportSize() const { return size_; }
  [[nodiscard]] float aspect() const;
  /// Vertical field of view of the perspective projection in degrees.
  [[nodiscard]] static constexpr float fieldOfView() { return 35.0F; }
  /// Distance eye–target of the perspective projection.
  [[nodiscard]] float distance() const;
  /// World length of one screen pixel in the target plane.
  [[nodiscard]] float worldPerPixel() const;
  /// World length of one screen pixel at the depth of `p` (perspective), else worldPerPixel().
  [[nodiscard]] float worldPerPixelAt(const QVector3D& p) const;

  [[nodiscard]] QMatrix4x4 viewMatrix() const;
  /// OpenGL-style projection (NDC z in [-1, 1]); the renderer applies the RHI clip correction.
  [[nodiscard]] QMatrix4x4 projectionMatrix() const;
  [[nodiscard]] QMatrix4x4 viewProjection() const { return projectionMatrix() * viewMatrix(); }

  /// Screen position of a world point (pixels). Empty if the point is behind the eye.
  [[nodiscard]] std::optional<QPointF> project(const QVector3D& world) const;
  /// Ray from the eye (perspective) or parallel to the view (orthographic) through `px`.
  [[nodiscard]] Ray pickRay(const QPointF& px) const;
  /// Point under `px` in the plane through the target perpendicular to the view direction.
  [[nodiscard]] QVector3D pointOnTargetPlane(const QPointF& px) const;

  // ---- navigation
  /// Moves the view so that the scene follows a cursor movement of `delta_px`.
  void pan(const QPointF& delta_px);
  /// Scales the visible region by `factor` (< 1 zooms in); the point under `px` stays put.
  void zoomAt(const QPointF& px, float factor);
  /// Turntable orbit around the target: horizontal motion turns around world Z, vertical motion
  /// tilts around the screen-right axis (never past the poles).
  void orbit(const QPointF& delta_px);
  /// Shows a sphere completely (zoom extents).
  void fitSphere(const QVector3D& center, float radius);
  /// Shows an axis-aligned box completely. Orthographic: tight fit of the projected box;
  /// perspective: fit of the enclosing sphere.
  void fitBox(const QVector3D& min, const QVector3D& max);

 private:
  QVector3D forward_{0.0F, 0.0F, -1.0F};
  QVector3D up_{0.0F, 1.0F, 0.0F};
  QVector3D target_;
  Projection projection_ = Projection::Orthographic;
  QSizeF size_{800.0, 600.0};
  float half_height_ = 10.0F;
  float scene_radius_ = 1000.0F;
};

/// Intersection of a ray with the plane (origin, normal); empty if parallel.
[[nodiscard]] std::optional<QVector3D> intersect_plane(const Ray& ray, const QVector3D& origin,
                                                       const QVector3D& normal);

}  // namespace nfcad::viewport
