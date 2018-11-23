#pragma once
#include <array>
#include <string_view>
#include <vector>
#include <mutex>
using namespace std::string_view_literals;

enum class LoggerMessageType {

    //preproc
    excessive_concatenation,
    redefinition_wo_undef,
    //config parser
    unquoted_string,
    //model config
    model_without_prefix,
    animated_without_skeleton,
    //p3d binarize
    no_proxy_face,
    unknown_bone,
    

    LoggerMessageTypeN
};
static constexpr size_t numberOfWarningTypes = static_cast<size_t>(LoggerMessageType::LoggerMessageTypeN);

static constexpr std::array<std::string_view, numberOfWarningTypes>
    loggerTypeToName{
    "excessive-concatenation"sv,
    "redefinition-wo-undef"sv,
    "unquoted-string"sv,
    "model-without-prefix"sv,
    "animated-without-skeleton"sv,
    "no-proxy-face"sv,
    "unknown-bone"sv
};

static constexpr std::string_view MessageTypeToString(LoggerMessageType t) {
    return loggerTypeToName[static_cast<size_t>(t)];
}

static constexpr LoggerMessageType StringToMessageType(std::string_view t) {
    size_t offs = 0;
    for (auto& it : loggerTypeToName) {
        if (it != t) offs++;
        else return static_cast<LoggerMessageType>(offs);
    }
    return LoggerMessageType::LoggerMessageTypeN;
}

class Logger {
    std::ostream& logTarget;
    std::ostream& logErr;
    std::mutex streamLock;

    std::array<bool, numberOfWarningTypes> warningEnabled{ true };

    void printInFile(std::string_view file, size_t line);

public:
	Logger(std::ostream& target) : logTarget(target), logErr(target) {}
	Logger(std::ostream& target, std::ostream& targetErr) : logTarget(target), logErr(targetErr) {}

    void disableWarning(LoggerMessageType type) {
        warningEnabled[static_cast<size_t>(type)] = false;
    }
    void disableWarnings(std::vector<LoggerMessageType> types) {
        for (auto type : types)
            warningEnabled[static_cast<size_t>(type)] = false;
    }


    void info(std::string_view message);
    void info(const char* format, ...);
    void debug(std::string_view message);
    void debug(const char* format, ...);

    bool warning_enabled(LoggerMessageType type);


    void warning(std::string_view message);
    void warning(const char* format, ...);
    void warning(std::string_view file, size_t line, const char* format, ...);
    void warning(LoggerMessageType type, const char* format, ...);
    void warning(std::string_view file, size_t line, LoggerMessageType type, const char* format, ...);


    void error(std::string_view message);
    void error(const char* format, ...);
    void error(std::string_view file, size_t line, const char* format, ...);
};
