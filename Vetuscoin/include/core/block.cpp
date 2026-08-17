#include "core/block.h"
#include "crypto/ecdsa.h"
#include <array>
#include <algorithm>

using namespace crypto;
using namespace serializer;

namespace block {
	std::vector<uint8_t> CBlockHeader::Serialize() const {
		/*
		 *** all as little-endian
		 *
		 * nVersion: 4
		 * hashPrevBlock: 32
		 * hashMerkleRoot: 32
		 * nTime: 4
		 * nBits: 4
		 * nNonce: 4
		 *
		 * total: 80 bytes in buffer
		 */
		std::string prefix = "CBlockHeader Serialize: ";

		// Main buffer.
		std::vector<uint8_t> buffer;

		// nVersion.
		write_uint_32LE(buffer, static_cast<uint32_t>(nVersion));
		Logger::instance().debug("{}Write nVersion 4 bytes.", prefix);

		// hashPrevBlock.
		write_bytes(buffer, hashPrevBlock.data(), hashPrevBlock.size());
		Logger::instance().debug("{}Write hashPrevBlock 32 bytes.", prefix);

		// hashMerkleRoot.
		write_bytes(buffer, hashMerkleRoot.data(), hashMerkleRoot.size());
		Logger::instance().debug("{}Write hashMerkleRoot 32 bytes.", prefix);

		// nTime.
		write_uint_32LE(buffer, static_cast<uint32_t>(nTime));
		Logger::instance().debug("{}Write nTime 4 bytes.", prefix);

		// nBits.
		write_uint_32LE(buffer, static_cast<uint32_t>(nBits));
		Logger::instance().debug("{}Write nBits 4 bytes.", prefix);

		// nNonce.
		write_uint_32LE(buffer, static_cast<uint32_t>(nNonce));
		Logger::instance().debug("{}Write nNonce 4 bytes.", prefix);

		return buffer;
	}

	CBlockHeader CBlockHeader::Deserialize(const std::vector<uint8_t>& data, size_t& offset) {
		CBlockHeader header{};
		header.nVersion = static_cast<int32_t>(read_uint_32LE(data, offset));
		header.hashPrevBlock = read_bytes32(data, offset);
		header.hashMerkleRoot = read_bytes32(data, offset);
		header.nTime = read_uint_32LE(data, offset);
		header.nBits = read_uint_32LE(data, offset);
		header.nNonce = read_uint_32LE(data, offset);
		return header;
	}

	std::array<uint8_t, 32> CBlockHeader::GetHash() const {
		// Serialize() builds the exact 80-byte buffer GetHash() used to build inline; hashing it
		// here keeps the hash and the on-disk header format guaranteed to match.
		std::vector<uint8_t> buffer = Serialize();
		std::array<uint8_t, 32> result;
		hash_double_sha256(buffer.data(), buffer.size(), result);
		return result;
	}

	std::array<uint8_t, 32> CBlock::BuildMerkleRoot() const {
		std::string prefix = "BuildMerkleRoot: ";

		// Merkle root for all txs in block.
		// Get all tx hashes to the vector through lambda function.
		std::vector<std::array<uint8_t, 32>> layers(vtx.size());
		std::transform(
			vtx.begin(), vtx.end(),
			std::begin(layers),
			[](transaction::Transaction tx) {
				return tx.tx_hash;
			}
		);
		Logger::instance().debug("{}Get txs from block (layers). Layers count: {}.", prefix, layers.size());

		while (layers.size() > 1) {
			std::vector<std::array<uint8_t, 32>> next_layer;
			for (size_t i = 0; i < layers.size(); i += 2) {
				std::array<uint8_t, 32> left = layers[i];
				std::array<uint8_t, 32> right = (i + 1 < layers.size()) ? layers[i + 1] : left;
				// Bytes concat via left + right hashes.
				std::array<uint8_t, 64> concat;
				std::copy(left.begin(), left.end(), concat.begin());
				std::copy(right.begin(), right.end(), concat.begin() + 32);
				std::array<uint8_t, 32> tmp;
				hash_double_sha256(concat.data(), concat.size(), tmp);
				next_layer.push_back(tmp);
				Logger::instance().debug("{}Concat tx_hashes: {} | {}.",
					prefix, bytes_to_hex(left.data(), left.size()), bytes_to_hex(right.data(), right.size()));
			}
			layers = std::move(next_layer);
		}
		
		return layers.empty() ? std::array<uint8_t, 32>{}  : layers[0];
	}

	bool CBlock::IsMerkleRootValid() const {
		return BuildMerkleRoot() == hashMerkleRoot;
	}

	bool CBlock::IsValid() const {
		return IsMerkleRootValid();
	}

	std::vector<uint8_t> CBlock::Serialize() const {
		std::string prefix = "CBlock Serialize: ";

		std::vector<uint8_t> buffer = CBlockHeader::Serialize();

		write_var_int32(buffer, static_cast<uint32_t>(vtx.size()));
		Logger::instance().debug("{}Write vtx count 4 bytes ({} transaction(s)).", prefix, vtx.size());

		for (const auto& tx : vtx) {
			std::vector<uint8_t> tx_bytes = tx.Serialize();
			buffer.insert(buffer.end(), tx_bytes.begin(), tx_bytes.end());
		}

		return buffer;
	}

	CBlock CBlock::Deserialize(const std::vector<uint8_t>& data, size_t& offset) {
		CBlock blk;
		static_cast<CBlockHeader&>(blk) = CBlockHeader::Deserialize(data, offset);

		uint32_t tx_count = read_var_int32(data, offset);
		blk.vtx.reserve(tx_count);
		for (uint32_t i = 0; i < tx_count; ++i) {
			blk.vtx.push_back(transaction::Transaction::Deserialize(data, offset));
		}

		return blk;
	}
}