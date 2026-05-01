#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/**
 * @brief Parse a host[:port] or scheme://host[:port] address into host and port buffers.
 * @param address Configured address string.
 * @param default_port Port to use when the address omits one. Pass NULL to require an explicit port.
 * @param host Destination buffer for the parsed host.
 * @param host_size Size of the host destination buffer in bytes.
 * @param port Destination buffer for the parsed port.
 * @param port_size Size of the port destination buffer in bytes.
 * @return True when parsing succeeds, otherwise false.
 */
bool line_socket_parse_host_port(
    const char *address, const char *default_port, char *host, size_t host_size, char *port, size_t port_size);

/**
 * @brief Open a TCP socket connection with configured send/receive timeouts.
 * @param host Hostname or IP address to connect to.
 * @param port Service port string.
 * @param timeout_ms Socket send and receive timeout in milliseconds.
 * @param out_sock Output socket descriptor on success.
 * @return ESP_OK on success, or an ESP error code if DNS, socket creation, or connect fails.
 */
esp_err_t line_socket_connect(const char *host, const char *port, int timeout_ms, int *out_sock);

/**
 * @brief Send an entire buffer over a connected socket.
 * @param sock Connected socket descriptor.
 * @param data Pointer to bytes to send.
 * @param len Number of bytes to send.
 * @return ESP_OK on success, or ESP_FAIL when send fails.
 */
esp_err_t line_socket_send_all(int sock, const void *data, size_t len);

/**
 * @brief Receive an exact number of bytes from a connected socket.
 * @param sock Connected socket descriptor.
 * @param data Destination buffer.
 * @param len Number of bytes to read.
 * @return ESP_OK on success, ESP_ERR_INVALID_RESPONSE on EOF, or ESP_ERR_TIMEOUT on receive failure.
 */
esp_err_t line_socket_recv_exact(int sock, void *data, size_t len);

/**
 * @brief Receive a newline-terminated text line from a connected socket.
 * @param sock Connected socket descriptor.
 * @param line Destination buffer for the received line.
 * @param line_size Size of the destination buffer in bytes.
 * @return ESP_OK on success, ESP_ERR_NO_MEM when the line is too long, or another ESP error code.
 */
esp_err_t line_socket_recv_line(int sock, char *line, size_t line_size);
