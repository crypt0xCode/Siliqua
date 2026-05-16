#include "core/transaction.h"
#include "crypto/ecdsa.h"
#include <algorithm>
#include <array>

using namespace crypto;
using namespace serializer;

namespace transaction {
	bool COutPoint::IsNull() const {
		return (COutPoint::n == 0xFFFFFFFF || COutPoint::n == 0xFFFFFFFF)
			&& std::all_of(COutPoint::txid.begin(), COutPoint::txid.end(), [](uint8_t b) {return b == 0; }	// lambda function
		);
	}
	
	std::array<uint8_t, 32> Transaction::GetHash() const {
		/*
		 * nVersion: 4 
		 * vin: 4
		 * 
		 * for each vin: 
		 *** txid: 32
		 *** prevout.n: 4
		 *** scriptSig length: 4
		 *** scriptSig: 5
		 *** nSequence: 4
		 * 
		 * vout: 4
		 * for each vout: 
		 *** nValue: 8
		 *** scriptPubKey length: 4
		 *** scriptPubKey: 3
		 *** nLockTime: 4
		 * 
		 * total: 80 bytes in buffer
		 */
		std::string prefix = "GetHash: ";

		// Main buffer.
		std::vector<uint8_t> buffer;

		// nVersion (4b LE).
		write_uint_32LE(buffer, static_cast<uint32_t>(nVersion));
		Logger::instance().debug("{}write nVersion 4 bytes.", prefix);

		// Entering count (varint).
		write_var_int32(buffer, static_cast<uint32_t>(vin.size()));
		Logger::instance().debug("{}write vin 4 bytes.", prefix);

		// Each entering vin:
		//	prevout.n
		//	scriptSig len (varint)
		//	scriptSig
		//	nSeq
		for (const auto& txin : vin) {
			write_bytes(buffer, txin.prevout.txid.data(), txin.prevout.txid.size());
			Logger::instance().debug("{}write txid 32 bytes.", prefix);
			write_uint_32LE(buffer, txin.prevout.n);
			Logger::instance().debug("{}write prevout.n 4 bytes.", prefix);
			if (!txin.scriptSig.empty()) {
				write_var_int32(buffer, static_cast<uint32_t>(txin.scriptSig.size()));
				Logger::instance().debug("{}write scriptSig length 4 bytes", prefix);
				write_bytes(buffer, txin.scriptSig.data(), txin.scriptSig.size());
				Logger::instance().debug("{}write scriptSig 5 bytes.", prefix);
			}
			else
				Logger::instance().debug("{}scriptSig is empty.", prefix);

			write_uint_32LE(buffer, txin.nSequence);
			Logger::instance().debug("{}write nSequence 4 bytes.", prefix);
		}

		// Out count (varint).
		write_var_int32(buffer, static_cast<uint32_t>(vout.size()));
		Logger::instance().debug("\n{}write vout 4 bytes.\n", prefix);

		// Each entering vout:
		//	nValue
		//	scriptPubKey len (varint)
		//	scriptPubKey
		for (const auto& txout : vout) {
			write_uint_64LE(buffer, static_cast<uint64_t>(txout.nValue));
			Logger::instance().debug("{}write nValue 8 bytes.", prefix);
			if (!txout.scriptPubKey.empty()) {
				write_var_int32(buffer, static_cast<uint32_t>(txout.scriptPubKey.size()));
				Logger::instance().debug("{}write scriptPubKey length 4 bytes.", prefix);
				write_bytes(buffer, txout.scriptPubKey.data(), txout.scriptPubKey.size());
				Logger::instance().debug("{}write scriptPubKey 3 bytes.", prefix);
			}
			else
				Logger::instance().debug("{}scriptPubKey is empty.", prefix);
		}

		// Lock time (nLockTime).
		write_uint_32LE(buffer, nLockTime);
		Logger::instance().debug("\n{}write nLockTime 4 bytes.\n", prefix);

		// Get double SHA-256 hash for filled buffer.
		std::array<uint8_t, 32> result;
		hash_double_sha256(buffer.data(), buffer.size(), result);
		return result;
	}

	bool Transaction::IsCoinbase() const {
		return vin.empty() || vin.at(0).prevout.IsNull();
	}
}