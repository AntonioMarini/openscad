#ifndef OPERATION_H
#define OPERATION_H

enum class OperationType : int {
	NONE = 0,
	UNION = 1,
	INTERSECTION = 2,
	DIFFERENCE = 4
};

struct alignas(16) Operation {
	int type;
	unsigned int left_id;
	unsigned int right_id;
	int _pad; // padding to align to 16 bytes

	Operation(OperationType t, unsigned int left, unsigned int right)
		: left_id(left), right_id(right), _pad(0) {
		type = static_cast<int>(t);
	}

	Operation() : type(0), left_id(0), right_id(0), _pad(0) {}
};

#endif // OPERATION_H