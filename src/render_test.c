#include "base/impl.c"
#include "render.c"

///// #DEFINES
#define GOAL_LOOPS_PER_S 24
#define GOAL_LOOP_US 1000000/GOAL_LOOPS_PER_S

///// TYPES

///// GLOBALS
global bool should_quit = false;

i32 main(i32 argc, ptr argv[]) {
	osInit();
	i32 ip = htonl(osLanIPAddress());
	struct sockaddr_in addr = {0};
	addr.sin_addr.s_addr = ip;
	printf("%d %s", ip, inet_ntoa(addr.sin_addr));
	return 0;
  Arena permanent_arena = {0};
  arenaInit(&permanent_arena);
  // set up the TUI incantations
  TermIOs old_terminal_attributes = osStartTUI(false);
  TuiState tui = tuiInit(&permanent_arena, MAX_SCREEN_WIDTH*MAX_SCREEN_HEIGHT);
  tui.screen_dimensions = osGetTerminalDimensions();

  // init our working state

  // ui loop (read input, simulate next frame, render)
  u8 input_buffer[5] = {0};
  u64 loop_start;
  u64 loop_count = 0;
  while (!should_quit) {
    loop_count += 1;
    loop_start = osTimeMicrosecondsNow();

    // both reads local input from the keyboard AND from the network
		osReadConsoleInput(input_buffer, 4);

    // operate on input
    {
			if (input_buffer[0] == 'q') {
				should_quit = true;
			}
    }

    // render
    MemoryZero(tui.frame_buffer, tui.buffer_len * sizeof(Pixel));
    tui.prev_screen_dimensions = tui.screen_dimensions;
    if (loop_count % 17 == 0) { // every 17 frames, update our terminal_dimensions
      tui.screen_dimensions = osGetTerminalDimensions();
    }
    tui.prev_cursor = tui.cursor;// save last frame's cursor
    renderStrToBuffer(tui.frame_buffer, (1+loop_count) % 20, 1, "blah blah", tui.screen_dimensions);
		drawAnsiBox(tui.frame_buffer, 5, 5, 5, 5, tui.screen_dimensions);
		printf("%d %d %d %d", input_buffer[0], input_buffer[1], input_buffer[2], input_buffer[3]);
		tui.frame_buffer[10].bytes[0] = input_buffer[0];
		tui.frame_buffer[11].bytes[0] = input_buffer[1];
		tui.frame_buffer[12].bytes[0] = input_buffer[2];
		tui.frame_buffer[13].bytes[0] = input_buffer[3];

		// blit
    printfBufferAndSwap(&tui);

    // loop timing
    u32 loop_duration = osTimeMicrosecondsNow() - loop_start;
    i32 remaining_time = GOAL_LOOP_US - loop_duration;
    if (remaining_time > 0) {
      osSleepMicroseconds(remaining_time);
    }
  }

  // cleanup terminal TUI incantations
  osEndTUI(old_terminal_attributes);
  return 0;
}
