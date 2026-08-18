#pragma once
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

class Logger {
public:
    // Close copy.
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Single access.
    static Logger& instance() {
        static Logger instance; // first call initialization
        return instance;
    }

    template<typename... Args>
    void info(fmt::format_string<Args...> fmt, Args&&... args) {
        logger_->info(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void debug(fmt::format_string<Args...> fmt, Args&&... args) {
        logger_->debug(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void error(fmt::format_string<Args...> fmt, Args&&... args) {
        logger_->error(fmt, std::forward<Args>(args)...);
    }

    // TODO: Warn, Critical?

private:
    Logger() {
        // Logger initialization.
        auto console = spdlog::stdout_color_mt("console");
        console->set_level(spdlog::level::debug);
        logger_ = console;
    }

    std::shared_ptr<spdlog::logger> logger_;
};