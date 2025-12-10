/*
 * Code conventions:
 *  MyStructType
 *  myFunction()
 *  my_variable
 *  MY_CONSTANT
 * */
#include <stdio.h>
#include <stdlib.h>
#include <sys/random.h>
#include "shared.h"
#include "base/impl.c"
#define NET_OUTGOING_MESSAGE_QUEUE_LEN 64
#include "lib/network.c"
#include "lib/thread.c"
#include "render.c"
#include "string_chunk.c"

///// CONSTANTS
#define MAX_ENTITIES (2<<18)
#define MAX_TREES_PER_ROOM 512
#define MAX_ROOMS 256
#define LEFT_ROOM_ENTITES_LEN (KB(1))
#define ROOM_MAP_COLLISIONS_LEN MAX_ROOMS/8
#define CLIENT_COMMAND_LIST_LEN 8
#define SERVER_PORT 7777
#define SERVER_MAX_HEAP_MEMORY MB(256)
#define SERVER_MAX_CLIENTS 16
#define GOAL_NETWORK_SEND_LOOPS_PER_S 4
#define GOAL_NETWORK_SEND_LOOP_US 1000000/GOAL_NETWORK_SEND_LOOPS_PER_S
#define GOAL_GAME_LOOPS_PER_S 8
#define GOAL_GAME_LOOP_US 1000000/GOAL_GAME_LOOPS_PER_S
#define CLIENT_TIMEOUT_FRAMES GOAL_GAME_LOOPS_PER_S*3
#define CHUNK_SIZE 64
#define ACCOUNT_CHUNK_SIZE 64
#define FIGHT_IP_MSG_LEN 11
#define BURN_TICK_US 1000000
#define REGEN_HP_TICK_US 3000000
#define PARSED_CLIENT_COMMAND_THREAD_QUEUE_LEN 64

///// TypeDefs
typedef struct ParsedClientCommand {
  CommandType type;
  Direction direction;
  SpellType spell_type;
  u8 byte;
  u16 sender_port;
  u16 alt_port;
  u32 sender_ip;
  u32 alt_ip;
  StringChunkList name;
  StringChunkList pass;
  u64 id;
  u64 hp;
  u64 mp;
} ParsedClientCommand;

typedef struct ParsedClientCommandThreadQueue {
  ParsedClientCommand items[PARSED_CLIENT_COMMAND_THREAD_QUEUE_LEN];
  u32 head;
  u32 tail;
  u32 count;
  Mutex mutex;
  Cond not_empty;
  Cond not_full;
} ParsedClientCommandThreadQueue;

typedef struct Entity {
  bool changed;
  u8 x;
  u8 y;
  u8 color;
  EntityType type;
  u16 hp;
  u16 max_hp;
  u16 mp;
  u16 max_mp;
  u16 wins;
  u32 misc;
  u64 id;
  u64 known_spells_flags;
  u64 features;
  XYZ room_xyz;
} Entity;

typedef struct Account {
  u64 id;
  String name;
  String pw;
  XYZ room_xyz;
  u64 character_eid;
} Account;

typedef struct AccountChunk {
  u64 length; // the currently used # of accounts in this chunk
  u64 capacity; // the "chunk size" / space in this chunk
  struct AccountChunk* next; // the next chunk
  Account* items; // the actual accounts
} AccountChunk;

typedef struct EntityList {
  u64 length; // the currently used length
  u64 capacity;
  Entity* items;
} EntityList;

typedef struct EntityChunk {
  u64 length; // the currently used # of entities in this chunk
  u64 capacity; // the "chunk size" / space in this chunk
  struct EntityChunk* next; // the next chunk
  Entity* items; // the actual entities
} EntityChunk;

typedef struct ChunkedEntityList {
  u64 length; // the current number of entities
  u64 chunk_size; // the # of entities per chunk
  u64 chunks; // the # of chunks in this list so far
  EntityChunk* first; // the first chunk of entities
} ChunkedEntityList;

typedef struct Room {
  i32 x;
  i32 y;
  i32 z;
  RoomClass class;
  TileType tiles[ROOM_TILE_COUNT];
  ChunkedEntityList entities;
} Room;

typedef struct RoomMap {
  Room items[MAX_ROOMS];
  Room collisions[ROOM_MAP_COLLISIONS_LEN];
} RoomMap;

typedef struct Client {
  u16 lan_port;
  i32 lan_ip;
  u64 character_eid;
  u64 account_id;
  XYZ room_xyz;
  SocketAddress address;
  CommandType commands[CLIENT_COMMAND_LIST_LEN];
  u64 entered_room_on_frame;
  u64 last_ping;
} Client;

typedef struct ClientList {
  u64 length;
  u64 capacity;
  Client* items;
} ClientList;

typedef struct LeftRoomEntity {
  u64 eid;
  u64 frame;
  XYZ room_xyz;
} LeftRoomEntity;

typedef struct State {
  Mutex client_mutex;
  Mutex mutex;
  RoomMap* rooms;
  ClientList clients;
  u64 next_eid;
  AccountChunk accounts;
  u64 frame;
  Arena game_scratch;
  LeftRoomEntity* entities_who_left_a_room_recently;
  StringArena string_arena;
  ParsedClientCommandThreadQueue* network_recv_queue;
  OutgoingMessageQueue* network_send_queue;
} State;

///// Global Variables
global State state = { 0 };
global Arena permanent_arena = { 0 };
global const Entity NULL_ENTITY = { 0 };
global bool debug_mode = false;
global ChunkedEntityList free_chunks = { 0, CHUNK_SIZE, 0, NULL };

///// functionImplementations
fn ParsedClientCommandThreadQueue* newPCCThreadQueue(Arena* a) {
  ParsedClientCommandThreadQueue* result = arenaAlloc(a, sizeof(ParsedClientCommandThreadQueue));
  MemoryZero(result, (sizeof *result));
  result->mutex = newMutex();
  result->not_full = newCond();
  result->not_empty = newCond();
  return result;
}

fn void pccThreadSafeQueuePush(ParsedClientCommandThreadQueue* queue, ParsedClientCommand* msg) {
  lockMutex(&queue->mutex); {
    while (queue->count == PARSED_CLIENT_COMMAND_THREAD_QUEUE_LEN) {
      waitForCondSignal(&queue->not_full, &queue->mutex);
    }

    MemoryCopy(&queue->items[queue->tail], msg, (sizeof *msg));
    queue->tail = (queue->tail + 1) % PARSED_CLIENT_COMMAND_THREAD_QUEUE_LEN;
    queue->count++;

    signalCond(&queue->not_empty);
  } unlockMutex(&queue->mutex);
}

fn ParsedClientCommand* pccThreadSafeNonblockingQueuePop(ParsedClientCommandThreadQueue* q, ParsedClientCommand* copy_target) {
  // immediately returns NULL if there's nothing in the ThreadQueue
  // copies the ParsedClientCommand into `copy_target` if there is something in the queue
  // and marks it as popped from the queue
  ParsedClientCommand* result = NULL;

  lockMutex(&q->mutex); {
    if (q->count > 0) {
      result = &q->items[q->head];
      MemoryCopy(copy_target, result, (sizeof *copy_target));
      q->head = (q->head + 1) % PARSED_CLIENT_COMMAND_THREAD_QUEUE_LEN;
      q->count--;

      signalCond(&q->not_full);
    }
  } unlockMutex(&q->mutex);

  return result;
}

fn bool entityCanSee(Room* room, Entity* entity, u16 pos) {
  bool result = true;
  u8 e_x = entity->x;
  u8 e_y = entity->y;
  u8 g_x = pos % ROOM_WIDTH;
  u8 g_y = pos / ROOM_WIDTH;
  // TODO: xy raycast step through all the positions
  return result;
}

fn u64 entitySerialize(Entity current, Account* acct, u64 index, u8 bytes[]) {
  // send entity header (common to all entity types)
  index += writeU64ToBufferLE(bytes + index, current.id);
  index += writeU64ToBufferLE(bytes + index, current.features);
  bytes[index++] = current.x;
  bytes[index++] = current.y;
  bytes[index++] = (u8)current.type;  // EntityType

  if (CheckFlag(current.features, FeatureRegensHp)) {
    index += writeU16ToBufferLE(bytes + index, current.hp);
    index += writeU16ToBufferLE(bytes + index, current.max_hp);
  }

  if (CheckFlag(current.features, FeatureRegensMp)) {
    index += writeU16ToBufferLE(bytes + index, current.mp);
    index += writeU16ToBufferLE(bytes + index, current.max_mp);
  }

  if (CheckFlag(current.features, FeatureKnowsSpells)) {
    index += writeU64ToBufferLE(bytes + index, current.known_spells_flags);
  }

  // send character-specific details (color and name)
  if (current.type == EntityCharacter) {
    bytes[index++] = current.color;
    // send name string
    index += writeU64ToBufferLE(bytes + index, acct->name.length);
    for (u32 j = 0; j < acct->name.length; j++) {
      bytes[index++] = acct->name.bytes[j];
    }
  }
  return index;
}

