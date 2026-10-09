// Минимальная эмуляция Arduino-окружения для host-тестов (gcc/clang).
// Позволяет компилировать заголовки модели (ProjectStore.h/ProjectJson.h)
// без платы. ArduinoJson распознаёт этот класс как строку: у него есть
// c_str() и length() (см. generic-адаптер StringObject.hpp в ArduinoJson).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Минимальная эмуляция Arduino String поверх std::string.
// Реализует только то подмножество API, которое использует код проекта.
class String {
 public:
  String() = default;
  String(const char* s) : data_(s ? s : "") {}
  String(const std::string& s) : data_(s) {}

  String& operator=(const char* s) {
    data_ = s ? s : "";
    return *this;
  }
  String& operator=(const std::string& s) {
    data_ = s;
    return *this;
  }

  unsigned int length() const {
    return static_cast<unsigned int>(data_.size());
  }
  bool isEmpty() const { return data_.empty(); }

  String substring(unsigned int from) const { return String(data_.substr(from)); }
  String substring(unsigned int from, unsigned int to) const {
    if (to <= from) {
      return String();
    }
    return String(data_.substr(from, to - from));
  }

  const char* c_str() const { return data_.c_str(); }

  String& operator+=(char c) {
    data_ += c;
    return *this;
  }
  String& operator+=(const String& s) {
    data_ += s.data_;
    return *this;
  }
  String& operator+=(const char* s) {
    data_ += (s ? s : "");
    return *this;
  }

  String& concat(const char* s) {
    data_ += (s ? s : "");
    return *this;
  }
  String& concat(const String& s) {
    data_ += s.data_;
    return *this;
  }
  String& concat(const char* s, unsigned int n) {
    data_.append(s, n);
    return *this;
  }

  char& operator[](unsigned int i) { return data_[i]; }
  const char& operator[](unsigned int i) const { return data_[i]; }

 private:
  std::string data_;
};
