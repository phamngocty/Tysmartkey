"""FastAPI Backend for Apple Find My (AirTag) & GraphHopper Routing.

Features:
- Apple ID Authentication with Anisette v3 server (Password + 2FA SMS/Trusted Device)
- Automated iCloud Find My Location Synchronization (via findmy library)
- NIST P-224 Keypair generation, export and BLE OTA delivery
- Local SQLite database storage for devices and real-time location reports
- Export live position to \\192.168.1.114\graphhopper-data\bike_live.json
"""

import os
import json
import sqlite3
import math
import asyncio
from pathlib import Path
from typing import Optional, Dict, Any, List
from datetime import datetime, timezone

from fastapi import FastAPI, HTTPException, Response, BackgroundTasks
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
import httpx

from crypto_engine import AirTagCryptoEngine

# Try importing findmy
try:
    from findmy import AppleAccount, RemoteAnisetteProvider, KeyPair, LoginState
    FINDMY_AVAILABLE = True
except ImportError:
    FINDMY_AVAILABLE = False

app = FastAPI(
    title="Tsmartkey AirTag Find My Backend",
    version="3.0.0",
    description="Backend Apple Find My & GraphHopper Live Tracking"
)

# Enable CORS for TYMAP Control Center (port 8085) and any web origin
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

DATA_DIR = Path("/app/data") if Path("/app/data").is_dir() else Path(__file__).parent / "data"
DATA_DIR.mkdir(parents=True, exist_ok=True)
DB_PATH = DATA_DIR / "airtag_testbench.db"
SESSION_PATH = DATA_DIR / "apple_session.json"
EXPORT_DIR = Path(os.environ.get("EXPORT_DIR", "/app/graphhopper_export"))
ANISETTE_URL = os.environ.get("ANISETTE_URL", "http://anisette:6969")
GRAPHHOPPER_URL = os.environ.get("GRAPHHOPPER_URL", "http://192.168.1.114:8989")

# Global Apple Account state
active_apple_account: Optional[Any] = None


def export_bike_live_json(device_id: str, lat: float, lon: float, accuracy: int, timestamp_iso: str, provider_str: str = "Apple Find My Network"):
    """Write bike position directly into \\192.168.1.114\graphhopper-data\bike_live.json."""
    candidates = [EXPORT_DIR, Path("/home/nas152/graphhopper-data"), DATA_DIR]
    written_paths = []

    live_data = {
        "device_id": device_id,
        "device_name": "Xe Máy Tsmartkey (AirTag Find My)",
        "latitude": round(lat, 7),
        "longitude": round(lon, 7),
        "accuracy_meters": accuracy,
        "timestamp_iso": timestamp_iso,
        "status": "online",
        "battery_level": "good",
        "provider": provider_str,
        "graphhopper_profile": "scooter"
    }

    for folder in candidates:
        if folder.exists() and folder.is_dir():
            target_file = folder / "bike_live.json"
            try:
                with open(target_file, "w", encoding="utf-8") as f:
                    json.dump(live_data, f, indent=2, ensure_ascii=False)
                written_paths.append(str(target_file))
            except Exception as e:
                print(f"[Warning] Cannot write to {target_file}: {e}")

    return written_paths, live_data


