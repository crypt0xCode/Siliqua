#ifndef MESSAGE_H
#define MESSAGE_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "socket.h"
#include "core/serializer.h"
#include "crypto/ecdsa.h"
#include "logger.h"

namespace network {
    // Own magic bytes (not Bitcoin's) - so this can never be mistaken for real Bitcoin P2P traffic.
    inline constexpr std::array<uint8_t, 4> MAGIC_BYTES = { 'V', 'E', 'T', 'U' };
    inline constexpr size_t COMMAND_SIZE = 12;
    inline constexpr size_t HEADER_SIZE = 4 /* magic */ + COMMAND_SIZE + 4 /* length */ + 4 /* checksum */;
    inline constexpr uint32_t MAX_PAYLOAD_SIZE = 4 * 1024 * 1024; // guard against a bad/hostile length field
    inline constexpr uint32_t PROTOCOL_VERSION = 1;

    namespace command {
        inline const std::string VERSION = "version";
        inline const std::string VERACK = "verack";
        inline const std::string INV = "inv";
        inline const std::string GETDATA = "getdata";
        inline const std::string BLOCK = "block";
    }

    struct Message {
        std::string command;
        std::vector<uint8_t> payload;
    };

    /* @brief   Build the VERSION message payload (just our protocol version, for simplify -
    *           real Bitcoin also carries services/height/user agent here).
    *  @return  serialized payload.
    */
    inline std::vector<uint8_t> build_version_payload() {
        std::vector<uint8_t> payload;
        serializer::write_uint_32LE(payload, PROTOCOL_VERSION);
        return payload;
    }

    /* @brief           Frame and send one message: magic + command + length + checksum + payload.
    *  @param   sock    connected socket.
    *  @param   cmd     command name (must fit in COMMAND_SIZE - 1 bytes).
    *  @param   payload message body, may be empty (e.g. VERACK).
    *  @return          true on success, false if the send failed (peer dropped the connection).
    */
    inline bool send_message(socket_t sock, const std::string& cmd, const std::vector<uint8_t>& payload) {
        std::string prefix = "send_message: ";

        std::vector<uint8_t> buffer;
        serializer::write_bytes(buffer, MAGIC_BYTES.data(), MAGIC_BYTES.size());

        // Zero-padded ascii command name, fixed COMMAND_SIZE bytes.
        std::array<uint8_t, COMMAND_SIZE> cmd_field{};
        std::memcpy(cmd_field.data(), cmd.data(), std::min(cmd.size(), COMMAND_SIZE));
        serializer::write_bytes(buffer, cmd_field.data(), cmd_field.size());

        serializer::write_uint_32LE(buffer, static_cast<uint32_t>(payload.size()));

        // Checksum: first 4 bytes of double-sha256(payload), same idea as Bitcoin's message header.
        std::array<uint8_t, 32> hash{};
        crypto::hash_double_sha256(payload.data(), payload.size(), hash);
        serializer::write_bytes(buffer, hash.data(), 4);

        buffer.insert(buffer.end(), payload.begin(), payload.end());

        if (!send_all(sock, buffer.data(), buffer.size())) {
            Logger::instance().error("{}Failed to send '{}'.", prefix, cmd);
            return false;
        }
        Logger::instance().debug("{}Sent '{}' ({} byte(s) payload).", prefix, cmd, payload.size());
        return true;
    }

    /* @brief           Receive and validate one message (magic, length bound, checksum).
    *  @param   sock    connected socket.
    *  @param   out     decoded command + payload on success.
    *  @return          true on success, false on disconnect or a malformed/failed message
    *                    (bad magic, oversized length, checksum mismatch).
    */
    inline bool receive_message(socket_t sock, Message& out) {
        std::string prefix = "receive_message: ";

        std::vector<uint8_t> header(HEADER_SIZE);
        if (!recv_exact(sock, header.data(), HEADER_SIZE)) {
            return false;
        }

        if (!std::equal(header.begin(), header.begin() + MAGIC_BYTES.size(), MAGIC_BYTES.begin())) {
            Logger::instance().error("{}Bad magic bytes, dropping connection.", prefix);
            return false;
        }

        size_t offset = MAGIC_BYTES.size();
        std::vector<uint8_t> cmd_bytes = serializer::read_bytes(header, offset, COMMAND_SIZE);
        std::string raw_cmd(cmd_bytes.begin(), cmd_bytes.end());
        out.command = raw_cmd.substr(0, raw_cmd.find('\0')); // trim the zero padding

        uint32_t length = serializer::read_uint_32LE(header, offset);
        if (length > MAX_PAYLOAD_SIZE) {
            Logger::instance().error("{}Payload length {} exceeds max {}, dropping connection.", prefix, length, MAX_PAYLOAD_SIZE);
            return false;
        }

        std::array<uint8_t, 4> checksum{};
        std::memcpy(checksum.data(), header.data() + offset, 4);

        out.payload.resize(length);
        if (length > 0 && !recv_exact(sock, out.payload.data(), length)) {
            return false;
        }

        std::array<uint8_t, 32> hash{};
        crypto::hash_double_sha256(out.payload.data(), out.payload.size(), hash);
        if (!std::equal(checksum.begin(), checksum.end(), hash.begin())) {
            Logger::instance().error("{}Checksum mismatch for '{}', dropping connection.", prefix, out.command);
            return false;
        }

        Logger::instance().debug("{}Received '{}' ({} byte(s) payload).", prefix, out.command, out.payload.size());
        return true;
    }
}

#endif
