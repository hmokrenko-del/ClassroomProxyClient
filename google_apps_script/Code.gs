// Google Apps Script Web App to append Gemini chat logs into a Google Doc.
//
// Setup:
// 1) Create a Google Doc on Google Drive and copy its ID from the URL.
// 2) Paste this script into https://script.google.com and set DOC_ID.
// 3) Deploy as Web App (Execute as: Me, Access: Anyone with the link).
// 4) Put the Web App URL into GOOGLE_DOC_WEBHOOK_URL for proxy.py.

const DOC_ID = "1DT2IBHgeKhtu4fjy7hr1zZqXzsZZt7viBHjOS69CXnk";
const WEBHOOK_TOKEN = "my-secret-token"; // Optional shared secret. Leave empty to disable token check.

function doPost(e) {
  try {
    if (!e || !e.postData || !e.postData.contents) {
      return jsonResponse_({ ok: false, error: "Missing POST body" });
    }

    const payload = JSON.parse(e.postData.contents);

    if (WEBHOOK_TOKEN && payload.token !== WEBHOOK_TOKEN) {
      return jsonResponse_({ ok: false, error: "Unauthorized" });
    }

    const action = toText_(payload.action) || "log_chat";

    if (action === "log_chat") {
      return handleChatLog_(payload);
    }
    if (action === "doc_append_text") {
      return handleDocAppendText_(payload);
    }
    if (action === "drive_read_text") {
      return handleDriveReadText_(payload);
    }
    if (action === "drive_read_lines") {
      return handleDriveReadLines_(payload);
    }
    if (action === "sheets_append_row") {
      return handleSheetsAppendRow_(payload);
    }
    if (action === "sheets_read_range") {
      return handleSheetsReadRange_(payload);
    }

    return jsonResponse_({ ok: false, error: "Unknown action: " + action });
  } catch (err) {
    return jsonResponse_({ ok: false, error: String(err) });
  }
}

function handleChatLog_(payload) {
  const defaultDocId = requireDefaultDocId_();
  if (!defaultDocId.ok) return defaultDocId.response;

  const prompt = toText_(payload.prompt);
  const answer = toText_(payload.answer);
  const error = toText_(payload.error);
  const status = toText_(payload.status) || "unknown";
  const model = toText_(payload.model) || "unknown";
  const device = toText_(payload.device) || "esp32";
  const session = toText_(payload.session);
  const clientIp = toText_(payload.client_ip);
  const ts = toText_(payload.timestamp_utc) || new Date().toISOString();

  if (!prompt) {
    return jsonResponse_({ ok: false, error: "Missing prompt" });
  }

  const doc = DocumentApp.openById(DOC_ID);
  const body = doc.getBody();

  const headerLine =
    "[" + ts + "] " +
    "device=" + device +
    (session ? " session=" + session : "") +
    " model=" + model +
    " status=" + status +
    (clientIp ? " ip=" + clientIp : "");

  body.appendParagraph(headerLine)
    .setHeading(DocumentApp.ParagraphHeading.HEADING3);
  body.appendParagraph("PROMPT:")
    .setBold(true);
  body.appendParagraph(prompt);

  if (status === "ok") {
    body.appendParagraph("ANSWER:")
      .setBold(true);
    body.appendParagraph(answer || "(empty)");
  } else {
    body.appendParagraph("ERROR:")
      .setBold(true);
    body.appendParagraph(error || "(unknown)");
    if (answer) {
      body.appendParagraph("ANSWER (partial):")
        .setBold(true);
      body.appendParagraph(answer);
    }
  }

  body.appendParagraph("--------------------------------------------------");
  doc.saveAndClose();

  return jsonResponse_({ ok: true, action: "log_chat", docId: DOC_ID });
}

function handleDocAppendText_(payload) {
  const targetDocId = toText_(payload.docId) || DOC_ID;
  if (!targetDocId || targetDocId === "PUT_GOOGLE_DOC_ID_HERE") {
    return jsonResponse_({ ok: false, error: "DOC_ID is not configured" });
  }

  const text = toText_(payload.text);
  if (!text) {
    return jsonResponse_({ ok: false, error: "Missing text" });
  }

  const prefix = toText_(payload.prefix); // optional
  const appendText = prefix ? (prefix + text) : text;

  const doc = DocumentApp.openById(targetDocId);
  doc.getBody().appendParagraph(appendText);
  doc.saveAndClose();

  return jsonResponse_({
    ok: true,
    action: "doc_append_text",
    docId: targetDocId,
    appendedChars: appendText.length
  });
}

function handleDriveReadText_(payload) {
  const fileId = toText_(payload.fileId);
  if (!fileId) {
    return jsonResponse_({ ok: false, error: "Missing fileId" });
  }

  const fileData = readDriveTextFile_(fileId);
  if (!fileData.ok) return jsonResponse_(fileData);

  const lines = splitLines_(fileData.text);
  return jsonResponse_({
    ok: true,
    action: "drive_read_text",
    fileId: fileId,
    fileName: fileData.fileName,
    mimeType: fileData.mimeType,
    lineCount: lines.length,
    text: fileData.text
  });
}