fn u64 entityFeaturesFromType(EntityType type) {
  u64 result = 0;
  switch (type) {
    case EntityCharacter: {
      SetFlag(result, FeatureWalksAround);
      SetFlag(result, FeatureRegensHp);
      SetFlag(result, FeatureRegensMp);
      SetFlag(result, FeatureKnowsSpells);
      SetFlag(result, FeatureCanFight);
    } break;
    case EntityFire:
      SetFlag(result, FeatureDealsContinuousDamage);
      break;
    default: {
    } break;
  }
  return result;
}

fn Account* findAccountById(u64 id) {
  AccountChunk* current = &state.accounts;
  while (current != NULL) {
    for (u32 i = 0; i < current->length; i++) {
      if (current->items[i].id == id) {
        return &current->items[i];
      }
    }
    current = current->next;
  }
  return NULL;
}

fn Account* findAccountByEId(u64 id) {
  AccountChunk* current = &state.accounts;
  while (current != NULL) {
    for (u32 i = 0; i < current->length; i++) {
      if (current->items[i].character_eid == id) {
        return &current->items[i];
      }
    }
    current = current->next;
  }
  return NULL;
}

fn Account* findAccountByName(String name) {
  AccountChunk* current = &state.accounts;
  while (current != NULL) {
    for (u32 i = 0; i < current->length; i++) {
      if (stringsEq(&current->items[i].name, &name)) {
        return &current->items[i];
      }
    }
    current = current->next;
  }
  return NULL;
}

fn Account* newAccount(Arena* a, Account details) {
  u64 id = 0;
  AccountChunk* current = &state.accounts;
  while (current != NULL) {
    id += current->length;
    if (current->length < current->capacity) {
      Account* result = &current->items[current->length];
      *result = details;
      result->id = id;
      current->length += 1;
      printf("new account created\n");
      return result;
    }
    if (current->next == NULL) {
      // alloc next chunk of accounts
      AccountChunk* next = arenaAlloc(a, sizeof(AccountChunk));
      next->capacity = ACCOUNT_CHUNK_SIZE;
      next->items = arenaAllocArray(a, Account, next->capacity);
      current->next = next;
    }
    current = current->next;
  }
  assert(false); // never should be reached
  return NULL;
}

fn bool isRoomNull(Room* r) {// true if x,y,z + tile(0,0) are all 0 (null)
  return r->x == 0 && r->y == 0 && r->z == 0 && r->tiles[0] == TileNull;
}

fn bool isBlockingEntity(EntityType e) {
  switch (e) {
    case EntityWall:
    case EntityRock:
    case EntityBoulder:
    case EntityTree:
    case EntityFountain:
    case EntityFairy:
    case EntityGenie:
    case EntityElf:
    case EntityMermaid:
    case EntityVampire:
    case EntitySuperVillain:
    case EntityTroll:
    case EntityWrestlingFight:
    case EntityBoar:
    case EntityScorpion:
    case EntityDragon:
    case EntityWolf:
    case EntityTiger:
    case EntityRabbit:
      return true;
    case EntityBush:
    case EntityWood:
    case EntityPortal:
    case EntityScroll:
    case EntityDoor:
    case EntityCharacter:
    case EntityNull:
    case EntityHole:
    case EntityChair:
    case EntityFire:
    case EntityBasket:
    case EntityGravestone:
    case EntityMoney:
    case EntityMushroom:
    case EntityTypeCount:
      return false;
  }
}

fn u64 pushEntity(EntityChunk* chunk, Entity e) {
  assert(chunk->length < chunk->capacity);
  chunk->items[chunk->length] = e;
  u64 result = chunk->length;
  chunk->length += 1;
  return result;
}

fn bool isSpaceInChunk(EntityChunk* chunk) {
  return chunk->length < chunk->capacity;
}

fn bool allChunksFull(ChunkedEntityList list) {
  return list.length >= (list.chunk_size * list.chunks);
}

fn EntityChunk* pushNewChunk(Arena* a, ChunkedEntityList* list) {
  EntityChunk* new_chunk;
  if (free_chunks.length > 0) {
    new_chunk = free_chunks.first;
    free_chunks.length -= 1;
    free_chunks.first = new_chunk->next;
  } else {
    new_chunk = arenaAlloc(a, sizeof(EntityChunk));
    // alloc the new chunk of entities
    new_chunk->length = 0;
    new_chunk->capacity = list->chunk_size;
    new_chunk->next = NULL;
    new_chunk->items = arenaAllocArray(a, Entity, list->chunk_size);
  }
  // bookeeping in the list
  list->chunks += 1;
  if (list->first == NULL) {
    list->first = new_chunk;
  } else {
    EntityChunk* last = list->first;
    while (last->next != NULL) {
      last = last->next;
    }
    last->next = new_chunk;
  }
  return new_chunk;
}

fn Entity* entityPtrFromChunkList(ChunkedEntityList* list, i32 index) {
  Entity* result = (Entity*)&NULL_ENTITY;
  i32 chunk_index = index / list->chunk_size;
  EntityChunk* chunk = list->first;
  for (i32 i = 0; i < chunk_index; i++) {
    chunk = chunk->next;
  }
  result = &chunk->items[index % list->chunk_size];
  return result;
}

fn Entity entityFromChunkList(ChunkedEntityList* list, i32 index) {
  Entity result = {0};
  i32 chunk_index = index / list->chunk_size;
  EntityChunk* chunk = list->first;
  for (i32 i = 0; i < chunk_index; i++) {
    chunk = chunk->next;
  }
  result = chunk->items[index % list->chunk_size];
  return result;
}

fn void addEntityToRoom(Arena* a, Room* room, Entity e) {
  // ensure that the xyz matches
  e.room_xyz.x = room->x;
  e.room_xyz.y = room->y;
  e.room_xyz.z = room->z;

  if (allChunksFull(room->entities)) {
    pushNewChunk(a, &room->entities);
  }
  EntityChunk* last_chunk = room->entities.first;
  while (last_chunk->next != NULL) {
    last_chunk = last_chunk->next;
  }
  pushEntity(last_chunk, e);
  room->entities.length += 1;
}

fn Entity* findEntityInRoom(Room* room, u64 id) {
  Entity* result = (Entity*)&NULL_ENTITY;
  EntityChunk* chunk = room->entities.first;
  Entity current = {0};
  for (i32 chunk_i = 0; chunk_i < room->entities.chunks; chunk_i++) {
    for (i32 i = 0; i < chunk->length; i++) {
      current = chunk->items[i];
      if (current.id == id) {
        result = &chunk->items[i];
        return result;
      }
    }
    chunk = chunk->next;
  }
  return result;
}

fn Entity* findCharacter(RoomMap* rooms, u64 id) {
  Entity* result = (Entity*)&NULL_ENTITY;
  for (u32 i = 0; i < MAX_ROOMS; i++) {
    if (!isRoomNull(&rooms->items[i])) {
      result = findEntityInRoom(&rooms->items[i], id);
    }
    if (result != &NULL_ENTITY) {
      return result;
    }
  }
  for (u32 i = 0; i < ROOM_MAP_COLLISIONS_LEN; i++) {
    if (!isRoomNull(&rooms->collisions[i])) {
      result = findEntityInRoom(&rooms->collisions[i], id);
    }
    if (result != &NULL_ENTITY) {
      return result;
    }
  }
  return result;
}

fn bool deleteLastEntity(ChunkedEntityList* list, EntityChunk* last_chunk, EntityChunk* second_to_last_chunk) {
  last_chunk->length -= 1;
  list->length -= 1;
  // don't delete the room's only chunk, but otherwise move the chunk to the free-list
  if (last_chunk->length == 0 && last_chunk != list->first) {
    list->chunks -= 1;
    second_to_last_chunk->next = NULL;
    free_chunks.length += 1;
    EntityChunk* last_free_chunk = free_chunks.first;
    if (last_free_chunk) {
      while (last_free_chunk->next != NULL) {
        last_free_chunk = last_free_chunk->next;
      }
      last_free_chunk->next = last_chunk;
    } else {
      free_chunks.first = last_chunk;
    }
  }
  return true;
}

fn bool recordEntityLeavingRoom(XYZ xyz, u64 eid, u64 frame) {
  bool result = false;
  for (u32 i = 0; i < LEFT_ROOM_ENTITES_LEN; i++) {
    if (state.entities_who_left_a_room_recently[i].frame+10 < frame) {
      state.entities_who_left_a_room_recently[i].eid = eid;
      state.entities_who_left_a_room_recently[i].frame = frame;
      state.entities_who_left_a_room_recently[i].room_xyz = xyz;
      result = true;
      break;
    }
  }
  return result;
}

