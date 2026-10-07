# Portfolio Server

REST API сервер на ESP32-S3 (Freenove ESP32 S3 WROOM, Arduino framework),
хранящий информацию о программных проектах. Данные лежат в файле
`/projects.json` в LittleFS (флеш-память), поэтому переживают перезагрузку
и отключение питания.

## Быстрый старт

1. Впишите данные своей Wi-Fi сети в `include/wifi_credentials.h`.
2. Соберите и залейте прошивку:

   ```sh
   pio run -t upload
   pio device monitor   # увидеть IP-адрес и логи
   ```

3. Сервер доступен по адресу `http://portfolio-server.local`
   (или по IP из логов).

## API

Все ответы — JSON. CORS разрешён, так что API можно дёргать прямо из браузера.

| Метод              | Путь          | Описание                                   | Успех |
| ------------------ | ------------- | ------------------------------------------ | ----- |
| `GET`              | `/projects`   | Список всех проектов                       | 200   |
| `GET`              | `/projects/:id` | Один проект                              | 200   |
| `PUT`              | `/projects`   | Создать проект (id выдаёт сервер)          | 201   |
| `PUT`              | `/projects/:id` | Обновить проект (можно частично)         | 200   |
| `DELETE`           | `/projects/:id` | Удалить проект                           | 204   |

Ошибки: `400` — невалидное тело, `404` — проект/путь не найден,
`405` — метод не поддерживается. Формат ошибки: `{"error": "..."}`.

### Модель проекта

```json
{
  "id": 1,
  "title": "Smart Home",
  "description": "Умный дом на ESP32",
  "githubLink": "https://github.com/me/smart-home",
  "imageURL": "https://example.com/preview.png"
}
```

- При создании `id` можно не передавать (или передавать — он игнорируется,
  сервер выдаёт свой автоинкрементный).
- При создании обязателен только `title`.
- При обновлении передаются только изменяемые поля, остальные сохраняются.

## Примеры (curl)

```sh
# Список проектов
curl http://portfolio-server.local/projects

# Создать проект
curl -X PUT http://portfolio-server.local/projects \
  -H "Content-Type: application/json" \
  -d '{"title":"Smart Home","description":"Умный дом на ESP32",
       "githubLink":"https://github.com/me/smart-home",
       "imageURL":"https://example.com/preview.png"}'

# Обновить только описание проекта #1
curl -X PUT http://portfolio-server.local/projects/1 \
  -H "Content-Type: application/json" \
  -d '{"description":"Новое описание"}'

# Удалить проект #1
curl -X DELETE http://portfolio-server.local/projects/1
```
