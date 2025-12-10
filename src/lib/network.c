#include "thread.h"
#include "../base/types.h"
#include "../base/macros.h"
#include "../base/os.h"

// https://stackoverflow.com/questions/1098897/what-is-the-largest-safe-udp-packet-size-on-the-internet
#define UDP_MAX_MESSAGE_LEN 508
#ifndef NET_OUTGOING_MESSAGE_QUEUE_LEN
#define NET_OUTGOING_MESSAGE_QUEUE_LEN 16
#endif

typedef struct sockaddr_in SocketAddress;

typedef struct UDPServer {
  bool ready;
  SocketAddress server_address;
  i32 server_socket;
} UDPServer;

typedef struct UDPClient {
  bool ready;
  SocketAddress server_address;
  u16 client_port;
  i32 socket;
} UDPClient;

typedef struct UDPMessage {
  u16 bytes_len;
  SocketAddress address;
  u8 bytes[UDP_MAX_MESSAGE_LEN];
} UDPMessage;

typedef struct OutgoingMessageQueue {
  UDPMessage items[NET_OUTGOING_MESSAGE_QUEUE_LEN];
  u32 head;
  u32 tail;
  u32 count;
  Mutex mutex;
  Cond not_empty;
  Cond not_full;
} OutgoingMessageQueue;

fn OutgoingMessageQueue* newOutgoingMessageQueue(Arena* a) {
  OutgoingMessageQueue* result = arenaAlloc(a, sizeof(OutgoingMessageQueue));
  MemoryZero(result, (sizeof *result));
  result->mutex = newMutex();
  result->not_full = newCond();
  result->not_empty = newCond();
  return result;
}

fn void outgoingMessageQueuePush(OutgoingMessageQueue* queue, UDPMessage* msg) {
  lockMutex(&queue->mutex); {
    while (queue->count == NET_OUTGOING_MESSAGE_QUEUE_LEN) {
      waitForCondSignal(&queue->not_full, &queue->mutex);
    }

    MemoryCopy(&queue->items[queue->tail], msg, (sizeof *msg));
    queue->tail = (queue->tail + 1) % NET_OUTGOING_MESSAGE_QUEUE_LEN;
    queue->count++;

    signalCond(&queue->not_empty);
  } unlockMutex(&queue->mutex);
}

fn UDPMessage* outgoingMessageNonblockingQueuePop(OutgoingMessageQueue* q, UDPMessage* copy_target) {
  // immediately returns NULL if there's nothing in the ThreadQueue
  // copies the ParsedClientCommand into `copy_target` if there is something in the queue
  // and marks it as popped from the queue
  UDPMessage* result = NULL;

  lockMutex(&q->mutex); {
    if (q->count > 0) {
      result = &q->items[q->head];
      MemoryCopy(copy_target, result, (sizeof *copy_target));
      q->head = (q->head + 1) % NET_OUTGOING_MESSAGE_QUEUE_LEN;
      q->count--;

      signalCond(&q->not_full);
    }
  } unlockMutex(&q->mutex);

  return result;
}

fn UDPMessage* outgoingMessageQueuePop(OutgoingMessageQueue* q, UDPMessage* copy_target) {
  UDPMessage* result = NULL;

  lockMutex(&q->mutex); {
    while (q->count == 0) {
        waitForCondSignal(&q->not_empty, &q->mutex);
    }

    result = &q->items[q->head];
    MemoryCopy(copy_target, result, (sizeof *copy_target));
    q->head = (q->head + 1) % NET_OUTGOING_MESSAGE_QUEUE_LEN;
    q->count--;

    signalCond(&q->not_full);
  } unlockMutex(&q->mutex);

  return result;
}


fn bool socketAddressEqual(SocketAddress a, SocketAddress b) {
  return a.sin_addr.s_addr == b.sin_addr.s_addr
    && a.sin_port == b.sin_port;
}

