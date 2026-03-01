# ClassroomProxyClient

Бібліотека для ESP32 (Arduino), яка працює через локальний/хмарний proxy:
- Gemini (`/gemini`)
- Google Doc append (`/doc/append`)
- Google Drive read text/lines (`/drive/readText`, `/drive/readLines`)
- Google Sheets append/read (`/sheets/appendRow`, `/sheets/readRange`)

Цей документ написаний як покрокова інструкція для студентів у режимі:
**Codespaces (браузер на ПК) + Wokwi + локальний proxy.py у Codespaces**.

## 1. Що потрібно підготувати

1. GitHub акаунт.
2. Репозиторій з PlatformIO-проєктом (ESP32).
3. API ключ Gemini (`GEMINI_API_KEY`).
4. (Опційно) Google Apps Script Web App URL для Google Doc/Drive:
   - `GOOGLE_DOC_WEBHOOK_URL`
   - `GOOGLE_DOC_WEBHOOK_TOKEN` (якщо ввімкнено в `Code.gs`)
5. Установлений Wokwi plugin у VS Code/Codespaces.

## 2. Підключення бібліотеки в PlatformIO

В `platformio.ini` додайте:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600

lib_deps =
  bblanchon/ArduinoJson @ ^6.21.5
  https://github.com/hmokrenko-del/ClassroomProxyClient.git#v0.1.3
```

Рекомендація: використовуйте **тег** (`#v0.1.3` або новіший), а не `main`.

## 3. Перший Build і автостворення файлів

1. Запустіть `PlatformIO: Build`.
2. На першій збірці бібліотека (bootstrap) створює, якщо файлів немає:
   - `include/classroom_config.h`
   - `.env.example`
   - `.env`

Якщо автостворення не спрацювало:
1. Скопіюйте вручну:
   - `templates/classroom_config.example.h` -> `include/classroom_config.h`
   - `templates/.env.example` -> `.env.example`
   - `.env.example` -> `.env`

## 4. Налаштування `include/classroom_config.h`

Для Codespaces + Wokwi використовуйте:

```cpp
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""

#define GEMINI_PROXY_HOST "host.wokwi.internal"
#define GEMINI_PROXY_PORT 8080
#define GEMINI_PROXY_PATH "/gemini"

#define GEMINI_MODEL "gemini-2.5-flash"
#define GEMINI_CLIENT_NAME "student-01"
```

`host.wokwi.internal` означає: ESP32 у Wokwi звертається до хоста, де працює `wokwigw` (у нашому випадку Codespace).

## 5. Налаштування `.env`

Мінімально:

```dotenv
GEMINI_API_KEY=PUT_YOUR_GEMINI_API_KEY_HERE
PROXY_HOST=0.0.0.0
PROXY_PORT=8080
PROXY_PATH=/gemini
PROXY_MODEL=gemini-2.5-flash
```

Для Google Doc/Drive:

```dotenv
GOOGLE_DOC_WEBHOOK_URL=https://script.google.com/macros/s/XXXXXXXX/exec
GOOGLE_DOC_WEBHOOK_TOKEN=my-secret-token
```

## 6. Налаштування `wokwi.toml` для Codespaces

`wokwi.toml` має містити:

```toml
[wokwi]
version = 1
firmware = '.pio/build/esp32dev/firmware.bin'
elf = '.pio/build/esp32dev/firmware.elf'

[net]
gateway = "wss://<YOUR-CODESPACE>-9011.app.github.dev"
```

Важливо:
1. Для Codespaces потрібен саме `wss://...`, не `ws://localhost:9011`.
2. URL береться з вкладки `Ports` для порту `9011`.

## 7. Запуск `wokwigw` у Codespaces

Відкрийте термінал A і виконайте:

```bash
go install github.com/wokwi/wokwigw/cmd/wokwigw@latest
$(go env GOPATH)/bin/wokwigw --listenPort 9011
```

Потім у вкладці `Ports`:
1. Переконайтесь, що порт `9011` з’явився.
2. Виставіть `Visibility = Public` (рекомендовано).
3. Скопіюйте URL `https://...-9011.app.github.dev`.
4. Вставте його в `wokwi.toml` як `wss://...`.

## 8. Запуск `proxy.py` у Codespaces

У терміналі B:

```bash
cd /workspaces/<your-project-folder>
set -a; source .env; set +a
python3 ./proxy.py --host 0.0.0.0 --port 8080 --path /gemini
```

Якщо `proxy.py` відсутній у проєкті, додайте його в корінь (поруч із `platformio.ini`).

## 9. Перевірка proxy перед запуском симуляції

У терміналі C:

```bash
curl http://127.0.0.1:8080/health
```