fn bool deleteEntity(Room* room , u64 id) {
  XYZ room_xyz = { .x = room->x, .y = room->y, .z = room->z };
  EntityChunk* second_to_last_chunk = room->entities.first;
  EntityChunk* last_chunk = room->entities.first;
  while (last_chunk->next != NULL) {
    second_to_last_chunk = last_chunk;
    last_chunk = last_chunk->next;
  }
  Entity last_entity = last_chunk->items[last_chunk->length-1];
  if (last_entity.id == id) {
    recordEntityLeavingRoom(room_xyz, id, state.frame);
    return deleteLastEntity(&room->entities, last_chunk, second_to_last_chunk);
  } else {
    EntityChunk* chunk = room->entities.first;
    Entity current = {0};
    for (i32 chunk_i = 0; chunk_i < room->entities.chunks; chunk_i++) {
      for (i32 i = 0; i < chunk->length; i++) {
        current = chunk->items[i];
        if (current.id == id) {
          recordEntityLeavingRoom(room_xyz, id, state.frame);
          // copy the last entity over the matching current position (overwriting)
          chunk->items[i] = last_entity;
          // and delete the last entity
          return deleteLastEntity(&room->entities, last_chunk, second_to_last_chunk);
        }
      }
      chunk = chunk->next;
    }
    return false; // should not get here
  }
}

// TODO make single for loop so "break;" statments in "code" work properly
#define iterateRoomEntities(room, current, code) do {\
  EntityChunk* iterate_room_chunk = room->entities.first;\
  for (i32 macro_chunk_i = 0; macro_chunk_i < room->entities.chunks; macro_chunk_i++) {\
    for (i32 inner_i = 0; inner_i < iterate_room_chunk->length; inner_i++) {\
      current = iterate_room_chunk->items[inner_i];\
      code \
    }\
    iterate_room_chunk = iterate_room_chunk->next;\
  }\
} while(0)

fn EntityList getEntitiesAtXY(Arena* a, Room* room, u8 x, u8 y) {
  // count how many entities we need to alloc
  u64 len = 0;
  Entity current = {0};
  iterateRoomEntities(room, current, {
    if (current.x == x && current.y == y) {
      len += 1;
    }
  });

  EntityList result = {len, len, 0};
  result.items = arenaAllocArraySized(a, sizeof(Entity), len);
  u64 result_i = 0;

  iterateRoomEntities(room, current, {
    if (current.x == x && current.y == y) {
      result.items[result_i] = current;
      result_i += 1;
    }
  });
  return result;
}

fn bool isBlocked(Room* room, u8 x, u8 y) {
  EntityChunk* chunk = room->entities.first;
  Entity current = {0};
  for (i32 chunk_i = 0; chunk_i < room->entities.chunks; chunk_i++) {
    for (i32 i = 0; i < chunk->length; i++) {
      current = chunk->items[i];
      if (current.x == x && current.y == y && isBlockingEntity(current.type)) {
        return true;
      }
    }
    chunk = chunk->next;
  }
  return false;
}

fn u32 roomHashXYZ(i32 x, i32 y, i32 z) {
  // TODO better hash function
  u32 key = x+11 - y*7 + (z << 2);
  return key % MAX_ROOMS;
}

fn void setRoomBorder(Room* room, TileType type) {
  // the top and bottom rows
  for (i32 i = 0; i < ROOM_WIDTH; i++) {
    i32 top_tile_position = i;
    i32 bottom_tile_position = i + ((ROOM_HEIGHT-1) * ROOM_WIDTH);
    room->tiles[top_tile_position] = type;
    room->tiles[bottom_tile_position] = type;
  }
  // the left and right columns
  for (i32 j = 0; j < ROOM_HEIGHT; j++) {
    i32 left_tile_position = j*ROOM_WIDTH;
    i32 right_tile_position = (ROOM_WIDTH - 1) + (j * ROOM_WIDTH);
    room->tiles[left_tile_position] = type;
    room->tiles[right_tile_position] = type;
  }
}

fn void fillRoom(Room* room, TileType type) {
  for (i32 i = 0; i < ROOM_WIDTH; i++) {
    for (i32 j = 0; j < ROOM_HEIGHT; j++) {
      i32 tile_position = i + (j * ROOM_WIDTH);
      room->tiles[tile_position] = type;
    }
  }
}

fn Room* getRoomXYZ(RoomMap* rooms, i32 x, i32 y, i32 z) {
  u32 index = roomHashXYZ(x,y,z);
  Room* main_guess = &rooms->items[index];
  // if the "correct" index is something else, it was a collision, so check those
  if (main_guess->x != x || main_guess->y != y || main_guess->z != z) {
    for (i32 i = 0; i < ROOM_MAP_COLLISIONS_LEN; i++) {
      Room r = rooms->collisions[i];
      if (r.x == x && r.y == y && r.z == z) {
        return &rooms->collisions[i];
      }
    }
  }
  return main_guess;
}

fn RoomClass getNearestRoomClass(RoomMap* rooms, i32 x, i32 y, i32 z) {
  RoomClass result = INVALIDRoom;
  Room* prev = getRoomXYZ(rooms, x, y+1, z);
  if (isRoomNull(prev)) {
    prev = getRoomXYZ(rooms, x, y-1, z);
    if (isRoomNull(prev)) {
      prev = getRoomXYZ(rooms, x+1, y, z);
      if (isRoomNull(prev)) {
        prev = getRoomXYZ(rooms, x-1, y, z);
      }
    }
  }
  result = prev->class;
  return result;
}

fn void addNEntitiesRandomly(Arena* a, Room* room, EntityType type, u32 n) {
    for (i32 i = 0; i < n; i++) {
      Entity entity = {0};
      entity.type = type;
      entity.id = state.next_eid;
      state.next_eid += 1;
      entity.x = (rand() % (ROOM_WIDTH-2)) + 1;
      entity.y = (rand() % (ROOM_HEIGHT-2)) + 1;
      dbg("%s x=%d y=%d", ENTITY_STRINGS[type], entity.x, entity.y);
      addEntityToRoom(a, room, entity);
    }
}

