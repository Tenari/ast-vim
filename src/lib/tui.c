#include "../base/all.h"
#include "../string_chunk.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#define UTF8_MAX_WIDTH 4
#define ANSI_HP_RED (196)
#define ANSI_MP_BLUE (33)
#define ANSI_LIGHT_GREEN (82)
#define ANSI_MID_GREEN (70)
#define ANSI_DARK_GREEN (35)
#define ANSI_BLACK (16)
#define ANSI_WHITE (15)
#define ANSI_BROWN (130)
#define ANSI_DULL_RED (1)
#define ANSI_HIGHLIGHT_RED (9)
#define ANSI_DULL_GREEN (2)
#define ANSI_HIGHLIGHT_GREEN (10)
#define ANSI_DULL_YELLOW (3)
#define ANSI_HIGHLIGHT_YELLOW (11)
#define ANSI_DULL_BLUE (4)
#define ANSI_HIGHLIGHT_BLUE (12)
#define ANSI_DULL_GRAY (7)
#define ANSI_HIGHLIGHT_GRAY (16)
#define MAX_COMMAND_PALETTE_COMMANDS (1000)

///// TYPES
typedef struct Pixel {
  u8 foreground;
  u8 background;
  u8 bytes[UTF8_MAX_WIDTH];
} Pixel;

typedef struct TuiState {
  bool redraw;
  ptr writeable_output_ansi_string;
  Pixel* frame_buffer;
  Pixel* back_buffer;
  u64 buffer_len; // how many Pixels
  Pos2 cursor;
  Pos2 prev_cursor;
  Dim2 screen_dimensions;
  Dim2 prev_screen_dimensions;
} TuiState;

typedef struct RGB {
    u8 r;
    u8 g;
    u8 b;
} RGB;

typedef struct CommandPaletteCommand {
  u32 id;
  ptr display_name;
  ptr description;
  ptr* tags;
} CommandPaletteCommand;

typedef struct CommandPaletteCommandList {
  u32 length;
  CommandPaletteCommand* items;
} CommandPaletteCommandList;

typedef struct StringSearchScore {
  bool name_matched;
  bool description_matched;
  u16 tag_match_count;
  u32 score;
  u32 name_match_start;
  u32 name_match_len;
  u32 description_match_start;
  u32 description_match_len;
} StringSearchScore;

///// Functions()
fn u32 rgbToNum(RGB rgb) {
    return ((rgb.r<<16) | (rgb.g<<8) | rgb.b);
}

fn u8 rgbToAnsi(RGB color) {
    u8 r = color.r / (255 / 5);
    u8 g = color.g / (255 / 5);
    u8 b = color.b / (255 / 5);
    return 16 + (36*r) + (6*g) + b;
}

fn RGB rgbDarken(RGB color, f32 factor) {
  RGB result = {
    .r = ((f32)color.r) * factor,
    .g = ((f32)color.g) * factor,
    .b = ((f32)color.b) * factor,
  };
  return result;
}

/*
fn RGB ansiToRGB(u8 ansi) {
  ansi -= 16;
  u8 b = ansi % 51;
  ansi -= ()
  u8 g = 
}
*/

