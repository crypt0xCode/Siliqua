# 📘 Siliqua Blockchain Platform — Technical Documentation

*Русская версия: [ARCHITECTURE.ru.md](ARCHITECTURE.ru.md) · interactive HTML diagram: [architecture.en.html](architecture.en.html)*

## Checklist of implemented features

### Cryptography and addresses
✅ Keypair generation (secp256k1): seckey/pubkey/compressed pubkey
✅ ECDSA signing and signature verification
✅ SHA-256 / double SHA-256 / RIPEMD-160 hashing
✅ Address derivation from a pubkey (RIPEMD160(SHA256(pubkey)))
✅ Base58Check address encoding (own version byte 0x1E)

### Chain core (core)
✅ Transaction structures: COutPoint, CTxIn, CTxOut, Transaction
✅ Double SHA-256 hash of transactions and blocks (GetHash)
✅ Binary serialization/deserialization of transactions and blocks
✅ Merkle Root and its verification (IsMerkleRootValid)
✅ UTXO model (unordered_map<COutPoint, CTxOut> for O(1) lookups)

### Consensus and economics
✅ Proof-of-Work: compact target (nBits) ↔ 256-bit target
✅ Block mining (brute-forcing nNonce, rolling nTime on overflow)
✅ Difficulty retargeting (same rule as Bitcoin, clamped to [0.25x, 4x])
✅ Total chain work calculation for fork selection
✅ Full from-scratch chain validation (validate_full_chain)
✅ Fees as the difference between input and output sums
✅ Block reward halving (same idea as Bitcoin's 210,000 blocks, on a smaller scale)

### Scripts and wallet
✅ P2PKH scriptPubKey / scriptSig (OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG)
✅ Stack machine for script execution (script::evaluate)
✅ Wallet class: key storage, address, building and signing transactions
✅ UTXO selection (greedy) with change output and a dust threshold

### P2P network protocol
✅ Own binary protocol over TCP (magic bytes "SILQ")
✅ Message framing: magic + command + length + checksum
✅ Commands: VERSION, VERACK, INV, GETDATA, BLOCK, TX, GETBLOCKS, CHAIN
✅ Peer class (RAII socket wrapper)
✅ Cross-platform sockets (Winsock/POSIX) behind one interface

### Node and daemon
✅ One-shot CLI operations: run_listener, run_connector, run_send_tx, run_receive_tx
✅ Persistent multi-threaded daemon (network::Node): AcceptLoop, MiningLoop, PeerPollLoop
✅ Size-capped mempool (MAX_MEMPOOL_SIZE)
✅ Accepting and validating peer blocks, extending the chain (try_extend_chain)
✅ Fork resolution by total work (try_reorg), not chain length
✅ Multi-peer daemon mode (list of known peers, periodic sync)

### Storage
✅ Binary (de)serialization of the whole chain to a file
✅ UTXO set persistence to a file
✅ Mempool persistence to a file
✅ Wallet (seckey) persistence to a file, restored across runs

### CLI
✅ `--seed`, `--listen`, `--connect`, `--address`, `--balance`, `--send`, `--receive-tx`, `--daemon`

### Build
✅ Cross-platform CMake (Windows/Linux/macOS), C++23
✅ secp256k1 and Crypto++ discovery via pkg-config / vcpkg-style libs
✅ Optimized for weak/old hardware (-O2 instead of -O3)

---

## Chapter 1: Introduction

### Project name
Siliqua is a peer-to-peer (P2P) blockchain platform prototype in the spirit of Bitcoin, written in plain C++ with no external blockchain frameworks.

### Project goal
Reproduce Bitcoin's key architectural and algorithmic patterns (UTXO, Proof-of-Work, the P2P protocol, scripts, halving, fees) in a compact, readable codebase optimized to run on weak/old hardware.

### Technology stack

| Component               | Technology                                                          |
| ------------------------- | ---------------------------------------------------------------------- |
| Language / standard       | C++23                                                                   |
| Cryptography (ECDSA)      | libsecp256k1                                                           |
| Hashing                   | picosha2 (SHA-256), Crypto++ (RIPEMD-160)                              |
| Networking                | raw TCP sockets (POSIX / Winsock), a custom binary protocol            |
| Logging                   | spdlog                                                                  |
| Build                     | CMake ≥ 3.15, cross-platform (Windows / Linux / macOS)                 |
| Storage                   | flat binary files (chain.dat, *.wallet, *.mempool)                     |

### Core platform features
- Wallet generation (secp256k1 keypair) and Base58Check addresses
- Sending and receiving signed transactions with a fee
- Mining blocks with Proof-of-Work and a block reward (with halving)
- Validating incoming blocks and transactions (signatures, UTXOs, sums)
- Fork resolution by total chain work (not length)
- A persistently running node (daemon) syncing with multiple peers
- Persistence of the chain, UTXO set, mempool and wallet across runs

### Development milestones
Based on the repository's commit history:

| Milestone                                    | Key results |
| ------------------------------------------------ | -------------- |
| Project bootstrap, cross-platform CMake         | Windows/Linux/macOS build |
| Cryptography layer                              | ECDSA (seckey/pubkey/sign/verify), address generation |
| Core: transactions and blocks                   | COutPoint/CTxIn/CTxOut, CBlockHeader/CBlock, Merkle Root |
| Consensus: Proof-of-Work                        | compact target ↔ 256-bit, mining, PoW check |
| Persistence                                     | chain and UTXO (de)serialization to files |
| P2P protocol                                    | messages, VERSION/VERACK handshake, sockets |
| First block exchange between nodes              | Listener/Connector, test transaction generator |
| Wallets and signing                             | Wallet class, unique wallet generator, per-address UTXOs |
| Network economics                               | fees, halving, mempool limit |
| Real Bitcoin scripts                            | scriptPubKey/scriptSig, P2PKH stack machine |
| Multi-peer daemon                               | multi-threaded Node (Accept/Mining/PeerPoll), Base58 addresses |

---

## Chapter 2: Architecture diagram

A Bitcoin-style blockchain prototype: a custom block and transaction format, Proof-of-Work with difficulty retargeting, a UTXO model with P2PKH scripts, secp256k1 signatures, and a binary P2P protocol over raw TCP sockets — no external node, no blockchain framework.

### System components
- **CLI (`Siliqua.cpp`):** parses launch flags, dispatches to a one-shot operation or the daemon.
- **`network::Node` (daemon, `daemon.h`):** three threads — accepting peers, mining on a timer, polling known peers, one shared chain under `std::mutex`.
- **Consensus (`consensus/pow.h`):** compact target, mining, difficulty retargeting, total chain work.
- **Core (`core/block.h`, `core/transaction.h`):** block and transaction structures, Merkle Root, the UTXO set.
- **Wallet (`wallet/*.h`):** keys, addresses (Base58Check), building and signing transactions, P2PKH scripts.
- **Crypto (`crypto/ecdsa.h`):** a wrapper over secp256k1 and the hash functions.
- **Network (`network/*.h`):** message framing, TCP sockets, the `Peer` class.
- **Storage (`storage/storage.h`):** binary persistence of the chain/UTXO set/mempool/wallet to files.

### Dependency graph and control-flow threads

```mermaid
flowchart TB
  CLI["Siliqua.cpp<br/>--seed --listen --connect<br/>--send --receive-tx --daemon<br/>--address --balance"]
  CLI --> NODE

  subgraph NODE["network::Node — daemon.h"]
    direction LR
    ACC["AcceptLoop<br/>accepts peers"]
    MINE["MiningLoop<br/>every 5s"]
    POLL["PeerPollLoop<br/>syncs known peers"]
  end

  NODE --> CONS & WAL & NET & STORE

  subgraph CONS["Consensus &amp; chain core"]
    direction TB
    POW["consensus::pow<br/>target · mining · retarget"]
    BLK["core::block<br/>CBlockHeader / CBlock / Merkle"]
    TX["core::transaction<br/>CTxIn / CTxOut / UtxoSet"]
    POW --> BLK --> TX
  end

  subgraph WAL["Wallet &amp; signing"]
    direction TB
    W["wallet::Wallet<br/>keypair · address · CreateTransaction"]
    SCR["wallet::script<br/>P2PKH stack machine"]
    B58["wallet::base58<br/>Base58Check"]
    ECD["crypto::ecdsa<br/>secp256k1 · SHA-256 · RIPEMD-160"]
    W --> SCR --> ECD
    W --> B58
  end

  subgraph NET["Network protocol"]
    direction TB
    MSG["message.h<br/>SILQ + command + length + checksum"]
    PEER["Peer — RAII socket wrapper"]
    SOCK["socket.h — TCP"]
    MSG --> PEER --> SOCK
  end

  subgraph STORE["storage.h — persistence"]
    direction LR
    CH[("chain.dat")]
    WF[("*.wallet")]
    MP[("*.mempool")]
  end

  TX -->|UTXO lookup on inputs| STORE
  SCR -->|scriptSig + scriptPubKey| TX
```

**Legend:** nodes are modules/classes in the codebase; solid arrows are direct calls/dependencies; the dashed `-->|label|` arrow is a cross-layer check/lookup; the cylinder shape is a file on disk.

### Modules by layer

| File                             | Layer                     | What it does |
| ----------------------------------- | --------------------------- | --------------- |
| `consensus/pow.h`                   | Proof-of-Work                | Compact difficulty → 256-bit target, mining by brute-forcing nNonce, a retarget every 5 blocks (60s target), total chain work decides forks. |
| `core/block.h` · `transaction.h`    | Blocks & UTXO                 | A 6-field header, double-SHA256 hash, Merkle Root over transactions; UTXO is an `unordered_map<COutPoint, CTxOut>` for O(1) input lookups. |
| `wallet/script.h`                   | P2PKH scripts                 | A stack machine with 4 opcodes (DUP, HASH160, EQUALVERIFY, CHECKSIG) — the same idea as real Bitcoin Script, just the necessary minimum. |
| `wallet/base58.h`                   | Base58Check addresses          | RIPEMD160(SHA256(pubkey)) + version byte 0x1E + a 4-byte checksum — its own prefix, distinct from real Bitcoin. |
| `network/message.h`                 | Message framing                | Magic `SILQ` + command (12 bytes) + length + a 4-byte checksum (double-SHA256 of the payload) — guards against foreign or corrupted traffic. |
| `network/daemon.h`                  | A persistent node              | Three threads around one chain under `std::mutex`: accepting peers, mining on a timer, polling known peers — no external scheduler. |

### Handshake and block exchange between two nodes

The same request cycle `run_connector` uses: if the received block doesn't extend the node's own tip, it isn't dropped right away — the node requests the peer's whole chain and compares total work (Bitcoin's rule: the winner is the chain with the most work, not the longest one).

```mermaid
sequenceDiagram
    participant A as Node A (Connector)
    participant B as Node B (Listener)
    A->>B: VERSION
    B-->>A: VERSION
    A->>B: VERACK
    B-->>A: VERACK
    B-->>A: INV (new block hash)
    A->>B: GETDATA (same hash)
    B-->>A: BLOCK (serialized)
    Note over A: Merkle root · PoW · hashPrevBlock<br/>nBits · input/output sums
    alt block extends A's own tip
        A->>A: chain.push_back(block)
    else tip mismatch — possible fork
        A->>B: GETBLOCKS
        B-->>A: CHAIN (the whole chain)
        A->>A: validate_full_chain +<br/>compare total work
    end
```

### Main "controller" modules
- **`network::Node` (`daemon.h`):** the persistent daemon — accepting connections, mining, syncing with peers.
- **`run_listener` / `run_connector` (`node.h`):** a one-shot exchange of a single block between two nodes (used by the `--listen`/`--connect` CLI flags).
- **`run_send_tx` / `run_receive_tx` (`node.h`):** sending a signed transaction to a peer / receiving a transaction into the mempool.
- **`storage::*` (`storage.h`):** saving and loading the chain, the UTXO set, the mempool.

The full table of the economic constants shown on the diagrams is in [Chapter 10](#chapter-10-network-economics-and-limits).

---

## Chapter 3: Cryptography and addresses

### Hash functions and signing (`crypto/ecdsa.h`)
```cpp
namespace crypto {
    inline void hash_sha256(const std::vector<uint8_t>& input, std::array<uint8_t, 32>& output);
    inline void hash_double_sha256(const std::vector<uint8_t>& input, std::array<uint8_t, 32>& output);
    inline void hash_ripemd160(unsigned char* ripemd160_hash, unsigned char* input, size_t len);

    int generate_keypair(std::string input, std::vector<std::string>& keypair,
        unsigned char* seckey, secp256k1_pubkey& pubkey, unsigned char* compressed_pubkey);
    int create_address_from_pubkey(unsigned char* compressed_pubkey,
        unsigned char* ripemd160_hash, std::string& ripemd160_hash_string);
    int generate_sign(std::string input, unsigned char* seckey,
        unsigned char* serialized_signature, std::string& str_signature,
        secp256k1_ecdsa_signature& sig, unsigned char* input_hash);
    int verify_sign(secp256k1_ecdsa_signature& sig, unsigned char* serialized_signature,
        secp256k1_pubkey& pubkey, unsigned char* compressed_pubkey, unsigned char* input_hash);
}
```
Address = `RIPEMD160(SHA256(compressed_pubkey))`, 20 bytes (`wallet::ADDRESS_SIZE`).

### Base58Check addresses (`wallet/base58.h`)
The same algorithm real Bitcoin uses: an alphabet without look-alike characters (0/O, I/l) plus a checksum to catch typos. Its own version byte, so a Siliqua address is never mistaken for a real Bitcoin address:
```cpp
constexpr uint8_t ADDRESS_VERSION_BYTE = 0x1E;

inline std::string encode_address(const std::array<uint8_t, ADDRESS_SIZE>& address) {
    std::vector<uint8_t> payload;
    payload.push_back(ADDRESS_VERSION_BYTE);
    payload.insert(payload.end(), address.begin(), address.end());

    std::array<uint8_t, 32> checksum{};
    crypto::hash_double_sha256(payload, checksum);
    payload.insert(payload.end(), checksum.begin(), checksum.begin() + 4);

    return base58_encode(payload);
}
```
`decode_address()` is the reverse operation, checking the version byte and the checksum (throws on a typo).

### Message signing (`wallet/signing.h`)
```cpp
constexpr size_t SIGNATURE_SIZE = 64; // secp256k1 compact signature
constexpr size_t PUBKEY_SIZE = 33;    // compressed pubkey

std::vector<uint8_t> sign_raw(const unsigned char seckey[32], const std::array<uint8_t, 32>& sighash);
bool verify_raw_signature(const std::vector<uint8_t>& sig_bytes,
    const std::vector<uint8_t>& pubkey_bytes, const std::array<uint8_t, 32>& sighash);
```
What gets signed is the transaction hash computed **with every `scriptSig` empty** (the same idea as Bitcoin's `SIGHASH_ALL`) — a signature cannot depend on its own bytes.

---

## Chapter 4: Transactions and the UTXO model

### Structures (`core/transaction.h`)

| Structure     | Fields                                              | Purpose                                  |
| ------------- | ------------------------------------------------------ | ------------------------------------------- |
| `COutPoint`   | `txid[32]`, `n`                                          | pointer to a specific output of a past tx  |
| `CTxIn`       | `prevout`, `scriptSig`, `nSequence`                       | an input — spends one UTXO                |
| `CTxOut`      | `nValue` (satoshi), `scriptPubKey`                        | an output — new unconfirmed balance       |
| `Transaction` | `nVersion`, `vin`, `vout`, `nLockTime`, `tx_hash`         | the whole transaction                     |

```cpp
// outpoint -> unspent output. O(1) lookups instead of std::map's O(log n) -
// this is exactly the structure that gets hit hardest as the chain grows.
using UtxoSet = std::unordered_map<COutPoint, CTxOut, COutPointHash>;
```

### Hash and coinbase
```cpp
std::array<uint8_t, 32> Transaction::GetHash() const;  // double SHA-256 of the serialized tx
bool Transaction::IsCoinbase() const;                    // the first tx in a block — the miner's reward
```

### Building the UTXO set from the whole chain (`network/node.h`)
```cpp
inline transaction::UtxoSet build_utxo_set(const std::vector<block::CBlock>& chain) {
    transaction::UtxoSet utxos;
    for (const auto& blk : chain)
        for (const auto& tx : blk.vtx)
            for (uint32_t i = 0; i < tx.vout.size(); ++i)
                utxos.emplace(transaction::COutPoint(tx.tx_hash, i), tx.vout.at(i));
    for (const auto& blk : chain)
        for (const auto& tx : blk.vtx)
            for (const auto& in : tx.vin)
                utxos.erase(in.prevout);
    return utxos;
}
```

### Validating a transaction and computing its fee
```cpp
// The fee (>= 0) if tx is valid; -1 otherwise.
inline int64_t validate_and_get_fee(const transaction::Transaction& tx, const transaction::UtxoSet& utxo_set) {
    int64_t input_total = 0;
    for (size_t i = 0; i < tx.vin.size(); ++i) {
        auto it = utxo_set.find(tx.vin.at(i).prevout);
        if (it == utxo_set.end() || !wallet::verify_transaction_signature(tx, i, it->second.scriptPubKey))
            return -1;
        input_total += it->second.nValue;
    }
    int64_t output_total = 0;
    for (const auto& out : tx.vout) output_total += out.nValue;
    if (output_total > input_total) return -1;
    return input_total - output_total; // the difference is the fee
}
```

---

## Chapter 5: Blocks and Proof-of-Work

### Block header (`core/block.h`)
```cpp
struct CBlockHeader {
    int32_t nVersion;
    std::array<uint8_t, 32> hashPrevBlock;
    std::array<uint8_t, 32> hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;   // compact target
    uint32_t nNonce;

    std::array<uint8_t, 32> GetHash() const; // double SHA-256
};

class CBlock : public CBlockHeader {
public:
    std::vector<transaction::Transaction> vtx;
    std::array<uint8_t, 32> BuildMerkleRoot() const;
    bool IsMerkleRootValid() const;
    bool IsValid() const;
};
```

### Compact target ↔ 256-bit (`consensus/pow.h`)
The top byte of `nBits` is the exponent (byte length), the low 3 bytes are the mantissa — the same scheme Bitcoin uses:
```cpp
inline std::array<uint8_t, 32> bits_to_target(uint32_t bits);
inline bool check_proof_of_work(const block::CBlockHeader& header); // hash <= target
inline void mine_block(block::CBlockHeader& header, uint32_t nBitsTarget); // brute-forces nNonce
```

### Difficulty retargeting
```cpp
constexpr uint32_t RETARGET_INTERVAL = 5;         // blocks between recalculations
constexpr uint32_t TARGET_TIMESPAN_SECONDS = 60;  // expected time for the interval
constexpr uint32_t INITIAL_BITS = 0x207fffff;

// actual_timespan is clamped to [target/4, target*4] - a burst of fast/slow
// blocks can't swing the difficulty by more than 4x in one retarget.
uint32_t get_next_work_required(const std::vector<block::CBlockHeader>& headers,
    uint32_t retarget_interval, uint32_t target_timespan_seconds);
```

### Fork selection — by total work, not by length
```cpp
double calculate_block_work(uint32_t nBits);              // 2^256 / target
double calculate_chain_work(const std::vector<block::CBlockHeader>& headers);
```
Bitcoin's key rule, reproduced here: the chain with the greatest **total work** wins (`try_reorg` in `node.h`), not simply the longer one — the two usually coincide, but not always.

### Block reward and halving (`network/node.h`)
```cpp
constexpr int64_t INITIAL_REWARD = 5000000000; // 50 coins, block 0
constexpr uint32_t HALVING_INTERVAL = 10;       // blocks per halving (small, so it's actually observable)

inline int64_t calculate_block_reward(uint32_t height) {
    uint32_t halvings = height / HALVING_INTERVAL;
    if (halvings >= 63) return 0;
    return INITIAL_REWARD >> halvings;
}
```
The coinbase input encodes the block height in `scriptSig` (the same idea as Bitcoin's BIP34) — otherwise two blocks with the same reward from the same address would produce an identical `tx_hash` and collide in the UTXO set.

---

## Chapter 6: P2PKH scripts

### Opcodes (`wallet/script.h`)

| Opcode            | Code    | Action                                                    |
| ------------------- | ------ | -------------------------------------------------------------- |
| `OP_DUP`             | `0x76` | duplicates the top of the stack                                |
| `OP_HASH160`         | `0xa9` | `RIPEMD160(SHA256(x))` of the top of the stack                 |
| `OP_EQUALVERIFY`     | `0x88` | compares the top two elements, aborts on mismatch               |
| `OP_CHECKSIG`        | `0xac` | verifies a signature against the sighash                         |

### Building scripts
```cpp
// OP_DUP OP_HASH160 <push 20-byte address> OP_EQUALVERIFY OP_CHECKSIG
std::vector<uint8_t> build_p2pkh_script_pubkey(const std::array<uint8_t, wallet::ADDRESS_SIZE>& address);

// <push signature><push pubkey>
std::vector<uint8_t> build_script_sig(const std::vector<uint8_t>& signature, const std::vector<uint8_t>& pubkey);
```

### Execution (the stack machine)
`scriptSig` and `scriptPubKey` are concatenated and run as one program — the same way real Bitcoin does it. A spend is valid if the stack ends with exactly one truthy value:
```cpp
bool evaluate(const std::vector<uint8_t>& script_sig,
    const std::vector<uint8_t>& script_pubkey, const std::array<uint8_t, 32>& sighash);
```
This isn't the full Bitcoin Script language (~100 opcodes), just the minimum needed for classic P2PKH.

---

## Chapter 7: P2P network protocol

### Message frame format (`network/message.h`)

| Field       | Size       | Description                                       |
| ----------- | ---------- | -------------------------------------------------------- |
| magic       | 4 bytes    | `"SILQ"` — not to be confused with real Bitcoin traffic  |
| command     | 12 bytes   | command name, zero-padded                                 |
| length      | 4 bytes LE | payload length                                             |
| checksum    | 4 bytes    | first 4 bytes of double-SHA256(payload)                    |
| payload     | variable   | the message body                                           |

```cpp
inline constexpr std::array<uint8_t, 4> MAGIC_BYTES = { 'S', 'I', 'L', 'Q' };
inline constexpr uint32_t MAX_PAYLOAD_SIZE = 4 * 1024 * 1024; // guards against a hostile length field
```

### Protocol commands

| Command      | Direction                           | Purpose                                        |
| ------------- | ------------------------------------- | -------------------------------------------------- |
| `version`     | initiator → recipient                 | protocol version negotiation                        |
| `verack`      | recipient → initiator                 | handshake acknowledgement                            |
| `inv`         | listener → connector                  | announces the new block's hash                       |
| `getdata`     | connector → listener                  | requests a block by hash                              |
| `block`       | listener → connector                  | the serialized block                                  |
| `tx`          | sender → recipient                    | a signed transaction for the mempool                  |
| `getblocks`   | connector → listener                  | requests the whole chain (for fork resolution)        |
| `chain`       | listener → connector                  | the whole chain (`storage::serialize_chain`)          |

### The Peer class — an RAII socket wrapper (`network/peer.h`)
```cpp
class Peer {
public:
    explicit Peer(socket_t sock) : sock_(sock) {}
    ~Peer() { close_socket(sock_); }
    Peer(const Peer&) = delete; // a socket has exactly one owner

    bool Send(const std::string& cmd, const std::vector<uint8_t>& payload) const;
    bool Receive(Message& out) const;
};
```
Sockets are cross-platform (`network/socket.h`): Winsock on Windows, standard POSIX sockets on Linux/macOS, behind one interface — `create_listener` / `connect_to` / `accept_connection`.

---

## Chapter 8: The node — CLI modes and the persistent daemon

### One-shot operations (`network/node.h`)
Each function performs exactly one exchange with exactly one peer and exits — convenient for the CLI and for testing:

| Function              | What it does                                                                            |
| ---------------------- | -------------------------------------------------------------------------------------------- |
| `run_listener`         | mines a new block on top of its own chain, listens on a port, serves one peer               |
| `run_connector`        | connects to a peer, fetches a block; if it doesn't extend its own tip — requests the peer's whole chain and compares total work |
| `run_send_tx`          | builds and signs a transaction from its own UTXOs, sends it to a peer's mempool             |
| `run_receive_tx`       | accepts one transaction, checks its signature/UTXOs, drops it into the mempool              |

### The persistent daemon `network::Node` (`network/daemon.h`)
The same building blocks (`build_next_block`, `try_extend_chain`, `try_reorg`, `validate_and_get_fee`), just running continuously instead of once:

```cpp
class Node {
public:
    Node(uint16_t listen_port, const std::string& chain_path, std::vector<PeerAddress> known_peers);
    [[noreturn]] void Run(); // starts 3 threads and never returns control

private:
    [[noreturn]] void AcceptLoop();    // accepts peers, each on its own detached thread
    void HandleConnection(socket_t client_sock);
    void AcceptTransaction(const transaction::Transaction& tx, const std::string& prefix);
    [[noreturn]] void MiningLoop();    // mines a block from the mempool every DAEMON_CYCLE_SECONDS
    [[noreturn]] void PeerPollLoop();  // polls known_peers_ every DAEMON_CYCLE_SECONDS
    void PollPeer(const PeerAddress& addr);

    std::mutex mutex_;                 // guards chain_ from races between the three loops
    std::vector<block::CBlock> chain_;
};
```

### Accepting a block and fork resolution
```cpp
bool try_extend_chain(std::vector<block::CBlock>& chain, const block::CBlock& received_block);
bool try_reorg(std::vector<block::CBlock>& chain, const std::vector<block::CBlock>& peer_chain);
bool validate_full_chain(const std::vector<block::CBlock>& chain); // from scratch, from genesis
```
The full exchange sequence is shown in the diagram in [Chapter 2](#chapter-2-architecture-diagram) (handshake and block exchange).

---

## Chapter 9: Storage (`storage/storage.h`)

| File                     | Format                                                                        |
| --------------------------- | ---------------------------------------------------------------------------- |
| `<chain_path>`               | block count (varint) + each block's `Serialize()` back to back                |
| `<chain_path>.wallet`        | 32 raw bytes of the seckey                                                    |
| `<chain_path>.mempool`       | tx count (varint) + each transaction's `Serialize()` back to back             |
| `<any>.utxo`                 | entry count (varint) + pairs of `COutPoint::Serialize()`+`CTxOut::Serialize()`|

```cpp
void save_chain(const std::vector<block::CBlock>& chain, const std::string& path);
std::vector<block::CBlock> load_chain(const std::string& path);
void save_mempool(const std::vector<transaction::Transaction>& mempool, const std::string& path);
std::vector<transaction::Transaction> load_mempool(const std::string& path);
```
The same `serialize_chain()`/`deserialize_chain()` is used both for the file and for the network's `CHAIN` message — one format, two carriers.

---

## Chapter 10: Network economics and limits

| Constant                    | Value               | Meaning                                                    | File                    |
| ------------------------------ | -------------------- | -------------------------------------------------------------- | ------------------------- |
| `INITIAL_REWARD`               | 5,000,000,000        | 50 coins — the reward for block 0, before halving              | `network/node.h`          |
| `HALVING_INTERVAL`             | 10 blocks            | reward halves every N blocks                                    | `network/node.h`          |
| `DEFAULT_FEE`                  | 1,000 satoshi         | flat fee paid by `run_send_tx`                                  | `network/node.h`          |
| `RETARGET_INTERVAL`            | 5 blocks              | how often difficulty is recomputed                              | `network/node.h`          |
| `TARGET_TIMESPAN_SECONDS`      | 60 s                  | expected time for `RETARGET_INTERVAL` blocks                    | `network/node.h`          |
| `MAX_MEMPOOL_SIZE`             | 1000 tx               | cap on unconfirmed transactions                                 | `network/node.h`          |
| `DUST_THRESHOLD`               | 1000 satoshi          | change below this isn't split into its own UTXO                 | `wallet/wallet.h`         |
| `DAEMON_CYCLE_SECONDS`         | 5 s                    | period of the MiningLoop and PeerPollLoop cycles                | `network/daemon.h`        |
| `ADDRESS_VERSION_BYTE`         | `0x1E`                 | own address prefix (not Bitcoin mainnet)                         | `wallet/base58.h`         |
| `MAGIC_BYTES`                  | `"SILQ"`               | marks Siliqua traffic apart from real Bitcoin P2P                 | `network/message.h`       |

Values are intentionally small (a halving every 10 blocks, a retarget every 5) — the behavior can be observed in a single test run rather than over years, as in real Bitcoin.

---

## Chapter 11: CLI — commands and a two-node scenario

### Flag reference (`src/Siliqua.cpp`)

| Command                                                          | Purpose                                                              |
| -------------------------------------------------------------------- | -------------------------------------------------------------------- |
| `Siliqua --seed <path>`                                              | mine a clean genesis chain (neutral address) and save it              |
| `Siliqua --listen <port> [chain_path]`                                | mine a block on top of `chain_path`, serve one peer                   |
| `Siliqua --connect <host> <port> [chain_path]`                        | fetch a block from a peer and, if valid, append it to `chain_path`    |
| `Siliqua --address <chain_path>`                                      | print the node's wallet address (Base58Check)                         |
| `Siliqua --balance <chain_path>`                                      | print its own UTXOs and their total                                   |
| `Siliqua --send <host> <port> <chain_path> <address> <amount>`        | sign a spend and send it to a peer's mempool                          |
| `Siliqua --receive-tx <port> <chain_path>`                            | accept one transaction from a peer into the mempool                   |
| `Siliqua --daemon <port> <chain_path> [host:port ...]`                | run a persistent node with (optionally) known peers                   |

### Practical scenario: two nodes, one transfer
```bash
# 1. Node A seeds the genesis chain
./Siliqua --seed nodeA.dat

# 2. Node B copies the SAME genesis BEFORE A mines the next block —
#    run_connector requires an already-existing chain sharing that genesis,
#    otherwise the very first received block won't link via hashPrevBlock
#    and will be rejected.
cp nodeA.dat nodeB.dat

# 3. Node A mines block 1 on top of its chain and listens on port 9000
./Siliqua --listen 9000 nodeA.dat &

# 4. Node B connects, fetches block 1, and appends it to its own copy of the chain
./Siliqua --connect 127.0.0.1 9000 nodeB.dat

# 5. Check node A's addresses and balance (block 1's reward went to its wallet)
./Siliqua --address nodeA.dat
./Siliqua --address nodeB.dat
./Siliqua --balance nodeA.dat

# 6. Node B listens on a port to receive a transaction
./Siliqua --receive-tx 9001 nodeB.dat &

# 7. Node A sends part of its balance to node B's address (from step 5)
./Siliqua --send 127.0.0.1 9001 nodeA.dat <node_B_address> 100000

# 8. A persistent daemon instead of one-shot commands, with a known peer
./Siliqua --daemon 9000 nodeA.dat 127.0.0.1:9001
```

---

## Chapter 12: Building the project

### Dependencies
- CMake ≥ 3.15, a compiler with C++23 support
- `libsecp256k1` (`brew install secp256k1` / `apt-get install libsecp256k1-dev`)
- `libcryptopp` (`brew install cryptopp` / `apt-get install libcrypto++-dev`)
- On Windows, dependencies are expected in `Siliqua/libs/` (`libsecp256k1.lib`, `cryptlib.lib`)

### Building (Linux / macOS)
```bash
mkdir -p build && cd build
cmake ..
cmake --build . --config Release
```
The binary ends up in `Siliqua/bin/Siliqua` (`RUNTIME_OUTPUT_DIRECTORY`).

### CMakeLists.txt highlights
```cmake
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Single-config generators (Make/Ninja) don't optimize by default unless a
# build type is given explicitly - force Release so `cmake .. && cmake --build .`
# still produces an optimized binary out of the box.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
```
`-O2` instead of `-O3` on GCC/Clang in Release is a deliberate choice for compatibility with weak/old hardware (no auto-vectorization targeting instructions the target CPU may not support).

### Threads
`network/daemon.h` uses `std::thread`; on Linux this needs `-lpthread`, resolved via `Threads::Threads` instead of a platform-specific flag.
