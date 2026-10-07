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
| `PUT`              | `/projects`   | Создать проект                             | 201   |
| `PUT`              | `/projects/:id` | Обновить проект (можно частично)         | 200   |
| `DELETE`           | `/projects/:id` | Удалить проект                           | 204   |

Ошибки: `400` — невалидное тело, `404` — проект/путь не найден,
`405` — метод не поддерживается, `409` — id уже занят при создании.
Формат ошибки: `{"error": "..."}`.

### Модель проекта

```ts
type Project = {
  id: string;
  title: string;
  tags: string[];
  description: string[];
  githubLink: string;
  imageURL: string;
};
```

- `id` — строка. При создании его можно не передавать: тогда сервер сгенерирует
  случайный. Если передать свой `id` и он уже занят — вернётся `409`.
- При создании обязателен только `title`.
- При обновлении передаются только изменяемые поля; для массивов `tags` и
  `description` присланное значение заменяет массив целиком, отсутствующие поля
  сохраняются.
- `id` через тело запроса не меняется — он берётся из URL.

### Пример объекта

```json
{
  "id": "a1b2c3d4e5f60718",
  "title": "Smart Home",
  "tags": ["esp32", "iot", "platformio"],
  "description": ["Умный дом на ESP32.", "Датчики + MQTT."],
  "githubLink": "https://github.com/me/smart-home",
  "imageURL": "https://example.com/preview.png"
}
```

## Примеры (curl)

```sh
# Список проектов
curl http://portfolio-server.local/projects

# Создать проект (id сгенерируется автоматически)
curl -X PUT http://portfolio-server.local/projects \
  -H "Content-Type: application/json" \
  -d '{"title":"Smart Home",
       "tags":["esp32","iot"],
       "description":["Умный дом на ESP32.","Датчики + MQTT."],
       "githubLink":"https://github.com/me/smart-home",
       "imageURL":"https://example.com/preview.png"}'

# Обновить только теги и описание проекта
curl -X PUT http://portfolio-server.local/projects/a1b2c3d4e5f60718 \
  -H "Content-Type: application/json" \
  -d '{"tags":["esp32","home-assistant"],
       "description":["Переписал на Home Assistant."]}'

# Удалить проект
curl -X DELETE http://portfolio-server.local/projects/a1b2c3d4e5f60718
```