def init_db():
    with sqlite3.connect(DB_PATH) as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS devices (
                device_id TEXT PRIMARY KEY,
                private_key_hex TEXT NOT NULL,
                public_key_hex TEXT NOT NULL,
                mac_address TEXT NOT NULL,
                ble_frame_hex TEXT NOT NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """)
        conn.execute("""
            CREATE TABLE IF NOT EXISTS location_reports (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                device_id TEXT NOT NULL,
                latitude REAL NOT NULL,
                longitude REAL NOT NULL,
                accuracy_meters INTEGER NOT NULL,
                timestamp_iso TEXT NOT NULL,
                provider TEXT DEFAULT 'Apple Find My Network',
                received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                FOREIGN KEY (device_id) REFERENCES devices(device_id)
            )
        """)
        # Ensure default device exists
        cur = conn.cursor()
        cur.execute("SELECT device_id FROM devices WHERE device_id = 'smartkey-airtag-c3'")
        if not cur.fetchone():
            priv_hex = "01" * 28  # Placeholder if not generated yet
            keys = AirTagCryptoEngine.generate_keypair()
            conn.execute("""
                INSERT INTO devices (device_id, private_key_hex, public_key_hex, mac_address, ble_frame_hex)
                VALUES (?, ?, ?, ?, ?)
            """, ("smartkey-airtag-c3", keys["private_key_hex"], keys["public_key_hex"], keys["mac_address"], keys["ble_advertisement_31b_hex"]))
            conn.commit()


def load_apple_session():
    global active_apple_account
    if not FINDMY_AVAILABLE:
        return
    if SESSION_PATH.exists():
        try:
            print("[iCloud] Loading saved Apple session from", SESSION_PATH)
            active_apple_account = AppleAccount.from_json(SESSION_PATH)
            print(f"[iCloud] Loaded session for {active_apple_account.account_name} successfully!")
        except Exception as e:
            print(f"[iCloud-Error] Failed to load session: {e}")
            active_apple_account = None


@app.on_event("startup")
async def startup_event():
    init_db()
    load_apple_session()
    asyncio.create_task(background_sync_loop())


async def background_sync_loop():
    """Background task to sync locations from Apple iCloud periodically."""
    while True:
        try:
            if active_apple_account:
                sync_all_devices()
        except Exception as e:
            print(f"[BackgroundSync] Error: {e}")
        await asyncio.sleep(45)  # Poll Apple every 45s


def sync_all_devices():
    """Query Apple Find My for all registered devices."""
    global active_apple_account
    if not active_apple_account or not FINDMY_AVAILABLE:
        return 0

    synced_count = 0
    with sqlite3.connect(DB_PATH) as conn:
        cur = conn.cursor()
        cur.execute("SELECT device_id, private_key_hex FROM devices")
        rows = cur.fetchall()

    for dev_id, priv_hex in rows:
        try:
            priv_bytes = bytes.fromhex(priv_hex)
            kp = KeyPair(private_key=priv_bytes)
            report = active_apple_account.fetch_location(kp)
            if report:
                lat = float(report.latitude)
                lon = float(report.longitude)
                acc = int(getattr(report, "confidence", 5) or 5)
                ts_iso = report.timestamp.isoformat() if hasattr(report, "timestamp") and report.timestamp else datetime.now(timezone.utc).isoformat()

                with sqlite3.connect(DB_PATH) as conn:
                    conn.execute("""
                        INSERT INTO location_reports (device_id, latitude, longitude, accuracy_meters, timestamp_iso, provider)
                        VALUES (?, ?, ?, ?, ?, 'Apple Find My (Real iCloud)')
                    """, (dev_id, lat, lon, acc, ts_iso))
                    conn.commit()

                export_bike_live_json(dev_id, lat, lon, acc, ts_iso, "Apple Find My (Real iCloud)")
                print(f"[iCloud-Live] Cập nhật thành công vị trí thật cho {dev_id}: {lat}, {lon}")
                synced_count += 1
        except Exception as e:
            print(f"[Sync-Device-Error] {dev_id}: {e}")

    return synced_count


# ==========================================
# APPLE ICLOUD AUTHENTICATION API
# ==========================================

class AppleLoginRequest(BaseModel):
    apple_id: str
    password: str

class Apple2FARequest(BaseModel):
    code: str


@app.get("/api/apple/status")
def get_apple_status():
    is_logged_in = False
    acc_name = None
    if active_apple_account:
        try:
            acc_name = active_apple_account.account_name
            is_logged_in = True
        except Exception:
            is_logged_in = False

    return {
        "status": "ok",
        "findmy_installed": FINDMY_AVAILABLE,
        "logged_in": is_logged_in,
        "apple_id": acc_name,
        "session_saved": SESSION_PATH.exists(),
        "anisette_url": ANISETTE_URL
    }


@app.post("/api/apple/login")
def login_apple(req: AppleLoginRequest):
    global active_apple_account
    if not FINDMY_AVAILABLE:
        raise HTTPException(status_code=500, detail="findmy library is not installed on backend")

    try:
        provider = RemoteAnisetteProvider(ANISETTE_URL)
        acc = AppleAccount(provider)
        login_res = acc.login(req.apple_id, req.password)

        active_apple_account = acc

        if login_res == LoginState.REQUIRE_2FA:
            return {
                "status": "require_2fa",
                "message": "Apple yêu cầu mã xác thực 2 lớp (2FA). Vui lòng nhập mã 6 số gửi về thiết bị của bạn."
            }
        elif login_res == LoginState.LOGGED_IN:
            acc.to_json(SESSION_PATH)
            return {
                "status": "success",
                "message": f"Đăng nhập thành công tài khoản {req.apple_id}!",
                "apple_id": acc.account_name
            }
        else:
            return {
                "status": "pending",
                "state": str(login_res),
                "message": "Đang trong tiến trình đăng nhập..."
            }
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"Lỗi đăng nhập Apple ID: {str(e)}")


@app.post("/api/apple/2fa")
def submit_2fa(req: Apple2FARequest):
    global active_apple_account
    if not active_apple_account:
        raise HTTPException(status_code=400, detail="Chưa khởi tạo phiên đăng nhập. Vui lòng nhập tài khoản và mật khẩu trước.")

    try:
        res = active_apple_account.td_2fa_submit(req.code.strip())
        if res == LoginState.LOGGED_IN:
            active_apple_account.to_json(SESSION_PATH)
            return {
                "status": "success",
                "message": "Xác thực 2FA thành công! Đã lưu phiên đăng nhập vĩnh viễn trên NAS.",
                "apple_id": active_apple_account.account_name
            }
        else:
            return {
                "status": "require_retry",
                "state": str(res),
                "message": "Mã xác thực chưa đúng hoặc đã hết hạn, vui lòng thử lại."
            }
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"Lỗi xác thực 2FA: {str(e)}")


@app.post("/api/apple/logout")
def logout_apple():
    global active_apple_account
    active_apple_account = None
    if SESSION_PATH.exists():
        try:
            SESSION_PATH.unlink()
        except Exception:
            pass
    return {"status": "success", "message": "Đã đăng xuất tài khoản Apple ID."}


@app.post("/api/apple/sync-now")
def sync_apple_now():
    if not active_apple_account:
        raise HTTPException(status_code=400, detail="Chưa đăng nhập Apple ID trên NAS.")
    count = sync_all_devices()
    return {
        "status": "success",
        "synced_devices": count,
        "message": f"Đã quét và đồng bộ dữ liệu từ Apple iCloud ({count} thiết bị có vị trí mới)."
    }


# ==========================================
# KEY MANAGEMENT & LIVE BIKE STATUS API
# ==========================================

class KeyGenerateRequest(BaseModel):
    device_id: str = "smartkey-airtag-c3"


@app.post("/api/key/generate")
def generate_key(req: KeyGenerateRequest):
    keys = AirTagCryptoEngine.generate_keypair()
    with sqlite3.connect(DB_PATH) as conn:
        conn.execute("""
            INSERT OR REPLACE INTO devices (device_id, private_key_hex, public_key_hex, mac_address, ble_frame_hex)
            VALUES (?, ?, ?, ?, ?)
        """, (req.device_id, keys["private_key_hex"], keys["public_key_hex"], keys["mac_address"], keys["ble_advertisement_31b_hex"]))
        conn.commit()

    pub_bytes = bytes.fromhex(keys["public_key_hex"])
    c_array_elements = ", ".join(f"0x{b:02X}" for b in pub_bytes)
    c_header = f"const uint8_t AIRTAG_PUB_KEY[28] = {{ {c_array_elements} }};"

    import base64
    b64_key = base64.b64encode(pub_bytes).decode('ascii')

    return {
        "status": "success",
        "device_id": req.device_id,
        "advertisement_key_base64": b64_key,
        "mac_address": keys["mac_address"],
        "public_key_hex": keys["public_key_hex"],
        "ble_advertisement_31b_hex": keys["ble_advertisement_31b_hex"],
        "c_style_array": c_header,
        "download_bin_url": f"/api/key/export-ota/{req.device_id}"
    }


@app.get("/api/key/list")
def list_keys():
    import base64
    with sqlite3.connect(DB_PATH) as conn:
        cur = conn.cursor()
        cur.execute("SELECT device_id, public_key_hex, mac_address, created_at FROM devices ORDER BY created_at DESC")
        rows = cur.fetchall()

    devices = []
    for dev_id, pub_hex, mac_str, created_at in rows:
        pub_bytes = bytes.fromhex(pub_hex)
        b64_key = base64.b64encode(pub_bytes).decode('ascii')
        devices.append({
            "device_id": dev_id,
            "advertisement_key_base64": b64_key,
            "mac_address": mac_str,
            "created_at": created_at
        })
    return {"devices": devices}


@app.delete("/api/key/{device_id}")
def delete_key(device_id: str):
    with sqlite3.connect(DB_PATH) as conn:
        conn.execute("DELETE FROM devices WHERE device_id = ?", (device_id,))
        conn.execute("DELETE FROM location_reports WHERE device_id = ?", (device_id,))
        conn.commit()
    return {"status": "success", "message": f"Đã xóa thiết bị '{device_id}'"}


@app.get("/api/key/info/{device_id}")
def get_key_info(device_id: str):
    with sqlite3.connect(DB_PATH) as conn:
        cur = conn.cursor()
        cur.execute("SELECT public_key_hex, mac_address, ble_frame_hex, created_at FROM devices WHERE device_id = ?", (device_id,))
        row = cur.fetchone()

    if not row:
        raise HTTPException(status_code=404, detail=f"Device '{device_id}' not found.")

    pub_hex, mac_str, frame_hex, created_at = row
    pub_bytes = bytes.fromhex(pub_hex)
    c_array_elements = ", ".join(f"0x{b:02X}" for b in pub_bytes)
    c_header = f"const uint8_t AIRTAG_PUB_KEY[28] = {{ {c_array_elements} }};"

    import base64
    b64_key = base64.b64encode(pub_bytes).decode('ascii')

    return {
        "device_id": device_id,
        "public_key_hex": pub_hex,
        "advertisement_key_base64": b64_key,
        "static_random_mac": mac_str,
        "ble_advertisement_31b_hex": frame_hex,
        "c_style_array": c_header,
        "download_bin_url": f"/api/key/export-ota/{device_id}",
        "created_at": created_at
    }


class ImportB64KeyRequest(BaseModel):
    device_id: str = "smartkey-airtag-c3"
    advertisement_key_base64: str


@app.post("/api/key/import-b64")
def import_b64_key(req: ImportB64KeyRequest):
    import base64
    try:
        raw_pub = base64.b64decode(req.advertisement_key_base64.strip())
        if len(raw_pub) != 28:
            raise ValueError("Độ dài khóa sau giải mã Base64 phải đúng 28 bytes")
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"Khóa Base64 không hợp lệ: {e}")

    # Calculate static random MAC
    mac = [raw_pub[0] | 0b11000000] + list(raw_pub[1:6])
    mac_str = ":".join(f"{b:02X}" for b in mac)
    pub_hex = raw_pub.hex()

    # Raw 31 bytes BLE frame
    raw_adv = bytearray(31)
    raw_adv[0] = 0x1E
    raw_adv[1] = 0xFF
    raw_adv[2] = 0x4C
    raw_adv[3] = 0x00
    raw_adv[4] = 0x12
    raw_adv[5] = 0x19
    raw_adv[6] = 0x00
    raw_adv[7:29] = raw_pub[6:28]
    raw_adv[29] = raw_pub[0] >> 6
    raw_adv[30] = 0x00

    with sqlite3.connect(DB_PATH) as conn:
        conn.execute("""
            INSERT OR REPLACE INTO devices (device_id, private_key_hex, public_key_hex, mac_address, ble_frame_hex)
            VALUES (?, COALESCE((SELECT private_key_hex FROM devices WHERE device_id = ?), ?), ?, ?, ?)
        """, (req.device_id, req.device_id, "00"*28, pub_hex, mac_str, raw_adv.hex()))
        conn.commit()

    c_array_elements = ", ".join(f"0x{b:02X}" for b in raw_pub)
    c_header = f"const uint8_t AIRTAG_PUB_KEY[28] = {{ {c_array_elements} }};"

    return {
        "status": "success",
        "device_id": req.device_id,
        "advertisement_key_base64": req.advertisement_key_base64.strip(),
        "static_random_mac": mac_str,
        "public_key_hex": pub_hex,
        "c_style_array": c_header
    }


@app.get("/api/key/export-ota/{device_id}")
def export_ota_key(device_id: str):
    with sqlite3.connect(DB_PATH) as conn:
        cur = conn.cursor()
        cur.execute("SELECT public_key_hex FROM devices WHERE device_id = ?", (device_id,))
        row = cur.fetchone()

    if not row:
        raise HTTPException(status_code=404, detail="Device not found")

    pub_bytes = bytes.fromhex(row[0])
    return Response(
        content=pub_bytes,
        media_type="application/octet-stream",
        headers={"Content-Disposition": f"attachment; filename={device_id}_pubkey.bin"}
    )


@app.get("/api/bike/live")
def get_bike_live(device_id: str = "smartkey-airtag-c3"):
    with sqlite3.connect(DB_PATH) as conn:
        cur = conn.cursor()
        cur.execute("""
            SELECT latitude, longitude, accuracy_meters, timestamp_iso, provider
            FROM location_reports
            WHERE device_id = ?
            ORDER BY id DESC LIMIT 1
        """, (device_id,))
        row = cur.fetchone()

    if not row:
        return {
            "device_id": device_id,
            "device_name": "Xe Máy Tsmartkey (AirTag Find My)",
            "latitude": 10.7769,
            "longitude": 106.7009,
            "accuracy_meters": 5,
            "timestamp_iso": "Chờ bản tin đầu tiên từ Apple",
            "status": "waiting_first_report",
            "provider": "Apple Find My (Đang chờ iPhone gửi)"
        }

    lat, lon, accuracy, ts_iso, provider = row
    return {
        "device_id": device_id,
        "device_name": "Xe Máy Tsmartkey (AirTag Find My)",
        "latitude": lat,
        "longitude": lon,
        "accuracy_meters": accuracy,
        "timestamp_iso": ts_iso,
        "status": "online",
        "battery_level": "good",
        "provider": provider or "Apple Find My Network"
    }


@app.post("/api/bike/sync-to-graphhopper")
def sync_to_graphhopper(device_id: str = "smartkey-airtag-c3"):
    live_info = get_bike_live(device_id)
    written_paths, _ = export_bike_live_json(
        device_id=device_id,
        lat=live_info["latitude"],
        lon=live_info["longitude"],
        accuracy=live_info.get("accuracy_meters", 5),
        timestamp_iso=live_info.get("timestamp_iso", ""),
        provider_str=live_info.get("provider", "Apple Find My Network")
    )
    return {
        "status": "synchronized",
        "device_id": device_id,
        "exported_files": written_paths,
        "bike_location": live_info
    }


@app.get("/api/route/to-bike")
def route_to_bike(user_lat: float, user_lon: float, device_id: str = "smartkey-airtag-c3"):
    live_info = get_bike_live(device_id)
    bike_lat = live_info["latitude"]
    bike_lon = live_info["longitude"]

    # GraphHopper point format: point=lat,lon&point=lat,lon
    gh_url = f"{GRAPHHOPPER_URL}/route"
    params = [
        ("point", f"{user_lat},{user_lon}"),
        ("point", f"{bike_lat},{bike_lon}"),
        ("profile", "scooter"),
        ("points_encoded", "false"),
        ("locale", "vi"),
        ("calc_points", "true")
    ]

    try:
        r = httpx.get(gh_url, params=params, timeout=5.0)
        if r.status_code == 200:
            return r.json()
    except Exception as e:
        print(f"[GraphHopper Routing Warning]: {e}")

    # Fallback straight line
    return {
        "paths": [{
            "distance": 0,
            "time": 0,
            "points": {
                "type": "LineString",
                "coordinates": [[user_lon, user_lat], [bike_lon, bike_lat]]
            },
            "instructions": [{"text": "Đường chim bay đến xe máy", "distance": 0}]
        }],
        "fallback": True
    }