fn void generateNewRoom(RoomMap* rooms, i32 x, i32 y, i32 z) {
  // this must no-op if the room already exists
  if (!isRoomNull(getRoomXYZ(rooms, x, y, z))) {
    return;
  }

  // init the room
  Room room = {0};
  room.x = x;
  room.y = y;
  room.z = z;
  room.entities.chunk_size = CHUNK_SIZE;
  XYZ xyz = {x, y, z};

  // try to detect hand-made hardcoded room definition
  char room_name_bytes[64] = {0};
  sprintf(room_name_bytes, "src/assets/rooms/%d_%d_%d.room", x, y, z);
  String filename = {
    .length = strlen(room_name_bytes),
    .capacity = strlen(room_name_bytes)+1,
    .bytes = room_name_bytes,
  };
  if (osFileExists(filename)) {
    // TODO: encode RoomClass in the editor and save file
    room.class = IndoorRoom;
    String file_data = osFileRead(&state.game_scratch, room_name_bytes);
    for (u32 i = 0; i < ROOM_TILE_COUNT; i++) {
      room.tiles[i] = (TileType)file_data.bytes[i];
    }
    for (u32 i = 0; i < ROOM_TILE_COUNT; i++) {
      EntityType t = (EntityType)file_data.bytes[ROOM_TILE_COUNT+i];
      if (t != EntityNull) {
        Entity e = {
          .type = t,
          .id = state.next_eid++,
          .room_xyz = xyz,
          .x = i % ROOM_WIDTH,
          .y = i / ROOM_WIDTH,
          .features = entityFeaturesFromType(t),
        };
        addEntityToRoom(&permanent_arena, &room, e);
      }
    }
  } else {
    RoomClass previous_class = getNearestRoomClass(rooms, x, y, z);
    room.class = previous_class;
    if (room.class == INVALIDRoom) {
      room.class = GrasslandRoom; // default to grass
    }
    u8 roll = rand() % 20;
    switch (room.class) {
      case INVALIDRoom:
      case RoomClassCount:
        assert(false); // this should not happen
        return;
      case DesertRoom: {
        if (roll == 17 || roll == 18) {
          room.class = ScrubRoom;
        } else if (roll == 19) {
          room.class = GrasslandRoom;
        }
      } break;
      case ScrubRoom: {
        if (roll == 16) {
          room.class = DesertRoom;
        } else if (roll == 17 || roll == 18) {
          room.class = GrasslandRoom;
        } else if (roll == 19) {
          room.class = ForestRoom;
        }
      } break;
      case GrasslandRoom: {
        if (roll == 15) {
          room.class = DesertRoom;
        } else if (roll == 16 || roll == 17) {
          room.class = ScrubRoom;
        } else if (roll == 18 || roll == 19) {
          room.class = ForestRoom;
        }
      } break;
      case ForestRoom: {
        if (roll == 16 || roll == 17) {
          room.class = ScrubRoom;
        } else if (roll == 18 || roll == 19) {
          room.class = GrasslandRoom;
        }
      } break;
      case UndergroundRoom: break; // stay the same always
      case IndoorRoom: {
        room.class = GrasslandRoom;
      } break;
    }
    // now actually generate the room
    switch (room.class) {
      case IndoorRoom: {
        fillRoom(&room, TileStoneFloor);
      } break;
      case UndergroundRoom: {
        fillRoom(&room, TileStoneFloor);
        // add some patches of gravel
        for (i32 i = 0; i < ((rand() % (ROOM_TILE_COUNT/3)) + 5); i++) {
          room.tiles[ROOM_WIDTH + 1 + (rand() % (ROOM_WIDTH*(ROOM_HEIGHT-2)))] = TileGravel;
        }
        setRoomBorder(&room, TileSolidStone);
      } break;
      case DesertRoom: {
        fillRoom(&room, TileDirt);
        // add some patches of gravel
        for (i32 i = 0; i < ((rand() % (ROOM_TILE_COUNT/3)) + 5); i++) {
          room.tiles[ROOM_WIDTH + 1 + (rand() % (ROOM_WIDTH*(ROOM_HEIGHT-2)))] = TileGravel;
        }
        // add some patches of grass
        for (i32 i = 0; i < ((rand() % (ROOM_TILE_COUNT/3)) + 1); i++) {
          room.tiles[ROOM_WIDTH + 1 + (rand() % (ROOM_WIDTH*(ROOM_HEIGHT-2)))] = TileShortGrass;
        }
        // add some random Boulders
        addNEntitiesRandomly(&permanent_arena, &room, EntityBoulder, (rand() % 32)+2);
      } break;
      case ScrubRoom: {
        fillRoom(&room, TileShortGrass);
        // add some patches of gravel
        for (i32 i = 0; i < ((rand() % (ROOM_TILE_COUNT/4)) + 1); i++) {
          room.tiles[ROOM_WIDTH + 1 + (rand() % (ROOM_WIDTH*(ROOM_HEIGHT-2)))] = TileGravel;
        }
        // add some patches of tall grass
        for (i32 i = 0; i < ((rand() % (ROOM_TILE_COUNT/3)) + 4); i++) {
          room.tiles[ROOM_WIDTH + 1 + (rand() % (ROOM_WIDTH*(ROOM_HEIGHT-2)))] = TileTallGrass;
        }
        // add some random bushes
        addNEntitiesRandomly(&permanent_arena, &room, EntityBush, (rand() % ROOM_TILE_COUNT/3)+(ROOM_TILE_COUNT/5));
      } break;
      case GrasslandRoom: {
        fillRoom(&room, TileTallGrass);
        // add some patches of short grass
        for (i32 i = 0; i < ((ROOM_TILE_COUNT/3) + 4); i++) {
          room.tiles[ROOM_WIDTH + 1 + (rand() % (ROOM_WIDTH*(ROOM_HEIGHT-2)))] = TileShortGrass;
        }
        // add some random bushes
        addNEntitiesRandomly(&permanent_arena, &room, EntityBush, (rand() % ROOM_TILE_COUNT/4)+(ROOM_TILE_COUNT/6));
        // add some random trees
        addNEntitiesRandomly(&permanent_arena, &room, EntityTree, (rand() % ROOM_TILE_COUNT/4)+(ROOM_TILE_COUNT/6));
      } break;
      case ForestRoom: {
        fillRoom(&room, TileShortGrass);
        // add some patches of dirt
        for (i32 i = 0; i < ((rand() % (ROOM_TILE_COUNT/3)) + 5); i++) {
          room.tiles[ROOM_WIDTH + 1 + (rand() % (ROOM_WIDTH*(ROOM_HEIGHT-2)))] = TileDirt;
        }
        // maybe add a little pond
        if (rand() % 6 == 0) {
          u8 starting_x = rand() % (ROOM_WIDTH-3);
          u8 starting_y = rand() % (ROOM_HEIGHT-3);
          room.tiles[starting_x + 1 + (ROOM_HEIGHT*starting_y)] = TileWater;
          room.tiles[starting_x + 2 + (ROOM_HEIGHT*starting_y)] = TileWater;
          room.tiles[starting_x + 0 + (ROOM_HEIGHT*(starting_y+1))] = TileWater;
          room.tiles[starting_x + 1 + (ROOM_HEIGHT*(starting_y+1))] = TileWater;
          room.tiles[starting_x + 2 + (ROOM_HEIGHT*(starting_y+1))] = TileWater;
          room.tiles[starting_x + 1 + (ROOM_HEIGHT*(starting_y+2))] = TileWater;
          room.tiles[starting_x + 2 + (ROOM_HEIGHT*(starting_y+2))] = TileWater;
        }
        // add some random trees
        addNEntitiesRandomly(&permanent_arena, &room, EntityTree, (rand() % ROOM_TILE_COUNT/2)+(ROOM_TILE_COUNT/4));
        // add some random bushes
        addNEntitiesRandomly(&permanent_arena, &room, EntityBush, (rand() % ROOM_TILE_COUNT/6));
        // add some random fallen wood
        addNEntitiesRandomly(&permanent_arena, &room, EntityWood, (rand() % ROOM_TILE_COUNT/8));
      } break;
      case INVALIDRoom:
      case RoomClassCount:
        assert(false); // this should not happen
        return;
    }
  }

  u32 index = roomHashXYZ(x,y,z);
  dbg("RoomMap index %d", index);
  Room* pre_existing = &rooms->items[index];
  if (isRoomNull(pre_existing)) { // pre-existing room is null, means no collision for sure
    rooms->items[index] = room;
  } else { // collision.
    assert(x != pre_existing->x);
    assert(y != pre_existing->y);
    assert(z != pre_existing->z);
    for (i32 i = 0; i < ROOM_MAP_COLLISIONS_LEN; i++) {
      if (isRoomNull(&rooms->collisions[i])) {
        rooms->collisions[i] = room;
        return;
      }
    }
    printf("error: RoomMap ran out of collisions space trying to generate (%d,%d,%d)\n", x, y, z);
    assert(1 == 2); // we should never arrive here during valid operation
  }
}

fn void moveEntityBetweenRooms(Room* from, Room* to, Entity* entity) {
  Entity copy = *entity;
  deleteEntity(from, copy.id);
  copy.room_xyz.x = to->x;
  copy.room_xyz.y = to->y;
  copy.room_xyz.z = to->z;
  addEntityToRoom(&permanent_arena, to, copy);
}

fn bool isRoomBorder(i32 x, i32 y) {
  return x == 0 || y == 0 || x == ROOM_WIDTH || y == ROOM_HEIGHT;
}

fn void exitWithErrorMessage(ptr msg) {
  printf("error: %s", msg);
  exit(1);
}

fn u32 pushClient(ClientList* clients, SocketAddress addr) {
  Client new_client = {0};
  new_client.last_ping = state.frame;
  new_client.address = addr;
  new_client.entered_room_on_frame = state.frame;

  // first, try to overwrite an old dc'ed client
  for (u32 i = 1; i < clients->length; i++) {
    if (clients->items[i].character_eid == 0) {
      clients->items[i] = new_client;
      return i;
    }
  }

  // if there are none, then add the client to the end of the list
  assert(clients->capacity > clients->length);//TODO grow the list if we need to
  clients->items[clients->length] = new_client;
  u32 result = clients->length;
  clients->length += 1;
  return result;
}

fn bool deleteClientByEId(ClientList* clients, u64 id) {
  bool succeeded = false;
  Client blank_client = {0};
  // i=1 because first client is null-client
  for (u32 i = 1; i < clients->length && !succeeded; i++) {
    if (clients->items[i].character_eid == id) {
      clients->items[i] = blank_client;
      succeeded = true;
    }
  }
  return succeeded;
}

fn u32 findClientHandleByEId(ClientList* clients, u64 id) {
  for (u32 i = 0; i < clients->length; i++) {
    Client c = clients->items[i];
    if (c.character_eid == id) {
      return i;
    }
  }
  return 0;
}

fn u32 findClientHandleBySocketAddress(ClientList* clients, SocketAddress address) {
  for (u32 i = 0; i < clients->length; i++) {
    Client c = clients->items[i];
    if (socketAddressEqual(address, c.address)) {
      return i;
    }
  }
  return 0;
}

