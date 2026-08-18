#ifndef PEER_H
#define PEER_H

#include "socket.h"
#include "message.h"

namespace network {
    // Owns one connected socket: closes it automatically when the Peer goes out of scope.
    // Not copyable - a socket handle has exactly one owner.
    class Peer {
    public:
        explicit Peer(socket_t sock) : sock_(sock) {}
        ~Peer() { close_socket(sock_); }

        Peer(const Peer&) = delete;
        Peer& operator=(const Peer&) = delete;

        bool Send(const std::string& cmd, const std::vector<uint8_t>& payload) const {
            return send_message(sock_, cmd, payload);
        }

        bool Receive(Message& out) const {
            return receive_message(sock_, out);
        }

    private:
        socket_t sock_;
    };
}

#endif
