#include "ProjectStore.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_system.h>

#include "ProjectJson.h"

bool ProjectStore::begin(const char* path) {
  path_ = path;

  // true — отформатировать раздел, если он ещё не размечан.
  if (!LittleFS.begin(true)) {
    return false;
  }

  return load();
}

const Project* ProjectStore::find(const String& id) const {
  for (const Project& p : projects_) {
    if (p.id == id) {
      return &p;
    }
  }
  return nullptr;
}

Project* ProjectStore::find(const String& id) {
  for (Project& p : projects_) {
    if (p.id == id) {
      return &p;
    }
  }
  return nullptr;
}

String ProjectStore::generateId() const {
  // 64-битный случайный id в hex; при коллизии пробуем ещё раз.
  for (int attempt = 0; attempt < 10; ++attempt) {
    char buf[17];
    snprintf(buf, sizeof(buf), "%08x%08x", esp_random(), esp_random());
    if (!find(String(buf))) {
      return String(buf);
    }
  }
  return String();
}

bool ProjectStore::create(Project& project) {
  if (!project.id.isEmpty() && find(project.id)) {
    return false;  // такой id уже занят
  }
  if (project.id.isEmpty()) {
    project.id = generateId();
  }

  projects_.push_back(project);
  save();
  return true;
}

bool ProjectStore::update(const String& id, const Project& updated) {
  Project* p = find(id);
  if (!p) {
    return false;
  }

  p->title = updated.title;
  p->tags = updated.tags;
  p->description = updated.description;
  p->githubLink = updated.githubLink;
  p->imageURL = updated.imageURL;

  save();
  return true;
}

bool ProjectStore::remove(const String& id) {
  for (auto it = projects_.begin(); it != projects_.end(); ++it) {
    if (it->id == id) {
      projects_.erase(it);
      save();
      return true;
    }
  }
  return false;
}

bool ProjectStore::load() {
  // Файла ещё нет — начнём с пустого хранилища.
  if (!LittleFS.exists(path_)) {
    return true;
  }

  File file = LittleFS.open(path_, "r");
  if (!file) {
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    return false;
  }

  projects_.clear();
  for (JsonObjectConst obj : doc["projects"].as<JsonArrayConst>()) {
    Project p;
    projectFromJson(obj, p);
    if (!p.id.isEmpty()) {
      projects_.push_back(p);
    }
  }

  return true;
}

bool ProjectStore::save() const {
  JsonDocument doc;

  JsonArray arr = doc["projects"].to<JsonArray>();
  for (const Project& p : projects_) {
    projectToJson(arr.add<JsonObject>(), p);
  }

  // Записываем во временный файл, затем атомарно переименовываем,
  // чтобы при сбое питания не остаться с обрезанным JSON.
  String tmpPath = path_ + ".tmp";
  File file = LittleFS.open(tmpPath, "w");
  if (!file) {
    return false;
  }
  size_t written = serializeJson(doc, file);
  file.close();
  if (written == 0) {
    return false;
  }

  LittleFS.remove(path_);
  return LittleFS.rename(tmpPath, path_);
}
