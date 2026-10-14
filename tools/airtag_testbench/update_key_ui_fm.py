import paramiko
import sys

sys.stdout.reconfigure(encoding='utf-8')
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect('192.168.1.114', username='nas152', password='271000', timeout=5)

sftp = ssh.open_sftp()
with sftp.open('/home/nas152/graphhopper-data/index.html', 'r') as f:
    html = f.read().decode('utf-8', errors='ignore')

# 1. New Modal HTML (Replacing old airtag-key-modal)
new_key_modal_html = """
<!-- MODAL QUẢN LÝ & TẠO KHÓA THIẾT BỊ (CHUẨN FM.HS2T.COM & OPENHAYSTACK) -->
<div id="airtag-key-modal" class="airtag-modal-overlay" style="display:none;">
    <div class="airtag-modal-card" style="max-width:520px;">
        <div class="airtag-modal-header">
            <div style="display:flex; align-items:center; gap:8px;">
                <i class="fa-solid fa-key" style="color:var(--emerald); font-size:16px;"></i>
                <h3 style="margin:0; font-size:14px; font-weight:800; color:#fff;">QUẢN LÝ KHÓA AIRTAG (CHUẨN FM.HS2T.COM)</h3>
            </div>
            <button onclick="closeKeyModal()" class="btn-modal-close"><i class="fa-solid fa-xmark"></i></button>
        </div>

        <div style="font-size:11px; color:#94a3b8; margin-bottom:12px; line-height:1.4;">
            Mỗi thiết bị dùng một <b>Advertisement Key (Base64)</b> 28 bytes NIST P-224. Bạn có thể tự sinh khóa mới trên NAS hoặc dán khóa từ <b>fm.hs2t.com</b> qua lại.
        </div>

        <!-- FORM TẠO / SỬA KHÓA THIẾT BỊ CHUẨN FM.HS2T.COM -->
        <div style="background:#090d16; border:1px solid var(--border); border-radius:10px; padding:14px; margin-bottom:14px;">
            <!-- Device Name -->
            <div style="margin-bottom:10px;">
                <label style="display:block; font-size:11px; font-weight:700; color:#cbd5e1; margin-bottom:4px;">
                    <i class="fa-solid fa-tag" style="color:#38bdf8;"></i> Device Name:
                </label>
                <input type="text" id="modal-device-id" value="ty1" placeholder="VD: ty1, xe_may_sh..." style="width:100%; box-sizing:border-box; background:#040711; border:1px solid var(--border); border-radius:8px; color:#fff; font-size:12px; padding:8px 10px; font-family:'JetBrains Mono'; outline:none;">
            </div>

            <!-- Advertisement Key (Base64) -->
            <div style="margin-bottom:12px;">
                <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:4px;">
                    <label style="font-size:11px; font-weight:700; color:#cbd5e1;">
                        <i class="fa-solid fa-shield-halved" style="color:#f59e0b;"></i> Advertisement Key (Base64):
                    </label>
                    <span id="modal-mac-badge" style="font-size:10px; font-family:'JetBrains Mono'; color:#38bdf8; font-weight:700;">MAC: --:--:--:--:--:--</span>
                </div>
                <div style="display:flex; gap:6px;">
                    <input type="text" id="modal-key-b64" placeholder="Dán hoặc sinh khóa Base64 tại đây..." style="flex:1; background:#040711; border:1px solid var(--border); border-radius:8px; color:#a7f3d0; font-size:11px; padding:8px 10px; font-family:'JetBrains Mono'; outline:none;">
                    <button onclick="copyB64Key()" title="Sao chép chuỗi Base64" style="background:#1e293b; color:#38bdf8; border:1px solid rgba(56,189,248,0.4); border-radius:8px; padding:0 12px; cursor:pointer;">
                        <i class="fa-solid fa-copy"></i>
                    </button>
                </div>
            </div>

            <!-- Hàng nút hành động -->
            <div style="display:grid; grid-template-columns: 1fr 1fr; gap:8px;">
                <button onclick="generateNewKeypair()" style="background:linear-gradient(135deg, #059669, #0d9488); color:#fff; border:none; border-radius:8px; padding:9px; font-size:11px; font-weight:700; cursor:pointer; display:flex; align-items:center; justify-content:center; gap:6px;">
                    <i class="fa-solid fa-wand-magic-sparkles"></i> Sinh Khóa Mới
                </button>
                <button onclick="saveImportB64Key()" style="background:linear-gradient(135deg, #0284c7, #2563eb); color:#fff; border:none; border-radius:8px; padding:9px; font-size:11px; font-weight:700; cursor:pointer; display:flex; align-items:center; justify-content:center; gap:6px;">
                    <i class="fa-solid fa-floppy-disk"></i> Lưu Khóa Này
                </button>
            </div>
        </div>

        <!-- MẢNG C++ & TẢI FILE BIN -->
        <div id="modal-extra-tools" style="background:#090d16; border:1px solid var(--border); border-radius:10px; padding:12px; margin-bottom:14px;">
            <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:6px;">
                <span style="font-size:11px; font-weight:700; color:#94a3b8;"><i class="fa-solid fa-code"></i> Mảng C++ nạp PlatformIO (test_airtag.cpp):</span>
                <button onclick="copyCppArray()" style="background:#1e293b; color:#34d399; border:1px solid rgba(52,211,153,0.4); border-radius:6px; padding:3px 8px; font-size:10px; font-weight:700; cursor:pointer;">
                    <i class="fa-solid fa-copy"></i> Copy C++
                </button>
            </div>
            <textarea id="res-cpp" rows="2" readonly style="width:100%; box-sizing:border-box; background:#040711; border:1px solid #1e293b; border-radius:6px; color:#a7f3d0; font-family:'JetBrains Mono'; font-size:10px; padding:6px; resize:none; margin-bottom:8px;"></textarea>

            <div style="display:flex; justify-content:space-between; align-items:center;">
                <span style="font-size:10px; color:#94a3b8;">File nhị phân 28 bytes (.bin):</span>
                <button onclick="downloadBinKey()" style="background:#0284c7; color:#fff; border:none; border-radius:6px; padding:5px 10px; font-size:10px; font-weight:700; cursor:pointer;">
                    <i class="fa-solid fa-download"></i> Tải key.bin
                </button>
            </div>
        </div>

        <!-- DANH SÁCH CÁC THIẾT BỊ ĐÃ LƯU TRÊN NAS -->
        <div>
            <div style="font-size:11px; font-weight:800; color:#fff; margin-bottom:6px; display:flex; justify-content:space-between; align-items:center;">
                <span><i class="fa-solid fa-list-check" style="color:var(--emerald);"></i> Danh Sách Thiết Bị Của Bạn:</span>
                <button onclick="loadDeviceList()" style="background:transparent; border:none; color:#38bdf8; font-size:11px; cursor:pointer;">
                    <i class="fa-solid fa-rotate-right"></i> Làm mới
                </button>
            </div>
            <div id="device-list-container" style="max-height:130px; overflow-y:auto; display:flex; flex-direction:column; gap:6px;">
                <div style="font-size:10px; color:#64748b; text-align:center; padding:10px;">Đang tải danh sách...</div>
            </div>
        </div>
    </div>
</div>
"""

