#include "ProjectStore.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

bool ProjectStore::begin(const char* path) {
  path_ = path;

  // true — отформатировать раздел, если он ещё не размечан.
  if (!LittleFS.begin(true)) {
    return false;
  }

  return load();
}

const Project* ProjectStore::find(uint32_t id) const {
  for (const Project& p : projects_) {
    if (p.id == id) {
      return &p;
    }
  }
  return nullptr;
}

Project* ProjectStore::find(uint32_t id) {
  for (Project& p : projects_) {
    if (p.id == id) {
      return &p;
    }
  }
  return nullptr;
}

Project ProjectStore::create(const String& title, const String& description,
                             const String& githubLink,
                             const String& imageURL) {
  Project p;
  p.id = nextId_++;
  p.title = title;
  p.description = description;
  p.githubLink = githubLink;
  p.imageURL = imageURL;

  projects_.push_back(p);
  save();
  return p;
}

bool ProjectStore::update(uint32_t id, const Project& updated) {
  Project* p = find(id);
  if (!p) {
    return false;
  }

  p->title = updated.title;
  p->description = updated.description;
  p->githubLink = updated.githubLink;
  p->imageURL = updated.imageURL;

  save();
  return true;
}

bool ProjectStore::remove(uint32_t id) {
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
  nextId_ = doc["nextId"] | 1u;

  JsonArray arr = doc["projects"].as<JsonArray>();
  for (JsonObject obj : arr) {
    Project p;
    p.id = obj["id"];
    p.title = obj["title"] | "";
    p.description = obj["description"] | "";
    p.githubLink = obj["githubLink"] | "";
    p.imageURL = obj["imageURL"] | "";
    projects_.push_back(p);

    if (p.id >= nextId_) {
      nextId_ = p.id + 1;
    }
  }

  return true;
}

bool ProjectStore::save() const {
  JsonDocument doc;
  doc["nextId"] = nextId_;

  JsonArray arr = doc["projects"].to<JsonArray>();
  for (const Project& p : projects_) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = p.id;
    obj["title"] = p.title;
    obj["description"] = p.description;
    obj["githubLink"] = p.githubLink;
    obj["imageURL"] = p.imageURL;
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
