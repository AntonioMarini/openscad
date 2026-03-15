#ifndef CSGCOMMAND_H
#define CSGCOMMAND_H

enum class CSGCommandType : unsigned int {
  PRIMITIVE = 0,
  OPERATION = 1,
  CACHED_REF = 2,
};

// OBB data lives in a separate GPU buffer (obbsSSBO, binding=4)
struct alignas(16) CSGCommand {
  unsigned int type;           // 4 bytes
  unsigned int id;             // 4 bytes
  unsigned int skip_children;  // 4 bytes
  unsigned int duplicate_id;   // 0 = unique, >0 = shared duplicate

  CSGCommand(CSGCommandType t, unsigned int i)
  {
    type = static_cast<unsigned int>(t);
    id = i;
    skip_children = 0;
    duplicate_id = 0;
  }
  CSGCommand() : type(0), id(0), skip_children(0), duplicate_id(0) {}
};

#endif  // CSGCOMMAND_H
