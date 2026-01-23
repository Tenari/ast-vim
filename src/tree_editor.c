#include "base/impl.c"
#include "lib/tui.c"
#include "string_chunk.c"

///// #DEFINES
#define MAX_SCREEN_HEIGHT 300
#define MAX_SCREEN_WIDTH 800
#define GOAL_INPUT_LOOPS_PER_S 60
#define GOAL_INPUT_LOOP_US 1000000/GOAL_INPUT_LOOPS_PER_S
#define PRIMITIVE_TYPE_COUNT (30)

///// TYPES
typedef enum Command {
  CommandInsertSiblingBefore,
  CommandInsertSiblingAfter,
  CommandQuit,
  CommandMoveToParent,
  CommandMoveToFirstChild,
  Command_Count
} Command;

typedef struct Pointu32 {
  u32 x;
  u32 y;
} Pointu32;

typedef enum Mode {
  ModeNormal,
  ModeEdit,
  Mode_Count
} Mode;
str MODE_STRINGS[Mode_Count] = {"Normal", "Edit"};

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
  StringChunkList type;
  StringChunkList name;
} CDecl;

typedef struct CFnDetails {
  u8 arg_count;
  StringChunkList name;
  StringChunkList return_type;
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
  bool show_command_palette;
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
  CommandPaletteCommandList commands;
  StringChunkList cmd_palette_search_input;
} State;

///// GLOBALS
// NOTE: keep these .id = fields in sync with `typedef enum Command`
global const CommandPaletteCommand COMMANDS[Command_Count] = {
  { .id = 0, .display_name = "Insert Sibling Node Before (I)",
    .description = "Insert a new node on the same conceptual 'level' of the tree.",
    .tags = {"new node", "insert", "sibling", "add node", "add item", "add element", "new item", "new element"},
  },
  { .id = 1, .display_name = "Insert Sibling Node After (i)",
    .description = "Insert a new node on the same conceptual 'level' of the tree.",
    .tags = {"new node", "insert", "sibling", "add node", "add item", "add element", "new item", "new element"},
  },
  { .id = 2, .display_name = "Quit (q)",
    .description = "Quit the application.",
    .tags = {"quit", "exit", "close"},
  },
  { .id = 3, .display_name = "Move to Parent Node (h/←)",
    .description = "Move the cursor to the node's parent node.",
    .tags = {"parent", "move", "up", "left"},
  },
  { .id = 4, .display_name = "Move to First Child Node (l/→)",
    .description = "Move the cursor to the node's first child node.",
    .tags = {"child", "move", "down", "right"},
  },
};

global str PRIMITIVE_TYPES[PRIMITIVE_TYPE_COUNT] = {
  "bool", "char", "int", "float", "double", "short", "long",
  "signed char", "unsigned char",
  "short int", "signed short", "signed short int",
  "unsigned short", "unsigned short int",
  "signed", "signed int",
  "unsigned", "unsigned int",
  "long int", "signed long", "signed long int",
  "unsigned long", "unsigned long int",
  "long long", "long long int", "signed long long", "signed long long int",
  "unsigned long long", "unsigned long long int",
  "long double"
};
global const String EMPTY_STRING = {
  .bytes = "",
  .length = 0,
  .capacity = 1,
};

global const String DEFAULT_RETURN_TYPE = {
  .bytes = "int",
  .length = 3,
  .capacity = 4,
};

