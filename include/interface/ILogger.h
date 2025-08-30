#pragma once

#include "Common.h"
#include <format>
#include <string>

namespace leigod::dns {
    /**
     * Interface for logging
     */
    class ILogger {
    public:
        enum Level {
            kTRACE = 0,
            kDEBUG,
            kINFO,
            kWARNING,
            kERROR,
            kCRITICAL
        };
        virtual ~ILogger() = default;

        virtual void log(int level, const char *file, const char *func, int line, const std::string &message) const = 0;

        void trace(const char *file, const char *func, int line, const std::string &message) const {
            log(Level::kTRACE, file, func, line, message);
        }

        void debug(const char *file, const char *func, int line, const std::string &message) const {
            log(Level::kDEBUG, file, func, line, message);
        }

        void info(const char *file, const char *func, int line, const std::string &message) const {
            log(Level::kINFO, file, func, line, message);
        }

        void warn(const char *file, const char *func, int line, const std::string &message) const {
            log(Level::kWARNING, file, func, line, message);
        }

        void error(const char *file, const char *func, int line, const std::string &message) const {
            log(Level::kERROR, file, func, line, message);
        }

        void critical(const char *file, const char *func, int line, const std::string &message) const {
            log(Level::kCRITICAL, file, func, line, message);
        }
    };

}// namespace leigod::dns


#ifndef DNS_LOGGER_TRACE
#define DNS_LOGGER_TRACE(log, fmt, ...) (log)->trace(__FILE__, __func__, __LINE__, std::format(fmt, __VA_ARGS__))
#endif

#ifndef DNS_LOGGER_DEBUG
#define DNS_LOGGER_DEBUG(log, fmt, ...) (log)->debug(__FILE__, __func__, __LINE__, std::format(fmt, __VA_ARGS__))
#endif

#ifndef DNS_LOGGER_INFO
#define DNS_LOGGER_INFO(log, fmt, ...) (log)->info(__FILE__, __func__, __LINE__, std::format(fmt, __VA_ARGS__))
#endif

#ifndef DNS_LOGGER_WARN
#define DNS_LOGGER_WARN(log, fmt, ...) (log)->warn(__FILE__, __func__, __LINE__, std::format(fmt, __VA_ARGS__))
#endif

#ifndef DNS_LOGGER_ERROR
#define DNS_LOGGER_ERROR(log, fmt, ...) (log)->error(__FILE__, __func__, __LINE__, std::format(fmt, __VA_ARGS__))
#endif

#ifndef DNS_LOGGER_CRITICAL
#define DNS_LOGGER_CRITICAL(log, fmt, ...) (log)->critical(__FILE__, __func__, __LINE__, std::format(fmt, __VA_ARGS__))
#endif
