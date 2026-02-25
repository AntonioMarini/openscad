#ifndef CSGCOMMAND_H
#define CSGCOMMAND_H

#include "OBB.h"

enum class CSGCommandType : unsigned int {
    PRIMITIVE = 0,
    OPERATION = 1
};

struct alignas(16) CSGCommand {
    unsigned int type; // 4 bytes
    unsigned int id;   // 4 bytes
    unsigned int skip_children; // 4 bytes, self + left_children + right_children: used to skip when pruning
    unsigned int _pad;
    OBB obb;

    CSGCommand(CSGCommandType t, unsigned int i) {
        type = static_cast<unsigned int>(t);
        id = i;
        skip_children = 0;
        _pad = 0;
    }
    CSGCommand() : type(0), id(0), skip_children(0), _pad(0) {}
};

#endif // CSGCOMMAND_H