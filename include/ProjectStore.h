#pragma once

#include <Arduino.h>
#include <vector>

// Программный проект — единица хранения.
struct Project {
  uint32_t id = 0;
  String title;
  String description;
  String githubLink;
  String imageURL;
};

// Хранилище проектов. Данные живут в оперативной памяти,
// а после каждого изменения сериализуются в JSON-файл в LittleFS,
// поэтому переживают перезагрузку устройства.
class ProjectStore {
 public:
  // Монтирует файловую систему и загружает данные из файла.
  // Возвращает false, если LittleFS не смогла смонтироваться.
  bool begin(const char* path = "/projects.json");

  // Все проекты (в порядке добавления).
  const std::vector<Project>& all() const { return projects_; }

  const Project* find(uint32_t id) const;
  Project* find(uint32_t id);

  // Создаёт проект с автоинкрементным id и сохраняет хранилище.
  Project create(const String& title, const String& description,
                 const String& githubLink, const String& imageURL);

  // Полностью заменяет поля существующего проекта и сохраняет хранилище.
  bool update(uint32_t id, const Project& updated);

  // Удаляет проект по id и сохраняет хранилище.
  bool remove(uint32_t id);

 private:
  bool load();
  bool save() const;

  std::vector<Project> projects_;
  uint32_t nextId_ = 1;
  String path_;
};
