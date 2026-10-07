#pragma once

#include <ArduinoJson.h>

#include <vector>

#include "ProjectStore.h"

// Общие преобразования между моделью Project и JSON.
// Используются и хранилищем (файл в LittleFS), и HTTP-слоем,
// чтобы формат данных на диске и в API всегда совпадал.

// Копирует непустые строки из JSON-массива в вектор.
inline void readStringArray(JsonVariantConst value, std::vector<String>& out) {
  for (JsonVariantConst item : value.as<JsonArrayConst>()) {
    String s = item.as<String>();
    s.trim();
    if (!s.isEmpty()) {
      out.push_back(s);
    }
  }
}

// Сериализует проект в JSON-объект формата API.
inline void projectToJson(JsonObject obj, const Project& p) {
  obj["id"] = p.id;
  obj["title"] = p.title;

  JsonArray tags = obj["tags"].to<JsonArray>();
  for (const String& tag : p.tags) {
    tags.add(tag);
  }

  JsonArray description = obj["description"].to<JsonArray>();
  for (const String& line : p.description) {
    description.add(line);
  }

  obj["githubLink"] = p.githubLink;
  obj["imageURL"] = p.imageURL;
}

// Читает все поля проекта из JSON-объекта (включая id).
inline void projectFromJson(JsonObjectConst obj, Project& p) {
  p.id = obj["id"] | "";
  p.id.trim();
  p.title = obj["title"] | "";
  p.title.trim();
  readStringArray(obj["tags"], p.tags);
  readStringArray(obj["description"], p.description);
  p.githubLink = obj["githubLink"] | "";
  p.imageURL = obj["imageURL"] | "";
}
