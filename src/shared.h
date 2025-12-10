#include "base/include.h"

#ifndef GAMESHARED_H
#define GAMESHARED_H

#define ROOM_WIDTH 30
#define ROOM_HEIGHT 20
#define ROOM_TILE_COUNT ROOM_WIDTH*ROOM_HEIGHT

typedef enum EntityFeature {
  FeatureWalksAround,
  FeatureRegensHp,
  FeatureRegensMp,
  FeatureKnowsSpells,
  FeatureCanFight,
  FeatureDealsContinuousDamage,
  EntityFeature_Count
} EntityFeature;

typedef enum EntityType {
  EntityNull,
  EntityWall,
  EntityDoor,
  EntityCharacter,
  EntityTree,
  EntityBush,
  EntityWood,
  EntityRock,
  EntityBoulder,
  EntityScroll,
  EntityPortal,
  EntityHole,
  EntityChair,
  EntityFire,
  EntityFountain,
  EntityBasket,
  EntityGravestone,
  EntityMoney,
  EntityFairy,
  EntityGenie,
  EntityElf,
  EntityMermaid,
  EntityVampire,
  EntitySuperVillain,
  EntityTroll,
  EntityWrestlingFight,
  EntityBoar,
  EntityScorpion,
  EntityDragon,
  EntityWolf,
  EntityTiger,
  EntityRabbit,
  EntityMushroom,
  EntityTypeCount,
} EntityType;
static const char* ENTITY_STRINGS[] = {
  "NULL",
  "Wall",
  "Door",
  "Character",
  "Tree",
  "Bush",
  "Wood",
  "Rock",
  "Boulder",
  "Scroll",
  "NextPortal",
  "Hole",
  "Chair",
  "Fire",
  "Fountain",
  "Basket",
  "Gravestone",
  "Money",
  "Fairy",
  "Genie",
  "Elf",
  "Mermaid",
  "Vampire",
  "SuperVillain",
  "Troll",
  "WrestlingFight",
  "Boar",
  "Scorpion",
  "Dragon",
  "Wolf",
  "Tiger",
  "Rabbit",
  "Mushroom",
};

// the "floor" of a location, intended to be represented by the background color of the location
#define TILE_MESSAGE_SIZE (2+1)
typedef enum { // TileType
  TileNull,
  TileDirt,
  TileShortGrass,
  TileTallGrass,
  TileStoneFloor,
  TileSolidStone, // like behind the walls of a cave
  TileGravel,
  TileWater,
  TileStaircaseDown,
  TileStaircaseUp,
  TileHole,
  TileTypeCount,
} TileType;
static str TILE_STRINGS[] = {
  "Null",
  "Dirt",
  "ShortGrass",
  "TallGrass",
  "StoneFloor",
  "SolidStone", // like behind the walls of a cave
  "Gravel",
  "Water",
  "StaircaseDown",
  "StaircaseUp",
  "Hole",
};

typedef struct {
  i32 x;
  i32 y;
  i32 z;
} XYZ;

typedef enum Direction {
  DirectionInvalid,
  North,
  South,
  East,
  West,
  Direction_Count
} Direction;

typedef enum CommandType {
  CommandInvalid,
  CommandKeepAlive,
  CommandWalk,
  CommandLogin, // TODO: actual login data
  CommandCreateCharacter,
  CommandStartFight,
  CommandWonFight,
  CommandLostFight,
  CommandType_Count,
} CommandType;
static const char* command_type_strings[] = {
  "Invalid",
  "KeepAlive",
  "Walk",
  "Login",
  "CreateCharacter",
  "StartFight",
  "WonFight",
  "LostFight",
};

#define ENTITY_HEADER_MESSAGE_SIZE (8+8+1+1+1)
#define ENTITY_MESSAGE_SIZE (ENTITY_HEADER_MESSAGE_SIZE+2+2+2+2+8+1)
typedef enum Message {
  MessageInvalid,
  MessageRoomLayout,
  MessageRoomEntities,
  MessageRoomDelta,
  MessageRoomDeletions,
  MessageCharacterId,
  MessageFightIp,
  MessageFightMe,
  MessageFightInput,
  MessageFightOpponentNotOnline,
  MessageFightMeAck,
  MessageBadPw,
  MessageNewAccountCreated,
  Message_Count,
} Message;
static const char* MESSAGE_STRINGS[] = {
  "Invalid",
  "RoomLayout",
  "RoomEntities",
  "RoomDelta",
  "RoomDeletions",
  "CharacterId",
  "FightIp",
  "FightMe",
  "FightInput",
  "FightOpponentNotOnline",
  "FightMeAck",
  "BadPw",
  "NewAccountCreated",
};

