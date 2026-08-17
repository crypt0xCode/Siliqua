#include "core/transaction.h"
#include "crypto/ecdsa.h"
#include <algorithm>
#include <array>

using namespace crypto;
using namespace serializer;

namespace transaction {
	bool COutPoint::IsNull() const {
		return (COutPoint::n == 0xFFFFFFFF)
			&& std::all_of(COutPoint::txid.begin(), COutPoint::txid.end(), [](uint8_t b) {return b == 0; }	// lambda function
		);
	}

	std::vector<uint8_t> COutPoint::Serialize() const {
		std::vector<uint8_t> buffer;
		write_bytes(buffer, txid.data(), txid.size());
		write_uint_32LE(buffer, n);
		return buffer;
	}

	COutPoint COutPoint::Deserialize(const std::vector<uint8_t>& data, size_t& offset) {
		COutPoint out;
		out.txid = read_bytes32(data, offset);
		out.n = read_uint_32LE(data, offset);
		return out;
	}

	bool COutPoint::operator<(const COutPoint& other) const {
		if (txid != other.txid) {
			return txid < other.txid;
		}
		return n < other.n;
	}

	std::vector<uint8_t> CTxIn::Serialize() const {
		std::string prefix = "CTxIn Serialize: ";

		std::vector<uint8_t> buffer;
		std::vector<uint8_t> prevout_bytes = prevout.Serialize();
		buffer.insert(buffer.end(), prevout_bytes.begin(), prevout_bytes.end());
		Logger::instance().debug("{}Write prevout {} bytes.", prefix, prevout_bytes.size());

		// scriptSig length is always written, even when 0, so Deserialize can tell it apart
		// from the next field - skipping it for an empty script would make the buffer ambiguous.
		write_var_int32(buffer, static_cast<uint32_t>(scriptSig.size()));
		write_bytes(buffer, scriptSig.data(), scriptSig.size());
		Logger::instance().debug("{}Write scriptSig {} bytes.", prefix, scriptSig.size());

		write_uint_32LE(buffer, nSequence);
		Logger::instance().debug("{}Write nSequence 4 bytes.", prefix);
		return buffer;
	}

	CTxIn CTxIn::Deserialize(const std::vector<uint8_t>& data, size_t& offset) {
		CTxIn txin;
		txin.prevout = COutPoint::Deserialize(data, offset);
		uint32_t script_len = read_var_int32(data, offset);
		txin.scriptSig = read_bytes(data, offset, script_len);
		txin.nSequence = read_uint_32LE(data, offset);
		return txin;
	}

	std::vector<uint8_t> CTxOut::Serialize() const {
		std::string prefix = "CTxOut Serialize: ";

		std::vector<uint8_t> buffer;
		write_uint_64LE(buffer, static_cast<uint64_t>(nValue));
		Logger::instance().debug("{}Write nValue 8 bytes.", prefix);

		// Same reasoning as CTxIn::Serialize(): always write the length, even 0.
		write_var_int32(buffer, static_cast<uint32_t>(scriptPubKey.size()));
		write_bytes(buffer, scriptPubKey.data(), scriptPubKey.size());
		Logger::instance().debug("{}Write scriptPubKey {} bytes.", prefix, scriptPubKey.size());
		return buffer;
	}

	CTxOut CTxOut::Deserialize(const std::vector<uint8_t>& data, size_t& offset) {
		CTxOut txout;
		txout.nValue = static_cast<int64_t>(read_uint_64LE(data, offset));
		uint32_t script_len = read_var_int32(data, offset);
		txout.scriptPubKey = read_bytes(data, offset, script_len);
		return txout;
	}

	std::vector<uint8_t> Transaction::Serialize() const {
		std::string prefix = "Transaction Serialize: ";

		// Main buffer.
		std::vector<uint8_t> buffer;

		// nVersion (4b LE).
		write_uint_32LE(buffer, static_cast<uint32_t>(nVersion));
		Logger::instance().debug("{}Write nVersion 4 bytes.", prefix);

		// Entering count (varint).
		write_var_int32(buffer, static_cast<uint32_t>(vin.size()));
		Logger::instance().debug("{}Write vin count 4 bytes ({} entering(s)).", prefix, vin.size());

		for (const auto& txin : vin) {
			std::vector<uint8_t> txin_bytes = txin.Serialize();
			buffer.insert(buffer.end(), txin_bytes.begin(), txin_bytes.end());
		}

		// Out count (varint).
		write_var_int32(buffer, static_cast<uint32_t>(vout.size()));
		Logger::instance().debug("{}Write vout count 4 bytes ({} out(s)).", prefix, vout.size());

		for (const auto& txout : vout) {
			std::vector<uint8_t> txout_bytes = txout.Serialize();
			buffer.insert(buffer.end(), txout_bytes.begin(), txout_bytes.end());
		}

		// Lock time (nLockTime).
		write_uint_32LE(buffer, nLockTime);
		Logger::instance().debug("{}Write nLockTime 4 bytes.", prefix);

		return buffer;
	}

	Transaction Transaction::Deserialize(const std::vector<uint8_t>& data, size_t& offset) {
		Transaction tx;
		tx.nVersion = static_cast<int32_t>(read_uint_32LE(data, offset));

		uint32_t vin_count = read_var_int32(data, offset);
		tx.vin.reserve(vin_count);
		for (uint32_t i = 0; i < vin_count; ++i) {
			tx.vin.push_back(CTxIn::Deserialize(data, offset));
		}

		uint32_t vout_count = read_var_int32(data, offset);
		tx.vout.reserve(vout_count);
		for (uint32_t i = 0; i < vout_count; ++i) {
			tx.vout.push_back(CTxOut::Deserialize(data, offset));
		}

		tx.nLockTime = read_uint_32LE(data, offset);

		// tx_hash is not stored: recompute it from the decoded fields (same as after construction).
		tx.tx_hash = tx.GetHash();
		return tx;
	}

	std::array<uint8_t, 32> Transaction::GetHash() const {
		// Serialize() builds the exact same buffer GetHash() used to build inline; hashing it here
		// keeps the hash and the on-disk format guaranteed to match instead of two copies drifting apart.
		std::vector<uint8_t> buffer = Serialize();
		std::array<uint8_t, 32> result;
		hash_double_sha256(buffer.data(), buffer.size(), result);
		return result;
	}

	bool Transaction::IsCoinbase() const {
		return vin.empty() || vin.at(0).prevout.IsNull();
	}
}