function handleDriveReadLines_(payload) {
  const fileId = toText_(payload.fileId);
  if (!fileId) {
    return jsonResponse_({ ok: false, error: "Missing fileId" });
  }

  const fileData = readDriveTextFile_(fileId);
  if (!fileData.ok) return jsonResponse_(fileData);

  const allLines = splitLines_(fileData.text);
  const total = allLines.length;

  let fromLine = parseOptionalInt_(payload.fromLine);
  let toLine = parseOptionalInt_(payload.toLine);

  if (fromLine !== null && fromLine < 1) {
    return jsonResponse_({ ok: false, error: "fromLine must be >= 1 (1-based indexing)" });
  }
  if (toLine !== null && toLine < 1) {
    return jsonResponse_({ ok: false, error: "toLine must be >= 1 (1-based indexing)" });
  }

  if (total === 0) {
    return jsonResponse_({
      ok: true,
      action: "drive_read_lines",
      fileId: fileId,
      fileName: fileData.fileName,
      mimeType: fileData.mimeType,
      lineCount: 0,
      fromLine: 0,
      toLine: 0,
      lines: [],
      text: ""
    });
  }

  if (fromLine === null) fromLine = 1;
  if (toLine === null) toLine = total;

  if (fromLine > total) {
    return jsonResponse_({ ok: false, error: "fromLine is greater than total line count" });
  }
  if (toLine < fromLine) {
    return jsonResponse_({ ok: false, error: "toLine must be >= fromLine" });
  }
  if (toLine > total) {
    toLine = total;
  }

  const selected = allLines.slice(fromLine - 1, toLine);
  return jsonResponse_({
    ok: true,
    action: "drive_read_lines",
    fileId: fileId,
    fileName: fileData.fileName,
    mimeType: fileData.mimeType,
    lineCount: total,
    fromLine: fromLine,
    toLine: toLine,
    lines: selected,
    text: selected.join("\n")
  });
}

function handleSheetsAppendRow_(payload) {
  const spreadsheetId = toText_(payload.spreadsheetId);
  const sheetName = toText_(payload.sheetName);
  const values = payload.values;

  if (!spreadsheetId) {
    return jsonResponse_({ ok: false, error: "Missing spreadsheetId" });
  }
  if (!sheetName) {
    return jsonResponse_({ ok: false, error: "Missing sheetName" });
  }
  if (!Array.isArray(values) || values.length === 0) {
    return jsonResponse_({ ok: false, error: "Missing values array" });
  }

  const ss = SpreadsheetApp.openById(spreadsheetId);
  const sheet = ss.getSheetByName(sheetName);
  if (!sheet) {
    return jsonResponse_({ ok: false, error: "Sheet not found: " + sheetName });
  }

  const row = values.map(function(v) { return toText_(v); });
  sheet.appendRow(row);
  const rowNumber = sheet.getLastRow();

  return jsonResponse_({
    ok: true,
    action: "sheets_append_row",
    spreadsheetId: spreadsheetId,
    sheetName: sheetName,
    rowNumber: rowNumber,
    updatedRange: sheetName + "!A" + rowNumber
  });
}

function handleSheetsReadRange_(payload) {
  const spreadsheetId = toText_(payload.spreadsheetId);
  const sheetName = toText_(payload.sheetName);
  const rangeA1 = toText_(payload.range);

  if (!spreadsheetId) {
    return jsonResponse_({ ok: false, error: "Missing spreadsheetId" });
  }
  if (!sheetName) {
    return jsonResponse_({ ok: false, error: "Missing sheetName" });
  }
  if (!rangeA1) {
    return jsonResponse_({ ok: false, error: "Missing range" });
  }

  const ss = SpreadsheetApp.openById(spreadsheetId);
  const sheet = ss.getSheetByName(sheetName);
  if (!sheet) {
    return jsonResponse_({ ok: false, error: "Sheet not found: " + sheetName });
  }

  const values = sheet.getRange(rangeA1).getDisplayValues();
  const rowCount = values.length;
  const columnCount = rowCount > 0 ? values[0].length : 0;

  return jsonResponse_({
    ok: true,
    action: "sheets_read_range",
    spreadsheetId: spreadsheetId,
    sheetName: sheetName,
    range: rangeA1,
    rowCount: rowCount,
    columnCount: columnCount,
    values: values,
    text: matrixToText_(values)
  });
}

function requireDefaultDocId_() {
  if (!DOC_ID || DOC_ID === "PUT_GOOGLE_DOC_ID_HERE") {
    return { ok: false, response: jsonResponse_({ ok: false, error: "DOC_ID is not configured" }) };
  }
  return { ok: true, response: null };
}

function readDriveTextFile_(fileId) {
  const file = DriveApp.getFileById(fileId);
  const mimeType = file.getMimeType();
  const fileName = file.getName();

  if (mimeType === MimeType.GOOGLE_DOCS) {
    const text = DocumentApp.openById(fileId).getBody().getText();
    return { ok: true, fileName: fileName, mimeType: mimeType, text: text };
  }

  if (mimeType === MimeType.GOOGLE_SHEETS) {
    return { ok: false, error: "Google Sheets is not supported by drive_read_text yet. Use a text file or Google Doc." };
  }

  // For txt/csv/json and other text-like files stored on Drive.
  const text = file.getBlob().getDataAsString();
  return { ok: true, fileName: fileName, mimeType: mimeType, text: text };
}

function splitLines_(text) {
  const s = toText_(text);
  if (!s) return [];
  return s.replace(/\r\n/g, "\n").replace(/\r/g, "\n").split("\n");
}

function matrixToText_(matrix) {
  if (!Array.isArray(matrix) || matrix.length === 0) return "";
  return matrix
    .map(function(row) {
      if (!Array.isArray(row)) return "";
      return row.map(function(cell) { return toText_(cell); }).join("\t");
    })
    .join("\n");
}

function parseOptionalInt_(value) {
  if (value === null || value === undefined || value === "") return null;
  const n = Number(value);
  if (!isFinite(n) || Math.floor(n) !== n) {
    throw new Error("Line indexes must be integers");
  }
  return n;
}

function jsonResponse_(obj) {
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

function toText_(value) {
  if (value === null || value === undefined) return "";
  return String(value);
}
