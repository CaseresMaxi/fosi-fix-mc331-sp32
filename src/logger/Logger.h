#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

enum class LogLevel : uint8_t { Debug = 0, Info = 1, Warn = 2, Error = 3 };

struct LogEntry {
  uint32_t millis;
  LogLevel level;
  String message;
};

using LogListener = std::function<void(const LogEntry&)>;

class Logger {
 public:
  static Logger& instance();

  void begin(size_t capacity = 80);
  void setMinLevel(LogLevel level);

  void debug(const String& message);
  void info(const String& message);
  void warn(const String& message);
  void error(const String& message);
  void log(LogLevel level, const String& message);

  size_t size() const;
  const std::vector<LogEntry>& entries() const { return entries_; }
  String toJsonArray(size_t maxEntries = 0) const;
  static const char* levelName(LogLevel level);

  void addListener(LogListener listener);
  void clearListeners();

 private:
  Logger() = default;

  size_t capacity_ = 80;
  LogLevel minLevel_ = LogLevel::Debug;
  std::vector<LogEntry> entries_;
  std::vector<LogListener> listeners_;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};
