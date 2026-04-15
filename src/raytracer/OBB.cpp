#include "OBB.h"
#include "RTCSGNode.h"
#include "TriTriIntersect.h"

#include <cfloat>
#include <cmath>

float OBB::getVolume() const
{
  Eigen::Matrix4f forward = inv_transform.inverse();
  float hx = forward.col(0).head<3>().norm();
  float hy = forward.col(1).head<3>().norm();
  float hz = forward.col(2).head<3>().norm();
  return 8.0f * hx * hy * hz;
}

bool OBB::containsPoint(const Eigen::Vector3f& point) const
{
  Eigen::Vector4f local = inv_transform * Eigen::Vector4f(point.x(), point.y(), point.z(), 1.0f);
  return std::abs(local.x()) <= 1.0f && std::abs(local.y()) <= 1.0f && std::abs(local.z()) <= 1.0f;
}

std::vector<Eigen::Vector3f> OBB::getCorners() const
{
  Eigen::Matrix4f transform = inv_transform.inverse();
  std::vector<Eigen::Vector3f> corners(8);

  int idx = 0;
  for (int sx = -1; sx <= 1; sx += 2) {
    for (int sy = -1; sy <= 1; sy += 2) {
      for (int sz = -1; sz <= 1; sz += 2) {
        Eigen::Vector4f local(sx, sy, sz, 1.0f);
        corners[idx++] = (transform * local).head<3>();
      }
    }
  }
  return corners;
}

inline std::vector<Triangle> triangulateOBBCorners(const std::vector<Eigen::Vector3f>& c)
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

Eigen::Vector3f OBB::half_sizes(PrimitiveType ptype)
{
  switch (ptype) {
  case PrimitiveType::NONE:     return Eigen::Vector3f::Zero();
  case PrimitiveType::SPHERE:   return Eigen::Vector3f(1.0f, 1.0f, 1.0f);
  case PrimitiveType::CUBE:     return Eigen::Vector3f(0.5f, 0.5f, 0.5f);
  case PrimitiveType::CYLINDER: return Eigen::Vector3f(1.0f, 0.5f, 1.0f);
  default:                      return Eigen::Vector3f::Zero();
  }
}

OBB OBB::buildPrimitiveOBB(const RTCSGNode& node)
{
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

  Eigen::Vector3f half_sizes_vec = half_sizes(node.primitive);

  Eigen::Vector3f he(scaleX * half_sizes_vec.x(), scaleY * half_sizes_vec.y(),
                     scaleZ * half_sizes_vec.z());

  Eigen::Matrix4f obb_mat = Eigen::Matrix4f::Identity();
  obb_mat.col(0).head<3>() = x_axis * he.x();
  obb_mat.col(1).head<3>() = y_axis * he.y();
  obb_mat.col(2).head<3>() = z_axis * he.z();
  obb_mat.col(3).head<3>() = center;

  OBB obb;
  obb.skip = 0;
  obb.inv_transform = obb_mat.inverse();
  return obb;
}

OBB OBB::buildPrimitiveAABB(const RTCSGNode& node)
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

  Eigen::Vector3f he = (aabb_max - aabb_min) * 0.5f;
  Eigen::Vector3f center = (aabb_max + aabb_min) * 0.5f;

  Eigen::Matrix4f aabbMat = Eigen::Matrix4f::Identity();
  aabbMat.col(0).head<3>() = Eigen::Vector3f(he.x(), 0, 0);
  aabbMat.col(1).head<3>() = Eigen::Vector3f(0, he.y(), 0);
  aabbMat.col(2).head<3>() = Eigen::Vector3f(0, 0, he.z());
  aabbMat.col(3).head<3>() = center;

  OBB obb;
  obb.skip = 0;
  obb.inv_transform = aabbMat.inverse();
  return obb;
}

OBB OBB::buildOperationOBB(OperationType optype, const OBB& leftOBB, const OBB& rightOBB)
{
  auto leftChildCorners = leftOBB.getCorners();
  auto rightChildCorners = rightOBB.getCorners();
  OBB resultOBB;
  resultOBB.skip = 0;
  std::vector<Eigen::Vector3f> selectedCorners;
  switch (optype) {
  case OperationType::NONE:  return resultOBB;
  case OperationType::UNION: {
    if (leftOBB.skip == 1 && rightOBB.skip == 1) {
      resultOBB.skip = 1;
      return resultOBB;
    }
    if (leftOBB.skip == 1) {
      selectedCorners = rightChildCorners;
    } else if (rightOBB.skip == 1) {
      selectedCorners = leftChildCorners;
    } else {
      for (size_t i = 0; i < leftChildCorners.size(); i++) {
        selectedCorners.push_back(leftChildCorners[i]);
        selectedCorners.push_back(rightChildCorners[i]);
      }
    }
    break;
  }
  case OperationType::INTERSECTION: {
    if (leftOBB.skip == 1 || rightOBB.skip == 1) {
      resultOBB.skip = 1;
      return resultOBB;
    }
    for (const auto& c : leftChildCorners) {
      if (rightOBB.containsPoint(c)) selectedCorners.push_back(c);
    }
    for (const auto& c : rightChildCorners) {
      if (leftOBB.containsPoint(c)) selectedCorners.push_back(c);
    }

    auto leftTris = triangulateOBBCorners(leftChildCorners);
    auto rightTris = triangulateOBBCorners(rightChildCorners);

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
      resultOBB.skip = 1;
      return resultOBB;
    }
    break;
  }
  case OperationType::DIFFERENCE: {
    if (leftOBB.skip == 1) {
      resultOBB.skip = 1;
      return resultOBB;
    }
    selectedCorners = leftChildCorners;
    break;
  }
  }

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

  // axis of obb are the eigen vectors of the covariance matrix (normalized)
  auto obbAxes = Eigen::SelfAdjointEigenSolver<Eigen::Matrix3f>(covariance).eigenvectors();
  Eigen::Vector3f axisX = obbAxes.col(0).normalized();
  Eigen::Vector3f axisY = obbAxes.col(1).normalized();
  Eigen::Vector3f axisZ = axisX.cross(axisY).normalized();

  // project corners onto OBB axes
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

  Eigen::Vector3f obb_he((maxX - minX) * 0.5f, (maxY - minY) * 0.5f, (maxZ - minZ) * 0.5f);
  Eigen::Vector3f obb_center =
    axisX * (minX + maxX) * 0.5f + axisY * (minY + maxY) * 0.5f + axisZ * (minZ + maxZ) * 0.5f;

  Eigen::Matrix4f obbMat = Eigen::Matrix4f::Identity();
  obbMat.col(0).head<3>() = axisX * obb_he.x();
  obbMat.col(1).head<3>() = axisY * obb_he.y();
  obbMat.col(2).head<3>() = axisZ * obb_he.z();
  obbMat.col(3).head<3>() = obb_center;
  float obb_volume = 8.0f * obb_he.x() * obb_he.y() * obb_he.z();

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

  // keep the tighter one
  if (aabb_volume <= obb_volume) {
    resultOBB.inv_transform = aabbMat.inverse();
  } else {
    resultOBB.inv_transform = obbMat.inverse();
  }

  return resultOBB;
}
