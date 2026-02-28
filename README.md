# ClassroomProxyClient (Local Dev Version)

ESP32 Arduino library for classroom proxy workflows:
- Gemini prompts via local/shared proxy
- serial console helper for Wokwi / USB monitor
- Google Docs / Google Drive wrappers via proxy (`/doc/append`, `/drive/readText`, `/drive/readLines`)

## Usage in this repo (local development)

PlatformIO automatically picks up libraries from the `lib/` folder.

## Usage from GitHub (after publishing)

Add to `platformio.ini`:

```ini
lib_deps =
  bblanchon/ArduinoJson @ ^6.21.5
  https://github.com/YOUR_ORG/ClassroomProxyClient.git#v0.1.0
```

For classroom stability, pin a tag (`#v0.1.0`) instead of using `main`.

## Student config

Copy:

- `include/classroom_config.example.h` -> `include/classroom_config.h`
- `.env.example` -> `.env`

Then:

- fill Wi-Fi + proxy host in `include/classroom_config.h`
- fill Gemini/API/webhook values in `.env`
- run `tools/start_proxy.ps1`

## Serial commands (built-in demo console)

- `/demo` - send a Gemini test prompt
- `/docappend <text>` - append a line/paragraph to the default Google Doc (Apps Script webhook)
- `/drivetext <fileId>` - read full text from a Google Doc or text file on Drive
- `/drivelines <fileId> [from] [to]` - read all or a line range (1-based) from a Google Doc/text file

Notes:
- `fileId` is the Google Drive file ID from the file URL.
- `Google Sheets` are not supported by `drive_read_text` yet (use Google Doc or a text file for now).
