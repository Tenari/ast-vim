#include "base/impl.c"

typedef enum NodeType {
  NodeTypeRoot,
  NodeTypeFunction,
  NodeTypeBlock,
  NodeTypeReturn,
  NodeTypeNumericLiteral,
  NodeType_Count
} NodeType;

typedef struct CNode {
  NodeType type;
  struct CNode* children;
  u32 child_count;
} CNode;

i32 main(i32 argc, ptr argv[]) {
  /*
  if (argc == 1) {
    puts("you gotta pass the filename you're editing");
    return -1;
  }
  */
  Arena permanent_arena = {0};
  arenaInit(&permanent_arena);

  /*
  String filename = {
    .bytes = argv[1],
    .capacity = strlen(argv[1])+1,
    .length = strlen(argv[1]),
  };
  String file;
  bool FILE_EXISTED = osFileExists(filename);
  if (FILE_EXISTED) {
    file = osFileRead(&permanent_arena, filename.bytes);
  } else {
    // default empty source tree
    file.bytes = "int main() { return 0; }";
    file.capacity = strlen(file.bytes) + 1;
    file.length = strlen(file.bytes);
  }
  */

  CNode root = { .type = NodeTypeRoot };

  return 0;
}