fn void moveCharacter(Arena* arena, Room* room, Entity* character, Direction direction, u32 client_index) {
  u8 x = character->x;
  u8 y = character->y;
  u8 new_y = character->y;
  u8 new_x = character->x;
  i32 next_room_x = room->x; // only used if stepping on NextRoomPad
  i32 next_room_y = room->y; // only used if stepping on NextRoomPad
  bool in_bounds = false;
  switch(direction) {
    case DirectionInvalid:
    case Direction_Count:
      dbg("invalid direction %d", (u8)direction);
      return;
    case North: {
      next_room_y -= 1;
      if (new_y > 0) {
        new_y -= 1;
        in_bounds = true;
      } else {
        new_y = ROOM_HEIGHT - 1;
      }
    } break;
    case South: {
      next_room_y += 1;
      if (new_y < (ROOM_HEIGHT - 1)) {
        new_y += 1;
        in_bounds = true;
      } else {
        new_y = 0;
      }
    } break;
    case East: {
      next_room_x += 1;
      if (new_x < (ROOM_WIDTH - 1)) {
        new_x += 1;
        in_bounds = true;
      } else {
        new_x = 0;
      }
    } break;
    case West: {
      next_room_x -= 1;
      if (new_x > 0) {
        new_x -= 1;
        in_bounds = true;
      } else {
        new_x = ROOM_WIDTH - 1;
      }
    } break;
  }
  if (in_bounds) {
    bool spot_open = !isBlocked(room, new_x, new_y);
    if (spot_open) {
      character->y = new_y;
      character->x = new_x;
      character->changed = true;
    }
  } else {
    generateNewRoom(state.rooms, next_room_x, next_room_y, room->z);
    Room* next_room = getRoomXYZ(state.rooms, next_room_x, next_room_y, room->z);
    bool spot_open = !isBlocked(next_room, new_x, new_y);
    if (spot_open) {
      character->y = new_y;
      character->x = new_x;
      character->changed = true;
      state.clients.items[client_index].room_xyz.y = next_room_y;
      state.clients.items[client_index].room_xyz.x = next_room_x;
      state.clients.items[client_index].entered_room_on_frame = state.frame;
      Account* account = findAccountById(state.clients.items[client_index].account_id);
      account->room_xyz.x = next_room_x;
      account->room_xyz.y = next_room_y;
      moveEntityBetweenRooms(room, next_room, character);
      printf("moved to new room\n");
    }
  }
}

fn void handleIncomingMessage(u8* message, u32 len, SocketAddress sender, i32 socket) {
  dbg("%d: %s from %s:%d\n", len, command_type_strings[message[0]], inet_ntoa(sender.sin_addr), sender.sin_port);
  u32 msg_idx = 0;
  ParsedClientCommand parsed = {
    .type = (CommandType)message[msg_idx++],
    .sender_ip = sender.sin_addr.s_addr,
    .sender_port = sender.sin_port,
  };
  u8 temp_bytes[UDP_MAX_MESSAGE_LEN] = {0};
  String temp_str = {
    .bytes = (char*)temp_bytes,
    .length = 0,
    .capacity = 0,
  };
  switch (parsed.type) {
    case CommandLogin: {
      // parse the login command
      parsed.alt_port = ~readU16FromBufferLE(message + msg_idx);
      msg_idx += 2;
      parsed.alt_ip = ~readI32FromBufferLE(message + msg_idx);
      msg_idx += 4;

      // parse the name
      u8 name_len = message[msg_idx++];
      MemoryZero(temp_bytes, UDP_MAX_MESSAGE_LEN);
      temp_str.length = name_len;
      temp_str.capacity = temp_str.length+1;
      for (u32 j = 0; j < temp_str.length; j++) {
        temp_str.bytes[j] = message[msg_idx+j];
      }
      parsed.name = allocStringChunkList(&state.string_arena, temp_str);
      msg_idx += temp_str.length;

      // parse the password
      u32 pw_len = 0;
      while (message[msg_idx+pw_len]) {
        pw_len += 1;
      }
      MemoryZero(temp_bytes, UDP_MAX_MESSAGE_LEN);
      temp_str.length = pw_len;
      temp_str.capacity = temp_str.length+1;
      for (u32 j = 0; j < temp_str.length; j++) {
        temp_str.bytes[j] = message[msg_idx+j];
      }
      parsed.pass = allocStringChunkList(&state.string_arena, temp_str);
      msg_idx += temp_str.length;

      printf("Logging in player: %s %d %s\n", MESSAGE_STRINGS[parsed.type], name_len, message + 7);
    } break;
    case CommandKeepAlive:
      break;
    case CommandWalk: {
      parsed.byte = message[msg_idx++];
    } break;
    case CommandStartFight: {
      parsed.id = readU64FromBufferLE(message + 1);
    } break;
    case CommandWonFight: {
      parsed.id = readU64FromBufferLE(message + 1);
      parsed.hp = readU64FromBufferLE(message + 9);
      parsed.mp = readU64FromBufferLE(message + 17);
    } break;
    case CommandLostFight: {
      printf("command not implemented\n");
      // TODO compare WonFight vs LostFight
    } break;
    case CommandCreateCharacter: {
      printf("command create character received\n");
      parsed.byte = message[1];
      parsed.spell_type = message[2];
    } break;
    case CommandInvalid:
    case CommandType_Count: {
      dbg("invalid command type");
    } break;
  }

  pccThreadSafeQueuePush(state.network_recv_queue, &parsed);
  fflush(stdout);
}

