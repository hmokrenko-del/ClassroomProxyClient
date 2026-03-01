#!/usr/bin/env python3
import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def extract_gemini_text(payload: dict) -> str:
    candidates = payload.get("candidates") or []
    if not candidates:
        return ""
    content = (candidates[0] or {}).get("content") or {}
    parts = content.get("parts") or []
    texts = []
    for part in parts:
        if isinstance(part, dict) and isinstance(part.get("text"), str):
            texts.append(part["text"])
    return "\n".join(texts).strip()


class GeminiProxyHandler(BaseHTTPRequestHandler):
    api_key = ""
    default_model = "gemini-2.5-flash"
    endpoint_path = "/gemini"
    timeout = 45
    log_webhook_url = ""
    log_webhook_token = ""
    log_webhook_timeout = 15
    local_log_jsonl = ""
    docs_append_path = "/doc/append"
    drive_read_text_path = "/drive/readText"
    drive_read_lines_path = "/drive/readLines"
    sheets_append_row_path = "/sheets/appendRow"
    sheets_read_range_path = "/sheets/readRange"

    def _safe_str(self, value: object, max_len: int = 256) -> str:
        if not isinstance(value, str):
            return ""
        return value.strip()[:max_len]

    def _append_local_jsonl(self, event: dict) -> None:
        if not self.local_log_jsonl:
            return
        try:
            with open(self.local_log_jsonl, "a", encoding="utf-8") as f:
                f.write(json.dumps(event, ensure_ascii=False) + "\n")
        except Exception as exc:  # noqa: BLE001
            print(f"[proxy] Local log write failed: {exc}", file=sys.stderr)

    def _read_json_request_body(self) -> dict | None:
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._write_json(400, {"error": "Invalid Content-Length"})
            return None

        raw_body = self.rfile.read(content_length)
        try:
            body = json.loads(raw_body.decode("utf-8") or "{}")
        except json.JSONDecodeError as exc:
            self._write_json(400, {"error": f"Invalid JSON: {exc}"})
            return None

        if not isinstance(body, dict):
            self._write_json(400, {"error": "JSON body must be an object"})
            return None

        return body

    def _call_apps_script(self, payload: dict) -> tuple[int, dict | None, str]:
        if not self.log_webhook_url:
            return 500, None, "GOOGLE_DOC_WEBHOOK_URL is not configured in proxy environment."

        outbound_payload = dict(payload)
        if self.log_webhook_token:
            outbound_payload["token"] = self.log_webhook_token

        req = urllib.request.Request(
            self.log_webhook_url,
            data=json.dumps(outbound_payload).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )

        try:
            with urllib.request.urlopen(req, timeout=self.log_webhook_timeout) as resp:
                status = resp.getcode()
                raw = resp.read().decode("utf-8", errors="replace")
        except urllib.error.HTTPError as exc:
            status = exc.code or 502
            raw = exc.read().decode("utf-8", errors="replace")
            try:
                parsed = json.loads(raw)
                if isinstance(parsed, dict):
                    msg = parsed.get("error") or raw
                else:
                    msg = raw or str(exc)
            except json.JSONDecodeError:
                msg = raw or str(exc)
            return status, None, f"Apps Script HTTP error: {msg}"
        except Exception as exc:  # noqa: BLE001
            return 502, None, f"Apps Script connection error: {exc}"

        try:
            parsed = json.loads(raw or "{}")
        except json.JSONDecodeError as exc:
            preview = raw[:300] if raw else ""
            suffix = f" | Raw: {preview}" if preview else ""
            return 502, None, f"Apps Script invalid JSON: {exc}{suffix}"

        if not isinstance(parsed, dict):
            return 502, None, "Apps Script response is not a JSON object"

        if parsed.get("ok") is False:
            return status, parsed, str(parsed.get("error") or "Apps Script returned error")

        return status, parsed, ""

    def _handle_doc_append(self, request_body: dict) -> None:
        text = request_body.get("text")
        if not isinstance(text, str) or not text:
            self._write_json(400, {"error": "Field 'text' is required"})
            return

        payload = {
            "action": "doc_append_text",
            "text": text,
            "device": self._safe_str(request_body.get("device"), 120) or "esp32",
            "session": self._safe_str(request_body.get("session"), 120),
        }
        doc_id = self._safe_str(request_body.get("docId"), 256)
        if doc_id:
            payload["docId"] = doc_id

        status, parsed, error_msg = self._call_apps_script(payload)
        if error_msg:
            self._write_json(status if status else 502, {"error": error_msg})
            return

        resp = {"ok": True}
        if parsed:
            for key in ("docId", "appendedChars", "message"):
                if key in parsed:
                    resp[key] = parsed[key]
        self._write_json(200, resp)

    def _handle_drive_read_text(self, request_body: dict) -> None:
        file_id = self._safe_str(request_body.get("fileId"), 256)
        if not file_id:
            self._write_json(400, {"error": "Field 'fileId' is required"})
            return

        payload = {
            "action": "drive_read_text",
            "fileId": file_id,
            "device": self._safe_str(request_body.get("device"), 120) or "esp32",
            "session": self._safe_str(request_body.get("session"), 120),
        }

        status, parsed, error_msg = self._call_apps_script(payload)
        if error_msg:
            self._write_json(status if status else 502, {"error": error_msg})
            return
        if not parsed or not isinstance(parsed.get("text"), str):
            self._write_json(502, {"error": "Apps Script returned no 'text'"})
            return

        resp = {"text": parsed["text"]}
        for key in ("mimeType", "fileName", "lineCount"):
            if key in parsed:
                resp[key] = parsed[key]
        self._write_json(200, resp)

    def _handle_drive_read_lines(self, request_body: dict) -> None:
        file_id = self._safe_str(request_body.get("fileId"), 256)
        if not file_id:
            self._write_json(400, {"error": "Field 'fileId' is required"})
            return

        payload = {
            "action": "drive_read_lines",
            "fileId": file_id,
            "device": self._safe_str(request_body.get("device"), 120) or "esp32",
            "session": self._safe_str(request_body.get("session"), 120),
        }
        for key in ("fromLine", "toLine"):
            if key in request_body:
                value = request_body.get(key)
                if not isinstance(value, int):
                    self._write_json(400, {"error": f"Field '{key}' must be an integer"})
                    return
                payload[key] = value

        status, parsed, error_msg = self._call_apps_script(payload)
        if error_msg:
            self._write_json(status if status else 502, {"error": error_msg})
            return
        if not parsed:
            self._write_json(502, {"error": "Apps Script returned empty response"})
            return

        resp = {}
        if isinstance(parsed.get("text"), str):
            resp["text"] = parsed["text"]
        if isinstance(parsed.get("lines"), list):
            resp["lines"] = parsed["lines"]
        for key in ("fromLine", "toLine", "lineCount", "fileName", "mimeType"):
            if key in parsed:
                resp[key] = parsed[key]
        if "text" not in resp and "lines" not in resp:
            self._write_json(502, {"error": "Apps Script returned no 'text' or 'lines'"})
            return
        self._write_json(200, resp)

    def _handle_sheets_append_row(self, request_body: dict) -> None:
        spreadsheet_id = self._safe_str(request_body.get("spreadsheetId"), 256)
        sheet_name = self._safe_str(request_body.get("sheetName"), 128)
        values = request_body.get("values")

        if not spreadsheet_id:
            self._write_json(400, {"error": "Field 'spreadsheetId' is required"})
            return
        if not sheet_name:
            self._write_json(400, {"error": "Field 'sheetName' is required"})
            return
        if not isinstance(values, list) or not values:
            self._write_json(400, {"error": "Field 'values' must be a non-empty array"})
            return

        payload = {
            "action": "sheets_append_row",
            "spreadsheetId": spreadsheet_id,
            "sheetName": sheet_name,
            "values": [str(v) if v is not None else "" for v in values],
            "device": self._safe_str(request_body.get("device"), 120) or "esp32",
            "session": self._safe_str(request_body.get("session"), 120),
        }

        status, parsed, error_msg = self._call_apps_script(payload)
        if error_msg:
            self._write_json(status if status else 502, {"error": error_msg})
            return

        resp = {"ok": True}
        if parsed:
            for key in ("spreadsheetId", "sheetName", "rowNumber", "updatedRange"):
                if key in parsed:
                    resp[key] = parsed[key]
        self._write_json(200, resp)

    def _handle_sheets_read_range(self, request_body: dict) -> None:
        spreadsheet_id = self._safe_str(request_body.get("spreadsheetId"), 256)
        sheet_name = self._safe_str(request_body.get("sheetName"), 128)
        range_a1 = self._safe_str(request_body.get("range"), 128)

        if not spreadsheet_id:
            self._write_json(400, {"error": "Field 'spreadsheetId' is required"})
            return
        if not sheet_name:
            self._write_json(400, {"error": "Field 'sheetName' is required"})
            return
        if not range_a1:
            self._write_json(400, {"error": "Field 'range' is required"})
            return

        payload = {
            "action": "sheets_read_range",
            "spreadsheetId": spreadsheet_id,
            "sheetName": sheet_name,
            "range": range_a1,
            "device": self._safe_str(request_body.get("device"), 120) or "esp32",
            "session": self._safe_str(request_body.get("session"), 120),
        }

        status, parsed, error_msg = self._call_apps_script(payload)
        if error_msg:
            self._write_json(status if status else 502, {"error": error_msg})
            return
        if not parsed:
            self._write_json(502, {"error": "Apps Script returned empty response"})
            return

        resp = {}
        if isinstance(parsed.get("text"), str):
            resp["text"] = parsed["text"]
        if isinstance(parsed.get("values"), list):
            resp["values"] = parsed["values"]
        for key in ("spreadsheetId", "sheetName", "range", "rowCount", "columnCount"):
            if key in parsed:
                resp[key] = parsed[key]

        if "text" not in resp and "values" not in resp:
            self._write_json(502, {"error": "Apps Script returned no 'text' or 'values'"})
            return

        self._write_json(200, resp)

    def _post_log_webhook(self, event: dict) -> None:
        if not self.log_webhook_url:
            return

        payload = dict(event)
        if self.log_webhook_token:
            payload["token"] = self.log_webhook_token

        req = urllib.request.Request(
            self.log_webhook_url,
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=self.log_webhook_timeout) as resp:
                raw = resp.read().decode("utf-8", errors="replace")
                if raw:
                    try:
                        parsed = json.loads(raw)
                        if isinstance(parsed, dict) and parsed.get("ok") is False:
                            print(
                                f"[proxy] Google Doc webhook returned error: {parsed.get('error')}",
                                file=sys.stderr,
                            )
                    except json.JSONDecodeError:
                        pass
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="replace")
            print(
                f"[proxy] Google Doc webhook HTTP {exc.code}: {body[:300]}",
                file=sys.stderr,
            )
        except Exception as exc:  # noqa: BLE001
            print(f"[proxy] Google Doc webhook error: {exc}", file=sys.stderr)

    def _log_chat_event(
        self,
        *,
        device: str,
        session: str,
        model: str,
        prompt: str,
        answer: str,
        status: str,
        error: str,
    ) -> None:
        event = {
            "timestamp_utc": datetime.now(timezone.utc).isoformat(),
            "client_ip": self.client_address[0] if self.client_address else "",
            "device": device,
            "session": session,
            "model": model,
            "status": status,
            "prompt": prompt,
            "answer": answer,
            "error": error,
        }
        self._append_local_jsonl(event)
        self._post_log_webhook(event)

    def _write_json(self, status_code: int, body: dict) -> None:
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self) -> None:
        if self.path == "/health":
            self._write_json(
                200,
                {
                    "ok": True,
                    "service": "classroom-proxy",
                    "gemini_path": self.endpoint_path,
                    "docs_append_path": self.docs_append_path,
                    "drive_read_text_path": self.drive_read_text_path,
                    "drive_read_lines_path": self.drive_read_lines_path,
                    "sheets_append_row_path": self.sheets_append_row_path,
                    "sheets_read_range_path": self.sheets_read_range_path,
                    "apps_script_webhook_configured": bool(self.log_webhook_url),
                },
            )
            return
        self._write_json(404, {"error": "Use POST " + self.endpoint_path + " or GET /health"})

    def do_POST(self) -> None:
        request_body = self._read_json_request_body()
        if request_body is None:
            return

        if self.path == self.docs_append_path:
            self._handle_doc_append(request_body)
            return
        if self.path == self.drive_read_text_path:
            self._handle_drive_read_text(request_body)
            return
        if self.path == self.drive_read_lines_path:
            self._handle_drive_read_lines(request_body)
            return
        if self.path == self.sheets_append_row_path:
            self._handle_sheets_append_row(request_body)
            return
        if self.path == self.sheets_read_range_path:
            self._handle_sheets_read_range(request_body)
            return
        if self.path != self.endpoint_path:
            self._write_json(404, {"error": f"Use POST {self.endpoint_path}"})
            return

        prompt = request_body.get("prompt")
        model = request_body.get("model") or self.default_model
        device = self._safe_str(request_body.get("device"), 120) or "esp32"
        session = self._safe_str(request_body.get("session"), 120)
        if not isinstance(prompt, str) or not prompt.strip():
            self._write_json(400, {"error": "Field 'prompt' is required"})
            return
        if not isinstance(model, str) or not model.strip():
            self._write_json(400, {"error": "Field 'model' must be a string"})
            return

        gemini_url = (
            "https://generativelanguage.googleapis.com/v1beta/models/"
            f"{model}:generateContent"
        )
        gemini_payload = {
            "contents": [
                {
                    "parts": [
                        {
                            "text": prompt,
                        }
                    ]
                }
            ]
        }

        outbound = urllib.request.Request(
            gemini_url,
            data=json.dumps(gemini_payload).encode("utf-8"),
            headers={
                "Content-Type": "application/json",
                "x-goog-api-key": self.api_key,
            },
            method="POST",
        )

        try:
            with urllib.request.urlopen(outbound, timeout=self.timeout) as resp:
                status = resp.getcode()
                raw = resp.read().decode("utf-8", errors="replace")
        except urllib.error.HTTPError as exc:
            raw = exc.read().decode("utf-8", errors="replace")
            try:
                parsed = json.loads(raw)
                error_node = parsed.get("error") or {}
                msg = error_node.get("message") or raw
            except json.JSONDecodeError:
                msg = raw or str(exc)
            self._log_chat_event(
                device=device,
                session=session,
                model=model,
                prompt=prompt,
                answer="",
                status="error",
                error=f"Gemini HTTP error: {msg}",
            )
            self._write_json(exc.code or 502, {"error": f"Gemini HTTP error: {msg}"})
            return
        except Exception as exc:  # noqa: BLE001
            self._log_chat_event(
                device=device,
                session=session,
                model=model,
                prompt=prompt,
                answer="",
                status="error",
                error=f"Proxy connection error: {exc}",
            )
            self._write_json(502, {"error": f"Proxy connection error: {exc}"})
            return

        try:
            parsed = json.loads(raw)
        except json.JSONDecodeError as exc:
            self._log_chat_event(
                device=device,
                session=session,
                model=model,
                prompt=prompt,
                answer="",
                status="error",
                error=f"Invalid Gemini JSON: {exc}",
            )
            self._write_json(502, {"error": f"Invalid Gemini JSON: {exc}"})
            return

        error_node = parsed.get("error")
        if isinstance(error_node, dict):
            msg = error_node.get("message") or "Unknown Gemini API error"
            self._log_chat_event(
                device=device,
                session=session,
                model=model,
                prompt=prompt,
                answer="",
                status="error",
                error=f"Gemini API error: {msg}",
            )
            self._write_json(status if status else 502, {"error": f"Gemini API error: {msg}"})
            return

        answer = extract_gemini_text(parsed)
        if not answer:
            self._log_chat_event(
                device=device,
                session=session,
                model=model,
                prompt=prompt,
                answer="",
                status="error",
                error="Gemini returned no text",
            )
            self._write_json(502, {"error": "Gemini returned no text"})
            return

        self._log_chat_event(
            device=device,
            session=session,
            model=model,
            prompt=prompt,
            answer=answer,
            status="ok",
            error="",
        )
        self._write_json(200, {"answer": answer, "model": model})

    def log_message(self, fmt: str, *args) -> None:
        sys.stdout.write(f"[proxy] {self.address_string()} - {fmt % args}\n")
        sys.stdout.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description="Local Gemini proxy for Wokwi ESP32")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8080, help="Bind port (default: 8080)")
    parser.add_argument("--path", default="/gemini", help="HTTP path (default: /gemini)")
    parser.add_argument("--model", default="gemini-2.5-flash", help="Default Gemini model")
    parser.add_argument("--api-key", default=None, help="Gemini API key (or use GEMINI_API_KEY env)")
    parser.add_argument("--timeout", type=int, default=45, help="Outbound timeout in seconds")
    parser.add_argument(
        "--log-webhook-url",
        default=None,
        help="Google Apps Script Web App URL for chat logging (or GOOGLE_DOC_WEBHOOK_URL env)",
    )
    parser.add_argument(
        "--log-webhook-token",
        default=None,
        help="Optional shared token passed in JSON body to webhook (or GOOGLE_DOC_WEBHOOK_TOKEN env)",
    )
    parser.add_argument(
        "--log-webhook-timeout",
        type=int,
        default=15,
        help="Timeout for webhook logging requests in seconds",
    )
    parser.add_argument(
        "--log-jsonl",
        default=None,
        help="Optional local JSONL transcript backup file (or CHAT_LOG_JSONL env)",
    )
    args = parser.parse_args()

    api_key = (args.api_key or os.getenv("GEMINI_API_KEY") or "").strip()
    if not api_key:
        print("Missing API key. Set GEMINI_API_KEY env var or pass --api-key.", file=sys.stderr)
        return 2

    GeminiProxyHandler.api_key = api_key
    GeminiProxyHandler.default_model = args.model
    GeminiProxyHandler.endpoint_path = args.path
    GeminiProxyHandler.timeout = args.timeout
    GeminiProxyHandler.log_webhook_url = (
        args.log_webhook_url or os.getenv("GOOGLE_DOC_WEBHOOK_URL") or ""
    ).strip()
    GeminiProxyHandler.log_webhook_token = (
        args.log_webhook_token or os.getenv("GOOGLE_DOC_WEBHOOK_TOKEN") or ""
    ).strip()
    GeminiProxyHandler.log_webhook_timeout = args.log_webhook_timeout
    GeminiProxyHandler.local_log_jsonl = (
        args.log_jsonl or os.getenv("CHAT_LOG_JSONL") or ""
    ).strip()

    server = ThreadingHTTPServer((args.host, args.port), GeminiProxyHandler)
    print(f"[proxy] Listening on http://{args.host}:{args.port}{args.path}")
    print(f"[proxy] Default model: {args.model}")
    if GeminiProxyHandler.log_webhook_url:
        print(f"[proxy] Google Doc logging: enabled -> {GeminiProxyHandler.log_webhook_url}")
    else:
        print("[proxy] Google Doc logging: disabled")
    if GeminiProxyHandler.local_log_jsonl:
        print(f"[proxy] Local JSONL log: {GeminiProxyHandler.local_log_jsonl}")
    print("[proxy] Press Ctrl+C to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        print("[proxy] Stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