# Tìm và thay thế thẻ div modal cũ
import re
old_modal_pattern = r'<!-- MODAL QUẢN LÝ & TẢI KHÓA NẠP FIRMWARE ESP32 -->.*?</div>\s*</div>\s*</div>'
if "airtag-key-modal" in html:
    # Replace from `<div id="airtag-key-modal"` to its closing div
    start_idx = html.find('<div id="airtag-key-modal"')
    if start_idx != -1:
        # find the end of this modal div block
        end_idx = html.find('<!-- MODAL ĐĂNG NHẬP APPLE ID', start_idx)
        if end_idx != -1:
            html = html[:start_idx] + new_key_modal_html + "\n" + html[end_idx:]
            print("Replaced key modal with fm.hs2t.com style modal!")

# 2. Cập nhật các hàm JS cho Key Modal
new_key_js = """
    // --- QUẢN LÝ KHÓA THEO CHUẨN FM.HS2T.COM ---
    function openKeyModal() {
        const modal = document.getElementById('airtag-key-modal');
        if (modal) modal.style.display = 'flex';
        loadDeviceList();
        fetchCurrentKeyInfo();
    }

    function closeKeyModal() {
        const modal = document.getElementById('airtag-key-modal');
        if (modal) modal.style.display = 'none';
    }

    async function loadDeviceList() {
        const container = document.getElementById('device-list-container');
        if (!container) return;
        try {
            const res = await fetch(`http://${nasIp}:8090/api/key/list`);
            if (res.ok) {
                const data = await res.json();
                renderDeviceList(data.devices || []);
            }
        } catch (err) {
            container.innerHTML = '<div style="font-size:10px; color:#ef4444; text-align:center;">Lỗi tải danh sách thiết bị!</div>';
        }
    }

    function renderDeviceList(devices) {
        const container = document.getElementById('device-list-container');
        if (!devices.length) {
            container.innerHTML = '<div style="font-size:10px; color:#64748b; text-align:center; padding:8px;">Chưa có thiết bị nào. Bấm Sinh Khóa Mới để tạo.</div>';
            return;
        }

        let html = '';
        devices.forEach(dev => {
            html += `
                <div style="background:#090d16; border:1px solid var(--border); border-radius:8px; padding:8px 10px; display:flex; justify-content:space-between; align-items:center;">
                    <div style="cursor:pointer;" onclick="selectDevice('${dev.device_id}')">
                        <div style="font-size:11px; font-weight:700; color:#fff;"><i class="fa-solid fa-motorcycle" style="color:var(--emerald);"></i> ${dev.device_id}</div>
                        <div style="font-size:9px; font-family:'JetBrains Mono'; color:#94a3b8;">MAC: ${dev.mac_address} | Key: ${dev.advertisement_key_base64.substring(0, 16)}...</div>
                    </div>
                    <div style="display:flex; gap:4px;">
                        <button onclick="selectDevice('${dev.device_id}')" title="Chọn thiết bị này" style="background:#1e293b; color:#38bdf8; border:1px solid rgba(56,189,248,0.3); border-radius:6px; padding:4px 8px; font-size:9px; cursor:pointer;">
                            Chọn
                        </button>
                        <button onclick="deleteDevice('${dev.device_id}')" title="Xóa thiết bị" style="background:rgba(239,68,68,0.15); color:#ef4444; border:1px solid rgba(239,68,68,0.3); border-radius:6px; padding:4px 8px; font-size:9px; cursor:pointer;">
                            <i class="fa-solid fa-trash"></i>
                        </button>
                    </div>
                </div>
            `;
        });
        container.innerHTML = html;
    }

    function selectDevice(devId) {
        document.getElementById('modal-device-id').value = devId;
        fetchCurrentKeyInfo();
        showToast(`Đã chọn thiết bị: ${devId}`);
    }

    async function deleteDevice(devId) {
        if (!confirm(`Bạn có chắc chắn muốn xóa thiết bị '${devId}' khỏi NAS?`)) return;
        try {
            await fetch(`http://${nasIp}:8090/api/key/${devId}`, { method: 'DELETE' });
            showToast(`Đã xóa thiết bị '${devId}'!`);
            loadDeviceList();
        } catch (err) {
            showToast("Lỗi khi xóa thiết bị!", true);
        }
    }

    async function fetchCurrentKeyInfo() {
        const devId = (document.getElementById('modal-device-id').value || "smartkey-airtag-c3").trim();
        try {
            const res = await fetch(`http://${nasIp}:8090/api/key/info/${devId}`);
            if (res.ok) {
                const data = await res.json();
                displayKeyData(data);
            }
        } catch (err) {
            console.error("Lỗi lấy thông tin khóa:", err);
        }
    }

    async function generateNewKeypair() {
        const devId = (document.getElementById('modal-device-id').value || "smartkey-airtag-c3").trim();
        showToast("Đang sinh cặp khóa P-224...");
        try {
            const res = await fetch(`http://${nasIp}:8090/api/key/generate`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ device_id: devId })
            });
            if (res.ok) {
                const data = await res.json();
                displayKeyData(data);
                showToast(`Đã sinh khóa mới cho '${devId}' thành công!`);
                loadDeviceList();
            }
        } catch (err) {
            showToast("Lỗi khi sinh khóa mới!", true);
        }
    }

    async function saveImportB64Key() {
        const devId = (document.getElementById('modal-device-id').value || "smartkey-airtag-c3").trim();
        const b64 = (document.getElementById('modal-key-b64').value || "").trim();
        if (!b64) {
            showToast("Vui lòng nhập chuỗi Base64 Advertisement Key!", true);
            return;
        }

        try {
            const res = await fetch(`http://${nasIp}:8090/api/key/import-b64`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ device_id: devId, advertisement_key_base64: b64 })
            });
            const data = await res.json();
            if (res.ok) {
                displayKeyData(data);
                showToast(`Đã lưu khóa cho '${devId}' thành công!`);
                loadDeviceList();
            } else {
                showToast(data.detail || "Khóa Base64 không hợp lệ!", true);
            }
        } catch (err) {
            showToast("Lỗi khi lưu khóa!", true);
        }
    }

    function displayKeyData(data) {
        document.getElementById('modal-key-b64').value = data.advertisement_key_base64 || "";
        document.getElementById('modal-mac-badge').innerText = `MAC: ${data.static_random_mac || "--"}`;
        document.getElementById('res-cpp').value = data.c_style_array || "";
    }

    function copyB64Key() {
        const b64 = document.getElementById('modal-key-b64').value;
        if (!b64) return;
        navigator.clipboard.writeText(b64).then(() => {
            showToast("Đã chép Advertisement Key (Base64)!");
        });
    }

    function copyCppArray() {
        const cpp = document.getElementById('res-cpp').value;
        if (!cpp) return;
        navigator.clipboard.writeText(cpp).then(() => {
            showToast("Đã chép mảng C++ nạp PlatformIO!");
        });
    }

    function downloadBinKey() {
        const devId = (document.getElementById('modal-device-id').value || "smartkey-airtag-c3").trim();
        window.open(`http://${nasIp}:8090/api/key/export-ota/${devId}`, '_blank');
    }
"""

# Thay thế các hàm JS cũ
old_js_start = html.find('    // --- MODAL QUẢN LÝ KHÓA NẠP ESP32 ---')
if old_js_start != -1:
    old_js_end = html.find('    // 8. GRAPHHopper', old_js_start)
    if old_js_end == -1:
        old_js_end = html.find('    // ---', old_js_start + 40)
    # Let's find end of function downloadBinKey
    download_idx = html.find('window.open(`http://${nasIp}:8090/api/key/export-ota/${devId}`', old_js_start)
    if download_idx != -1:
        func_close = html.find('}', download_idx)
        if func_close != -1:
            html = html[:old_js_start] + new_key_js + "\n" + html[func_close+1:]
            print("Replaced JS logic with new fm.hs2t.com key management functions!")

with sftp.open('/home/nas152/graphhopper-data/index.html', 'w') as f:
    f.write(html)
print("Uploaded updated index.html to NAS successfully!")

sftp.close()
ssh.close()
