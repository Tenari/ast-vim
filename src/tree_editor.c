//#include "../tree-sitter/lib/include/tree_sitter/api.h"
//#include "../tree-sitter-c/src/parser.c"
#include "base/impl.c"
#include "lib/tui.c"

///// #DEFINES
#define MAX_SCREEN_HEIGHT 300
#define MAX_SCREEN_WIDTH 800
#define GOAL_INPUT_LOOPS_PER_S 60
#define GOAL_INPUT_LOOP_US 1000000/GOAL_INPUT_LOOPS_PER_S

///// TYPES
typedef struct Pointu32 {
  u32 x;
  u32 y;
} Pointu32;

typedef enum Mode {
  ModeNormal,
  ModeInsert,
  Mode_Count
} Mode;
str MODE_STRINGS[Mode_Count] = {"Normal", "Insert"};

typedef enum NodeType {
  NodeTypeInvalid,
  NodeTypeRoot,
  NodeTypeFunction,
  NodeTypeBlock,
  NodeTypeReturn,
  NodeTypeNumericLiteral,
  NodeType_Count
} NodeType;

typedef struct CDecl {
  String type;
  String name;
} CDecl;

typedef struct CFnDetails {
  String name;
  String return_type;
  CDecl args[16];
} CFnDetails;

typedef struct CNode CNode;
struct CNode {
  NodeType type;
  u32 id;
  u32 child_count;
  u32 flags;
  CNode* parent;
  CNode* first_child;
  CNode* next_sibling;
  CNode* prev_sibling;
  Pointu32 render_start;
  union {
    CFnDetails function;
    String numeric_literal;
  };
};

typedef struct Nodes {
  CNode* nodes;
  u32 length;
} Nodes;

typedef struct CTree {
  u32 capacity;
  u32 length;
  u32 next_id;
  CNode* nodes;
  Arena arena;
} CTree;

typedef struct State {
  bool should_quit;
  bool pending_command;
  Mode mode;
  CNode* selected_node;
  CNode* function_node;
  u64 saved_on;
  CTree tree;
} State;

///// GLOBALS
//const TSLanguage *tree_sitter_c(void);

///// functions
fn Pointu32 decompose(u32 pos, u32 width) {
  Pointu32 result = {
    .x = pos % width,
    .y = pos / width,
  };
  return result;
}

fn CTree cTreeCreate() {
  CTree result = {
    .capacity = 64,
    .length = 0,
  };
  arenaInit(&result.arena);
  result.nodes = arenaAllocArray(&result.arena, CNode, result.capacity);
  CNode root_node = {
    .type = NodeTypeRoot,
    .id = result.length++,
  };
  root_node.parent = result.nodes; // points back to self
  result.nodes[0] = root_node;
  return result;
}

fn CNode* addNode(CTree* tree, NodeType type, CNode* parent) {
  if (tree->capacity == tree->length) {
    arenaAllocArray(&tree->arena, CNode, tree->capacity);
    tree->capacity *= 2;
  }
  CNode* node = &tree->nodes[tree->length++];
  node->id = tree->next_id++;
  node->type = type;
  node->parent = parent;
  if (parent->child_count == 0) {
    assert(parent->first_child == NULL);
    parent->first_child = node;
  } else {
    CNode* last_sibling = parent->first_child;
    while (last_sibling->next_sibling != NULL) {
      last_sibling = last_sibling->next_sibling;
    }
    assert(last_sibling->next_sibling == NULL);
    last_sibling->next_sibling = node;
    node->prev_sibling = last_sibling;
  }
  parent->child_count += 1;

  return node;
}

fn CNode getNode(CTree* tree, u32 node_id) {
  CNode result = {0};
  for (u32 i = 0; i < tree->length; i++) {
    if (tree->nodes[i].id == node_id) {
      return tree->nodes[i];
    }
  }
  return result;
}

fn Pointu32 renderNumericLiteralNode(TuiState* tui, u32 x, u32 y, CNode* node) {
  assert(node->type == NodeTypeNumericLiteral);
  Pointu32 result = {.y = 1,};
  node->render_start.x = x;
  node->render_start.y = y;

  renderStrToBuffer(
    tui->frame_buffer,
    x,
    y,
    node->numeric_literal.bytes,
    tui->screen_dimensions
  );
  result.x += node->numeric_literal.length;

  return result;
}