// MessageFullRoom
// we send a "room" to the user-client as:
// | MessageEnumValByte |
// | int | Entity | Entity | ... | Tile | Tile | ...
//    |     |                       |-> {TileType,visible?}
//    |     -------------------------------> {x,y,EntityType,visible?}
//    -------------------------------------> # of entities which follow
// the "Entity" representation which we send to the client
typedef struct {
    bool visible; // if false, it means that the user-client only "remembers" what is here and may be wrong
                  // if true, it means the user-client has accurate information about this position (either through eyes or magic)
    u8 x;
    u8 y;
    EntityType type; // used to generate the foreground utf8 codepoint to show
} SendableEntity;

// the "Tile" representation which we send to the client
typedef struct {
    bool visible; // if false, it means that the user-client only "remembers" what is here and may be wrong
                  // if true, it means the user-client has accurate information about this position (either through eyes or magic)
    int x;
    int y;
    TileType type; // maps to the background-color for rendering the "floor"
} SendableTile;

typedef struct {
    SendableEntity* items;
    int length;
    int capacity;
} SendableEntityList;

typedef struct {
    SendableTile* items;
    int length;
    int capacity;
} SendableTileList;

typedef enum RoomClass {
  INVALIDRoom,
  DesertRoom,
  ScrubRoom,
  GrasslandRoom,
  ForestRoom,
  IndoorRoom,
  UndergroundRoom,
  RoomClassCount,
} RoomClass;
static str ROOM_CLASS_NAMES[] = {
  "INVALID",
  "Desert",
  "Scrub",
  "Grassland",
  "Forest",
  "Indoor",
  "Underground",
};

typedef enum SpellType {
  SpellTypeInvalid,
  SpellTypeMagicMissile,
  SpellTypeShield,
  SpellTypeSilence,
  SpellTypeEmpower,
  SpellTypeFireball,
  SpellTypeManaRain,
  SpellTypeHeal,
  SpellTypeEarthenWall,
  SpellType_Count,
} SpellType;
static str SPELL_TYPE_STRINGS[] = {
  "Invalid",
  "Magic Missile",
  "Shield",
  "Silence",
  "Empower",
  "Fireball",
  "Mana-Rain",
  "Heal",
  "Earthen Wall",
};
static str SPELL_INCANTATIONS[] = {
  "n/a",
  "Missile acutum velocitas",
  "Aegida confirmo",
  "Profundum sonet nullus",
  "Crescit fortitudine repletus",
  "Globum flammae evoco",
  "Pluvea mana caelum aperit",
  "Sanitas restituta dolorem",
  "Petra surge murum terraeum",
};
typedef struct Spell {
  u64 mana_cost;
  u64 effect;
  u64 duration;
  SpellType type;
  str cheat;
} Spell;
global Spell ALL_SPELLS[] = {
  { .type = SpellTypeInvalid },
  {
    .mana_cost = 5,  .effect = 5,  .duration = 50,
    .type = SpellTypeMagicMissile, .cheat = "m",
  },
  {
    .mana_cost = 4,  .effect = 10, .duration = 500,
    .type = SpellTypeShield,       .cheat = "s",
  },
  {
    .mana_cost = 4,  .effect = 10, .duration = 500,
    .type = SpellTypeSilence,      .cheat = "si",
  },
  {
    .mana_cost = 4,  .effect = 10, .duration = 500,
    .type = SpellTypeEmpower,      .cheat = "e",
  },
  {
    .mana_cost = 5,  .effect = 5,  .duration = 50,
    .type = SpellTypeFireball,     .cheat = "f",
  },
  {
    .mana_cost = 4,  .effect = 10, .duration = 500,
    .type = SpellTypeManaRain,     .cheat = "ma",
  },
  {
    .mana_cost = 10, .effect = 6, .duration = 50,
    .type = SpellTypeHeal,        .cheat = "h",
  },
  {
    .mana_cost = 10, .effect = 16,.duration = 600,
    .type = SpellTypeEarthenWall, .cheat = "w",
  },
};


typedef struct Effect {
  SpellType type;
  u64 start_frame;
  u64 end_frame;
  u64 counter;
} Effect;

#endif //GAMESHARED_H
