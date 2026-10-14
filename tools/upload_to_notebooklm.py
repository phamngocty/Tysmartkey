"""Script to upload session handoff document to NotebookLM.
Notebook ID: c7bfc20f-400d-4a5b-9846-e06f583af905
"""

import sys
import os
from pathlib import Path
from notebooklm_mcp.api_client import NotebookLMClient
from notebooklm_mcp.auth import load_cached_tokens

if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")

NOTEBOOK_ID = "c7bfc20f-400d-4a5b-9846-e06f583af905"
DOC_PATH = Path(r"c:\Users\phamn\Documents\PlatformIO\Tysmartkey\docs\handoff\2026-10-11-airtag-findmy-session-handoff.md")
TITLE = "Báo Cáo Bàn Giao: AirTag Find My, Web NAS TYMAP & Khóa fm.hs2t.com (11/10/2026)"

def upload():
    cached = load_cached_tokens()
    if not cached:
        print("ERROR: Không tìm thấy cached tokens. Vui lòng chạy 'python -m notebooklm_mcp.auth_cli' trước.")
        return False

    client = NotebookLMClient(cookies=cached.cookies, csrf_token=cached.csrf_token, session_id=cached.session_id)
    
    if not DOC_PATH.exists():
        print(f"ERROR: Không tìm thấy file {DOC_PATH}")
        return False

    with open(DOC_PATH, "r", encoding="utf-8") as f:
        content = f.read()

    print(f"Đang tải lên NotebookLM ({NOTEBOOK_ID}): '{TITLE}'...")
    try:
        res = client.add_text_source(NOTEBOOK_ID, text=content, title=TITLE)
        if res:
            print("ĐÃ TẢI LÊN THÀNH CÔNG VÀO NOTEBOOKLM!")
            return True
        else:
            print("Tải lên thất bại (không có phản hồi từ máy chủ).")
            return False
    except Exception as e:
        print(f"LỖI: {e}")
        return False

if __name__ == "__main__":
    upload()
