import paramiko
import sys

sys.stdout.reconfigure(encoding='utf-8')
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect('192.168.1.114', username='nas152', password='271000', timeout=5)

sftp = ssh.open_sftp()
with sftp.open('/home/nas152/graphhopper-data/index.html', 'r') as f:
    html = f.read().decode('utf-8', errors='ignore')

# Normalize newlines
html = html.replace('\r\n', '\n')

btn_old = """        <!-- Nút Tạo & Tải Key ESP32 -->
        <button class="btn-key-manage" onclick="openKeyModal()">
            <i class="fa-solid fa-key"></i> Tạo & Tải Khóa P-224 Nạp Firmware ESP32
        </button>"""

btn_new = """        <!-- Nút Tạo & Tải Key ESP32 -->
        <button class="btn-key-manage" onclick="openKeyModal()">
            <i class="fa-solid fa-key"></i> Tạo & Tải Khóa P-224 Nạp Firmware ESP32
        </button>

        <!-- Nút Đăng Nhập Apple ID iCloud (chuẩn fm.hs2t.com) -->
        <button id="btn-apple-auth-modal" class="btn-key-manage" onclick="openAppleModal()" style="margin-top:6px; background:linear-gradient(135deg, #0f172a, #1e293b); border:1px solid #38bdf8; color:#7dd3fc; display:flex; align-items:center; justify-content:center; gap:8px;">
            <i class="fa-brands fa-apple" style="font-size:14px; color:#fff;"></i> <span id="apple-auth-btn-text">Đăng Nhập Apple ID (Kéo Vị Trí Thật iCloud)</span>
        </button>"""

if btn_old in html:
    html = html.replace(btn_old, btn_new)
    print("Replaced button successfully!")
else:
    print("Still not matching btn_old!")

# Also ensure checkAppleStatus is called when page loads:
load_old = "fetchBikeLivePosition(false);"
load_new = "fetchBikeLivePosition(false); checkAppleStatus();"
if load_old in html and "checkAppleStatus();" not in html:
    html = html.replace(load_old, load_new)
    print("Added checkAppleStatus() to page load!")

with sftp.open('/home/nas152/graphhopper-data/index.html', 'w') as f:
    f.write(html)
print("Uploaded to NAS successfully!")

sftp.close()
ssh.close()
