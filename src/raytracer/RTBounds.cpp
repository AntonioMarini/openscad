#include "RTBounds.h"
#include "RTCSGNode.h"
#include "TriTriIntersect.h"

#include <cfloat>
#include <cmath>

RTBounds RTBounds::makeAABB(const Eigen::Vector3f& mn, const Eigen::Vector3f& mx)
{
  RTBounds bb;
  bb.skip = 0;
  bb.bounds_type = 0;
  bb.inv_transform = Eigen::Matrix4f::Zero();
  bb.inv_transform.col(0) = Eigen::Vector4f(mn.x(), mn.y(), mn.z(), 0.0f);
  bb.inv_transform.col(1) = Eigen::Vector4f(mx.x(), mx.y(), mx.z(), 0.0f);
  return bb;
}

float RTBounds::getVolume() const
{
  if (bounds_type == 0) {
    Eigen::Vector3f ext = inv_transform.col(1).head<3>() - inv_transform.col(0).head<3>();
    return std::max(0.0f, ext.x()) * std::max(0.0f, ext.y()) * std::max(0.0f, ext.z());
  }
  Eigen::Matrix4f forward = inv_transform.inverse();
  float hx = forward.col(0).head<3>().norm();
  float hy = forward.col(1).head<3>().norm();
  float hz = forward.col(2).head<3>().norm();
  return 8.0f * hx * hy * hz;
}

bool RTBounds::containsPoint(const Eigen::Vector3f& point) const
{
  if (bounds_type == 0) {
    Eigen::Vector3f mn = inv_transform.col(0).head<3>();
    Eigen::Vector3f mx = inv_transform.col(1).head<3>();
    return (point.array() >= mn.array()).all() && (point.array() <= mx.array()).all();
  }
  Eigen::Vector4f local = inv_transform * Eigen::Vector4f(point.x(), point.y(), point.z(), 1.0f);
  return std::abs(local.x()) <= 1.01f && std::abs(local.y()) <= 1.01f && std::abs(local.z()) <= 1.01f;
}

RTBounds RTBounds::shifted(const Eigen::Vector3f& offset) const
{
  RTBounds result = *this;
  if (skip) return result;
  if (bounds_type == 0) {
    // AABB: shift min/max
    result.inv_transform.col(0).head<3>() += offset;
    result.inv_transform.col(1).head<3>() += offset;
  } else {
    // OBB: inv_transform maps world→local. Shifting world by 'offset' means
    // the new inv maps (p+offset) → local, i.e. inv_new * (p,1) = inv * (p - offset, 1).
    // So we apply a pre-translation: inv_new = inv * T(-offset).
    Eigen::Matrix4f T = Eigen::Matrix4f::Identity();
    T.col(3).head<3>() = -offset;
    result.inv_transform = inv_transform * T;
  }
  return result;
}

std::vector<Eigen::Vector3f> RTBounds::getCorners() const
{
  if (bounds_type == 0) {
    Eigen::Vector3f mn = inv_transform.col(0).head<3>();
    Eigen::Vector3f mx = inv_transform.col(1).head<3>();
    std::vector<Eigen::Vector3f> corners(8);
    int idx = 0;
    for (int sx = 0; sx < 2; ++sx)
      for (int sy = 0; sy < 2; ++sy)
        for (int sz = 0; sz < 2; ++sz)
          corners[idx++] = {sx ? mx.x() : mn.x(), sy ? mx.y() : mn.y(), sz ? mx.z() : mn.z()};
    return corners;
  }
  Eigen::Matrix4f transform = inv_transform.inverse();
  std::vector<Eigen::Vector3f> corners(8);
  int idx = 0;
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2)
      for (int sz = -1; sz <= 1; sz += 2)
        corners[idx++] = (transform * Eigen::Vector4f(sx, sy, sz, 1.0f)).head<3>();
  return corners;
}

static std::vector<Triangle> triangulateBoundsCorners(const std::vector<Eigen::Vector3f>& c)
{
  // Corner order from getCorners():
  // sx loops outer, sy mid, sz inner
  // 0:(-,-,-) 1:(-,-,+) 2:(-,+,-) 3:(-,+,+)
  // 4:(+,-,-) 5:(+,-,+) 6:(+,+,-) 7:(+,+,+)
  //
  // 6 faces, 2 triangles each, CCW winding from outside

  std::vector<Triangle> tris(12);

  // -X face: 0,1,3,2
  tris[0] = {c[0], c[1], c[3]};
  tris[1] = {c[0], c[3], c[2]};

  // +X face: 4,6,7,5
  tris[2] = {c[4], c[6], c[7]};
  tris[3] = {c[4], c[7], c[5]};

  // -Y face: 0,4,5,1
  tris[4] = {c[0], c[4], c[5]};
  tris[5] = {c[0], c[5], c[1]};

  // +Y face: 2,3,7,6
  tris[6] = {c[2], c[3], c[7]};
  tris[7] = {c[2], c[7], c[6]};

  // -Z face: 0,2,6,4
  tris[8] = {c[0], c[2], c[6]};
  tris[9] = {c[0], c[6], c[4]};

  // +Z face: 1,5,7,3
  tris[10] = {c[1], c[5], c[7]};
  tris[11] = {c[1], c[7], c[3]};

  return tris;
}

