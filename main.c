#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define LISTEN_PORT (12345)
#define LISTEN_HOST "127.0.0.1"

#define MAX_NUM_CLIENTS (64)
#define BUFFER_SIZE (2048)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

struct client {
  int fd;
  char *buffer;
  // -1 when in reading state, otherwise the offset in buffer to write.
  ssize_t offset;
  // The length left to write (only useful when in writing state).
  ssize_t length;
};

bool set_nonblock(int fd) {
  int r;

  do {
    r = fcntl(fd, F_GETFL);
  } while (r == -1 && errno == EINTR);

  if (r == -1) {
    return false;
  }

  // Bail out now if already set.
  if (r & O_NONBLOCK) {
    return true;
  }

  const int flags = r | O_NONBLOCK;

  do {
    r = fcntl(fd, F_SETFL, flags);
  } while (r == -1 && errno == EINTR);

  return r == 0 ? true : false;
}

void serve(int server_fd) {
  struct client clients[MAX_NUM_CLIENTS];
  int num_clients = 0;

  while (true) {
    if (num_clients < ARRAY_SIZE(clients)) {
      const int client_fd = accept(server_fd, NULL, NULL);
      if (client_fd != -1) {
        if (!set_nonblock(client_fd)) {
          close(client_fd);
        } else {
          clients[num_clients].buffer = malloc(BUFFER_SIZE);
          if (!clients[num_clients].buffer) {
            close(client_fd);
          }
          clients[num_clients].fd = client_fd;
          clients[num_clients].offset = -1;
          num_clients++;
        }
      }
    }

    for (int i = 0; i < num_clients; i++) {
      struct client *client = &clients[i];
      const bool is_reading = client->offset == -1;

      ssize_t result;
      if (is_reading) {
        result = read(client->fd, client->buffer, sizeof(client->buffer));
      } else {
        result =
            write(client->fd, client->buffer + client->offset, client->length);
      }

      if (result != -1) {
        if (is_reading) {
          client->offset = 0; // Move to writing state.
          client->length = result;
        } else {
          client->length -= result;

          if (client->length == 0) {
            client->offset = -1; // Move to reading state.
          } else {
            client->offset += result;
          }
        }
      } else if (errno != EWOULDBLOCK) {
        close(client->fd);
        free(client->buffer);
        num_clients--;
        memcpy(client, &clients[num_clients], sizeof(*client));
        i--; // Don't skip the moved client.
      }
    }
  }
}

int main() {
  int result = 1;

  int server = socket(AF_INET, SOCK_STREAM, 0);
  if (server == -1) {
    perror("socket");
    goto error;
  }

  struct sockaddr_in addr = {0};

  addr.sin_port = htons(12345);
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  const int enable = 1;
  if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    perror("SO_REUSEADDR");
    goto error;
  }

  if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind");
    goto error;
  }

  if (listen(server, MAX_NUM_CLIENTS) == -1) {
    perror("listen");
    goto error;
  }

  if (!set_nonblock(server)) {
    perror("set_nonblock");
    goto error;
  }

  serve(server);

  result = 0;

error:
  close(server);
  return result;
}