fn void* sendNetworkUpdates(void* sock) {
  i32* socket_ptr = (i32*)sock;
  i32 socket = *socket_ptr;
  while (true) {
    u64 loop_start = osTimeMicrosecondsNow();

    // 1. clear out our "outgoingMessage" queue
    {
      UDPMessage to_send = { 0 };
      UDPMessage* next_to_send = outgoingMessageNonblockingQueuePop(state.network_send_queue, &to_send);
      u8List bytes_list = { 0 };
      while (next_to_send != NULL) {
        bytes_list.items = to_send.bytes;
        bytes_list.length = to_send.bytes_len;
        bytes_list.capacity = to_send.bytes_len;
        sendUDPu8List(socket, &to_send.address, &bytes_list);
        next_to_send = outgoingMessageNonblockingQueuePop(state.network_send_queue, &to_send);
      }
    }

    lockMutex(&state.client_mutex); {
      // WARNING the `i` starts at 1 here because state.clients.items[0] is a "null" Client
      for (u32 i = 1; i < state.clients.length; i++) {
        Client client = state.clients.items[i];
        if (client.last_ping+CLIENT_TIMEOUT_FRAMES < state.frame) {
          memset(&state.clients.items[i], 0, sizeof(Client));
          continue;
        }
        if (client.character_eid == 0) {
          continue; // they are still creating their character
        }
        lockMutex(&state.mutex);
          // copy out the relevant character, room, and visible entities
          Room room = *getRoomXYZ(state.rooms, client.room_xyz.x, client.room_xyz.y, client.room_xyz.z);
        unlockMutex(&state.mutex);

        if (client.entered_room_on_frame+3 >= state.frame) {
          // 1. Send the Tiles
          // split up the message across multiple udp msgs of size UDP_MAX_MESSAGE_LEN
          Entity* client_character = findEntityInRoom(&room, client.character_eid);
          bool finished_sending_whole_room = false;
          u16 tile_pos = 0;
          while (!finished_sending_whole_room) {
            u64 index = 0;
            u8 bytes[UDP_MAX_MESSAGE_LEN] = {0};
            // msg type enum
            bytes[index++] = (u8)MessageRoomLayout;
            // state.frame
            index += writeU64ToBufferLE(bytes + index, state.frame);
            // room-xyz
            index += writeI32ToBufferLE(bytes + index, room.x);
            index += writeI32ToBufferLE(bytes + index, room.y);
            index += writeI32ToBufferLE(bytes + index, room.z);
            // tiles
            while (tile_pos < ROOM_TILE_COUNT && index+TILE_MESSAGE_SIZE < UDP_MAX_MESSAGE_LEN) {
              if (entityCanSee(&room, client_character, tile_pos)) {
                index += writeU16ToBufferLE(bytes + index, tile_pos);
                bytes[index++] = (u8)room.tiles[tile_pos];
              }
              tile_pos++;
            }
            if (tile_pos >= ROOM_TILE_COUNT) {
              finished_sending_whole_room = true;
            }
            u8List msg = {0};
            msg.capacity = UDP_MAX_MESSAGE_LEN;
            msg.items = bytes;
            msg.length = index;
            printf("sendNetworkUpdates MessageRoomLayout client i=%d, len=%lld port=%d addr=%s sock=%d\n", i, msg.length, ntohs(client.address.sin_port), inet_ntoa(client.address.sin_addr), socket);
            sendUDPu8List(socket, &client.address, &msg);
          }

          // 2. Send the Entities
          {
            u64 index = 0;
            u8 bytes[UDP_MAX_MESSAGE_LEN] = {0};
            u64 bytes_header = 1+8+4+4+4;
            // msg type enum
            bytes[index++] = (u8)MessageRoomEntities;
            // state.frame
            index += writeU64ToBufferLE(bytes + index, state.frame);
            // room-xyz
            index += writeI32ToBufferLE(bytes + index, room.x);
            index += writeI32ToBufferLE(bytes + index, room.y);
            index += writeI32ToBufferLE(bytes + index, room.z);
            printf("room_xyz: %d %d %d\n", room.x, room.y, room.z);

            for (u32 j = 0; j < room.entities.length; j++) {
              Entity current = entityFromChunkList(&room.entities, j);
              Account* acct = findAccountByEId(current.id);
              bool this_entity_fits = ENTITY_HEADER_MESSAGE_SIZE+index < UDP_MAX_MESSAGE_LEN;
              if (current.type == EntityCharacter) {
                this_entity_fits = ENTITY_MESSAGE_SIZE+index+(acct->name.length +8) < UDP_MAX_MESSAGE_LEN;
              }

              if (!this_entity_fits) {
                // bytes is full, so send our message and start a new one
                u8List msg = {0};
                msg.capacity = UDP_MAX_MESSAGE_LEN;
                msg.items = bytes;
                msg.length = index;
                printf("sendNetworkUpdates MessageRoomEntities client i=%d, len=%lld port=%d addr=%d sock=%d\n", i, msg.length, client.address.sin_port, client.address.sin_addr.s_addr, socket);
                sendUDPu8List(socket, &client.address, &msg);

                // clear old bytes
                memset(bytes + bytes_header, 0, UDP_MAX_MESSAGE_LEN - bytes_header);
                index = bytes_header;
              }

              index = entitySerialize(current, acct, index, bytes);
            }
            // send whatever's left
            if (index > bytes_header) {
              u8List msg = {0};
              msg.capacity = UDP_MAX_MESSAGE_LEN;
              msg.items = bytes;
              msg.length = index;
              printf("sendNetworkUpdates MessageRoomDelta client i=%d, len=%lld port=%d addr=%d sock=%d\n", i, msg.length, client.address.sin_port, client.address.sin_addr.s_addr, socket);
              sendUDPu8List(socket, &client.address, &msg);
            }
          }

        } else {
          // diff/updates only
          {
            u64 index = 0;
            u8 bytes[UDP_MAX_MESSAGE_LEN] = {0};
            u64 bytes_header = 1+8+4+4+4;
            // msg type enum
            bytes[index++] = (u8)MessageRoomDelta;
            // state.frame
            index += writeU64ToBufferLE(bytes + index, state.frame);
            // room-xyz
            index += writeI32ToBufferLE(bytes + index, room.x);
            index += writeI32ToBufferLE(bytes + index, room.y);
            index += writeI32ToBufferLE(bytes + index, room.z);

            Entity current = {0};
            Room* r_ptr = &room;
            iterateRoomEntities(r_ptr, current, {
              if (current.changed) {
                Account* acct = findAccountByEId(current.id);
                bool this_entity_fits = ENTITY_HEADER_MESSAGE_SIZE+index < UDP_MAX_MESSAGE_LEN;
                if (current.type == EntityCharacter) {
                  this_entity_fits = ENTITY_MESSAGE_SIZE+index+(acct->name.length +8) < UDP_MAX_MESSAGE_LEN;
                }
                if (!this_entity_fits) {
                  // bytes is full, so send our message and start a new one
                  u8List msg = {0};
                  msg.capacity = UDP_MAX_MESSAGE_LEN;
                  msg.items = bytes;
                  msg.length = index;
                  printf("sendNetworkUpdates MessageRoomDelta client i=%d, len=%lld port=%d addr=%d sock=%d\n", i, msg.length, client.address.sin_port, client.address.sin_addr.s_addr, socket);
                  sendUDPu8List(socket, &client.address, &msg);

                  // clear old bytes
                  memset(bytes + bytes_header, 0, UDP_MAX_MESSAGE_LEN - bytes_header);
                  index = bytes_header;
                }

                index = entitySerialize(current, acct, index, bytes);
              }
            });
            // send whatever's left
            if (index > bytes_header) {
              u8List msg = {0};
              msg.capacity = UDP_MAX_MESSAGE_LEN;
              msg.items = bytes;
              msg.length = index;
              printf("sendNetworkUpdates MessageRoomDelta client i=%d, len=%lld port=%d addr=%d sock=%d\n", i, msg.length, client.address.sin_port, client.address.sin_addr.s_addr, socket);
              sendUDPu8List(socket, &client.address, &msg);
            }
          }
          // deletes
          {
            u64 index = 0;
            u8 bytes[UDP_MAX_MESSAGE_LEN] = {0};
            u8List msg = { .capacity = UDP_MAX_MESSAGE_LEN, .items = bytes, };
            u64 bytes_header = 1+8+4+4+4;
            // msg type enum
            bytes[index++] = (u8)MessageRoomDeletions;
            // state.frame
            index += writeU64ToBufferLE(bytes + index, state.frame);
            // room-xyz
            index += writeI32ToBufferLE(bytes + index, room.x);
            index += writeI32ToBufferLE(bytes + index, room.y);
            index += writeI32ToBufferLE(bytes + index, room.z);

            for (u32 i = 0; i < LEFT_ROOM_ENTITES_LEN; i++) {
              LeftRoomEntity current = state.entities_who_left_a_room_recently[i];
              bool frame_is_recent = current.frame+6 >= state.frame;
              bool same_room = current.room_xyz.x == room.x && current.room_xyz.y == room.y && current.room_xyz.z == room.z;
              if (current.eid > 0 && frame_is_recent && same_room) {
                printf("eid=%lld c.frame=%lld state.frame=%lld x=%d y=%d z=%d | ", current.eid, current.frame, state.frame, current.room_xyz.x, current.room_xyz.y, current.room_xyz.z);
                bool this_entity_fits = 8+index < UDP_MAX_MESSAGE_LEN;
                if (!this_entity_fits) {
                  // bytes is full, so send our message and start a new one
                  msg.length = index;
                  printf("sendNetworkUpdates MessageRoomDeletions frame=%lld, len=%lld port=%d addr=%s\n", state.frame, msg.length, htons(client.address.sin_port), inet_ntoa(client.address.sin_addr));
                  sendUDPu8List(socket, &client.address, &msg);

                  // clear old bytes
                  memset(bytes + bytes_header, 0, UDP_MAX_MESSAGE_LEN - bytes_header);
                  index = bytes_header;
                }
                index += writeU64ToBufferLE(bytes + index, current.eid);
              }
            }
            // send whatever's left
            if (index > bytes_header) {
              msg.length = index;
              printf("sendNetworkUpdates MessageRoomDeletions msg_index=%lld, frame=%lld, port=%d addr=%s, ent_frame=%lld\n", index, state.frame, htons(client.address.sin_port), inet_ntoa(client.address.sin_addr), state.entities_who_left_a_room_recently[0].frame);
              sendUDPu8List(socket, &client.address, &msg);
            }
          }
        }
      }
    } unlockMutex(&state.client_mutex);

    // unmark everything changed by manually iterating all the rooms in the game
    for (u32 i = 0; i < MAX_ROOMS; i++) {
      Room *room = state.rooms->items + i;
      if (!isRoomNull(room)) {
        EntityChunk* chunk = room->entities.first;
        for (u32 chunk_i = 0; chunk_i < room->entities.chunks; chunk_i++) {
          for (u32 j = 0; j < chunk->length; j++) {
            chunk->items[j].changed = false;
          }
          chunk = chunk->next;
        }
      }
    }
    for (u32 i = 0; i < ROOM_MAP_COLLISIONS_LEN; i++) {
      Room *room = state.rooms->collisions + i;
      if (!isRoomNull(room)) {
        EntityChunk* chunk = room->entities.first;
        for (u32 chunk_i = 0; chunk_i < room->entities.chunks; chunk_i++) {
          for (u32 j = 0; j < chunk->length; j++) {
            chunk->items[j].changed = false;
          }
          chunk = chunk->next;
        }
      }
    }

    u32 loop_duration = osTimeMicrosecondsNow() - loop_start;
    i32 remaining_time = GOAL_NETWORK_SEND_LOOP_US - loop_duration;
    if (remaining_time > 0) {
      osSleepMicroseconds(remaining_time);
    }
  }
  return NULL;
}

