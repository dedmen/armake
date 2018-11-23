#include "logger.h"
#include <cstdarg>
#include "utils.h"


#define PRINTF     char buffer[4096];\
va_list argptr;\
va_start(argptr, format);\
vsprintf(buffer, format, argptr);\
va_end(argptr)






void Logger::printInFile(std::string_view file, size_t line) {
    std::unique_lock lock(streamLock);
    if (line > 0)
        logTarget << "In file " << file << ":" << line << ":";
    else
        logTarget << "In file " << file << ":";
}

void Logger::info(std::string_view message) {
    std::unique_lock lock(streamLock);
#ifdef _WIN32
    logTarget << "info: " << message;
    logTarget << COLOR_GREEN << "info:" << COLOR_RESET << " " << message;
#else

    logTarget << COLOR_GREEN << "info:" << COLOR_RESET << " " << message;
#endif
    logTarget.flush();
}

void Logger::info(const char* format, ...) {
    PRINTF;

    info(std::string_view(buffer));
}

void Logger::debug(std::string_view message) {
    std::unique_lock lock(streamLock);
#ifdef _WIN32
    logTarget << "debug: " << message;
#else
    logTarget << COLOR_CYAN << "debug:" << COLOR_RESET << " " << message;
#endif

    logTarget.flush();
}

void Logger::debug(const char* format, ...) {
    PRINTF;

    debug(std::string_view(buffer));
}

bool Logger::warning_enabled(LoggerMessageType type) {

    return warningEnabled[static_cast<size_t>(type)];

    //extern struct arguments args;
    //int i;
    //
    //for (i = 0; i < args.num_mutedwarnings; i++) {
    //    if (strcmp(args.mutedwarnings[i], name) == 0)
    //        return true;
    //}
    //return false;
}

void Logger::warning(std::string_view message) {
    std::unique_lock lock(streamLock);
#ifdef _WIN32
    logTarget << "warning: " << message;
    logTarget << COLOR_YELLOW << "warning:" << COLOR_RESET << " " << message;
#else
    logTarget << COLOR_YELLOW << "warning:" << COLOR_RESET << " " << message;
#endif

    logTarget.flush();
}

void Logger::warning(const char* format, ...) {
    char buffer[4096];
    va_list argptr;

    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);

    warning(std::string_view(buffer));
}

void Logger::warning(std::string_view file, size_t line, const char* format, ...) {
    char buffer[4096];
    va_list argptr;

    va_start(argptr, format);
    vsprintf(buffer, format, argptr);
    va_end(argptr);

    printInFile(file, line);

    warning(std::string_view(buffer));
}

void Logger::warning(LoggerMessageType type, const char* format, ...) {
    if (!warning_enabled(type)) return;

    char buffer[4096];
    va_list argptr;

    if (!warning_enabled(type))
        return;

    va_start(argptr, format);
    vsprintf(buffer, format, argptr);
    va_end(argptr);

    if (buffer[strlen(buffer) - 1] == '\n')
        buffer[strlen(buffer) - 1] = 0;

    std::string msg(buffer);
    msg += "[";
    msg += MessageTypeToString(type);
    msg += "]\n";
    warning(std::string_view(msg));
}

void Logger::warning(std::string_view file, size_t line, LoggerMessageType type, const char* format, ...) {
    if (!warning_enabled(type)) return;

    PRINTF;

    printInFile(file, line);

    warning(type, buffer);
}

void Logger::error(std::string_view message) {
    std::unique_lock lock(streamLock);
#ifdef _WIN32
    logTarget << "error: " << message;
    logTarget << COLOR_RED << "error:" << COLOR_RESET << " " << message;
#else
    logTarget << COLOR_RED << "error:" << COLOR_RESET << " " << message;
#endif

    logTarget.flush();
}


void Logger::error(const char* format, ...) {
    PRINTF;

    error(std::string_view(buffer));
}

void Logger::error(std::string_view file, size_t line, const char* format, ...) {
    PRINTF;

    printInFile(file, line);
    error(std::string_view(buffer));
}
