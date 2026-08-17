#ifndef SOCKET_H
#define SOCKET_H

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#else
#error "Couldn't identify the OS"
#endif

#include <cstdint>
#include <stdexcept>
#include <string>
#include "logger.h"

namespace network {
#if defined(_WIN32)
    using socket_t = SOCKET;
    using socklen_type = int;
    constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
    using socket_t = int;
    using socklen_type = socklen_t;
    constexpr socket_t INVALID_SOCK = -1;
#endif

    // One-time WinSock init/cleanup (WSAStartup/WSACleanup). No-op on POSIX - a socket() call
    // there needs no library init. Singleton, same pattern as Logger::instance().
    class WinsockGuard {
    public:
        static WinsockGuard& instance() {
            static WinsockGuard guard;
            return guard;
        }

        WinsockGuard(const WinsockGuard&) = delete;
        WinsockGuard& operator=(const WinsockGuard&) = delete;

    private:
        WinsockGuard() {
#if defined(_WIN32)
            WSADATA wsa_data;
            int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
            if (result != 0) {
                Logger::instance().error("WinsockGuard: WSAStartup failed with code {}.", result);
                throw std::runtime_error("WinsockGuard: WSAStartup failed");
            }
#endif
        }

        ~WinsockGuard() {
#if defined(_WIN32)
            WSACleanup();
#endif
        }
    };

    inline void close_socket(socket_t sock) {
#if defined(_WIN32)
        closesocket(sock);
#else
        close(sock);
#endif
    }

    /* @brief           Create a listening TCP socket bound to port on all interfaces.
    *  @param   port    TCP port to listen on.
    *  @return          listening socket.
    */
    inline socket_t create_listener(uint16_t port) {
        WinsockGuard::instance();
        std::string prefix = "create_listener: ";

        socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCK) {
            Logger::instance().error("{}socket() failed.", prefix);
            throw std::runtime_error("create_listener: socket() failed");
        }

        int reuse = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            Logger::instance().error("{}bind() failed on port {}.", prefix, port);
            close_socket(sock);
            throw std::runtime_error("create_listener: bind() failed");
        }

        if (listen(sock, SOMAXCONN) != 0) {
            Logger::instance().error("{}listen() failed.", prefix);
            close_socket(sock);
            throw std::runtime_error("create_listener: listen() failed");
        }

        Logger::instance().info("{}Listening on port {}.", prefix, port);
        return sock;
    }

    /* @brief               Block until a peer connects, then return the accepted socket.
    *  @param   listener    listening socket returned by create_listener().
    *  @return              connected socket for the accepted peer.
    */
    inline socket_t accept_connection(socket_t listener) {
        sockaddr_in peer_addr{};
        socklen_type addr_len = sizeof(peer_addr);

        socket_t sock = accept(listener, reinterpret_cast<sockaddr*>(&peer_addr), &addr_len);
        if (sock == INVALID_SOCK) {
            Logger::instance().error("accept_connection: accept() failed.");
            throw std::runtime_error("accept_connection: accept() failed");
        }

        char ip_str[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &peer_addr.sin_addr, ip_str, sizeof(ip_str));
        Logger::instance().info("accept_connection: Accepted connection from {}:{}.", ip_str, ntohs(peer_addr.sin_port));
        return sock;
    }

    /* @brief           Open a TCP connection to host:port.
    *  @param   host    peer IPv4 address (dotted form, e.g. "127.0.0.1").
    *  @param   port    peer TCP port.
    *  @return          connected socket.
    */
    inline socket_t connect_to(const std::string& host, uint16_t port) {
        WinsockGuard::instance();
        std::string prefix = "connect_to: ";

        socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCK) {
            Logger::instance().error("{}socket() failed.", prefix);
            throw std::runtime_error("connect_to: socket() failed");
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            Logger::instance().error("{}invalid IPv4 address '{}'.", prefix, host);
            close_socket(sock);
            throw std::runtime_error("connect_to: invalid address");
        }

        if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            Logger::instance().error("{}connect() to {}:{} failed.", prefix, host, port);
            close_socket(sock);
            throw std::runtime_error("connect_to: connect() failed");
        }

        Logger::instance().info("{}Connected to {}:{}.", prefix, host, port);
        return sock;
    }

    /* @brief   Send exactly len bytes, looping over partial sends.
    *  @return  true on success, false if the peer dropped the connection or an error occurred.
    */
    inline bool send_all(socket_t sock, const uint8_t* data, size_t len) {
        size_t sent_total = 0;
        while (sent_total < len) {
#if defined(_WIN32)
            int chunk = send(sock, reinterpret_cast<const char*>(data + sent_total), static_cast<int>(len - sent_total), 0);
#else
            ssize_t chunk = send(sock, data + sent_total, len - sent_total, 0);
#endif
            if (chunk <= 0) {
                return false;
            }
            sent_total += static_cast<size_t>(chunk);
        }
        return true;
    }

    /* @brief   Receive exactly len bytes, looping over partial reads.
    *  @return  true on success, false if the peer dropped the connection or an error occurred.
    */
    inline bool recv_exact(socket_t sock, uint8_t* out, size_t len) {
        size_t received_total = 0;
        while (received_total < len) {
#if defined(_WIN32)
            int chunk = recv(sock, reinterpret_cast<char*>(out + received_total), static_cast<int>(len - received_total), 0);
#else
            ssize_t chunk = recv(sock, out + received_total, len - received_total, 0);
#endif
            if (chunk <= 0) {
                return false;
            }
            received_total += static_cast<size_t>(chunk);
        }
        return true;
    }
}

#endif