fn void* gameLoop(void* unused) {
  // TODO: https://www.rfleury.com/p/multi-core-by-default
  // go wide here. use all the cores to compute
  // rooms don't interact, so each core can grab a room as it finishes
  UDPMessage outgoing_message = {0};
  u64 loop_start;
  u64 last_burn = 0;
  u64 last_hp_regen = 0;
  Arena scratch_arena = {0};
  arenaInit(&scratch_arena);
  while (true) {
    loop_start = osTimeMicrosecondsNow();
    state.frame += 1;

    lockMutex(&state.client_mutex); lockMutex(&state.mutex); {

    // 1. process client messages
    {
      u32 msg_iters = 0;
      SocketAddress sender = {0};
      ParsedClientCommand msg = {0};
      ParsedClientCommand* next_net_msg = pccThreadSafeNonblockingQueuePop(state.network_recv_queue, &msg);
      while (next_net_msg != NULL) {
        msg_iters += 0;
        sender.sin_addr.s_addr = msg.sender_ip;
        sender.sin_port = msg.sender_port;
        // find which client it is
        u32 client_handle = findClientHandleBySocketAddress(&state.clients, sender);
        Client* client = &state.clients.items[client_handle];
        Room* room = getRoomXYZ(state.rooms, client->room_xyz.x, client->room_xyz.y, client->room_xyz.z);
        switch (msg.type) {
          case CommandKeepAlive: {
            dbg("KeepAlive for client_handle=%d, %ld", client_handle, state.frame);
            state.clients.items[client_handle].last_ping = state.frame;
          } break;
          case CommandWalk: {
            Entity* character = findEntityInRoom(room, client->character_eid);
            moveCharacter(&scratch_arena, room, character, (Direction)msg.byte, client_handle);
          } break;
          case CommandLogin: {
            if (client_handle == 0) {
              client_handle = pushClient(&state.clients, sender);
              Client* client = &state.clients.items[client_handle];
              printf("pushed new client handle = %d\n", client_handle);
            }
            // update/set the lan_ip/port info for p2p connections
            client->lan_ip = htonl(msg.alt_ip);
            client->lan_port = htons(msg.alt_port);

            /*
            struct in_addr ipaddr;
            ipaddr.s_addr = htonl(msg.alt_ip);
            printf("client #%d: SENDER=%s:%d\n", client_handle, inet_ntoa(sender.sin_addr), ntohs(sender.sin_port));
            printf("            LAN=%s:%d   %d vs %d vs %d\n", inet_ntoa(ipaddr), msg.alt_port, msg.alt_ip, htonl(msg.alt_ip), sender.sin_addr.s_addr);
            */

            printf("msg.name.total_size=%lld  ", msg.name.total_size);
            String name = stringChunkToString(&permanent_arena, msg.name);
            releaseStringChunkList(&state.string_arena, &msg.name);

            String pw = stringChunkToString(&permanent_arena, msg.pass);
            releaseStringChunkList(&state.string_arena, &msg.pass);

            Account* existing_account = findAccountByName(name);
            //printf("pw(%lld): %s acct?: %d\n", pw.length, pw.bytes, existing_account != NULL);
            printf("name(%lld): %s pw(%lld): %s acct?: %d\n", name.length, name.bytes, pw.length, pw.bytes, existing_account != NULL);
            fflush(stdout);
            if (existing_account) {
              printf(" existing account\n");
              bool pw_matches = stringsEq(&pw, &existing_account->pw);
              arenaDealloc(&permanent_arena, pw.capacity);
              arenaDealloc(&permanent_arena, name.capacity);
              if (pw_matches) {
                printf(" pw matched\n");
                client->room_xyz = existing_account->room_xyz;
              } else {
                // tell the client they did a bad pw
                outgoing_message.bytes[0] = (u8)MessageBadPw;
                outgoing_message.bytes_len = 1;
                outgoing_message.address = sender;
                outgoingMessageQueuePush(state.network_send_queue, &outgoing_message);
                printf("MessageBadPw sent\n");
                break;
              }
            } else {
              Account new_account = {
                .character_eid = 0,
                .name = name,
                .pw = pw,
              };
              existing_account = newAccount(&permanent_arena, new_account);
            }
            client->account_id = existing_account->id;
            if (existing_account->character_eid != 0) {
              client->character_eid = existing_account->character_eid;
              client->entered_room_on_frame = state.frame;

              // tell the client their character id
              outgoing_message.bytes[0] = (u8)MessageCharacterId;
              writeU64ToBufferLE(outgoing_message.bytes + 1, existing_account->character_eid);
              outgoing_message.bytes_len = 9;
              outgoing_message.address = sender;
              outgoingMessageQueuePush(state.network_send_queue, &outgoing_message);
              printf("MessageCharacterId sent\n");
            } else {
              // tell the client they made a new account
              outgoing_message.bytes[0] = (u8)MessageNewAccountCreated;
              outgoing_message.bytes_len = 1;
              outgoing_message.address = sender;
              outgoingMessageQueuePush(state.network_send_queue, &outgoing_message);
              printf("MessageNewAccountCreated sent\n");
            }
            printf("eid=%lld, client_handle=%d, acct_id=%lld\n", existing_account->character_eid, client_handle, existing_account->id);
          } break;
          case CommandCreateCharacter: {
            if (client->character_eid == 0) {
              // Create new character
              XYZ room_xyz = {0, 0, 0 };
              Entity character = {
                .type = EntityCharacter,
                .id = state.next_eid++,
                .room_xyz = room_xyz,
                .x = ROOM_WIDTH / 2 + 2,
                .y = ROOM_HEIGHT / 2,
                .hp = 10,
                .max_hp = 10,
                .mp = 20,
                .max_mp = 20,
                .changed = true,
                .features = entityFeaturesFromType(EntityCharacter),
                .color = msg.byte,
              };
              SetFlag(character.known_spells_flags, SpellTypeMagicMissile);
              SetFlag(character.known_spells_flags, SpellTypeShield);
              // they pick Silence or Empower
              if (msg.spell_type == SpellTypeSilence) {
                SetFlag(character.known_spells_flags, SpellTypeSilence);
              } else {
                SetFlag(character.known_spells_flags, SpellTypeEmpower);
              }
              if (character.color == ANSI_HP_RED) {
                SetFlag(character.known_spells_flags, SpellTypeFireball);
              } else if (character.color == ANSI_MP_BLUE) {
                SetFlag(character.known_spells_flags, SpellTypeManaRain);
              } else if (character.color == ANSI_MID_GREEN) {
                SetFlag(character.known_spells_flags, SpellTypeHeal);
              } else if (character.color == ANSI_BROWN) {
                SetFlag(character.known_spells_flags, SpellTypeEarthenWall);
              }
              dbg("made new character %ld\n", character.id);
              Room* room = getRoomXYZ(state.rooms, 0,0,0);
              addEntityToRoom(&permanent_arena, room, character);
              client->room_xyz = room_xyz;
              client->character_eid = character.id;
              client->entered_room_on_frame = state.frame;
              Account* account = findAccountById(client->account_id);
              account->room_xyz = room_xyz;
              account->character_eid = character.id;

              // tell the client their character id
              outgoing_message.address = sender;
              outgoing_message.bytes_len = 9;
              outgoing_message.bytes[0] = (u8)MessageCharacterId;
              writeU64ToBufferLE(outgoing_message.bytes + 1, character.id);
              outgoingMessageQueuePush(state.network_send_queue, &outgoing_message);
              printf("MessageCharacterId sent");
            } else {
              dbg("client tried to create a character when he already has one.");
            }
          } break;
          case CommandStartFight: {
            printf("startfight with id=%lld\n", msg.id);
            // TODO: bounds checking to ensure this guy and his target are still on the same tile
            // and that the target enemy is not already in a fight
            bool sent_fight_ip = false;
            for (u32 i = 0; i < state.clients.length; i++) {
              Client c = state.clients.items[i];
              if (c.character_eid == msg.id) {
                SocketAddress target_enemy_address = c.address;
                u32 msg_idx = 0;

                // 1. respond to fight-starter
                outgoing_message.bytes[msg_idx++] = MessageFightIp;
                msg_idx += writeU32ToBufferLE(outgoing_message.bytes + msg_idx, ntohl(target_enemy_address.sin_addr.s_addr));
                msg_idx += writeU16ToBufferLE(outgoing_message.bytes + msg_idx, ntohs(target_enemy_address.sin_port));
                msg_idx += writeU32ToBufferLE(outgoing_message.bytes + msg_idx, ntohl(c.lan_ip));
                msg_idx += writeU16ToBufferLE(outgoing_message.bytes + msg_idx, ntohs(c.lan_port));
                outgoing_message.address = sender;
                outgoing_message.bytes_len = msg_idx;
                outgoingMessageQueuePush(state.network_send_queue, &outgoing_message);
                printf("sent FightIP to %s:%d", inet_ntoa(sender.sin_addr), ntohs(sender.sin_port));

                // 2. warn fight-victim, (so they can pierce the NAT)
                Client sc = state.clients.items[findClientHandleBySocketAddress(&state.clients, sender)];
                msg_idx = 1;
                msg_idx += writeU32ToBufferLE(outgoing_message.bytes + msg_idx, ntohl(sender.sin_addr.s_addr));
                msg_idx += writeU16ToBufferLE(outgoing_message.bytes + msg_idx, ntohs(sender.sin_port));
                msg_idx += writeU32ToBufferLE(outgoing_message.bytes + msg_idx, ntohl(sc.lan_ip));
                msg_idx += writeU16ToBufferLE(outgoing_message.bytes + msg_idx, ntohs(sc.lan_port));
                outgoing_message.address = target_enemy_address;
                outgoing_message.bytes_len = msg_idx;
                outgoingMessageQueuePush(state.network_send_queue, &outgoing_message);
                printf(" | sent FightIP to %s:%d\n", inet_ntoa(target_enemy_address.sin_addr), ntohs(target_enemy_address.sin_port));

                sent_fight_ip = true;
                break;
              }
            }
            if (!sent_fight_ip) {
              outgoing_message.bytes[0] = (u8)MessageFightOpponentNotOnline;
              outgoing_message.address = sender;
              outgoing_message.bytes_len = 1;
              outgoingMessageQueuePush(state.network_send_queue, &outgoing_message);
            }
          } break;
          case CommandWonFight: {
            // update the winner's info
            printf("client_handle=%d won a fight\n", client_handle);
            Entity* winner_character = findEntityInRoom(room, client->character_eid);
            winner_character->hp = msg.hp;
            winner_character->mp = msg.mp;
            winner_character->wins += 1;
            winner_character->changed = true;
            // delete the loser... he's dead
            deleteClientByEId(&state.clients, msg.id);
            deleteEntity(room, msg.id);
            Account* loser_account = findAccountByEId(msg.id);
            //TODO - this should imply the "create a character" screen
            loser_account->character_eid = 0;
            // TODO figure out how to send "entity destroyed" to clients
          } break;
          case CommandLostFight: {
            // TODO
          } break;
          case CommandType_Count:
          case CommandInvalid:
            dbg("invalid message from queue");
            break;
        }
        next_net_msg = pccThreadSafeNonblockingQueuePop(state.network_recv_queue, &msg);
        msg_iters++;
      }
    }

    // 2. tick non-user entities
    // iterate all the rooms
    bool burn_tick = (loop_start - last_burn > BURN_TICK_US);
    if (burn_tick) {
      last_burn = loop_start;
    }
    bool regen_hp_tick = (loop_start - last_hp_regen > REGEN_HP_TICK_US);
    if (regen_hp_tick) {
      last_hp_regen = loop_start;
    }
    Room* room = NULL;
    for (u32 i = 0; i < MAX_ROOMS; i++) {
      room = &state.rooms->items[i];
      if (!isRoomNull(room)) {
        for (u32 j = 0; j < room->entities.length; j++) {
          Entity* current = entityPtrFromChunkList(&room->entities, j);

          bool can_deal_burn_damage = burn_tick && CheckFlag(current->features, FeatureDealsContinuousDamage);
          if (can_deal_burn_damage) {
            EntityList same_spot_list = getEntitiesAtXY(&scratch_arena, room, current->x, current->y); 
            for (u32 k = 0; k < same_spot_list.length; k++) {
              Entity* e = &same_spot_list.items[k];
              bool can_be_burned = e->hp > 0;
              if (can_be_burned) {
                printf("burning %lld; ", e->id);
                e = findEntityInRoom(room, e->id);
                e->hp -= 1;
                e->changed = true;
              }
            }
          }

          bool can_regen_hp = regen_hp_tick && CheckFlag(current->features, FeatureRegensHp) && current->hp < current->max_hp;
          if (can_regen_hp) {
            current->hp += 1;
            current->changed = true;
          }
        }
      }
    }
    for (u32 i = 0; i < ROOM_MAP_COLLISIONS_LEN; i++) {
      room = &state.rooms->collisions[i];
      if (!isRoomNull(room)) {
        for (u32 j = 0; j < room->entities.length; j++) {
          Entity* current = entityPtrFromChunkList(&room->entities, j);

          bool can_deal_burn_damage = burn_tick && CheckFlag(current->features, FeatureDealsContinuousDamage);
          if (can_deal_burn_damage) {
            EntityList same_spot_list = getEntitiesAtXY(&scratch_arena, room, current->x, current->y); 
            for (u32 k = 0; k < same_spot_list.length; k++) {
              Entity* e = &same_spot_list.items[k];
              bool can_be_burned = CheckFlag(e->features, FeatureRegensHp) && e->hp > 0;
              if (can_be_burned) {
                printf("burning %lld; ", e->id);
                e = findEntityInRoom(room, e->id);
                e->hp -= 1;
                e->changed = true;
              }
            }
          }

          bool can_regen_hp = regen_hp_tick && CheckFlag(current->features, FeatureRegensHp) && current->hp < current->max_hp;
          if (can_regen_hp) {
            current->hp += 1;
            current->changed = true;
          }
        }
      }
    }

    } unlockMutex(&state.mutex); unlockMutex(&state.client_mutex);

    // 3. scratch cleanup
    arenaClear(&scratch_arena);

    // 4. loop timing
    u32 loop_duration = osTimeMicrosecondsNow() - loop_start;
    i32 remaining_time = GOAL_GAME_LOOP_US - loop_duration;
    if (remaining_time > 0) {
      osSleepMicroseconds(remaining_time);
    }
  }
  return NULL;
}