fn bool rgbEq(RGB a, RGB b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

fn bool isPixelEq(Pixel a, Pixel b) {
  return a.background == b.background
      && a.foreground == b.foreground
      && a.bytes[0] == b.bytes[0]
      && a.bytes[1] == b.bytes[1]
      && a.bytes[2] == b.bytes[2]
      && a.bytes[3] == b.bytes[3]
    ;
}

fn TuiState tuiInit(Arena* a, u64 buffer_len) {
  TuiState result = {
    .redraw = false,
    .writeable_output_ansi_string = arenaAlloc(a, MB(1)),
    .buffer_len = buffer_len,
    .back_buffer = arenaAllocArray(a, Pixel, buffer_len), // allocate biggest possible dimensions
    .frame_buffer = arenaAllocArray(a, Pixel, buffer_len), // allocate biggest possible dimensions
  };
  MemoryZero(result.back_buffer, buffer_len * sizeof(Pixel));
  MemoryZero(result.frame_buffer, buffer_len * sizeof(Pixel));
  return result;
}

fn void copyStr(u8* bytes, str cstring) {
  for (u32 i = 0; i < strlen(cstring); i++) {
    bytes[i] = cstring[i];
  }
}

fn void drawAnsiBox(Pixel* buf, Box box, Dim2 sd, bool bold) {
  str items[] =   {"┌","─","┐","│","└","┘"};
  str b_items[] = {"┏","━","┓","┃","┗","┛"};
  ptr* use = (ptr*)items;
  if (bold) {
    use = (ptr*)b_items;
  }
  u32 pos = XYToPos(box.x, box.y, sd.width);

  // print the upper box border
  copyStr(buf[pos].bytes, use[0]);
  for (i32 i = 0; i < box.width; i++) {
    copyStr(buf[pos+1+i].bytes, use[1]);
  }
  copyStr(buf[pos+1+box.width].bytes, use[2]);

  // start printing the rows
  for (i32 i = 0; i < box.height; i++) {
    pos = XYToPos(box.x, (box.y+1+i), sd.width); // move cursor to beginning of the row
    copyStr(buf[pos].bytes, use[3]);
    copyStr(buf[pos+1+box.width].bytes, use[3]);
  }

  // print the bottom box border
  pos = XYToPos(box.x, (box.y+1+box.height), sd.width);
  copyStr(buf[pos].bytes, use[4]);
  for (i32 i = 0; i < box.width; i++) {
    copyStr(buf[pos+1+i].bytes, use[1]);
  }
  copyStr(buf[pos+1+box.width].bytes, use[5]);
}

fn void renderUtf8CharToBuffer(Pixel* buf, u16 x, u16 y, str text, Dim2 screen_dimensions) {
  assert(strlen(text) <= UTF8_MAX_WIDTH);
  u32 pos = x + (screen_dimensions.width*y);
  for (u32 i = 0; i < strlen(text); i++) {
    buf[pos].bytes[i] = text[i];
  }
}

fn void renderStrToBufferMaxWidth(Pixel* buf, u16 x, u16 y, str text, u16 width, Dim2 screen_dimensions) {
  u32 pos = x + (screen_dimensions.width*y);
  for (u32 i = 0; i < strlen(text); i++) {
    buf[pos + (i % width)].background = 0;
    buf[pos + (i % width)].foreground = 0;
    buf[pos + (i % width)].bytes[0] = text[i];
    if (i % width == (width-1)) {     // on last char i inside width
      pos += screen_dimensions.width; // move pos to next line
    }
  }
}

fn void renderStrToBufferMaxWidthWithoutChangingColor(Pixel* buf, u16 x, u16 y, str text, u16 width, Dim2 screen_dimensions) {
  u32 pos = x + (screen_dimensions.width*y);
  for (u32 i = 0; i < strlen(text); i++) {
    buf[pos + (i % width)].bytes[0] = text[i];
    if (i % width == (width-1)) {     // on last char i inside width
      pos += screen_dimensions.width; // move pos to next line
    }
  }
}

fn void renderStrToBuffer(Pixel* buf, u16 x, u16 y, str text, Dim2 screen_dimensions) {
  u32 pos = x + (screen_dimensions.width*y);
  for (u32 i = 0; i < strlen(text); i++) {
    buf[pos+i].bytes[0] = text[i];
  }
}

fn void renderStringChunkList(TuiState* tui, StringChunkList* list, u16 x, u16 y) {
  u32 pos = XYToPos(x, y, tui->screen_dimensions.width);
  StringChunk* chunk = list->first;
  for (u32 i = 0; i < list->total_size; i++) {
    if (i > 0 && i % STRING_CHUNK_PAYLOAD_SIZE == 0) {
      chunk = chunk->next;
    }
    tui->frame_buffer[pos+i].bytes[0] = *((char*)(chunk + 1) + (i%STRING_CHUNK_PAYLOAD_SIZE));
  }
}

fn u32 sprintfAnsiMoveCursorTo(ptr output, u16 x, u16 y) {
  return sprintf(output, "\x1b[%d;%df",y,x);
}

fn void printfBufferAndSwap(TuiState* tui) {
  Pixel* old = tui->back_buffer;
  Pixel* next = tui->frame_buffer;
  u32 length = tui->screen_dimensions.height * tui->screen_dimensions.width;
  bool screen_dimensions_changed = tui->screen_dimensions.height != tui->prev_screen_dimensions.height
    || tui->screen_dimensions.width != tui->prev_screen_dimensions.width;
  bool should_redraw_whole_screen = screen_dimensions_changed || tui->redraw;

  // "quick" exit this fn if old == next
  if (!should_redraw_whole_screen) {
    bool old_equals_new = true;
    for (u32 i = 0; i < length; i++) {
      if (old[i].background != next[i].background
          || old[i].foreground != next[i].foreground
          || old[i].bytes[0] != next[i].bytes[0]
          || old[i].bytes[1] != next[i].bytes[1]
          || old[i].bytes[2] != next[i].bytes[2]
          || old[i].bytes[3] != next[i].bytes[3]) {
        old_equals_new = false;
        break;
      }
    }
    if (old_equals_new
        && tui->prev_cursor.x == tui->cursor.x
        && tui->prev_cursor.y == tui->cursor.y
    ) {
      length += 0; // debugging helper line
      return; // skip all the write() and sprintf() calls, since the frames are the same.
    }
  }

  ptr output = tui->writeable_output_ansi_string;
  u8 bg = 0;
  u8 fg = 0;
  u16 x = 1;
  u16 y = 1;
  u16 last_x = 0;
  u16 last_y = 0;
  str RESET_STYLES = "\033[0m";
  str CLEAR_SCREEN = "\033[2J";

  if (should_redraw_whole_screen) {
    output += sprintf(output, "\033[0m\033[2J");
    output += sprintfAnsiMoveCursorTo(output, x, y);
    bool printed_last = false;
    for (u32 i = 0; i < length; i++) {
      x = (i % tui->screen_dimensions.width) + 1;
      y = (i / tui->screen_dimensions.width) + 1;
      if (next[i].bytes[0] != 0) {
        if (printed_last == false) {
          output += sprintfAnsiMoveCursorTo(output, x, y);
        }
        char bytes[5] = {0}; // do this nonsense to ensure null-terminated characters
        bytes[0] = next[i].bytes[0];
        bytes[1] = next[i].bytes[1];
        bytes[2] = next[i].bytes[2];
        bytes[3] = next[i].bytes[3];

        if (next[i].background != bg || next[i].foreground != fg) {
          bg = next[i].background;
          fg = next[i].foreground;
          if (bg == 0 && fg == 0) {
            output += sprintf(output, "%s%s", RESET_STYLES, bytes);
          } else if (bg != 0 && fg == 0) {
            output += sprintf(output, "\033[48;5;%dm%s", bg, bytes);
          } else if (bg == 0 && fg != 0) {
            output += sprintf(output, "\033[38;5;%dm%s", fg, bytes);
          } else {
            output += sprintf(output, "\033[48;5;%d;38;5;%dm%s", bg, fg, bytes);
          }
        } else {
          output += sprintf(output, "%s", bytes);
        }
        printed_last = true;
      } else {
        printed_last = false;
      }
    }
  } else {
    // clearing pass, to overwrite things that were there on the last frame, but are no longer present
    // we do this before the "rendering" pass so that multi-space characters (emojis) are easier to deal with
    output += sprintf(output, "%s", RESET_STYLES);
    output += sprintfAnsiMoveCursorTo(output, x, y);
    for (u32 i = 0; i < length; i++) {
      x = (i % tui->screen_dimensions.width) + 1;
      y = (i / tui->screen_dimensions.width) + 1;
      bool needs_clearing = (next[i].bytes[0] == 0 && old[i].bytes[0] != 0)
                         || (next[i].background == 0 && old[i].background != 0)
                         || (next[i].foreground == 0 && old[i].foreground != 0);
      if (needs_clearing) {
        bool last_printed_pos_is_adjacent_to_current_x_y = last_x+1 == x && last_y == y;
        if (!last_printed_pos_is_adjacent_to_current_x_y) {
          output += sprintfAnsiMoveCursorTo(output, x, y);
        }
        output += sprintf(output, " ");
        last_x = x;
        last_y = y;
      }
    }

    // rendering pass
    last_x = 0;
    last_y = 0;
    for (u32 i = 0; i < length; i++) {
      x = (i % tui->screen_dimensions.width) + 1;
      y = (i / tui->screen_dimensions.width) + 1;
      if (!isPixelEq(old[i], next[i])) {
        if (next[i].bytes[0] != 0) {
          bool last_printed_pos_is_adjacent_to_current_x_y = last_x+1 == x && last_y == y;
          if (!last_printed_pos_is_adjacent_to_current_x_y) {
            output += sprintfAnsiMoveCursorTo(output, x, y);
          } 

          u8 bytes[5] = {0}; // do this nonsense to ensure null-terminated characters
          bytes[0] = next[i].bytes[0];
          bytes[1] = next[i].bytes[1];
          bytes[2] = next[i].bytes[2];
          bytes[3] = next[i].bytes[3];

          if (next[i].background != bg || next[i].foreground != fg) {
            bg = next[i].background;
            fg = next[i].foreground;
            if (bg == 0 && fg == 0) {
              output += sprintf(output, "%s%s", RESET_STYLES, bytes);
            } else if (bg != 0 && fg == 0) {
              output += sprintf(output, "\033[48;5;%dm%s", bg, bytes);
            } else if (bg == 0 && fg != 0) {
              output += sprintf(output, "\033[38;5;%dm%s", fg, bytes);
            } else {
              output += sprintf(output, "\033[48;5;%d;38;5;%dm%s", bg, fg, bytes);
            }
          } else {
            output += sprintf(output, "%s", bytes);
          }

          last_x = x;
          last_y = y;
        }
      }
    }
  }

  output += sprintfAnsiMoveCursorTo(output, tui->cursor.x+1, tui->cursor.y+1); // re-position the cursor according to what the rendering logic set it to

  // finally write our whole string to the terminal
  i64 count = output - tui->writeable_output_ansi_string;
	osBlitToTerminal(tui->writeable_output_ansi_string, count);

  // swap our buffers
  Pixel* tmp = tui->back_buffer;
  tui->back_buffer = tui->frame_buffer;
  tui->frame_buffer = tmp;

  // reset the `redraw` flag
  tui->redraw = false;
}

fn u32 matchCommandPaletteCommands(String current_search, CommandPaletteCommandList commands, u32 menu_index, u32* scores, StringSearchScore* score_details) {
  // returns the `commands` id that matches the `menu_index`
  for (u32 i = 0; i < commands.length; i++) {
    CommandPaletteCommand* cmd = &commands.items[i];
    bool name_matches = false;
    for (u32 j = 0; j < strlen(cmd->display_name); j++) {
      if (current_search.length > 0 && lowerAscii(cmd->display_name[j]) == lowerAscii(current_search.bytes[0])) {
        for (u32 k = 0; k < current_search.length && k+j < strlen(cmd->display_name); k++) {
          if (lowerAscii(current_search.bytes[k]) != lowerAscii(cmd->display_name[j+k])) {
            break;
          }
          if (k == current_search.length - 1) {
            name_matches = true;
            score_details[i].name_match_start = j;
            score_details[i].name_match_len = current_search.length;
          }
        }
      }
    }
    bool description_matches = false;
    u32 tag_match_count = 0;
    scores[i] = (name_matches * 100 * MAX_COMMAND_PALETTE_COMMANDS) +
                (description_matches * 10 * MAX_COMMAND_PALETTE_COMMANDS) +
                (tag_match_count * MAX_COMMAND_PALETTE_COMMANDS) +
                cmd->id;
    score_details[i].score = scores[i];
    score_details[i].name_matched = name_matches;
    score_details[i].description_matched = description_matches;
    score_details[i].tag_match_count = tag_match_count;
  }
  u32Quicksort(scores, 0, commands.length - 1);
  u32ReverseArray(scores, commands.length);
  return (scores[menu_index] % MAX_COMMAND_PALETTE_COMMANDS);
}

fn Pos2 renderCommandPalette(TuiState* tui, String current_search, CommandPaletteCommandList commands, u32 menu_index) {
  // returns the cursor position as Pos2

  ScratchMem scratch = scratchGet();
  Dim2 sd = tui->screen_dimensions;
  Pos2 result = { 0 };

  // draw the outline
  Box outline = {
    .width = sd.width / 2,
    .height = sd.height / 2,
  };
  outline.x = outline.width / 2;
  outline.y = outline.height / 2;
  drawAnsiBox(tui->frame_buffer, outline, sd, true);

  // draw the "search bar"
  result.x = outline.x + 1 + current_search.length;
  result.y = outline.y + 1;
  renderStrToBufferMaxWidth(tui->frame_buffer, outline.x+1, outline.y+1, current_search.bytes, outline.width - 2, sd);
  for (u32 i = 1; i < outline.width-1; i++) {
    u32 pos = XYToPos(outline.x+1, outline.y+2, sd.width);
    copyStr(tui->frame_buffer[pos].bytes, "━");
  }

  // sort the command options
  u32* scores = arenaAllocArray(&scratch.arena, u32, commands.length);
  StringSearchScore* score_details = arenaAllocArray(&scratch.arena, StringSearchScore, commands.length);
  matchCommandPaletteCommands(current_search, commands, menu_index, scores, score_details);

  // draw the command options
  u32 x = outline.x + 1;
  u32 y = outline.y + 3;
  for (u32 i = 0; (y-outline.y) < (outline.height-1) && i < commands.length; i++, y+=2) {
    if (i == menu_index) {
      for (u32 ii = 0; ii < outline.width-1; ii++) {
        u32 pos = XYToPos(x+ii, y, tui->screen_dimensions.width);
        tui->frame_buffer[pos].bytes[0] = ' ';
        tui->frame_buffer[pos].foreground = ANSI_BLACK;
        tui->frame_buffer[pos].background = ANSI_WHITE;
        pos = XYToPos(x+ii, y+1, tui->screen_dimensions.width);
        tui->frame_buffer[pos].bytes[0] = ' ';
        tui->frame_buffer[pos].foreground = ANSI_BLACK;
        tui->frame_buffer[pos].background = ANSI_WHITE;
      }
    }
    u32 id = scores[i] % MAX_COMMAND_PALETTE_COMMANDS;
    StringSearchScore score = score_details[id];
    CommandPaletteCommand* cmd = NULL;
    for (u32 j = 0; j < commands.length; j++) {
      if (commands.items[j].id == id) {
        cmd = &commands.items[j];
        break;
      }
    }
    assert(cmd != NULL);
    if (score.name_matched) {
      u32 end_idx = score.name_match_len + score.name_match_start;
      for (u32 ii = 0; ii < strlen(cmd->display_name); ii++) {
        u32 pos = XYToPos(x+ii, y, tui->screen_dimensions.width);
        if (ii >= score.name_match_start && ii < end_idx) {
          if (i == menu_index) {
            tui->frame_buffer[pos].foreground = ANSI_DULL_RED;
          } else {
            tui->frame_buffer[pos].foreground = ANSI_HIGHLIGHT_YELLOW;
            tui->frame_buffer[pos].background = 0;
          }
        }
        tui->frame_buffer[pos].bytes[0] = cmd->display_name[ii];
      }
    } else {
      renderStrToBufferMaxWidthWithoutChangingColor(tui->frame_buffer, x, y, cmd->display_name, outline.width - 2, sd);
    }
    renderStrToBufferMaxWidthWithoutChangingColor(tui->frame_buffer, x, y+1, cmd->description, outline.width - 2, sd);
  }

  scratchReturn(&scratch);
  return result;
}

fn void renderChoiceMenu(TuiState* tui, u16 x, u16 y, ptr options[], u32 len, bool choosable, u32 selected_index, u8* colors) {
  for (u32 i = 0; i < len; i++) {
    u32 pos = x + (tui->screen_dimensions.width*(y+i));
    if (choosable && selected_index == i) {
      tui->cursor.x = x;
      tui->cursor.y = y+i;
    }
    u8 bytes[80] = {0};
    sprintf((char*)bytes, "- %d. ", i+1);
    u32 offset = strlen((char*)bytes);
    u32 bytes_remaining = (80 - offset);
    for (
      u32 j = 0;
      j < strlen(options[i]) && j < bytes_remaining;
      j++
    ) {
      bytes[offset+j] = options[i][j];
    }
    // write our `bytes` buffer into the Pixel* buf
    for (u32 j = 0; j < 80; j++) {
      tui->frame_buffer[pos+j].bytes[0] = bytes[j];
      if (choosable && selected_index == i) {
        tui->frame_buffer[pos+j].foreground = ANSI_BLACK;
        tui->frame_buffer[pos+j].background = ANSI_WHITE;
      } else {
        tui->frame_buffer[pos+j].foreground = colors == NULL ? ANSI_WHITE : colors[i];
        tui->frame_buffer[pos+j].background = ANSI_BLACK;
      }
    }
  }
}

fn void infiniteUILoop(
  u32 max_screen_width,
  u32 max_screen_height,
  u64 goal_input_loop_us,
  void* state,
  // updateAndRender should return a bool `should_quit`
  bool (*updateAndRender)(TuiState* tui, void* state, u8* input_buffer, u64 loop_count)
) {
  bool should_quit = false;
  Arena permanent_arena = {0};
  arenaInit(&permanent_arena);
  // set up the TUI incantations
  TermIOs old_terminal_attributes = osStartTUI(false);
  TuiState tui = tuiInit(&permanent_arena, max_screen_width*max_screen_height);
  tui.screen_dimensions = osGetTerminalDimensions();

  // ui loop (read input, simulate next frame, render)
  u8 input_buffer[5] = {0};
  u64 loop_start;
  u64 loop_count = 0;
  while (!should_quit) {
    loop_count += 1;
    loop_start = osTimeMicrosecondsNow();

    // both reads local input from the keyboard AND from the network
    osReadConsoleInput(input_buffer, 4);

    // prep rendering
    MemoryZero(tui.frame_buffer, tui.buffer_len * sizeof(Pixel));
    tui.prev_screen_dimensions = tui.screen_dimensions;
    if (loop_count % 17 == 0) { // every 17 frames, update our terminal_dimensions
      tui.screen_dimensions = osGetTerminalDimensions();
    }
    tui.prev_cursor = tui.cursor;// save last frame's cursor

    // operate on input + render new tui.frame_buffer
    should_quit = updateAndRender(&tui, state, input_buffer, loop_count);

    printfBufferAndSwap(&tui);

    // loop timing
    u32 loop_duration = osTimeMicrosecondsNow() - loop_start;
    i32 remaining_time = goal_input_loop_us - loop_duration;
    if (remaining_time > 0) {
      osSleepMicroseconds(remaining_time);
    }
  }

  // cleanup terminal TUI incantations
  osEndTUI(old_terminal_attributes);
}

