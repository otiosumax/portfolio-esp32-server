#pragma once

#include <ArduinoJson.h>

#include <vector>

#include "ProjectStore.h"

// Общие преобразования между моделью Project и JSON.
// Используются и хранилищем (файл в LittleFS), и HTTP-слоем,
// чтобы формат данных на диске и в API всегда совпадал.

// Символ относится к ASCII-пробелам. Важно не использовать isspace():
// байты многобайтового UTF-8 (кириллица и т.п.) имеют код >= 0x80,
// и обрезка по ниму может испортить последовательность.
inline bool isAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Обрезает только пробелы по краям, не задевая UTF-8-символы.
inline void trimAscii(String &s) {
  unsigned int begin = 0;
  unsigned int end = s.length();
  while (begin < end && isAsciiSpace(s[begin])) {
    ++begin;
  }
  while (end > begin && isAsciiSpace(s[end - 1])) {
    --end;
  }
  if (begin != 0 || end != s.length()) {
    s = s.substring(begin, end);
  }
}

// id допускает только латиницу и цифры (a-z, A-Z, 0-9) и непустой.
inline bool isValidId(const String &id) {
  if (id.isEmpty()) {
    return false;
  }
  for (unsigned int i = 0; i < id.length(); ++i) {
    const char c = id[i];
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9');
    if (!alnum) {
      return false;
    }
  }
  return true;
}

// Копирует непустые строки из JSON-массива в вектор.
inline void readStringArray(JsonVariantConst value, std::vector<String> &out) {
  for (JsonVariantConst item : value.as<JsonArrayConst>()) {
    String s = item.as<String>();
    trimAscii(s);
    if (!s.isEmpty()) {
      out.push_back(s);
    }
  }
}

// Сериализует проект в JSON-объект формата API.
inline void projectToJson(JsonObject obj, const Project &p) {
  obj["id"] = p.id;
  obj["title"] = p.title;

  JsonArray tags = obj["tags"].to<JsonArray>();
  for (const String &tag : p.tags) {
    tags.add(tag);
  }

  obj["description"] = p.description;

  obj["githubLink"] = p.githubLink;
  obj["imageURL"] = p.imageURL;
}

// Читает все поля проекта из JSON-объекта (включая id).
inline void projectFromJson(JsonObjectConst obj, Project &p) {
  p.id = obj["id"] | "";
  trimAscii(p.id);
  p.title = obj["title"] | "";
  trimAscii(p.title);
  readStringArray(obj["tags"], p.tags);
  p.description = obj["description"] | "";
  p.githubLink = obj["githubLink"] | "";
  p.imageURL = obj["imageURL"] | "";
}
