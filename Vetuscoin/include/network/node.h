#ifndef NODE_H
#define NODE_H

#include "socket.h"
#include "message.h"
#include "peer.h"
#include "core/block.h"
#include "consensus/pow.h"
#include "logger.h"
#include "utils.h"

namespace network {
    /* @brief                       Listen on a port, accept exactly one peer, complete the
    *                               VERSION/VERACK handshake, then announce block_to_announce
    *                               (INV) and hand it over once the peer asks for it (GETDATA).
    *  @param   port                TCP port to listen on.
    *  @param   block_to_announce   block to advertise and serve.
    */
    inline void run_listener(uint16_t port, const block::CBlock& block_to_announce) {
        std::string prefix = "run_listener: ";

        socket_t listener = create_listener(port);
        socket_t client_sock = accept_connection(listener);
        Peer peer(client_sock);

        Message msg;
        if (!peer.Receive(msg) || msg.command != command::VERSION) {
            Logger::instance().error("{}Expected VERSION, got '{}'. Dropping connection.", prefix, msg.command);
            close_socket(listener);
            return;
        }
        Logger::instance().info("{}Received VERSION.", prefix);
        peer.Send(command::VERSION, build_version_payload());

        if (!peer.Receive(msg) || msg.command != command::VERACK) {
            Logger::instance().error("{}Expected VERACK, got '{}'. Dropping connection.", prefix, msg.command);
            close_socket(listener);
            return;
        }
        peer.Send(command::VERACK, {});
        Logger::instance().info("{}Handshake complete.", prefix);

        std::array<uint8_t, 32> block_hash = block_to_announce.GetHash();
        peer.Send(command::INV, std::vector<uint8_t>(block_hash.begin(), block_hash.end()));
        Logger::instance().info("{}Sent INV for block {}.", prefix, bytes_to_hex(block_hash.data(), block_hash.size()));

        if (!peer.Receive(msg) || msg.command != command::GETDATA) {
            Logger::instance().error("{}Expected GETDATA, got '{}'. Dropping connection.", prefix, msg.command);
            close_socket(listener);
            return;
        }

        std::vector<uint8_t> block_bytes = block_to_announce.Serialize();
        peer.Send(command::BLOCK, block_bytes);
        Logger::instance().info("{}Sent BLOCK ({} byte(s)).", prefix, block_bytes.size());

        close_socket(listener);
    }

    /* @brief           Connect to a peer, complete the handshake, then request and validate
    *                   whatever block it announces via INV/GETDATA/BLOCK.
    *  @param   host    peer IPv4 address.
    *  @param   port    peer TCP port.
    */
    inline void run_connector(const std::string& host, uint16_t port) {
        std::string prefix = "run_connector: ";

        socket_t sock = connect_to(host, port);
        Peer peer(sock);

        peer.Send(command::VERSION, build_version_payload());

        Message msg;
        if (!peer.Receive(msg) || msg.command != command::VERSION) {
            Logger::instance().error("{}Expected VERSION, got '{}'. Dropping connection.", prefix, msg.command);
            return;
        }
        Logger::instance().info("{}Received VERSION.", prefix);
        peer.Send(command::VERACK, {});

        if (!peer.Receive(msg) || msg.command != command::VERACK) {
            Logger::instance().error("{}Expected VERACK, got '{}'. Dropping connection.", prefix, msg.command);
            return;
        }
        Logger::instance().info("{}Handshake complete.", prefix);

        if (!peer.Receive(msg) || msg.command != command::INV) {
            Logger::instance().error("{}Expected INV, got '{}'. Dropping connection.", prefix, msg.command);
            return;
        }
        Logger::instance().info("{}Received INV for block {}.", prefix, bytes_to_hex(msg.payload.data(), msg.payload.size()));

        peer.Send(command::GETDATA, msg.payload); // echo the announced hash back as the request

        if (!peer.Receive(msg) || msg.command != command::BLOCK) {
            Logger::instance().error("{}Expected BLOCK, got '{}'. Dropping connection.", prefix, msg.command);
            return;
        }

        size_t offset = 0;
        block::CBlock received_block = block::CBlock::Deserialize(msg.payload, offset);
        std::array<uint8_t, 32> received_hash = received_block.GetHash();

        Logger::instance().info("{}Received BLOCK. Hash: {}. Is Merkle Root valid: {}. Is Proof of Work valid: {}. Is valid: {}.",
            prefix, bytes_to_hex(received_hash.data(), received_hash.size()),
            received_block.IsMerkleRootValid(), proof_of_work::check_proof_of_work(received_block), received_block.IsValid());
    }
}

#endif