Очікувано:
- `"ok": true`
- `"apps_script_webhook_configured": true` (якщо задано webhook URL)
- `"sheets_append_row_path": "/sheets/appendRow"`
- `"sheets_read_range_path": "/sheets/readRange"`

Тест запису в Google Doc:

```bash
curl -X POST http://127.0.0.1:8080/doc/append \
  -H "Content-Type: application/json" \
  -d '{"text":"test via proxy","device":"student","session":"manual"}'
```

## 10. Запуск прошивки в Wokwi

1. `PlatformIO: Build`
2. Запустіть `Wokwi Simulator`
3. Перевірте у логах:
   - підключення до Wi-Fi (`Wokwi-GUEST`)
   - запити до proxy (`/gemini`, `/doc/append`, `/drive/readText`, `/drive/readLines`)

## 11. Типові помилки і рішення

1. `Failed to connect to IoT Gateway at wss://...`
   - `wokwigw` не запущений
   - неправильний URL у `wokwi.toml`
   - порт `9011` не `Public`

2. `python3: can't open file ... proxy.py`
   - у проєкті немає `proxy.py` в корені

3. `HTTP proxy error: connection refused`
   - proxy не запущений або не на порту `8080`

4. `Missing prompt` при webhook-тесті
   - старий deployment Apps Script
   - потрібно оновити Web App deployment і перевірити URL `/exec`

5. Компіляція не бачить `classroom_config.h`
   - перевірте, чи файл існує в `include/classroom_config.h`
   - якщо авто-bootstrap не спрацював, створіть файл вручну з шаблону

## 12. Безпека

1. Не комітьте реальні ключі в Git:
   - `GEMINI_API_KEY`
   - `GOOGLE_DOC_WEBHOOK_TOKEN`
2. Якщо ключ випадково опублікували, одразу перевипустіть його.
3. Зберігайте ключі в `.env`, а не в коді.

## 13. Короткий чекліст (для лабораторної)

1. Підключив бібліотеку в `platformio.ini` (через тег).
2. Заповнив `include/classroom_config.h`.
3. Заповнив `.env`.
4. Запустив `wokwigw --listenPort 9011`.
5. Оновив `wokwi.toml` на `wss://...-9011.app.github.dev`.
6. Запустив `proxy.py`.
7. Перевірив `curl http://127.0.0.1:8080/health`.
8. Зробив `Build`.
9. Запустив Wokwi.
10. Переконався, що дані пишуться в Google Doc/читаються з Drive.

## 14. Налаштування Proxy на ПК (Windows)

Цей розділ для сценарію, коли студент запускає proxy.py локально на своєму ПК (не в Codespaces).

1. Відкрийте **PowerShell (вікно №1)** і перейдіть у папку проєкту:

```powershell
cd E:\<your-project-folder>
```

2. Переконайтесь, що в корені проєкту є `proxy.py` (поруч із `platformio.ini`).

3. Створіть `.env` (або перевірте, що він вже є) і заповніть мінімум:

```dotenv
GEMINI_API_KEY=PUT_YOUR_GEMINI_API_KEY_HERE
PROXY_HOST=0.0.0.0
PROXY_PORT=8080
PROXY_PATH=/gemini
PROXY_MODEL=gemini-2.5-flash
```

Для Google Doc/Drive (опційно):

```dotenv
GOOGLE_DOC_WEBHOOK_URL=https://script.google.com/macros/s/XXXXXXXX/exec
GOOGLE_DOC_WEBHOOK_TOKEN=my-secret-token
```

4. Запустіть proxy у **вікні №1**:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\start_proxy.ps1
```

`start_proxy.ps1` рекомендований спосіб запуску: скрипт підвантажує `.env` і прибирає старі процеси `proxy.py` на тому ж порту, щоб не було конфліктів.

Якщо `tools/start_proxy.ps1` відсутній, запустіть proxy напряму.
Для Windows краще використовувати `py -3` (а не `python`), щоб обійти проблему з Microsoft Store alias:

```powershell
$env:GEMINI_API_KEY="PUT_YOUR_GEMINI_API_KEY_HERE"
py -3 .\proxy.py --host 0.0.0.0 --port 8080 --path /gemini
```

5. Відкрийте **PowerShell (вікно №2)** і перевірте health endpoint:

```powershell
Invoke-RestMethod http://127.0.0.1:8080/health
```

Очікувано:
- `ok = True`
- `apps_script_webhook_configured = True` (якщо в `.env` заданий webhook URL)

6. Відкрийте **PowerShell (вікно №3)** і запустіть локальний Wokwi gateway:

```powershell
.\wokwigw.exe
```

7. У `wokwi.toml` використовуйте локальний gateway:

```toml
[net]
gateway = "ws://localhost:9011"
```

8. У `include/classroom_config.h` для сценарію ПК + Wokwi:

```cpp
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define GEMINI_PROXY_HOST "host.wokwi.internal"
#define GEMINI_PROXY_PORT 8080
#define GEMINI_PROXY_PATH "/gemini"
```

9. У VS Code: `PlatformIO: Build`, потім перезапустіть `Wokwi Simulator`.

10. Швидка діагностика:
- `HTTP proxy error: connection refused` -> proxy не запущений або не слухає порт `8080`.
- `Failed to connect to IoT Gateway` -> не запущений `wokwigw.exe` або в `wokwi.toml` не `ws://localhost:9011`.
- `Missing prompt` у webhook тестах -> у запиті не передано потрібне поле (`text`/`prompt`) або старий deployment Apps Script.

