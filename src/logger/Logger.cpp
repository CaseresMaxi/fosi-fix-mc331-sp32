#include "Logger.h"

#include <ArduinoJson.h>

Logger& Logger::instance() {
  static Logger logger;
  return logger;
}

void Logger::begin(size_t capacity) {
  capacity_ = capacity > 0 ? capacity : 80;
  entries_.reserve(capacity_);
}

void Logger::setMinLevel(LogLevel level) { minLevel_ = level; }

const char* Logger::levelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
    default:
      return "INFO";
  }
}

void Logger::debug(const String& message) { log(LogLevel::Debug, message); }
void Logger::info(const String& message) { log(LogLevel::Info, message); }
void Logger::warn(const String& message) { log(LogLevel::Warn, message); }
void Logger::error(const String& message) { log(LogLevel::Error, message); }

void Logger::log(LogLevel level, const String& message) {
  if (static_cast<uint8_t>(level) < static_cast<uint8_t>(minLevel_)) {
    return;
  }

  LogEntry entry;
  entry.millis = millis();
  entry.level = level;
  entry.message = message;
  if (entry.message.length() > 160) {
    entry.message.remove(160);
  }

  Serial.printf("[%lu][%s] %s\n", static_cast<unsigned long>(entry.millis),
                levelName(level), entry.message.c_str());

  portENTER_CRITICAL(&mux_);
  if (entries_.size() >= capacity_) {
    entries_.erase(entries_.begin());
  }
  entries_.push_back(entry);
  auto listeners = listeners_;
  portEXIT_CRITICAL(&mux_);

  for (auto& listener : listeners) {
    if (listener) {
      listener(entry);
    }
  }
}

size_t Logger::size() const { return entries_.size(); }

String Logger::toJsonArray(size_t maxEntries) const {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  size_t start = 0;
  if (maxEntries > 0 && entries_.size() > maxEntries) {
    start = entries_.size() - maxEntries;
  }

  for (size_t i = start; i < entries_.size(); ++i) {
    JsonObject obj = arr.add<JsonObject>();
    obj["t"] = entries_[i].millis;
    obj["level"] = levelName(entries_[i].level);
    obj["msg"] = entries_[i].message;
  }

  String out;
  serializeJson(arr, out);
  return out;
}

void Logger::addListener(LogListener listener) {
  portENTER_CRITICAL(&mux_);
  listeners_.push_back(std::move(listener));
  portEXIT_CRITICAL(&mux_);
}

void Logger::clearListeners() {
  portENTER_CRITICAL(&mux_);
  listeners_.clear();
  portEXIT_CRITICAL(&mux_);
}