global const String DEFAULT_FUNCTION_NAME = {
  .bytes = "myFunction",
  .length = 10,
  .capacity = 11,
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

  Pointu32 result = {.y = 1,};
  node->render_start.x = x;
  node->render_start.y = y;
  u32 pos = x + (y*tui->screen_dimensions.width);

  // print function's return type
  if (node->function.return_type.total_size > 0) {
    renderStringChunkList(tui, &node->function.return_type, x+result.x, y);
    // and color it green
    for (u32 i = 0; i < node->function.return_type.total_size; i++) {
      tui->frame_buffer[pos+result.x+i].foreground = ANSI_HIGHLIGHT_GREEN;
    }
    result.x += node->function.return_type.total_size;
  } else {
    for (u32 i = 0; i < DEFAULT_RETURN_TYPE.length; i++) {
      tui->frame_buffer[pos+result.x+i].foreground = ANSI_DULL_GREEN;
      tui->frame_buffer[pos+result.x+i].bytes[0] = DEFAULT_RETURN_TYPE.bytes[i];
    }
    result.x += DEFAULT_RETURN_TYPE.length;
  }
  result.x += 1; // space
  // print function's name
  if (node->function.name.total_size > 0) {
    renderStringChunkList(tui, &node->function.name, x+result.x, y);
    result.x += node->function.name.total_size;
  } else {
    for (u32 i = 0; i < DEFAULT_FUNCTION_NAME.length; i++) {
      tui->frame_buffer[pos+result.x+i].foreground = ANSI_DULL_GRAY;
      tui->frame_buffer[pos+result.x+i].bytes[0] = DEFAULT_FUNCTION_NAME.bytes[i];
    }
    result.x += DEFAULT_FUNCTION_NAME.length;
  }
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
      // TODO render this with foreground ANSI_DULL_GRAY if the node is the currently selected node AND we are in insert mode
      //tui->frame_buffer[pos].foreground = ANSI_DULL_GRAY;
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

fn PtrArray listMatchingTypes(Arena* a, StringChunkList list) {
  String type_name = stringChunkToString(a, list);
  u32 matching_count = 0;
  for (u32 i = 0; i < PRIMITIVE_TYPE_COUNT; i++) {
    if (strstr(PRIMITIVE_TYPES[i], type_name.bytes) != NULL) {
      matching_count += 1;
    }
  }
  PtrArray result = {
    .length = matching_count,
    .capacity = matching_count,
    .items = arenaAllocArray(a, ptr, matching_count),
  };
  u32 result_index = 0;
  for (u32 i = 0; i < PRIMITIVE_TYPE_COUNT; i++) {
    if (strstr(PRIMITIVE_TYPES[i], type_name.bytes) != NULL) {
      result.items[result_index] = (ptr)PRIMITIVE_TYPES[i];
      result_index += 1;
    }
  }
  arenaDealloc(a, type_name.capacity);
  return result;
}

fn bool doCommand(State* s, u32 cmd_id) {
  bool result = true;
  Command cmd_type = (Command)cmd_id;
  switch (cmd_type) {
    case CommandInsertSiblingAfter: {
      // insert sibling BELOW
      s->mode = ModeEdit;
      s->selected_node = addNode(&s->tree, NodeTypeIncomplete, s->selected_node->parent);
    } break;
    case CommandInsertSiblingBefore: {
      // insert sibling ABOVE
      s->mode = ModeEdit;
      s->selected_node = addNodeBeforeSibling(&s->tree, NodeTypeIncomplete, s->selected_node->parent, s->selected_node);
    } break;
    case CommandMoveToParent: {
      s->selected_node = s->selected_node->parent;
    } break;
    case CommandMoveToFirstChild: {
      if (s->selected_node->first_child != NULL) {
        s->selected_node = s->selected_node->first_child;
      }
    } break;
    case CommandQuit: {
      s->should_quit = true;
    } break;

    default:
    case Command_Count:
      result = false;
    break;
  }
  return result;
}

fn bool updateAndRender(TuiState* tui, void* state, u8* input_buffer, u64 loop_count) {
  State* s = (State*)state;
  ScratchMem scratch = scratchGet();

  // "always" rendering logic
  // indicate if we saved
  if (s->saved_on && (loop_count - s->saved_on < 100)) {
    renderStrToBuffer(tui->frame_buffer, 1, 0, "saved", tui->screen_dimensions);
  }
  // indicate what mode we are in
  renderStrToBuffer(tui->frame_buffer, 0, 0, MODE_STRINGS[s->mode], tui->screen_dimensions);
  // MAIN RENDER of CODE TREE
  for (u32 i = 0; i < s->views.nodes[s->selected_view].length; i++) {
    CNode* node = &s->views.nodes[s->selected_view].nodes[i];
    renderNode(tui, 2 + (2*tui->screen_dimensions.width), node);
  }
  tui->cursor.x = s->selected_node->render_start.x;
  tui->cursor.y = s->selected_node->render_start.y;

  // input/mode-dependent rendering logic
  String input_string = {
    .bytes = (ptr)input_buffer,
    .length = strlen((ptr)input_buffer),
    .capacity = strlen((ptr)input_buffer)+1,
  };
  bool left_arrow_pressed = input_buffer[0] == 27 && input_buffer[1] == 91 && input_buffer[2] == 68;
  bool right_arrow_pressed = input_buffer[0] == 27 && input_buffer[1] == 91 && input_buffer[2] == 67;
  bool down_arrow_pressed = input_buffer[0] == 27 && input_buffer[1] == 91 && input_buffer[2] == 66;
  bool up_arrow_pressed = input_buffer[0] == 27 && input_buffer[1] == 91 && input_buffer[2] == 65;
  bool esc_pressed = input_buffer[0] == ASCII_ESCAPE && input_buffer[1] == 0;
  bool tab_pressed = input_buffer[0] == ASCII_TAB;
  bool enter_pressed = input_buffer[0] == ASCII_RETURN || input_buffer[0] == ASCII_LINE_FEED;
  bool backspace_pressed = input_buffer[0] == ASCII_BACKSPACE || input_buffer[0] == ASCII_DEL;
  switch (s->mode) {
    case ModeNormal: {
      if (s->show_command_palette) {
        String search = stringChunkToString(&scratch.arena, s->cmd_palette_search_input);

        if (esc_pressed) {
          s->show_command_palette = false;
        } else if (isAlphaUnderscoreSpace(input_buffer[0])) {
          stringChunkListAppend(&s->string_arena, &s->cmd_palette_search_input, input_string);
        } else if (backspace_pressed) {
          stringChunkListDeleteLast(&s->string_arena, &s->cmd_palette_search_input);
        } else if (up_arrow_pressed) {
          s->menu_index -= 1;
        } else if (down_arrow_pressed) {
          s->menu_index += 1;
        } else if (enter_pressed || tab_pressed) {
          u32* scores = arenaAllocArray(&scratch.arena, u32, s->commands.length);
          StringSearchScore* score_details = arenaAllocArray(&scratch.arena, StringSearchScore, s->commands.length);
          u32 cmd_id = matchCommandPaletteCommands(search, s->commands, s->menu_index, scores, score_details);
          s->menu_index = 0;
          s->show_command_palette = false;
          assert(doCommand(s, cmd_id));
          break;
        }

        Pos2 cursor = renderCommandPalette(tui, search, s->commands, s->menu_index);
        tui->cursor.x = cursor.x;
        tui->cursor.y = cursor.y;
      } else {
        if (input_buffer[0] == 'q' && input_buffer[1] == 0) {
          assert(
              doCommand(s, (u32)CommandQuit)
              && "CommandQuit failed"
          );
        } else if (input_buffer[0] == '?') {
          s->show_command_palette = true;
          s->menu_index = 0;
        } else if (left_arrow_pressed || input_buffer[0] == 'h') {
          doCommand(s, (u32)CommandMoveToParent);
        } else if (right_arrow_pressed || input_buffer[0] == 'l') {
          doCommand(s, (u32)CommandMoveToFirstChild);
        } else if (down_arrow_pressed || input_buffer[0] == 'j') {
          if (s->selected_node->next_sibling != NULL) {
            s->selected_node = s->selected_node->next_sibling;
          }
        } else if (up_arrow_pressed || input_buffer[0] == 'k') {
          if (s->selected_node->prev_sibling != NULL) {
            s->selected_node = s->selected_node->prev_sibling;
          }
        } else if (input_buffer[0] == 'I' && input_buffer[1] == 0) {
          doCommand(s, (u32)CommandInsertSiblingBefore);
        } else if (input_buffer[0] == 'i' && input_buffer[1] == 0) {
          doCommand(s, (u32)CommandInsertSiblingAfter);
        } else if (input_buffer[0] == 'e' && input_buffer[1] == 0) {
          s->mode = ModeEdit;
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
      }
    } break;
    case ModeEdit: {
      if (input_buffer[0] == ASCII_ESCAPE && input_buffer[1] == 0) {
        s->mode = ModeNormal;
      }
      switch (s->selected_node->type) {
        case NodeTypeIncomplete: {
          // handle input
          if (input_buffer[0] == 'f' && input_buffer[1] == 0) {
            s->selected_node->type = NodeTypeFunction;
            s->selected_node->function.name = allocStringChunkList(&s->string_arena, EMPTY_STRING);
            s->selected_node->function.return_type = allocStringChunkList(&s->string_arena, EMPTY_STRING);
          } else if (input_buffer[0] == 'r' && input_buffer[1] == 0) {
            s->selected_node->type = NodeTypeReturn;
          }

          // render
          tui->frame_buffer[8].foreground = ANSI_HP_RED;
          tui->frame_buffer[8].bytes[0] = 'f';
          renderStrToBuffer(tui->frame_buffer, 9, 0, ": function", tui->screen_dimensions);
          tui->frame_buffer[19].foreground = ANSI_HP_RED;
          tui->frame_buffer[19].bytes[0] = 'r';
          renderStrToBuffer(tui->frame_buffer, 20, 0, ": return", tui->screen_dimensions);
        } break;
        case NodeTypeFunction: {
          // handle input
          if (s->node_section == 0) { // editing fn declaration return type section
            if (down_arrow_pressed) {
              s->menu_index += 1;
            } else if (up_arrow_pressed) {
              s->menu_index -= 1;
            } else if (tab_pressed || enter_pressed) {
              PtrArray matching_types = listMatchingTypes(&scratch.arena, s->selected_node->function.return_type);
              //printf("%d", matching_types.length);
              releaseStringChunkList(&s->string_arena, &s->selected_node->function.return_type);
              String temp = {
                .bytes = matching_types.items[s->menu_index],
                .length = strlen(matching_types.items[s->menu_index]),
                .capacity = strlen(matching_types.items[s->menu_index]) + 1,
              };
              s->selected_node->function.return_type = allocStringChunkList(&s->string_arena, temp);
              s->menu_index = 0;
              s->node_section += 1;
            } else if (isAlphaUnderscoreSpace(input_buffer[0])) {
              stringChunkListAppend(&s->string_arena, &s->selected_node->function.return_type, input_string);
            } else if (backspace_pressed) {
              stringChunkListDeleteLast(&s->string_arena, &s->selected_node->function.return_type);
            }
          } else if (s->node_section == 1) { // editing fn declaration identifier/name section
            if (backspace_pressed) {
              stringChunkListDeleteLast(&s->string_arena, &s->selected_node->function.name);
            } else if (enter_pressed || tab_pressed) {
              s->selected_node->function.arg_count += 1;
              s->node_section += 1;
            } else if (isSimplePrintable(input_buffer[0])) {
              stringChunkListAppend(&s->string_arena, &s->selected_node->function.name, input_string);
            }
          } else { // editing fn decl args list
          }

          // render
          renderStrToBuffer(tui->frame_buffer, 8, 0, "Choose Function Return Type", tui->screen_dimensions);
          renderStrToBuffer(tui->frame_buffer, 40, 0, "Name Function", tui->screen_dimensions);
          if (s->node_section == 0) { // editing fn declaration return type section
            tui->cursor.x = s->selected_node->render_start.x + s->selected_node->function.return_type.total_size;
            tui->cursor.y = s->selected_node->render_start.y;
            PtrArray matching_types = listMatchingTypes(&scratch.arena, s->selected_node->function.return_type);
            u32 pos = s->selected_node->render_start.x + (tui->screen_dimensions.width * (s->selected_node->render_start.y+1));
            u32 list_size = Min(matching_types.length, 5);
            u32 goal_i = list_size;
            if (s->menu_index > (list_size/2)) {
              goal_i = Min(s->menu_index + (list_size/2), matching_types.length);
            }
            for (u32 i = goal_i - list_size; i < goal_i; i++) {
              u32 row_pos = pos+((list_size - (goal_i - i))*tui->screen_dimensions.width);
              for (u32 j = 0; j < 24; j++) {
                if (s->menu_index == i) {
                  tui->frame_buffer[row_pos+j].background = 230;
                  tui->frame_buffer[row_pos+j].foreground = 33;
                } else {
                  tui->frame_buffer[row_pos+j].background = 33;
                  tui->frame_buffer[row_pos+j].foreground = 230;
                }
                if (j < strlen(matching_types.items[i])) {
                  tui->frame_buffer[row_pos+j].bytes[0] = matching_types.items[i][j];
                } else {
                  tui->frame_buffer[row_pos+j].bytes[0] = ' ';
                }
              }
            }
          } else if (s->node_section == 1) { // editing fn declaration identifier/name section
            tui->cursor.x = s->selected_node->render_start.x + s->selected_node->function.return_type.total_size + 1 + s->selected_node->function.name.total_size;
            tui->cursor.y = s->selected_node->render_start.y;
            u32 pos = tui->cursor.x + (tui->screen_dimensions.width * (s->selected_node->render_start.y));
            for (u32 i = 0; i < s->selected_node->function.name.total_size; i++) {
              tui->frame_buffer[pos+i].foreground = ANSI_DULL_GRAY;
            }
          }
        } break;
        case NodeTypeRoot: {
          // TODO message that you can't edit the root node
        } break;
        case NodeTypeStatement:
        case NodeTypeExpression:
        case NodeTypeInvalid:
        case NodeTypeReturn:
        case NodeTypeNumericLiteral:
        case NodeTypeBlock:
        case NodeType_Count:
          break;
      }
    } break;
    case Mode_Count: {
      printf("error");
      s->should_quit = true;
    } break;
  }

  scratchReturn(&scratch);
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

  state.commands.length = Command_Count;
  state.commands.items = arenaAllocArray(&state.permanent_arena, CommandPaletteCommand, state.commands.length);
  for (u32 i = 0; i < state.commands.length; i++) {
    state.commands.items[i] = COMMANDS[i];
  }
  state.cmd_palette_search_input = allocStringChunkList(&state.string_arena, EMPTY_STRING);

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
  fn_node->function.return_type = allocStringChunkList(&state.string_arena, DEFAULT_RETURN_TYPE);

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