## 15. Налаштування Google Apps Script (`script.google.com`)

Це налаштування робиться один раз для проєкту, після чого proxy може:
- писати в Google Docs;
- читати текст із Google Drive;
- писати/читати дані в Google Sheets.

### 15.1 Підготуйте Google файли

1. Створіть Google Doc (для логів/тексту).
2. Створіть Google Sheet (для табличних даних, опційно).
3. Скопіюйте їхні ID з URL:
   - Doc ID: `https://docs.google.com/document/d/<DOC_ID>/edit`
   - Spreadsheet ID: `https://docs.google.com/spreadsheets/d/<SPREADSHEET_ID>/edit`

### 15.2 Створіть Apps Script Web App

1. Відкрийте `https://script.google.com/`.
2. Натисніть `Новий проєкт`.
3. Відкрийте файл `google_apps_script/Code.gs` у цьому репозиторії.
4. Замініть вміст `Code.gs` у Apps Script на код із репозиторію.
5. У верхній частині коду задайте:
   - `DOC_ID` = ID вашого Google Doc (документ за замовчуванням);
   - `WEBHOOK_TOKEN` = ваш секретний токен (або порожній рядок, якщо без токена).
6. Збережіть проєкт (`Ctrl+S`).

### 15.3 Розгортання (Deploy)

1. Натисніть `Розгорнути` -> `Нове розгортання`.
2. Тип: `Вебпрограма`.
3. Параметри:
   - `Виконувати як`: **Я (Me)**.
   - `Доступ`: **Усі, хто має посилання (Anyone with the link)**.
4. Натисніть `Розгорнути`.
5. Підтвердьте дозволи Google (доступ до Docs/Drive/Sheets).
6. Скопіюйте URL вебпрограми, який закінчується на `/exec`.

### 15.3.1 Важливо для Google Sheets: oauth scopes

Щоб працювали `sheets_append_row` і `sheets_read_range`, у проєкті Apps Script мають бути дозволи `spreadsheets`.

1. Відкрийте `Налаштування проєкту` (Project Settings).
2. Увімкніть `Показати файл маніфесту appsscript.json`.
3. У `appsscript.json` переконайтесь, що є:

```json
"oauthScopes": [
  "https://www.googleapis.com/auth/documents",
  "https://www.googleapis.com/auth/drive",
  "https://www.googleapis.com/auth/spreadsheets"
]
```

4. Збережіть файл.

### 15.3.2 Примусова авторизація Sheets

Після додавання scope виконайте один раз тестову функцію в `Code.gs`:

```javascript
function authSheets() {
  SpreadsheetApp.openById("PUT_SPREADSHEET_ID_HERE").getName();
}
```

Далі:
1. Виберіть `authSheets` у списку функцій біля кнопки `Запустити`.
2. Натисніть `Запустити` і підтвердіть дозволи Google.
3. Після цього обов'язково оновіть Web App deployment (`Керувати розгортаннями` -> `Редагувати` -> `Розгорнути`).

### 15.4 Прив’яжіть Web App до proxy

У `.env` заповніть:

```dotenv
GOOGLE_DOC_WEBHOOK_URL=https://script.google.com/macros/s/XXXXXXXXXXXX/exec
GOOGLE_DOC_WEBHOOK_TOKEN=my-secret-token
```

Правило:
- `GOOGLE_DOC_WEBHOOK_TOKEN` має збігатися зі значенням `WEBHOOK_TOKEN` у `Code.gs`.
- якщо `WEBHOOK_TOKEN=""`, тоді змінну `GOOGLE_DOC_WEBHOOK_TOKEN` можна не задавати.

Після зміни `.env` перезапустіть proxy.

### 15.5 Швидка перевірка: важливо який термінал використовується

Перед запуском тестів перевірте shell:
- якщо у терміналі запрошення виду `PS C:\...>` або `PS E:\...>` -> це **PowerShell**, використовуйте `Invoke-RestMethod`;
- якщо у терміналі запрошення виду `user@...$` (або просто `$`) -> це **bash** (типово у Codespaces), використовуйте `curl`.