fn Pointu32 renderReturnNode(TuiState* tui, u32 pos, CNode* node) {
  assert(node->type == NodeTypeReturn);

  Pointu32 result = {.y = 1,};
  node->render_start.x = decompose(pos, tui->screen_dimensions.width).x;
  node->render_start.y = decompose(pos, tui->screen_dimensions.width).y;

  tui->frame_buffer[pos+(result.x++)].bytes[0] = 'r';
  tui->frame_buffer[pos+(result.x++)].bytes[0] = 'e';
  tui->frame_buffer[pos+(result.x++)].bytes[0] = 't';
  tui->frame_buffer[pos+(result.x++)].bytes[0] = 'u';
  tui->frame_buffer[pos+(result.x++)].bytes[0] = 'r';
  tui->frame_buffer[pos+(result.x++)].bytes[0] = 'n';
  tui->frame_buffer[pos+(result.x++)].bytes[0] = ' ';

  CNode* child = node->first_child;
  if (child && child->type == NodeTypeNumericLiteral && child->numeric_literal.length <= 6) {
    Pointu32 decomp = decompose(pos + result.x, tui->screen_dimensions.width);
    Pointu32 used = renderNumericLiteralNode(tui, decomp.x + 1, decomp.y, child);
    tui->frame_buffer[pos+result.x+child->numeric_literal.length+1].bytes[0] = ';';
    result.x += used.x+1;
  }

  return result;
}

fn Pointu32 renderFunctionNode(TuiState* tui, u16 x, u16 y, CNode* node) {
  assert(node->type == NodeTypeFunction);

  Pointu32 result = {.y = 1,};
  node->render_start.x = x;
  node->render_start.y = y;
  u32 pos = x + (y*tui->screen_dimensions.width);

  // print function's return type
  for (u32 i = 0; i < node->function.return_type.length; i++, result.x++) {
    tui->frame_buffer[pos+result.x].bytes[0] = node->function.return_type.bytes[i];
  }
  result.x += 1; // space
  // print function's name
  for (u32 i = 0; i < node->function.name.length; i++, result.x++) {
    tui->frame_buffer[pos+result.x].bytes[0] = node->function.name.bytes[i];
  }
  result.x += 1; // space
  tui->frame_buffer[pos+result.x++].bytes[0] = '(';
  tui->frame_buffer[pos+result.x++].bytes[0] = ')';
  result.x += 1; // space
  tui->frame_buffer[pos+result.x++].bytes[0] = '{';

  // TODO recursively print the children?
  for (CNode* child = node->first_child; child != NULL; child = child->next_sibling) {
    if (child->type == NodeTypeReturn) {
      Pointu32 used = renderReturnNode(tui, pos+2+(result.y * tui->screen_dimensions.width), child);
      result.y += used.y;
    }
  }

  // print final closing brace
  tui->frame_buffer[pos+((result.y++) * tui->screen_dimensions.width)].bytes[0] = '}';
  return result;
}

fn Pointu32 renderNode(TuiState* tui, u32 pos, CNode* node) {
  Pointu32 decomp = decompose(pos, tui->screen_dimensions.width);
  Pointu32 result = {0};

  switch (node->type) {
    case NodeTypeRoot:
    case NodeTypeInvalid:
    case NodeType_Count:
      break;
    case NodeTypeFunction:
      return renderFunctionNode(tui, decomp.x, decomp.y, node);
    case NodeTypeReturn:
      return renderReturnNode(tui, pos, node);
    case NodeTypeNumericLiteral:
      return renderReturnNode(tui, pos, node);
  }

  return result;
}

fn bool update(void* state, u8* input_buffer, u64 loop_count) {
  State* s = (State*)state;
  bool left_arrow_pressed = input_buffer[0] == 27 && input_buffer[1] == 91 && input_buffer[2] == 68;
  bool right_arrow_pressed = input_buffer[0] == 27 && input_buffer[1] == 91 && input_buffer[2] == 67;
  bool down_arrow_pressed = input_buffer[0] == 27 && input_buffer[1] == 91 && input_buffer[2] == 66;
  bool up_arrow_pressed = input_buffer[0] == 27 && input_buffer[1] == 91 && input_buffer[2] == 65;
  switch (s->mode) {
    case ModeNormal: {
      if (input_buffer[0] == 'q' && input_buffer[1] == 0) {
        s->should_quit = true;
      } else if (input_buffer[0] == 'i' && input_buffer[1] == 0) {
        s->mode = ModeInsert;
      } else if (up_arrow_pressed) {
        s->selected_node = s->selected_node->parent;
      } else if (down_arrow_pressed) {
        if (s->selected_node->first_child != NULL) {
          s->selected_node = s->selected_node->first_child;
        }
      } else if (right_arrow_pressed) {
        if (s->selected_node->next_sibling != NULL) {
          s->selected_node = s->selected_node->next_sibling;
        }
      } else if (left_arrow_pressed) {
        if (s->selected_node->prev_sibling != NULL) {
          s->selected_node = s->selected_node->prev_sibling;
        }
      } else if (input_buffer[0] == 'S' && input_buffer[1] == 0) {
        /*
        String data = {
          .bytes = arenaAllocArray(&permanent_arena, u8, byte_count_of_file),
          .capacity = byte_count_of_file,
          .length = byte_count_of_file,
        };
        for (u32 i = 0; i < ROOM_TILE_COUNT; i++) {
          data.bytes[i] = room.background[i];
          data.bytes[i+ROOM_TILE_COUNT] = room.foreground[i];
        }
        if (FILE_EXISTED) {
          osFileWrite(filename, data);
        } else {
          osFileCreateWrite(filename, data);
        }
        saved_on = loop_count;
        */
      }
    } break;
    case ModeInsert: {
      if (input_buffer[0] == ASCII_ESCAPE && input_buffer[1] == 0) {
        s->mode = ModeNormal;
      }
    } break;
    case Mode_Count: {
      printf("error");
      s->should_quit = true;
    } break;
  }

  return s->should_quit;
}

