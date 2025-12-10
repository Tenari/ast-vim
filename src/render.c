#include "shared.h"
#include "lib/tui.c"

#define MAX_SCREEN_HEIGHT 300
#define MAX_SCREEN_WIDTH 800

typedef struct RenderableRoom {
  TileType background[ROOM_TILE_COUNT];
  EntityType foreground[ROOM_TILE_COUNT];
} RenderableRoom;

fn u8 ansiColorForTile(TileType t) {
  // bright red = return 160;
  switch(t) {
    case TileDirt:
      return 58;
    case TileShortGrass:
      return ANSI_MID_GREEN;
    case TileTallGrass:
      return ANSI_DARK_GREEN;
    case TileStoneFloor:
      return 240;
    case TileSolidStone:
      return 233;
    case TileGravel:
      return 249;
    case TileWater:
      return 21;
    case TileStaircaseDown:
      return 196;
    case TileStaircaseUp:
      return 220;
    case TileHole:
      return 232;
    case TileNull:
    //default:
      return 0;
    case TileTypeCount:
      assert(1 == 2);
      return 0;
  }
}

fn u8 ansiColorForEntity(EntityType e) {
  switch (e) {
    case EntityMushroom:
    case EntityRabbit:
    case EntityTiger:
    case EntityWolf:
    case EntityDragon:
    case EntityScorpion:
    case EntityWrestlingFight:
    case EntitySuperVillain:
    case EntityVampire:
    case EntityMermaid:
    case EntityElf:
    case EntityGenie:
    case EntityFairy:
    case EntityTroll:
    case EntityMoney:
    case EntityGravestone:
    case EntityBasket:
    case EntityChair:
    case EntityHole:
    case EntityPortal:
    case EntityScroll:
    case EntityFire:
    case EntityFountain:
    case EntityBoar:
    case EntityWall:
      return ANSI_WHITE;
    case EntityDoor:
      return 94;
    case EntityCharacter:
      return 14;
    case EntityTree:
      return 40;
    case EntityBush:
      return 10;
    case EntityWood:
      return 136;
    case EntityRock:
      return 240;
    case EntityBoulder:
      return 102;
    case EntityNull:
    //default: // commented out so that we get a warning for missing entity
      return 0;
    case EntityTypeCount:
      assert(1 == 2);
      return 0;
  }
}

fn str charForEntity(EntityType e) {
  switch (e) {
    case EntityMushroom:
      return "🍄";
    case EntityRabbit:
      return "🐇";
    case EntityTiger:
      return "🐅";
    case EntityWolf:
      return "🐺";
    case EntityDragon:
      return "🐉";
    case EntityScorpion:
      return "🦂";
    case EntityBoar:
      return "🐗";
    case EntityWrestlingFight:
      return "🤼";
    case EntityTroll:
      return "🧌";
    case EntitySuperVillain:
      return "🦹";
    case EntityVampire:
      return "🧛";
    case EntityMermaid:
      return "🧜";
    case EntityElf:
      return "🧝";
    case EntityGenie:
      return "🧞";
    case EntityFairy:
      return "🧚";
    case EntityMoney:
      return "💰";
    case EntityGravestone:
      return "🪦";
    case EntityBasket:
      return "🧺";
    case EntityFountain:
      return "⛲";
    case EntityFire:
      return "🔥";
    case EntityChair:
      return "🪑";
    case EntityHole:
      return "\xF0\x9F\x95\xB3 ";
    case EntityPortal:
      return "🪞";
    case EntityWall:
      return "# ";
    case EntityDoor:
      return "🚪";
    case EntityCharacter:
      return "🧙";
      //return "웃";
    case EntityTree:
      return "🌲";
    case EntityBush:
      return "🌱";
    case EntityWood:
      return "🪵";
    case EntityRock:
      return "* ";
    case EntityBoulder:
      return "🪨";
    case EntityScroll:
      return "📜";
    case EntityNull:
    case EntityTypeCount:
    //default: // commented out so that we get a warning for missing entity
      return "  ";
  }
}

fn void renderRoom(Pixel* buf, u32 x, u32 y, RenderableRoom* room, Dim2 screen_dimensions) {
  // x,y is starting cursor location of upper-left corner

  drawAnsiBox(buf, x, y, ROOM_WIDTH*2, ROOM_HEIGHT, screen_dimensions);

  // start printing the rows
  // move cursor to beginning of the room
  for (i32 j = 0; j < ROOM_HEIGHT; j++) {
    for (i32 i = 0; i < ROOM_WIDTH; i++) {
      u32 pos = (x+1+(i*2)) + (screen_dimensions.width*(y+1+j));
      u32 index = i + ROOM_WIDTH*j;

      buf[pos].background = ansiColorForTile(room->background[index]);
      buf[pos].foreground = ansiColorForEntity(room->foreground[index]);
      str fg_char = charForEntity(room->foreground[index]);
      Utf8Character first_character_class = classifyUtf8Character((u8)fg_char[0]);
      // if it's a single ASCII character
      if (first_character_class == Utf8CharacterAscii && fg_char[1] == '\0') {
        buf[pos].bytes[0] = fg_char[0];
      // if it's a pair of ASCII characters
      } else if (first_character_class == Utf8CharacterAscii && fg_char[1] > 0 && fg_char[2] == '\0') {
        buf[pos].bytes[0] = fg_char[0];
        buf[pos+1].bytes[0] = fg_char[1];
        buf[pos+1].background = buf[pos].background;
        buf[pos+1].foreground = buf[pos].foreground;
      } else if (first_character_class == Utf8CharacterThreeByte) {
        if (strlen(fg_char) == 3) {// handle 2-wide characters
          buf[pos+1].background = buf[pos].background;
          buf[pos+1].foreground = buf[pos].foreground;
        }
        for (u32 k = 0; k < strlen(fg_char)/3; k++) {
          if (k != 0) {
            buf[pos+k].background = buf[pos].background;
            buf[pos+k].foreground = buf[pos].foreground;
          }
          for (u32 l = 0; l < 3; l++) {
            buf[pos+k].bytes[l] = fg_char[l+(k*3)];
          }
        }
      } else if (first_character_class == Utf8CharacterFourByte) {
        if (strlen(fg_char) == UTF8_MAX_WIDTH) {// assume all of these characters are 2-wide
          buf[pos+1].background = buf[pos].background;
          buf[pos+1].foreground = buf[pos].foreground;
        }
        for (u32 k = 0; k < strlen(fg_char)/UTF8_MAX_WIDTH; k++) {
          for (u32 l = 0; l < UTF8_MAX_WIDTH; l++) {
            buf[pos+k].bytes[l] = fg_char[l+(k*UTF8_MAX_WIDTH)];
          }
        }
        // handle secondary bytes that didn't divide evenly
        for (u32 k = 0; k < strlen(fg_char)%UTF8_MAX_WIDTH; k++) {
          buf[pos+1].background = buf[pos].background;
          buf[pos+1].foreground = buf[pos].foreground;
          buf[pos+1].bytes[k] = fg_char[UTF8_MAX_WIDTH+k];
        }
      } else {
        assert(false && "unhandled bullshit");
      }
    }
  }
}