Eigen::Vector3f RTBounds::half_sizes(PrimitiveType ptype)
{
  switch (ptype) {
  case PrimitiveType::NONE:     return Eigen::Vector3f::Zero();
  case PrimitiveType::SPHERE:   return Eigen::Vector3f(1.0f, 1.0f, 1.0f);
  case PrimitiveType::CUBE:     return Eigen::Vector3f(0.5f, 0.5f, 0.5f);
  case PrimitiveType::CYLINDER: return Eigen::Vector3f(1.0f, 0.5f, 1.0f);
  default:                      return Eigen::Vector3f::Zero();
  }
}

RTBounds RTBounds::buildPrimitiveBounds(const RTCSGNode& node)
{
  // ---- OBB: aligned to the primitive's transform axes ----
  Eigen::Vector3f center = node.transform.col(3).head<3>();
  Eigen::Vector3f x_axis = node.transform.col(0).head<3>();
  Eigen::Vector3f y_axis = node.transform.col(1).head<3>();
  Eigen::Vector3f z_axis = node.transform.col(2).head<3>();

  float scaleX = x_axis.stableNorm();
  float scaleY = y_axis.stableNorm();
  float scaleZ = z_axis.stableNorm();
  x_axis /= scaleX;
  y_axis /= scaleY;
  z_axis /= scaleZ;

  Eigen::Vector3f hs_vec = half_sizes(node.primitive);
  Eigen::Vector3f he(scaleX * hs_vec.x(), scaleY * hs_vec.y(), scaleZ * hs_vec.z());

  Eigen::Matrix4f boundsMat = Eigen::Matrix4f::Identity();
  boundsMat.col(0).head<3>() = x_axis * he.x();
  boundsMat.col(1).head<3>() = y_axis * he.y();
  boundsMat.col(2).head<3>() = z_axis * he.z();
  boundsMat.col(3).head<3>() = center;
  float bounds_volume = 8.0f * he.x() * he.y() * he.z();

  // ---- AABB: bounding box of the 8 transformed primitive corners ----
  Eigen::Vector3f aabb_min(FLT_MAX, FLT_MAX, FLT_MAX);
  Eigen::Vector3f aabb_max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2)
      for (int sz = -1; sz <= 1; sz += 2) {
        Eigen::Vector3f w =
          (node.transform * Eigen::Vector4f(sx * hs_vec.x(), sy * hs_vec.y(), sz * hs_vec.z(), 1.0f))
            .head<3>();
        aabb_min = aabb_min.cwiseMin(w);
        aabb_max = aabb_max.cwiseMax(w);
      }
  Eigen::Vector3f aabb_he = (aabb_max - aabb_min) * 0.5f;
  float aabb_volume = 8.0f * aabb_he.x() * aabb_he.y() * aabb_he.z();

  // ---- Select tighter bound ----
  if (bounds_volume <= TIGHT_BOUNDS_THRESHOLD * aabb_volume) {
    RTBounds bb;
    bb.skip = 0;
    bb.bounds_type = 1;
    bb.inv_transform = boundsMat.inverse();
    return bb;
  }
  return makeAABB(aabb_min, aabb_max);
}

RTBounds RTBounds::buildPrimitiveAABB(const RTCSGNode& node)
{
  Eigen::Vector3f hs = half_sizes(node.primitive);

  Eigen::Vector3f aabb_min(FLT_MAX, FLT_MAX, FLT_MAX);
  Eigen::Vector3f aabb_max(-FLT_MAX, -FLT_MAX, -FLT_MAX);

  for (int sx = -1; sx <= 1; sx += 2) {
    for (int sy = -1; sy <= 1; sy += 2) {
      for (int sz = -1; sz <= 1; sz += 2) {
        Eigen::Vector4f local(sx * hs.x(), sy * hs.y(), sz * hs.z(), 1.0f);
        Eigen::Vector3f world = (node.transform * local).head<3>();
        aabb_min = aabb_min.cwiseMin(world);
        aabb_max = aabb_max.cwiseMax(world);
      }
    }
  }

  return makeAABB(aabb_min, aabb_max);
}