fn bool renderFrame(TuiState* tui, void* s, u64 loop_count) {
  ScratchMem scratch = scratchGet();
  State* state = (State*)s;

  // indicate if we saved
  if (state->saved_on && (loop_count - state->saved_on < 100)) {
    renderStrToBuffer(tui->frame_buffer, 1, 0, "saved", tui->screen_dimensions);
  }
  // indicate what mode we are in
  renderStrToBuffer(tui->frame_buffer, 0, 0, MODE_STRINGS[state->mode], tui->screen_dimensions);
  // MAIN RENDER of CODE TREE
  renderFunctionNode(tui, 2, 2, state->function_node);
  tui->cursor.x = state->selected_node->render_start.x;
  tui->cursor.y = state->selected_node->render_start.y;

  scratchReturn(&scratch);
  return state->should_quit;
}

i32 main(i32 argc, ptr argv[]) {
  osInit();
  ThreadContext tctx = {0};
  tctxInit(&tctx);
  // init state
  State state = {
    .should_quit = false,
    .pending_command = false,
    .mode = ModeNormal,
  };
  state.tree = cTreeCreate();

  CNode* fn_node = addNode(&state.tree, NodeTypeFunction, state.tree.nodes);
  state.function_node = fn_node;
  state.selected_node = fn_node;
  fn_node->function.name.bytes = "main";
  fn_node->function.name.length = 4;
  fn_node->function.name.capacity = 5;
  fn_node->function.return_type.bytes = "int";
  fn_node->function.return_type.length = 3;
  fn_node->function.return_type.capacity = 4;

  CNode* ret_node = addNode(&state.tree, NodeTypeReturn, fn_node);

  CNode* ret_literal_node = addNode(&state.tree, NodeTypeNumericLiteral, ret_node);
  ret_literal_node->numeric_literal.bytes = "0";
  ret_literal_node->numeric_literal.length = 1;
  ret_literal_node->numeric_literal.capacity = 2;

  // ui loop (read input, simulate next frame, render)
  infiniteUILoop(
    MAX_SCREEN_WIDTH,
    MAX_SCREEN_HEIGHT,
    GOAL_INPUT_LOOP_US,
    &state,
    update,
    renderFrame
  );

  //ts_tree_delete(tree);
  //ts_parser_delete(parser);
  return 0;
}
  /*
  if (argc == 1) {
    puts("you gotta pass the filename you're editing");
    return -1;
  }
  //TSParser* parser = ts_parser_new();
  //const TSLanguage* clang = tree_sitter_c();
  //ts_parser_set_language(parser, clang);
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
  TSTree *tree = ts_parser_parse_string(parser, NULL, file.bytes, file.length);
  TSNode root_node = ts_tree_root_node(tree);
  */

    /*
    u32 prev_child_count = 0;
    for (u32 i = 0; i < ts_node_named_child_count(root_node); i++) {
      TSNode node = ts_node_named_child(root_node, i);
      renderStrToBuffer(
        tui.frame_buffer,
        1,
        2+i+prev_child_count,
        ts_node_type(node),
        tui.screen_dimensions
      );
      prev_child_count = ts_node_named_child_count(node);
      for (u32 ii = 0; ii < prev_child_count; ii++) {
        TSNode inner_node = ts_node_named_child(node, ii);
        renderStrToBuffer(
          tui.frame_buffer,
          3,
          2+i+1+ii,
          ts_node_type(inner_node),
          tui.screen_dimensions
        );
      }
    }
    */