// THE SERVER
i32 main(i32 argc, ptr argv[]) {
  // multi-thread architecture:
  //  - the gameLoop() which just inifinite loops every "tick" and processes user input and updates gameworld state
  //    - TODO: actually make this N "lanes" as described https://www.rfleury.com/p/multi-core-by-default
  //      such that we can split room-work among all the various cores on our machine
  //  - one is the sendNetworkUpdates() infinite loop, which sends a UDP snapshot-or-delta update to each connected client N/sec
  //  - one is the receiveNetworkUpdates() infinte loop, which waits for new UDP messages from clients

  // 1. initialize gameworld, and spin off infinite game-loop thread
  // 2. spin off sendNetworkUpdates() infinite loop thread
  // 3. infinitely wait for incoming UDP messages and process them (usually by just dropping user-commands into the relevant block of shared memory)

  // 1. initialize gameworld, and spin off infinite game-loop thread
  arenaInit(&permanent_arena);
  arenaInit(&state.game_scratch);
  arenaInit(&state.string_arena.a);
  state.string_arena.mutex = newMutex();
  state.client_mutex = newMutex();
  state.mutex = newMutex();
  state.entities_who_left_a_room_recently = arenaAllocArray(&permanent_arena, LeftRoomEntity, LEFT_ROOM_ENTITES_LEN);
  state.network_recv_queue = newPCCThreadQueue(&permanent_arena);
  state.network_send_queue = newOutgoingMessageQueue(&permanent_arena);
  // alloc the global hashmap of rooms
  state.rooms = (RoomMap*)arenaAlloc(&permanent_arena, sizeof(RoomMap));
  // generate some starting rooms
  generateNewRoom(state.rooms, 0,0,0);
  generateNewRoom(state.rooms, 0,0,-1);
  generateNewRoom(state.rooms, 1,0,0);
  generateNewRoom(state.rooms, 0,1,0);
  generateNewRoom(state.rooms, -1,0,0);
  generateNewRoom(state.rooms, 0,-1,0);
  // init + alloc clients
  state.clients.capacity = SERVER_MAX_CLIENTS;
  state.clients.length = 1; // making entry 0 to be a "null" client 
  state.clients.items = (Client*)arenaAllocArray(&permanent_arena, Client, SERVER_MAX_CLIENTS);
  state.accounts.capacity = ACCOUNT_CHUNK_SIZE;
  state.accounts.items = arenaAllocArray(&permanent_arena, Account, ACCOUNT_CHUNK_SIZE);

  Thread game_thread = spawnThread(&gameLoop, NULL);

  // 2. spin off sendNetworkUpdates() infinite loop thread
  UDPServer listener = createUDPServer(SERVER_PORT);
  Thread send_thread = spawnThread(&sendNetworkUpdates, &listener.server_socket);

  // 3. infinitely wait for incoming UDP messages and process them (usually by just dropping user-commands into the relevant block of shared memory)
  if (!listener.ready) {
    exitWithErrorMessage("Couldn't start the udp server");
  }
  infiniteReadUDPServer(&listener, handleIncomingMessage);

  return 0;
}

