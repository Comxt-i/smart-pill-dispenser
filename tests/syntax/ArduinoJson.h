// Stub สำหรับตรวจไวยากรณ์เท่านั้น: รูปทรงของ API ตรงกับ ArduinoJson v7 แต่ไม่เก็บข้อมูลจริง
#pragma once

#include <Arduino.h>

class DeserializationError {
 public:
  enum Code { Ok, EmptyInput, IncompleteInput, InvalidInput, NoMemory, TooDeep };

  DeserializationError() {}
  DeserializationError(Code value) : code(value) {}

  explicit operator bool() const { return code != Ok; }
  const char *c_str() const { return "Ok"; }

  bool operator==(Code other) const { return code == other; }
  bool operator!=(Code other) const { return code != other; }

  Code code = Ok;
};

/** ตัวแทนของ JsonVariant / JsonObject / JsonArray รวมกัน เพียงพอสำหรับตรวจไวยากรณ์ */
class JsonAny {
 public:
  JsonAny operator[](const char *) const { return JsonAny(); }
  JsonAny operator[](int) const { return JsonAny(); }

  template <typename T>
  T as() const {
    return T();
  }

  template <typename T>
  T to() {
    return T();
  }

  template <typename T>
  T add() {
    return T();
  }

  template <typename T>
  T operator|(T fallback) const {
    return fallback;
  }

  template <typename T>
  JsonAny &operator=(const T &) {
    return *this;
  }

  class Iterator {
   public:
    explicit Iterator(bool atEnd) : done(atEnd) {}
    JsonAny operator*() const { return JsonAny(); }
    Iterator &operator++() { return *this; }
    bool operator!=(const Iterator &other) const { return done != other.done; }

   private:
    bool done;
  };

  Iterator begin() const { return Iterator(true); }
  Iterator end() const { return Iterator(true); }
};

template <>
inline const char *JsonAny::as<const char *>() const {
  return "";
}

using JsonVariant = JsonAny;
using JsonObject = JsonAny;
using JsonArray = JsonAny;

class JsonDocument : public JsonAny {};

inline DeserializationError deserializeJson(JsonDocument &, Stream &) {
  return DeserializationError();
}
inline DeserializationError deserializeJson(JsonDocument &, const String &) {
  return DeserializationError();
}
inline size_t serializeJson(const JsonDocument &, String &) { return 0; }