RTBounds RTBounds::buildOperationBounds(OperationType optype, const RTBounds& leftBounds,
                                        const RTBounds& rightBounds)
{
  auto leftCorners = leftBounds.getCorners();
  auto rightCorners = rightBounds.getCorners();

  // Shift all corners near the origin for numerical stability.
  // At large world coordinates (e.g. [123, 210, 0]), float32 tri-tri
  // intersection and containment tests lose too much precision.
  Eigen::Vector3f centroid = Eigen::Vector3f::Zero();
  for (const auto& c : leftCorners) centroid += c;
  for (const auto& c : rightCorners) centroid += c;
  centroid /= static_cast<float>(leftCorners.size() + rightCorners.size());
  for (auto& c : leftCorners) c -= centroid;
  for (auto& c : rightCorners) c -= centroid;

  // Rebuild shifted bounds for containsPoint tests
  RTBounds leftShifted = leftBounds.shifted(-centroid);
  RTBounds rightShifted = rightBounds.shifted(-centroid);

  RTBounds result;
  result.skip = 0;
  std::vector<Eigen::Vector3f> selectedCorners;
  switch (optype) {
  case OperationType::NONE:  return result;
  case OperationType::UNION: {
    if (leftBounds.skip == 1 && rightBounds.skip == 1) {
      result.skip = 1;
      return result;
    }
    if (leftBounds.skip == 1) {
      selectedCorners = rightCorners;
    } else if (rightBounds.skip == 1) {
      selectedCorners = leftCorners;
    } else {
      for (size_t i = 0; i < leftCorners.size(); i++) {
        selectedCorners.push_back(leftCorners[i]);
        selectedCorners.push_back(rightCorners[i]);
      }
    }
    break;
  }
  case OperationType::INTERSECTION: {
    if (leftBounds.skip == 1 || rightBounds.skip == 1) {
      result.skip = 1;
      return result;
    }
    for (const auto& c : leftCorners) {
      if (rightShifted.containsPoint(c)) selectedCorners.push_back(c);
    }
    for (const auto& c : rightCorners) {
      if (leftShifted.containsPoint(c)) selectedCorners.push_back(c);
    }

    auto leftTris = triangulateBoundsCorners(leftCorners);
    auto rightTris = triangulateBoundsCorners(rightCorners);

    for (auto& tA : leftTris) {
      for (auto& tB : rightTris) {
        int coplanar = 0;
        float isectpt1[3], isectpt2[3];
        int hit =
          tri_tri_intersect_with_isectline(tA.v0.data(), tA.v1.data(), tA.v2.data(), tB.v0.data(),
                                           tB.v1.data(), tB.v2.data(), &coplanar, isectpt1, isectpt2);
        if (hit && !coplanar) {
          selectedCorners.push_back(Eigen::Vector3f(isectpt1[0], isectpt1[1], isectpt1[2]));
          selectedCorners.push_back(Eigen::Vector3f(isectpt2[0], isectpt2[1], isectpt2[2]));
        }
      }
    }

    if (selectedCorners.empty()) {
      result.skip = 1;
      return result;
    }
    break;
  }
  case OperationType::DIFFERENCE: {
    if (leftBounds.skip == 1) {
      result.skip = 1;
      return result;
    }
    if (rightBounds.skip) {
      // No right volume — difference is just left
      selectedCorners = leftCorners;
      break;
    }
    // Left corners NOT inside right (surviving corners of A)
    for (const auto& c : leftCorners) {
      if (!rightShifted.containsPoint(c)) selectedCorners.push_back(c);
    }
    // Right corners inside left (subtraction boundary within A)
    for (const auto& c : rightCorners) {
      if (leftShifted.containsPoint(c)) selectedCorners.push_back(c);
    }
    // Edge-edge intersection points (where B's surface cuts A's surface)
    auto leftTris = triangulateBoundsCorners(leftCorners);
    auto rightTris = triangulateBoundsCorners(rightCorners);
    for (auto& tA : leftTris) {
      for (auto& tB : rightTris) {
        int coplanar = 0;
        float isectpt1[3], isectpt2[3];
        int hit =
          tri_tri_intersect_with_isectline(tA.v0.data(), tA.v1.data(), tA.v2.data(), tB.v0.data(),
                                           tB.v1.data(), tB.v2.data(), &coplanar, isectpt1, isectpt2);
        if (hit && !coplanar) {
          selectedCorners.emplace_back(isectpt1[0], isectpt1[1], isectpt1[2]);
          selectedCorners.emplace_back(isectpt2[0], isectpt2[1], isectpt2[2]);
        }
      }
    }
    if (selectedCorners.empty()) {
      // Heuristic found no surviving corners, but A \ B ⊆ A always.
      // The subtractand's bounds may envelope A without its geometry
      // filling the volume (e.g. union of cross-holes in a menger sponge).
      // Fall back to left bounds as a safe over-approximation.
      selectedCorners = leftCorners;
    }
    break;
  }
  }

  // Shift corners back to world space
  for (auto& c : selectedCorners) c += centroid;

  // center ~ avg
  Eigen::Vector3f sumPoints = Eigen::Vector3f::Zero();
  for (size_t i = 0; i < selectedCorners.size(); i++) {
    sumPoints += selectedCorners[i];
  }
  Eigen::Vector3f avg = sumPoints / static_cast<float>(selectedCorners.size());

  // covariance matrix
  Eigen::Matrix3f covariance = Eigen::Matrix3f::Zero();
  for (size_t i = 0; i < selectedCorners.size(); i++) {
    Eigen::Vector3f centered = selectedCorners[i] - avg;
    covariance += centered * centered.transpose();
  }
  covariance /= static_cast<float>(selectedCorners.size());

  // axes are the eigenvectors of the covariance matrix (normalized)
  auto boundsAxes = Eigen::SelfAdjointEigenSolver<Eigen::Matrix3f>(covariance).eigenvectors();
  Eigen::Vector3f axisX = boundsAxes.col(0).normalized();
  Eigen::Vector3f axisY = boundsAxes.col(1).normalized();
  Eigen::Vector3f axisZ = axisX.cross(axisY).normalized();

  // project corners onto axes
  float minX = FLT_MAX, maxX = -FLT_MAX;
  float minY = FLT_MAX, maxY = -FLT_MAX;
  float minZ = FLT_MAX, maxZ = -FLT_MAX;
  for (size_t i = 0; i < selectedCorners.size(); i++) {
    float px = selectedCorners[i].dot(axisX);
    float py = selectedCorners[i].dot(axisY);
    float pz = selectedCorners[i].dot(axisZ);
    minX = std::min(minX, px);
    maxX = std::max(maxX, px);
    minY = std::min(minY, py);
    maxY = std::max(maxY, py);
    minZ = std::min(minZ, pz);
    maxZ = std::max(maxZ, pz);
  }

  Eigen::Vector3f bounds_he((maxX - minX) * 0.5f, (maxY - minY) * 0.5f, (maxZ - minZ) * 0.5f);
  Eigen::Vector3f bounds_center =
    axisX * (minX + maxX) * 0.5f + axisY * (minY + maxY) * 0.5f + axisZ * (minZ + maxZ) * 0.5f;

  Eigen::Matrix4f boundsMat = Eigen::Matrix4f::Identity();
  boundsMat.col(0).head<3>() = axisX * bounds_he.x();
  boundsMat.col(1).head<3>() = axisY * bounds_he.y();
  boundsMat.col(2).head<3>() = axisZ * bounds_he.z();
  boundsMat.col(3).head<3>() = bounds_center;
  float bounds_volume = 8.0f * bounds_he.x() * bounds_he.y() * bounds_he.z();

  // compute AABB for comparison
  Eigen::Vector3f aabb_min(FLT_MAX, FLT_MAX, FLT_MAX);
  Eigen::Vector3f aabb_max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
  for (const auto& c : selectedCorners) {
    aabb_min = aabb_min.cwiseMin(c);
    aabb_max = aabb_max.cwiseMax(c);
  }
  Eigen::Vector3f aabb_he = (aabb_max - aabb_min) * 0.5f;
  Eigen::Vector3f aabb_center = (aabb_max + aabb_min) * 0.5f;
  float aabb_volume = 8.0f * aabb_he.x() * aabb_he.y() * aabb_he.z();

  Eigen::Matrix4f aabbMat = Eigen::Matrix4f::Identity();
  aabbMat.col(0).head<3>() = Eigen::Vector3f(aabb_he.x(), 0, 0);
  aabbMat.col(1).head<3>() = Eigen::Vector3f(0, aabb_he.y(), 0);
  aabbMat.col(2).head<3>() = Eigen::Vector3f(0, 0, aabb_he.z());
  aabbMat.col(3).head<3>() = aabb_center;

  // Use OBB only when tighter than AABB
  if (bounds_volume <= aabb_volume) {
    result.bounds_type = 1;
    result.inv_transform = boundsMat.inverse();
  } else {
    result = makeAABB(aabb_min, aabb_max);
  }

  return result;
}
