#ifndef STORAGE_H
#define STORAGE_H

#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "core/block.h"
#include "core/serializer.h"
#include "core/transaction.h"
#include "logger.h"

namespace storage {
    /* @brief           Read the whole file into a byte buffer.
    *  @param   path    source file path.
    *  @return          file content.
    */
    inline std::vector<uint8_t> read_file(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            throw std::runtime_error("read_file: cannot open " + path + " for reading");
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(buffer.data()), size);
        return buffer;
    }

    /* @brief           Write a byte buffer to file, overwriting it if it already exists.
    *  @param   path    destination file path.
    *  @param   buffer  bytes to write.
    */
    inline void write_file(const std::string& path, const std::vector<uint8_t>& buffer) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("write_file: cannot open " + path + " for writing");
        }
        file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    }

    /* @brief           Save the whole chain to a binary file: block count (varint) followed by
    *                   each block's Serialize() back to back.
    *  @param   chain   blocks to persist, in order.
    *  @param   path    destination file path.
    */
    inline void save_chain(const std::vector<block::CBlock>& chain, const std::string& path) {
        std::string prefix = "save_chain: ";

        std::vector<uint8_t> buffer;
        serializer::write_var_int32(buffer, static_cast<uint32_t>(chain.size()));
        for (const auto& blk : chain) {
            std::vector<uint8_t> block_bytes = blk.Serialize();
            buffer.insert(buffer.end(), block_bytes.begin(), block_bytes.end());
        }

        write_file(path, buffer);
        Logger::instance().info("{}Saved {} block(s), {} bytes, to {}.", prefix, chain.size(), buffer.size(), path);
    }

    /* @brief           Load the whole chain from a binary file written by save_chain().
    *  @param   path    source file path.
    *  @return          blocks in the order they were saved.
    */
    inline std::vector<block::CBlock> load_chain(const std::string& path) {
        std::string prefix = "load_chain: ";

        std::vector<uint8_t> buffer = read_file(path);
        size_t offset = 0;
        uint32_t block_count = serializer::read_var_int32(buffer, offset);

        std::vector<block::CBlock> chain;
        chain.reserve(block_count);
        for (uint32_t i = 0; i < block_count; ++i) {
            chain.push_back(block::CBlock::Deserialize(buffer, offset));
        }

        Logger::instance().info("{}Loaded {} block(s) from {}.", prefix, chain.size(), path);
        return chain;
    }

    /* @brief           Save the UTXO set to a binary file: entry count (varint) followed by
    *                   each COutPoint::Serialize() + CTxOut::Serialize() pair back to back.
    *  @param   utxos   outpoint -> unspent output map.
    *  @param   path    destination file path.
    */
    inline void save_utxo_set(const std::map<transaction::COutPoint, transaction::CTxOut>& utxos, const std::string& path) {
        std::string prefix = "save_utxo_set: ";

        std::vector<uint8_t> buffer;
        serializer::write_var_int32(buffer, static_cast<uint32_t>(utxos.size()));
        for (const auto& [outpoint, txout] : utxos) {
            std::vector<uint8_t> outpoint_bytes = outpoint.Serialize();
            std::vector<uint8_t> txout_bytes = txout.Serialize();
            buffer.insert(buffer.end(), outpoint_bytes.begin(), outpoint_bytes.end());
            buffer.insert(buffer.end(), txout_bytes.begin(), txout_bytes.end());
        }

        write_file(path, buffer);
        Logger::instance().info("{}Saved {} UTXO(s), {} bytes, to {}.", prefix, utxos.size(), buffer.size(), path);
    }

    /* @brief           Load the UTXO set from a binary file written by save_utxo_set().
    *  @param   path    source file path.
    *  @return          outpoint -> unspent output map.
    */
    inline std::map<transaction::COutPoint, transaction::CTxOut> load_utxo_set(const std::string& path) {
        std::string prefix = "load_utxo_set: ";

        std::vector<uint8_t> buffer = read_file(path);
        size_t offset = 0;
        uint32_t entry_count = serializer::read_var_int32(buffer, offset);

        std::map<transaction::COutPoint, transaction::CTxOut> utxos;
        for (uint32_t i = 0; i < entry_count; ++i) {
            transaction::COutPoint outpoint = transaction::COutPoint::Deserialize(buffer, offset);
            transaction::CTxOut txout = transaction::CTxOut::Deserialize(buffer, offset);
            utxos.emplace(outpoint, txout);
        }

        Logger::instance().info("{}Loaded {} UTXO(s) from {}.", prefix, utxos.size(), path);
        return utxos;
    }

    /* @brief           Save a mempool (pending transactions) to a binary file: entry count
    *                   (varint) followed by each Transaction::Serialize() back to back.
    *  @param   mempool transactions to persist.
    *  @param   path    destination file path.
    */
    inline void save_mempool(const std::vector<transaction::Transaction>& mempool, const std::string& path) {
        std::string prefix = "save_mempool: ";

        std::vector<uint8_t> buffer;
        serializer::write_var_int32(buffer, static_cast<uint32_t>(mempool.size()));
        for (const auto& tx : mempool) {
            std::vector<uint8_t> tx_bytes = tx.Serialize();
            buffer.insert(buffer.end(), tx_bytes.begin(), tx_bytes.end());
        }

        write_file(path, buffer);
        Logger::instance().info("{}Saved {} transaction(s), {} bytes, to {}.", prefix, mempool.size(), buffer.size(), path);
    }

    /* @brief           Load a mempool from a binary file written by save_mempool().
    *  @param   path    source file path.
    *  @return          pending transactions in the order they were saved.
    */
    inline std::vector<transaction::Transaction> load_mempool(const std::string& path) {
        std::string prefix = "load_mempool: ";

        std::vector<uint8_t> buffer = read_file(path);
        size_t offset = 0;
        uint32_t count = serializer::read_var_int32(buffer, offset);

        std::vector<transaction::Transaction> mempool;
        mempool.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            mempool.push_back(transaction::Transaction::Deserialize(buffer, offset));
        }

        Logger::instance().info("{}Loaded {} transaction(s) from {}.", prefix, mempool.size(), path);
        return mempool;
    }
}

#endif
