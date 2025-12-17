#include "base/impl.c"
#include "lib/tui.c"
#include "string_chunk.c"

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
  NodeTypeIncomplete,
  NodeTypeRoot,
  NodeTypeFunction,
  NodeTypeBlock,
  NodeTypeReturn,
  NodeTypeNumericLiteral,
  NodeTypeStatement,
  NodeTypeExpression,
  NodeType_Count
} NodeType;

typedef struct CDecl {
  String type;
  String name;
} CDecl;

typedef struct CFnDetails {
  StringChunkList name;
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
  u32 length;
  u32 capacity;
  CNode* nodes;
} Nodes;

typedef struct Views {
  u32 length;
  u32 capacity;
  Arena arena;
  Nodes* nodes;
} Views;

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
  u32 selected_view;
  Views views;
  u32 node_section;
  u32 menu_index;
  StringArena string_arena;
  Arena permanent_arena;
  String type_input_buffer;
} State;

///// GLOBALS
global str TYPES[] = {"int", "float", "char", "double", "unsigned int"};
global const String DEFAULT_RETURN_TYPE = {
  .bytes = "int",
  .length = 3,
  .capacity = 4,
};

fn Pointu32 renderNode(TuiState* tui, u32 pos, CNode* node);

///// functions()
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

fn CNode* addNodeBeforeSibling(CTree* tree, NodeType type, CNode* parent, CNode* sibling) {
  if (tree->capacity == tree->length) {
    arenaAllocArray(&tree->arena, CNode, tree->capacity);
    tree->capacity *= 2;
  }
  CNode* node = &tree->nodes[tree->length++];
  node->id = tree->next_id++;
  node->type = type;
  node->parent = parent;
  node->next_sibling = sibling;
  if (parent->child_count == 0) {
    assert(parent->first_child == NULL);
    parent->first_child = node;
  } else {
    CNode* iter = parent->first_child;
    if (iter == sibling) {
      parent->first_child = node;
      node->prev_sibling = NULL;
      sibling->prev_sibling = node;
    } else {
      while (iter->next_sibling != NULL) {
        if (iter->next_sibling == sibling) {
          iter->next_sibling = node;
          sibling->prev_sibling = iter;
          node->prev_sibling = iter;
          break;
        }
        iter = iter->next_sibling;
      }
    }
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

  u32 pos = x + (y*tui->screen_dimensions.width);
  for (u32 i = 0; i < node->numeric_literal.length; i++) {
    tui->frame_buffer[pos+i].foreground = ANSI_HIGHLIGHT_RED;
  }
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

  tui->frame_buffer[pos+(result.x)].foreground = ANSI_HIGHLIGHT_YELLOW;
  tui->frame_buffer[pos+(result.x++)].bytes[0] = 'r';
  tui->frame_buffer[pos+(result.x)].foreground = ANSI_HIGHLIGHT_YELLOW;
  tui->frame_buffer[pos+(result.x++)].bytes[0] = 'e';
  tui->frame_buffer[pos+(result.x)].foreground = ANSI_HIGHLIGHT_YELLOW;
  tui->frame_buffer[pos+(result.x++)].bytes[0] = 't';
  tui->frame_buffer[pos+(result.x)].foreground = ANSI_HIGHLIGHT_YELLOW;
  tui->frame_buffer[pos+(result.x++)].bytes[0] = 'u';
  tui->frame_buffer[pos+(result.x)].foreground = ANSI_HIGHLIGHT_YELLOW;
  tui->frame_buffer[pos+(result.x++)].bytes[0] = 'r';
  tui->frame_buffer[pos+(result.x)].foreground = ANSI_HIGHLIGHT_YELLOW;
  tui->frame_buffer[pos+(result.x++)].bytes[0] = 'n';
  tui->frame_buffer[pos+(result.x)].foreground = ANSI_HIGHLIGHT_YELLOW;
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
  String rt = node->function.return_type;
  if (rt.bytes == NULL) {
    rt = DEFAULT_RETURN_TYPE;
  }

  Pointu32 result = {.y = 1,};
  node->render_start.x = x;
  node->render_start.y = y;
  u32 pos = x + (y*tui->screen_dimensions.width);

  // print function's return type
  for (u32 i = 0; i < rt.length; i++, result.x++) {
    tui->frame_buffer[pos+result.x].bytes[0] = rt.bytes[i];
    tui->frame_buffer[pos+result.x].foreground = ANSI_HIGHLIGHT_GREEN;
  }
  result.x += 1; // space
  // print function's name
  renderStringChunkList(tui, &node->function.name, x+result.x, y);
  result.x += node->function.name.total_size;
  tui->frame_buffer[pos+result.x++].bytes[0] = '(';
  tui->frame_buffer[pos+result.x++].bytes[0] = ')';
  result.x += 1; // space
  tui->frame_buffer[pos+result.x++].bytes[0] = '{';

  // recursively print the children
  for (CNode* child = node->first_child; child != NULL; child = child->next_sibling) {
    pos = x+2 + ((y+(result.y))*tui->screen_dimensions.width);
    Pointu32 used = renderNode(tui, pos, child);
    result.y += used.y;
  }

  // print final closing brace
  pos = x + (y*tui->screen_dimensions.width);
  tui->frame_buffer[pos+((result.y++) * tui->screen_dimensions.width)].bytes[0] = '}';

  result.y++; // final trailing empty line for visual appeal

  return result;
}

fn Pointu32 renderBlockNode(TuiState* tui, u32 pos, CNode* node) {
  assert(node->type == NodeTypeBlock);

  u32 width = tui->screen_dimensions.width;
  Pointu32 result = {.y = 1,};
  node->render_start.x = decompose(pos, width).x;
  node->render_start.y = decompose(pos, width).y;

  tui->frame_buffer[pos+(result.x++)].bytes[0] = '{';
  tui->frame_buffer[pos+(result.y*width)].bytes[0] = '}';

  CNode* child = node->first_child;
  if (child && child->type == NodeTypeNumericLiteral && child->numeric_literal.length <= 6) {
    Pointu32 decomp = decompose(pos + result.x, tui->screen_dimensions.width);
    Pointu32 used = renderNumericLiteralNode(tui, decomp.x + 1, decomp.y, child);
    tui->frame_buffer[pos+result.x+child->numeric_literal.length+1].bytes[0] = ';';
    result.x += used.x+1;
  }

  return result;
}

fn Pointu32 renderNode(TuiState* tui, u32 pos, CNode* node) {
  Pointu32 decomp = decompose(pos, tui->screen_dimensions.width);
  Pointu32 result = {0};
  node->render_start.x = decomp.x;
  node->render_start.y = decomp.y;

  switch (node->type) {
    case NodeTypeRoot: {
      u32 inc = 0;
      for (CNode* n = node->first_child; n != NULL; n = n->next_sibling) {
        Pointu32 used = renderNode(tui, pos+inc, n);
        result.y += used.y;
        result.x = Max(result.x, used.x);
        inc += (used.y * tui->screen_dimensions.width);
      }
    } break;
    case NodeTypeInvalid:
    case NodeType_Count:
      break;
    case NodeTypeFunction:
      return renderFunctionNode(tui, decomp.x, decomp.y, node);
    case NodeTypeReturn:
      return renderReturnNode(tui, pos, node);
    case NodeTypeNumericLiteral:
      return renderReturnNode(tui, pos, node);
    case NodeTypeBlock:
      return renderBlockNode(tui, pos, node);
    case NodeTypeIncomplete: {
      //if () {
      //}
      // TODO render this with foreground ANSI_GRAY if the node is the currently selected node AND we are in insert mode
      //tui->frame_buffer[pos].foreground = ANSI_GRAY;
      tui->frame_buffer[pos].bytes[0] = '_';
      tui->frame_buffer[pos+1].bytes[0] = '_';
      tui->frame_buffer[pos+2].bytes[0] = '_';
      tui->frame_buffer[pos+3].bytes[0] = '_';
      result.x += 4;
      result.y += 1;
      return result;
    } break;
  }

  return result;
}

fn bool updateAndRender(TuiState* tui, void* state, u8* input_buffer, u64 loop_count) {
  State* s = (State*)state;
  bool left_arrow_pressed = input_buffer[0] == 27 && input_buffer[1] == 91 && input_buffer[2] == 68;
  bool right_arrow_pressed = input_buffer[0] == 27 && input_buffer[1] == 91 && input_buffer[2] == 67;
  bool down_arrow_pressed = input_buffer[0] == 27 && input_buffer[1] == 91 && input_buffer[2] == 66;
  bool up_arrow_pressed = input_buffer[0] == 27 && input_buffer[1] == 91 && input_buffer[2] == 65;
  switch (s->mode) {
    case ModeNormal: {
      if (input_buffer[0] == 'q' && input_buffer[1] == 0) {
        s->should_quit = true;
      } else if (left_arrow_pressed) {
        s->selected_node = s->selected_node->parent;
      } else if (right_arrow_pressed) {
        if (s->selected_node->first_child != NULL) {
          s->selected_node = s->selected_node->first_child;
        }
      } else if (down_arrow_pressed) {
        if (s->selected_node->next_sibling != NULL) {
          s->selected_node = s->selected_node->next_sibling;
        }
      } else if (up_arrow_pressed) {
        if (s->selected_node->prev_sibling != NULL) {
          s->selected_node = s->selected_node->prev_sibling;
        }
      } else if (input_buffer[0] == 'I' && input_buffer[1] == 0) {
        // insert sibling ABOVE
        s->mode = ModeInsert;
        CNode* new_node = addNodeBeforeSibling(&s->tree, NodeTypeIncomplete, s->selected_node->parent, s->selected_node);
        s->selected_node = new_node;
      } else if (input_buffer[0] == 'i' && input_buffer[1] == 0) {
        // insert sibling BELOW
        s->mode = ModeInsert;
        CNode* new_node = addNode(&s->tree, NodeTypeIncomplete, s->selected_node->parent);
        s->selected_node = new_node;
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
      if (s->node_section == 1 && isAlphaUnderscoreSpace(input_buffer[0])) {
        String input_string = {
          .bytes = (ptr)input_buffer,
          .length = strlen((ptr)input_buffer),
          .capacity = strlen((ptr)input_buffer)+1,
        };
        stringChunkListAppend(&s->string_arena, &s->selected_node->function.name, input_string);
      } else {
        if (down_arrow_pressed) {
          s->menu_index += 1;
        } else if (up_arrow_pressed) {
          s->menu_index -= 1;
        } else if (input_buffer[0] == ASCII_TAB) {
          s->selected_node->function.return_type.bytes = (ptr)TYPES[s->menu_index];
          s->selected_node->function.return_type.length = strlen(TYPES[s->menu_index]);
          s->selected_node->function.return_type.capacity = s->selected_node->function.return_type.length + 1;
          s->menu_index = 0;
          s->node_section += 1;
        } else if (input_buffer[0] == 'f' && input_buffer[1] == 0) {
          s->selected_node->type = NodeTypeFunction;
          String default_fn_name = {
            .bytes = "myFunction",
            .length = 10,
            .capacity = 11,
          };
          s->selected_node->function.name = allocStringChunkList(&s->string_arena, default_fn_name);
          s->selected_node->function.return_type.bytes = NULL;
        } else if (input_buffer[0] == 'r' && input_buffer[1] == 0) {
          // TODO return type node
        }
      }
    } break;
    case Mode_Count: {
      printf("error");
      s->should_quit = true;
    } break;
  }

  // indicate if we saved
  if (s->saved_on && (loop_count - s->saved_on < 100)) {
    renderStrToBuffer(tui->frame_buffer, 1, 0, "saved", tui->screen_dimensions);
  }
  // indicate what mode we are in
  renderStrToBuffer(tui->frame_buffer, 0, 0, MODE_STRINGS[s->mode], tui->screen_dimensions);
  // MAIN RENDER of CODE TREE
  //renderFunctionNode(tui, 2, 2, s->function_node);
  for (u32 i = 0; i < s->views.nodes[s->selected_view].length; i++) {
    CNode* node = &s->views.nodes[s->selected_view].nodes[i];
    renderNode(tui, 2 + (2*tui->screen_dimensions.width), node);
  }
  switch(s->mode) {
    case ModeInsert: {
      if (s->selected_node->type == NodeTypeIncomplete) {
        tui->frame_buffer[8].foreground = ANSI_HP_RED;
        tui->frame_buffer[8].bytes[0] = 'f';
        renderStrToBuffer(tui->frame_buffer, 9, 0, ": function", tui->screen_dimensions);

        tui->frame_buffer[19].foreground = ANSI_HP_RED;
        tui->frame_buffer[19].bytes[0] = 'r';
        renderStrToBuffer(tui->frame_buffer, 20, 0, ": return", tui->screen_dimensions);
      } else if (s->selected_node->type == NodeTypeFunction) {
        renderStrToBuffer(tui->frame_buffer, 8, 0, "Choose Function Return Type", tui->screen_dimensions);
        renderStrToBuffer(tui->frame_buffer, 40, 0, "Name Function", tui->screen_dimensions);

        if (s->node_section == 0) {
          u32 pos = s->selected_node->render_start.x + (tui->screen_dimensions.width * (s->selected_node->render_start.y+1));
          for (u32 i = 0; i < 5; i++) {
            u32 row_pos = pos+(i*tui->screen_dimensions.width);
            for (u32 j = 0; j < 16; j++) {
              if (s->menu_index == i) {
                tui->frame_buffer[row_pos+j].background = 230;
                tui->frame_buffer[row_pos+j].foreground = 33;
              } else {
                tui->frame_buffer[row_pos+j].background = 33;
                tui->frame_buffer[row_pos+j].foreground = 230;
              }
              if (j < strlen(TYPES[i])) {
                tui->frame_buffer[row_pos+j].bytes[0] = TYPES[i][j];
              } else {
                tui->frame_buffer[row_pos+j].bytes[0] = ' ';
              }
            }
          }
        } else {
          tui->cursor.x = s->selected_node->render_start.x + s->selected_node->function.return_type.length + 1;
          tui->cursor.y = s->selected_node->render_start.y;
          u32 pos = tui->cursor.x + (tui->screen_dimensions.width * (s->selected_node->render_start.y));
          for (u32 i = 0; i < s->selected_node->function.name.total_size; i++) {
            tui->frame_buffer[pos+i].foreground = ANSI_GRAY;
          }
        }
      }
    } break;
    case ModeNormal: {
      tui->cursor.x = s->selected_node->render_start.x;
      tui->cursor.y = s->selected_node->render_start.y;
    } break;
    case Mode_Count: {} break;
  }

  return s->should_quit;
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
  arenaInit(&state.permanent_arena);
  arenaInit(&state.string_arena.a);
  state.string_arena.mutex = newMutex();
  state.views.capacity = 32;
  arenaInit(&state.views.arena);
  state.views.nodes = arenaAllocArray(&state.views.arena, Nodes, state.views.capacity);
  state.tree = cTreeCreate();
  state.views.nodes[0].capacity = 1;
  state.views.nodes[0].length = 1;
  state.views.nodes[0].nodes = state.tree.nodes;// only works because it's a single root node. would have to alloc otherwise

  CNode* fn_node = addNode(&state.tree, NodeTypeFunction, state.tree.nodes);
  state.function_node = fn_node;
  state.selected_node = fn_node;
  String main_fn_name = {
    .bytes = "main",
    .length = 4,
    .capacity = 5,
  };
  fn_node->function.name = allocStringChunkList(&state.string_arena, main_fn_name);

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
    updateAndRender
  );

  return 0;
}
