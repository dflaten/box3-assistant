#include "net/line_socket.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lwip/netdb.h"

bool line_socket_parse_host_port(
    const char *address, const char *default_port, char *host, size_t host_size, char *port, size_t port_size) {
    if (address == NULL || host == NULL || port == NULL || host_size == 0 || port_size == 0) {
        return false;
    }

    const char *cursor = strstr(address, "://");
    cursor = cursor != NULL ? cursor + 3 : address;
    const char *host_start = cursor;
    while (*cursor != '\0' && *cursor != ':' && *cursor != '/') {
        cursor++;
    }

    size_t host_len = (size_t) (cursor - host_start);
    if (host_len == 0 || host_len >= host_size) {
        return false;
    }
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    const char *port_start = NULL;
    if (*cursor == ':') {
        port_start = ++cursor;
        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }
    }

    if (port_start == NULL || port_start == cursor) {
        if (default_port == NULL || default_port[0] == '\0') {
            return false;
        }
        snprintf(port, port_size, "%s", default_port);
        return true;
    }

    size_t port_len = (size_t) (cursor - port_start);
    if (port_len >= port_size) {
        return false;
    }
    memcpy(port, port_start, port_len);
    port[port_len] = '\0';
    return true;
}

esp_err_t line_socket_connect(const char *host, const char *port, int timeout_ms, int *out_sock) {
    if (host == NULL || port == NULL || out_sock == NULL || timeout_ms <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *result = NULL;
    int gai_err = getaddrinfo(host, port, &hints, &result);
    if (gai_err != 0 || result == NULL) {
        return ESP_FAIL;
    }

    int sock = -1;
    esp_err_t err = ESP_FAIL;
    for (struct addrinfo *addr = result; addr != NULL; addr = addr->ai_next) {
        sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (sock < 0) {
            continue;
        }

        struct timeval timeout = {
            .tv_sec = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        if (connect(sock, addr->ai_addr, addr->ai_addrlen) == 0) {
            err = ESP_OK;
            break;
        }

        close(sock);
        sock = -1;
    }

    freeaddrinfo(result);
    if (err != ESP_OK) {
        return err;
    }

    *out_sock = sock;
    return ESP_OK;
}

esp_err_t line_socket_send_all(int sock, const void *data, size_t len) {
    const uint8_t *cursor = (const uint8_t *) data;
    while (len > 0) {
        ssize_t sent = send(sock, cursor, len, 0);
        if (sent <= 0) {
            return ESP_FAIL;
        }
        cursor += sent;
        len -= (size_t) sent;
    }
    return ESP_OK;
}

esp_err_t line_socket_recv_exact(int sock, void *data, size_t len) {
    uint8_t *cursor = (uint8_t *) data;
    while (len > 0) {
        ssize_t received = recv(sock, cursor, len, 0);
        if (received <= 0) {
            return received == 0 ? ESP_ERR_INVALID_RESPONSE : ESP_ERR_TIMEOUT;
        }
        cursor += received;
        len -= (size_t) received;
    }
    return ESP_OK;
}

esp_err_t line_socket_recv_line(int sock, char *line, size_t line_size) {
    if (line == NULL || line_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = 0;
    while (len + 1 < line_size) {
        char c = '\0';
        ssize_t received = recv(sock, &c, 1, 0);
        if (received <= 0) {
            return received == 0 ? ESP_ERR_INVALID_RESPONSE : ESP_ERR_TIMEOUT;
        }
        if (c == '\n') {
            line[len] = '\0';
            return ESP_OK;
        }
        line[len++] = c;
    }

    line[line_size - 1] = '\0';
    return ESP_ERR_NO_MEM;
}
