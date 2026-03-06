#pragma once
#include <secp256k1.h>
#include <memory>
#include <stdexcept>
#include <windows.h>
#include <bcrypt.h>
#include "logger.h"
#include "utils.h"
#pragma comment(lib, "bcrypt.lib")

class Secp256k1Context {
public:
    // Close copy.
    Secp256k1Context(const Secp256k1Context&) = delete;
    Secp256k1Context& operator=(const Secp256k1Context&) = delete;

    // Single access.
    static Secp256k1Context& instance() {
        static Secp256k1Context instance;
        return instance;
    }

    // Raw ptr.
    secp256k1_context* get() const { return ctx_.get(); }

private:
    // Create context.
    Secp256k1Context()
        : ctx_(secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY),
            secp256k1_context_destroy)
    {
        if (!ctx_) {
            throw std::runtime_error("Failed to create secp256k1 context");
        }
        
        const int STD_SIZE = 32;

        // Context randomize.
        unsigned char randomize[STD_SIZE];
        Logger::instance().debug("Generate randomizer for context.");
        if (!fill_random(randomize, sizeof(randomize))) {
            throw std::runtime_error("Failed to generate randomness");
        }
        // Context randomizing.
        if (secp256k1_context_randomize(ctx_.get(), randomize) != 1) {
            throw std::runtime_error("Failed to randomize secp256k1 context");
        }
    }

    std::unique_ptr<secp256k1_context, decltype(&secp256k1_context_destroy)> ctx_;
};