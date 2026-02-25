#ifndef PRIMITIVE_H
#define PRIMITIVE_H

#include <string>
#include <Eigen/Dense>

enum class PrimitiveType : int {
	NONE = 0,
	SPHERE = 1,
	CUBE = 2,
	CYLINDER = 4
};

struct alignas(16) Primitive {
	int type; // 4 bytes
	int _pad[3]; // padding to align to 16 bytes
	Eigen::Vector4f color; // 16 bytes
	Eigen::Matrix4f inv_transform; // 64 bytes
	Eigen::Matrix4f normal_mat; // 64 bytes

	Primitive(PrimitiveType t, const Eigen::Vector3f& c, const Eigen::Matrix4f trans) {
		type = static_cast<int>(t);
		color = Eigen::Vector4f(c[0], c[1], c[2], 1.0f);
		inv_transform = trans.inverse();

		// calcualate here the normal matrix
		Eigen::Matrix3f reduced_transform = trans.topLeftCorner<3, 3>(); // ignore omogeneous coordinate
		Eigen::Matrix3f normal_mat_3 = reduced_transform.inverse().transpose();
		normal_mat = Eigen::Matrix4f::Identity();
		normal_mat.topLeftCorner<3, 3>() = normal_mat_3;

		_pad[0] = _pad[1] = _pad[2] = 0; 
	}

	Primitive() : type(0), color(Eigen::Vector4f::Zero()), inv_transform(Eigen::Matrix4f::Identity()) {}
};

inline std::string printPrimitive(const PrimitiveType& type) {
	switch (type) {
		case PrimitiveType::NONE:
			return "None";
		case PrimitiveType::SPHERE:
			return "Sphere";
		case PrimitiveType::CUBE:
			return "Cube";
		case PrimitiveType::CYLINDER:
			return "Cylinder";
		default:
			return "Unknown";
	}
}

#endif // PRIMITIVE_H