Якщо запустити PowerShell-команди в bash, будуть помилки `command not found`.

### 15.5.1 Перевірка з ПК (PowerShell)

Перевірка прямого виклику Web App:

```powershell
$webhook = "https://script.google.com/macros/s/XXXXXXXXXXXX/exec"
$body = @{
  action = "doc_append_text"
  text   = "test from powershell"
  token  = "my-secret-token"
} | ConvertTo-Json -Compress
Invoke-RestMethod -Method Post -Uri $webhook -ContentType "application/json" -Body $body
```

Очікувано: відповідь з `ok = true`.

Перевірка запису в Google Sheets (прямо у Web App):

```powershell
$webhook = "https://script.google.com/macros/s/XXXXXXXXXXXX/exec"
$body = @{
  action = "sheets_append_row"
  spreadsheetId = "PUT_SPREADSHEET_ID_HERE"
  sheetName = "Sheet1"
  values = @("2026-03-01T12:00:00Z","student-01","23.7","81")
  token = "my-secret-token"
} | ConvertTo-Json -Depth 5 -Compress
Invoke-RestMethod -Method Post -Uri $webhook -ContentType "application/json" -Body $body
```

Перевірка читання з Google Sheets (прямо у Web App):

```powershell
$webhook = "https://script.google.com/macros/s/XXXXXXXXXXXX/exec"
$body = @{
  action = "sheets_read_range"
  spreadsheetId = "PUT_SPREADSHEET_ID_HERE"
  sheetName = "Sheet1"
  range = "A1:D20"
  token = "my-secret-token"
} | ConvertTo-Json -Depth 5 -Compress
Invoke-RestMethod -Method Post -Uri $webhook -ContentType "application/json" -Body $body
```

Перевірка через proxy:

```powershell
Invoke-RestMethod http://127.0.0.1:8080/health
```

Очікувано: `apps_script_webhook_configured = True`.

Перевірка через proxy (Sheets):

```powershell
$body = @{
  spreadsheetId = "PUT_SPREADSHEET_ID_HERE"
  sheetName = "Sheet1"
  values = @("2026-03-01T12:00:00Z","student-01","23.7","81")
} | ConvertTo-Json -Depth 5 -Compress
Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:8080/sheets/appendRow" -ContentType "application/json" -Body $body
```

### 15.5.2 Перевірка у Codespaces (bash)

Перевірка через proxy (health):

```bash
curl -s http://127.0.0.1:8080/health
```

Запис у Google Sheets через proxy:

```bash
curl -s -X POST http://127.0.0.1:8080/sheets/appendRow \
  -H "Content-Type: application/json" \
  -d '{"spreadsheetId":"PUT_SPREADSHEET_ID_HERE","sheetName":"Sheet1","values":["2026-03-01T12:00:00Z","student-01","23.7","81"]}'
```

Читання з Google Sheets через proxy:

```bash
curl -s -X POST http://127.0.0.1:8080/sheets/readRange \
  -H "Content-Type: application/json" \
  -d '{"spreadsheetId":"PUT_SPREADSHEET_ID_HERE","sheetName":"Sheet1","range":"A1:D20"}'
```

Прямий виклик Web App (без proxy) у bash:

```bash
WEBHOOK="https://script.google.com/macros/s/XXXXXXXXXXXX/exec"
curl -s -X POST "$WEBHOOK" \
  -H "Content-Type: application/json" \
  -d '{"action":"sheets_append_row","spreadsheetId":"PUT_SPREADSHEET_ID_HERE","sheetName":"Sheet1","values":["2026-03-01T12:00:00Z","student-01","23.7","81"],"token":"my-secret-token"}'
```

### 15.6 Важливо після редагування `Code.gs`

Після будь-якої зміни коду Apps Script потрібно оновити deployment:

1. `Розгорнути` -> `Керувати розгортаннями`.
2. Виберіть поточний Web App.
3. `Редагувати` -> `Розгорнути`.

Якщо цього не зробити, проксі працюватиме зі старою версією скрипта.

Ознака, що deployment застарілий або неавторизований:
- `Unknown action: sheets_append_row`
- `Exception: У вас немає дозволу викликати функцію SpreadsheetApp.openById`

### 15.7 Кілька документів і таблиць (для студентів)

- `DOC_ID` у `Code.gs` використовується як документ за замовчуванням.
- Для запису в інший документ передавайте `docId` у запиті (або через метод бібліотеки з параметром `docId`).
- Для таблиць завжди можна працювати з різними файлами через `spreadsheetId` + `sheetName`.
- Це дозволяє одній лабораторній групі працювати з кількома Docs/Sheets без зміни самого proxy.
