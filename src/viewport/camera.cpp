#include "viewport/camera.hpp"

#include <QQuaternion>
#include <QVector4D>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace nfcad::viewport {

namespace {
constexpr float kMinHalfHeight = 1e-5F;
constexpr float kMaxHalfHeight = 1e7F;
constexpr float kOrbitDegPerPixel = 0.4F;
constexpr float kPoleMarginDeg = 0.5F;

float tan_half_fov() {
  return std::tan(Camera::fieldOfView() * 0.5F * std::numbers::pi_v<float> / 180.0F);
}
}  // namespace

void Camera::setView(const QVector3D& forward, const QVector3D& up) {
  forward_ = forward.normalized();
  up_ = (up - QVector3D::dotProduct(up, forward_) * forward_).normalized();
}

void Camera::setViewportSize(const QSizeF& size) {
  size_ = QSizeF(std::max(size.width(), 1.0), std::max(size.height(), 1.0));
}

void Camera::setHalfHeight(float h) { half_height_ = std::clamp(h, kMinHalfHeight, kMaxHalfHeight); }

void Camera::setSceneRadius(float r) { scene_radius_ = std::max(r, 1e-3F); }

float Camera::aspect() const { return static_cast<float>(size_.width() / size_.height()); }

float Camera::distance() const { return half_height_ / tan_half_fov(); }

float Camera::worldPerPixel() const {
  return 2.0F * half_height_ / static_cast<float>(size_.height());
}

float Camera::worldPerPixelAt(const QVector3D& p) const {
  if (projection_ == Projection::Orthographic) return worldPerPixel();
  const float depth = std::max(QVector3D::dotProduct(p - eye(), forward_), distance() * 1e-3F);
  return 2.0F * depth * tan_half_fov() / static_cast<float>(size_.height());
}

QVector3D Camera::eye() const {
  // The orthographic eye only defines the view orientation; the depth range is symmetric.
  const float d = projection_ == Projection::Perspective ? distance() : 1.0F;
  return target_ - forward_ * d;
}

QMatrix4x4 Camera::viewMatrix() const {
  QMatrix4x4 m;
  m.lookAt(eye(), target_, up_);
  return m;
}

QMatrix4x4 Camera::projectionMatrix() const {
  QMatrix4x4 m;
  // Depth extent: the scene sphere plus a margin around the target, whichever is larger.
  const float extent = std::max(4.0F * scene_radius_, 20.0F * half_height_) +
                       target_.length();
  if (projection_ == Projection::Orthographic) {
    const float hh = half_height_;
    const float hw = hh * aspect();
    m.ortho(-hw, hw, -hh, hh, 1.0F - extent, 1.0F + extent);
  } else {
    const float d = distance();
    // The model lies within scene_radius_ of the target: a near plane close to it keeps the
    // depth buffer precise (the grid may be clipped near the eye, which is harmless).
    const float near_plane = std::max(d * 5e-3F, d - 2.0F * scene_radius_);
    m.perspective(fieldOfView(), aspect(), near_plane, d + extent);
  }
  return m;
}

std::optional<QPointF> Camera::project(const QVector3D& world) const {
  const QVector4D clip = viewProjection() * QVector4D(world, 1.0F);
  if (clip.w() <= 0.0F) return std::nullopt;
  const float x = clip.x() / clip.w();
  const float y = clip.y() / clip.w();
  return QPointF((x + 1.0) * 0.5 * size_.width(), (1.0 - y) * 0.5 * size_.height());
}

Ray Camera::pickRay(const QPointF& px) const {
  // Built from the target plane instead of inverting the projection: exact in float even with
  // a large depth range.
  const QVector3D p = pointOnTargetPlane(px);
  if (projection_ == Projection::Perspective) {
    const QVector3D e = eye();
    return {e, (p - e).normalized()};
  }
  return {p - forward_ * (2.0F * scene_radius_ + target_.length()), forward_};
}

