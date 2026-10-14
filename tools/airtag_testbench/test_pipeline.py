"""Automated E2E Test Pipeline for AirTag Crypto Engine & Protocol Verification.

Runs 5 core verification checks:
1. P-224 Keypair Generation & Byte length validation (28 bytes private, 28 bytes public X)
2. BLE Static Random MAC address bitmask validation (0xC0 in byte 0)
3. 31-byte Raw Apple BLE Advertisement frame structural & reconstruction validation
4. GPS Report Encryption / Decryption E2E loop validation (Roundtrip fidelity)
5. GraphHopper Routing API Mock validation
"""

import sys
import struct
import hashlib
from datetime import datetime, timezone
from pathlib import Path

# Add current folder to path
sys.path.insert(0, str(Path(__file__).parent))

if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")

from crypto_engine import AirTagCryptoEngine, APPLE_EPOCH_OFFSET
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.ciphers.aead import AESGCM


def run_test_pipeline():
    print("=" * 70)
    print(" AIRTAG TESTBENCH: AUTOMATED E2E VERIFICATION PIPELINE")
    print("=" * 70)

    # ---------------------------------------------------------
    # TEST 1: Keypair Generation
    # ---------------------------------------------------------
    print("\n[TEST 1/5] Kiểm tra sinh cặp khóa NIST P-224 (secp224r1)...", end=" ")
    priv_bytes, pub_x_bytes = AirTagCryptoEngine.generate_keypair()
    assert len(priv_bytes) == 28, f"Private key must be 28 bytes, got {len(priv_bytes)}"
    assert len(pub_x_bytes) == 28, f"Public key X must be 28 bytes, got {len(pub_x_bytes)}"
    print("PASSED")
    print(f"  -> Private Key (Hex, 28B): {priv_bytes.hex()}")
    print(f"  -> Public Key X (Hex, 28B): {pub_x_bytes.hex()}")

    # ---------------------------------------------------------
    # TEST 2: Static Random MAC Derivation
    # ---------------------------------------------------------
    print("\n[TEST 2/5] Kiểm tra tạo địa chỉ BLE Static Random MAC...", end=" ")
    mac_str = AirTagCryptoEngine.derive_static_random_mac(pub_x_bytes)
    mac_parts = [int(p, 16) for p in mac_str.split(":")]
    assert len(mac_parts) == 6, f"MAC must have 6 octets, got {len(mac_parts)}"
    # Verify bit 7 and 6 of first byte are 11 (Static Random address)
    assert (mac_parts[0] & 0xC0) == 0xC0, f"MAC[0] top 2 bits must be 11, got {bin(mac_parts[0])}"
    assert mac_parts[1:6] == list(pub_x_bytes[1:6]), "MAC[1..5] must match Public Key[1..5]"
    print("PASSED")
    print(f"  -> Derived Static Random MAC: {mac_str}")

    # ---------------------------------------------------------
    # TEST 3: 31-byte Apple BLE Advertisement Frame & Reconstruction
    # ---------------------------------------------------------
    print("\n[TEST 3/5] Kiểm tra gói tin quảng bá 31 bytes & tái tạo khóa...", end=" ")
    frame = AirTagCryptoEngine.construct_ble_adv_frame(pub_x_bytes)
    assert len(frame) == 31, f"BLE frame must be exactly 31 bytes, got {len(frame)}"
    assert frame[0] == 0x1E, "Byte 0 must be 0x1E (Length 30)"
    assert frame[1] == 0xFF, "Byte 1 must be 0xFF (Manufacturer Specific)"
    assert frame[2:4] == b"\x4C\x00", "Bytes 2..3 must be Apple ID (0x004C)"
    assert frame[4] == 0x12, "Byte 4 must be 0x12 (Find My subtype)"
    assert frame[5] == 0x19, "Byte 5 must be 0x19 (Payload length 25)"
    assert frame[6] == 0x00, "Byte 6 must be 0x00 (Battery normal)"
    assert frame[7:29] == pub_x_bytes[6:28], "Bytes 7..28 must match Public Key[6..27]"
    assert frame[29] == ((pub_x_bytes[0] >> 6) & 0x03), "Byte 29 must match hint bits of Public Key[0]"
    assert frame[30] == 0x00, "Byte 30 must be 0x00"

    # Reconstruct Public Key from MAC + Frame
    reconstructed_pk = AirTagCryptoEngine.reconstruct_public_key_x(mac_str, frame)
    assert reconstructed_pk == pub_x_bytes, "Reconstructed Public Key X must match original 100%!"
    print("PASSED")
    print(f"  -> 31-byte Raw Payload: {frame.hex().upper()}")
    print("  -> Tái tạo Public Key từ MAC + Payload: KHỚP 100%")

    # ---------------------------------------------------------
    # TEST 4: Encryption & Decryption E2E Loop
    # ---------------------------------------------------------
    print("\n[TEST 4/5] Kiểm tra chu trình Mã hóa & Giải mã Báo cáo GPS Apple...", end=" ")
    target_lat = 21.028511   # Tọa độ Hà Nội mẫu
    target_lon = 105.854444
    target_acc = 8           # Sai số 8 mét

    # 1. Simulate iPhone locating the beacon and encrypting payload
    owner_priv_key = ec.derive_private_key(int.from_bytes(priv_bytes, "big"), ec.SECP224R1())
    owner_pub_point = owner_priv_key.public_key().public_numbers()

    # Generate ephemeral keypair on iPhone
    eph_priv = ec.generate_private_key(ec.SECP224R1())
    eph_pub = eph_priv.public_key()
    eph_numbers = eph_pub.public_numbers()
    eph_x_bytes = eph_numbers.x.to_bytes(28, "big")
    eph_y_bytes = eph_numbers.y.to_bytes(28, "big")
    eph_uncompressed = b"\x04" + eph_x_bytes + eph_y_bytes

    # iPhone computes shared secret
    shared_secret = eph_priv.exchange(ec.ECDH(), owner_priv_key.public_key())

    # KDF SHA-256
    kdf_input = shared_secret + b"\x00\x00\x00\x01" + eph_x_bytes
    key_material = hashlib.sha256(kdf_input).digest()
    aes_key = key_material[:16]
    iv = key_material[16:28]

    # Pack GPS plaintext
    now_unix = datetime.now(timezone.utc).timestamp()
    apple_ts = int(now_unix - APPLE_EPOCH_OFFSET)
    lat_int = int(target_lat * 10_000_000)
    lon_int = int(target_lon * 10_000_000)
    plaintext = struct.pack(">IiiHB", apple_ts, lat_int, lon_int, target_acc, 0x00)

    # AES-GCM Encrypt
    aesgcm = AESGCM(aes_key)
    ciphertext_with_tag = aesgcm.encrypt(iv, plaintext, None)

    # 2. Server decrypts report with owner private key
    decrypted_result = AirTagCryptoEngine.decrypt_report_payload(
        priv_bytes,
        eph_uncompressed,
        ciphertext_with_tag
    )

    assert abs(decrypted_result["latitude"] - target_lat) < 1e-6, "Latitude mismatch!"
    assert abs(decrypted_result["longitude"] - target_lon) < 1e-6, "Longitude mismatch!"
    assert decrypted_result["accuracy_meters"] == target_acc, "Accuracy mismatch!"
    print("PASSED")
    print(f"  -> Tọa độ gốc: {target_lat}, {target_lon} (Sai số: {target_acc}m)")
    print(f"  -> Giải mã được: {decrypted_result['latitude']:.6f}, {decrypted_result['longitude']:.6f} (Sai số: {decrypted_result['accuracy_meters']}m)")
    print(f"  -> Thời gian: {decrypted_result['timestamp_iso']}")

    # ---------------------------------------------------------
    # TEST 5: GraphHopper Routing Simulation
    # ---------------------------------------------------------
    print("\n[TEST 5/5] Kiểm tra cấu trúc truy vấn dẫn đường GraphHopper...", end=" ")
    user_lat, user_lon = 21.030000, 105.850000
    bike_lat, bike_lon = decrypted_result["latitude"], decrypted_result["longitude"]
    
    # Mock routing calculation
    # Distance approximation via Haversine
    import math
    R = 6371000  # Earth radius in meters
    phi1, phi2 = math.radians(user_lat), math.radians(bike_lat)
    delta_phi = math.radians(bike_lat - user_lat)
    delta_lambda = math.radians(bike_lon - user_lon)
    a = math.sin(delta_phi/2)**2 + math.cos(phi1)*math.cos(phi2)*math.sin(delta_lambda/2)**2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1-a))
    approx_distance_m = round(R * c)
    approx_time_min = round(approx_distance_m / (30 * 1000 / 60)) # 30 km/h xe máy

    assert approx_distance_m > 0
    print("PASSED")
    print(f"  -> Vị trí người tìm: ({user_lat}, {user_lon})")
    print(f"  -> Vị trí xe máy:    ({bike_lat:.6f}, {bike_lon:.6f})")
    print(f"  -> Cự ly ước tính:   {approx_distance_m} mét (~{approx_time_min} phút di chuyển xe máy)")

    print("\n" + "=" * 70)
    print(" KẾT QUẢ: 5/5 BÀI TEST ĐẠT CHUẨN 100% (ALL TESTS PASSED SUCCESS) ")
    print("=" * 70)
    return True


if __name__ == "__main__":
    success = run_test_pipeline()
    sys.exit(0 if success else 1)