// ONLY WORKS ON POSIX. taken from https://gist.github.com/miekg/a61d55a8ec6560ad6c4a2747b21e6128

// the only real difference between a udp "server" and a "client" is the bind() syscall
// that the server makes in order to specify a port/address that it's listening on
UDPServer createUDPServer(u16 server_port) {
  UDPServer result = {0};
  // define the address we'll be listening on
  result.server_address.sin_family = AF_INET;
	result.server_address.sin_addr.s_addr = inet_addr("0.0.0.0");//htonl(INADDR_ANY);
	result.server_address.sin_port = htons(server_port);

  // get a FileDescriptor number from the OS to use for our socket
  result.server_socket = socket(PF_INET, SOCK_DGRAM, 0);
  if (result.server_socket < 0) {
    return result;
  }
  // to let us immediately kill and restart server
  i32 optval = 1;
	setsockopt(result.server_socket, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(i32));

  result.ready = bind(result.server_socket, (struct sockaddr *)&result.server_address, sizeof(result.server_address)) >= 0;

  return result;
}

UDPClient createUDPClient(u16 server_port, str addr) {
  UDPClient result = {0};
  // define the address we'll be listening on
  result.server_address.sin_family = AF_INET;
  if (addr == 0) {
    result.server_address.sin_addr.s_addr = inet_addr("129.212.181.99");
  } else {
    result.server_address.sin_addr.s_addr = inet_addr(addr);
  }
	result.server_address.sin_port = htons(server_port);

  // get a FileDescriptor number from the OS to use for our socket
  result.socket = socket(PF_INET, SOCK_DGRAM, 0);
  if (result.socket < 0) {
    return result;
  }

  struct sockaddr_in client_address = {0};
  client_address.sin_family = AF_INET;
  client_address.sin_addr.s_addr = htonl(INADDR_ANY);
  client_address.sin_port = 0;
  result.ready = bind(result.socket, (struct sockaddr *)&client_address, sizeof(client_address)) >= 0;
  struct sockaddr_in empty_addr;
  socklen_t addr_len = sizeof(empty_addr);
  getsockname(result.socket, (struct sockaddr *)&empty_addr, &addr_len);
  result.client_port = ntohs(empty_addr.sin_port);
  return result;
}

void infiniteReadUDPServer(UDPServer* server, void (*handleMessage)(u8* udp_message, u32 udp_len, SocketAddress sending_address, i32 socket)) {
  u8 message_buffer[UDP_MAX_MESSAGE_LEN] = {0};
  i32 bytes_recieved = 0;
  SocketAddress client_address = {0};
  i32 addrlen = sizeof(struct sockaddr);
  while (true) {
    bytes_recieved = recvfrom(server->server_socket, message_buffer, UDP_MAX_MESSAGE_LEN, 0, (struct sockaddr *)&client_address, (socklen_t*)&addrlen);

		//gethostbyaddr: determine who sent the datagram
		//struct hostent* hostp = gethostbyaddr(
    //    (const char *)&client_address.sin_addr.s_addr,
    //    sizeof(client_address.sin_addr.s_addr),
    //    AF_INET
    //);

		//ptr printable_host_IP_address_string = inet_ntoa(client_address.sin_addr);

    handleMessage(message_buffer, bytes_recieved, client_address, server->server_socket);
    MemoryZero(message_buffer, UDP_MAX_MESSAGE_LEN);
  }
}

// TODO: sendall() to handle cases when the sendto() bytes return value is less than the intended bytes to send... stupid kernel fuckin wit us.
i32 sendUDPu8List(i32 using_socket, SocketAddress* to, u8List* message) {
  return sendto(
    using_socket,
    message->items,
    message->length,
    0,
    (const struct sockaddr *)to,
    sizeof(struct sockaddr)
  );
}

i32 sendUDPMessage(UDPServer* to, u8* message, u32 len) {
  return sendto(to->server_socket, message, len, 0, (struct sockaddr *)&to->server_address, sizeof(struct sockaddr));
}