QVector3D Camera::pointOnTargetPlane(const QPointF& px) const {
  // Exact for both projections, without going through the depth range.
  const auto dx = static_cast<float>(px.x() - 0.5 * size_.width());
  const auto dy = static_cast<float>(0.5 * size_.height() - px.y());
  const float wpp = worldPerPixel();
  return target_ + right() * (dx * wpp) + up_ * (dy * wpp);
}

void Camera::pan(const QPointF& delta_px) {
  const float wpp = worldPerPixel();
  target_ -= right() * static_cast<float>(delta_px.x()) * wpp;
  target_ += up_ * static_cast<float>(delta_px.y()) * wpp;
}

void Camera::zoomAt(const QPointF& px, float factor) {
  const QVector3D p = pointOnTargetPlane(px);  // before the scale changes
  const float old_h = half_height_;
  setHalfHeight(half_height_ * factor);
  const float f = half_height_ / old_h;  // factor after clamping
  target_ = p + (target_ - p) * f;
}

void Camera::orbit(const QPointF& delta_px) {
  const QVector3D z(0.0F, 0.0F, 1.0F);
  // Yaw around world Z.
  const QQuaternion yaw = QQuaternion::fromAxisAndAngle(
      z, -static_cast<float>(delta_px.x()) * kOrbitDegPerPixel);
  QVector3D f = yaw.rotatedVector(forward_);
  QVector3D u = yaw.rotatedVector(up_);

  // Pitch around the screen-right axis, limited so that the view never flips over a pole.
  const QVector3D r = QVector3D::crossProduct(f, u).normalized();
  float pitch = -static_cast<float>(delta_px.y()) * kOrbitDegPerPixel;
  const float elevation =  // angle of forward against the XY plane, degrees, in (-90, 90)
      std::asin(std::clamp(QVector3D::dotProduct(f, z), -1.0F, 1.0F)) * 180.0F /
      std::numbers::pi_v<float>;
  const float limit = 90.0F - kPoleMarginDeg;
  // Rotating around r by +a lifts forward towards +Z when up points towards +Z.
  const float new_elevation = std::clamp(elevation + pitch, -limit, limit);
  pitch = new_elevation - elevation;
  const QQuaternion tilt = QQuaternion::fromAxisAndAngle(r, pitch);
  f = tilt.rotatedVector(f);
  u = tilt.rotatedVector(u);
  setView(f, u);
}

void Camera::fitSphere(const QVector3D& center, float radius) {
  target_ = center;
  const float r = std::max(radius, kMinHalfHeight) * 1.1F;
  setHalfHeight(aspect() < 1.0F ? r / aspect() : r);
  setSceneRadius(std::max(radius, 1.0F));
}

void Camera::fitBox(const QVector3D& min, const QVector3D& max) {
  const QVector3D c = (min + max) * 0.5F;
  const float radius = (max - min).length() * 0.5F;
  if (projection_ == Projection::Perspective) {
    fitSphere(c, radius);
    return;
  }
  float half_w = 0.0F;
  float half_h = 0.0F;
  for (int i = 0; i < 8; ++i) {
    const QVector3D corner((i & 1) != 0 ? max.x() : min.x(), (i & 2) != 0 ? max.y() : min.y(),
                           (i & 4) != 0 ? max.z() : min.z());
    half_w = std::max(half_w, std::abs(QVector3D::dotProduct(corner - c, right())));
    half_h = std::max(half_h, std::abs(QVector3D::dotProduct(corner - c, up_)));
  }
  target_ = c;
  setHalfHeight(std::max({half_h, half_w / aspect(), radius * 1e-3F, kMinHalfHeight}) * 1.1F);
  setSceneRadius(std::max(radius, 1.0F));
}

std::optional<QVector3D> intersect_plane(const Ray& ray, const QVector3D& origin,
                                         const QVector3D& normal) {
  const float denom = QVector3D::dotProduct(ray.direction, normal);
  if (std::abs(denom) < 1e-9F) return std::nullopt;
  const float t = QVector3D::dotProduct(origin - ray.origin, normal) / denom;
  return ray.origin + ray.direction * t;
}

}  // namespace nfcad::viewport
