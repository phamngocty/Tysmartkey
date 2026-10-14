import paramiko
import sys

sys.stdout.reconfigure(encoding='utf-8')
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect('192.168.1.114', username='nas152', password='271000', timeout=5)

sftp = ssh.open_sftp()
with sftp.open('/home/nas152/graphhopper-data/index.html', 'r') as f:
    html = f.read().decode('utf-8', errors='ignore')

# 1. Thêm nút Đăng nhập Apple ID vào widget AirTag
target_btn = """        <!-- Nút Tạo & Tải Key ESP32 -->
        <button class="btn-key-manage" onclick="openKeyModal()">
            <i class="fa-solid fa-key"></i> Tạo & Tải Khóa P-224 Nạp Firmware ESP32
        </button>"""

replacement_btn = """        <!-- Nút Tạo & Tải Key ESP32 -->
        <button class="btn-key-manage" onclick="openKeyModal()">
            <i class="fa-solid fa-key"></i> Tạo & Tải Khóa P-224 Nạp Firmware ESP32
        </button>

        <!-- Nút Đăng Nhập Apple ID iCloud (chuẩn fm.hs2t.com) -->
        <button id="btn-apple-auth-modal" class="btn-key-manage" onclick="openAppleModal()" style="margin-top:6px; background:linear-gradient(135deg, #0f172a, #1e293b); border:1px solid #38bdf8; color:#7dd3fc; display:flex; align-items:center; justify-content:center; gap:8px;">
            <i class="fa-brands fa-apple" style="font-size:14px; color:#fff;"></i> <span id="apple-auth-btn-text">Đăng Nhập Apple ID (Kéo Vị Trí Thật iCloud)</span>
        </button>"""

if target_btn in html:
    html = html.replace(target_btn, replacement_btn)
    print("Added Apple Login button to AirTag widget!")
else:
    print("Warning: target_btn not found!")

# 2. Thêm Modal Apple ID & Modal HTML
apple_modal_html = """
<!-- MODAL ĐĂNG NHẬP APPLE ID (ICLOUD FIND MY) -->
<div id="apple-auth-modal" class="airtag-modal-overlay" style="display:none;">
    <div class="airtag-modal-card" style="max-width:440px;">
        <div class="airtag-modal-header">
            <div style="display:flex; align-items:center; gap:8px;">
                <i class="fa-brands fa-apple" style="color:#fff; font-size:18px;"></i>
                <h3 style="margin:0; font-size:14px; font-weight:800; color:#fff;">KẾT NỐI APPLE ID (ICLOUD FIND MY)</h3>
            </div>
            <button onclick="closeAppleModal()" class="btn-modal-close"><i class="fa-solid fa-xmark"></i></button>
        </div>

        <div style="font-size:11px; color:#94a3b8; margin-bottom:12px; line-height:1.4;">
            Kết nối trực tiếp qua máy chủ <b>Anisette (:6969)</b> trên NAS của bạn. Dữ liệu xác thực và tọa độ được xử lý <b>nội bộ 100%</b>, không qua bên thứ 3 nào.
        </div>

        <!-- Trạng thái đăng nhập -->
        <div id="apple-current-status-box" style="background:#090d16; border:1px solid var(--border); border-radius:8px; padding:10px; margin-bottom:12px; display:flex; justify-content:space-between; align-items:center;">
            <div>
                <div style="font-size:10px; color:#94a3b8;">Trạng thái iCloud:</div>
                <div id="apple-status-text" style="font-size:12px; font-weight:700; color:#f59e0b;">Chưa đăng nhập</div>
            </div>
            <button id="btn-apple-logout" onclick="logoutAppleAccount()" style="display:none; background:#dc2626; color:#fff; border:none; border-radius:6px; padding:5px 10px; font-size:10px; font-weight:700; cursor:pointer;">
                <i class="fa-solid fa-right-from-bracket"></i> Đăng Xuất
            </button>
        </div>

        <!-- BƯỚC 1: Form Đăng Nhập Email + Pass -->
        <div id="apple-login-form">
            <div style="margin-bottom:8px;">
                <label style="display:block; font-size:11px; color:#94a3b8; margin-bottom:4px;">Apple ID (Email iCloud):</label>
                <input type="email" id="apple-input-email" placeholder="example@icloud.com" style="width:100%; box-sizing:border-box; background:#090d16; border:1px solid var(--border); border-radius:8px; color:#fff; font-size:11px; padding:8px 10px; outline:none;">
            </div>
            <div style="margin-bottom:12px;">
                <label style="display:block; font-size:11px; color:#94a3b8; margin-bottom:4px;">Mật Khẩu Apple ID:</label>
                <input type="password" id="apple-input-password" placeholder="••••••••••••" style="width:100%; box-sizing:border-box; background:#090d16; border:1px solid var(--border); border-radius:8px; color:#fff; font-size:11px; padding:8px 10px; outline:none;">
            </div>
            <button id="btn-submit-apple-login" onclick="submitAppleLogin()" style="width:100%; background:linear-gradient(135deg, #0284c7, #2563eb); color:#fff; border:none; border-radius:8px; padding:10px; font-size:11px; font-weight:800; cursor:pointer; display:flex; align-items:center; justify-content:center; gap:6px;">
                <i class="fa-solid fa-lock"></i> Đăng Nhập iCloud & Lấy Token
            </button>
        </div>

        <!-- BƯỚC 2: Form Nhập Mã Xác Thực 2FA -->
        <div id="apple-2fa-form" style="display:none; background:#090d16; border:1px solid #3b82f6; border-radius:8px; padding:12px;">
            <div style="font-size:12px; font-weight:700; color:#60a5fa; margin-bottom:6px; display:flex; align-items:center; gap:6px;">
                <i class="fa-solid fa-shield-halved"></i> Xác thực 2 lớp (Apple 2FA)
            </div>
            <div style="font-size:10px; color:#cbd5e1; margin-bottom:10px;">
                Apple đã gửi mã 6 chữ số về màn hình iPhone/iPad của bạn. Vui lòng nhập mã:
            </div>
            <input type="text" id="apple-input-2fa" maxlength="6" placeholder="123456" style="width:100%; box-sizing:border-box; text-align:center; letter-spacing:6px; font-size:18px; font-weight:800; background:#040711; border:1px solid #3b82f6; border-radius:8px; color:#38bdf8; padding:8px; font-family:'JetBrains Mono'; outline:none; margin-bottom:10px;">
            <div style="display:flex; gap:6px;">
                <button onclick="cancel2FA()" style="flex:1; background:#1e293b; color:#94a3b8; border:1px solid var(--border); border-radius:6px; padding:8px; font-size:10px; font-weight:700; cursor:pointer;">Hủy</button>
                <button id="btn-submit-apple-2fa" onclick="submitApple2FA()" style="flex:2; background:#059669; color:#fff; border:none; border-radius:6px; padding:8px; font-size:11px; font-weight:800; cursor:pointer;">Xác Nhận 2FA</button>
            </div>
        </div>

        <!-- Nút Kéo Vị Trí Ngay Lập Tức -->
        <div id="apple-sync-section" style="margin-top:12px; display:none;">
            <button onclick="syncAppleNowFromModal()" style="width:100%; background:linear-gradient(135deg, #059669, #0d9488); color:#fff; border:none; border-radius:8px; padding:10px; font-size:11px; font-weight:800; cursor:pointer; display:flex; align-items:center; justify-content:center; gap:6px;">
                <i class="fa-solid fa-satellite-dish"></i> Quét & Kéo Tọa Độ Thật Từ Apple Ngay
            </button>
        </div>
    </div>
</div>
"""

# Chèn modal HTML trước thẻ đóng </body>
if "<!-- MODAL ĐĂNG NHẬP APPLE ID" not in html:
    html = html.replace("</body>", apple_modal_html + "\n</body>")
    print("Injected Apple Modal HTML!")

# 3. Thêm các hàm Javascript điều khiển Apple ID
apple_js = """
    // --- APPLE ID AUTHENTICATION & ICLOUD SYNC LOGIC ---
    function openAppleModal() {
        const modal = document.getElementById('apple-auth-modal');
        if (modal) modal.style.display = 'flex';
        checkAppleStatus();
    }

    function closeAppleModal() {
        const modal = document.getElementById('apple-auth-modal');
        if (modal) modal.style.display = 'none';
    }

    function cancel2FA() {
        document.getElementById('apple-2fa-form').style.display = 'none';
        document.getElementById('apple-login-form').style.display = 'block';
    }

    async function checkAppleStatus() {
        try {
            const res = await fetch(`http://${nasIp}:8090/api/apple/status`);
            if (res.ok) {
                const data = await res.json();
                updateAppleUIStatus(data);
            }
        } catch (err) {
            console.error("Lỗi kiểm tra trạng thái Apple:", err);
        }
    }

    function updateAppleUIStatus(data) {
        const statusText = document.getElementById('apple-status-text');
        const btnLogout = document.getElementById('btn-apple-logout');
        const loginForm = document.getElementById('apple-login-form');
        const syncSec = document.getElementById('apple-sync-section');
        const btnAuthLabel = document.getElementById('apple-auth-btn-text');

        if (data.logged_in && data.apple_id) {
            if (statusText) {
                statusText.innerText = `● Đã kết nối: ${data.apple_id}`;
                statusText.style.color = '#34d399';
            }
            if (btnLogout) btnLogout.style.display = 'block';
            if (loginForm) loginForm.style.display = 'none';
            if (syncSec) syncSec.style.display = 'block';
            if (btnAuthLabel) btnAuthLabel.innerText = `iCloud: ${data.apple_id} (Đã Kết Nối)`;
        } else {
            if (statusText) {
                statusText.innerText = 'Chưa đăng nhập';
                statusText.style.color = '#f59e0b';
            }
            if (btnLogout) btnLogout.style.display = 'none';
            if (loginForm) loginForm.style.display = 'block';
            if (syncSec) syncSec.style.display = 'none';
            if (btnAuthLabel) btnAuthLabel.innerText = 'Đăng Nhập Apple ID (Kéo Vị Trí Thật iCloud)';
        }
    }

    async function submitAppleLogin() {
        const email = (document.getElementById('apple-input-email').value || '').trim();
        const password = (document.getElementById('apple-input-password').value || '').trim();
        const btn = document.getElementById('btn-submit-apple-login');

        if (!email || !password) {
            showToast("Vui lòng nhập đầy đủ Apple ID và Mật khẩu!", true);
            return;
        }

        btn.disabled = true;
        btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Đang kết nối Anisette & Apple...';
        showToast("Đang xác thực với Apple iCloud qua Anisette...");

        try {
            const res = await fetch(`http://${nasIp}:8090/api/apple/login`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ apple_id: email, password: password })
            });
            const data = await res.json();

            if (res.ok) {
                if (data.status === 'require_2fa') {
                    showToast("Apple yêu cầu mã 2FA! Vui lòng kiểm tra iPhone.");
                    document.getElementById('apple-login-form').style.display = 'none';
                    document.getElementById('apple-2fa-form').style.display = 'block';
                    document.getElementById('apple-input-2fa').focus();
                } else if (data.status === 'success') {
                    showToast(data.message || "Đăng nhập iCloud thành công!");
                    checkAppleStatus();
                    syncAppleNowFromModal();
                }
            } else {
                showToast(data.detail || "Lỗi đăng nhập Apple ID!", true);
            }
        } catch (err) {
            showToast("Lỗi kết nối tới Backend (:8090)!", true);
        } finally {
            btn.disabled = false;
            btn.innerHTML = '<i class="fa-solid fa-lock"></i> Đăng Nhập iCloud & Lấy Token';
        }
    }

    async function submitApple2FA() {
        const code = (document.getElementById('apple-input-2fa').value || '').trim();
        const btn = document.getElementById('btn-submit-apple-2fa');

        if (code.length < 6) {
            showToast("Mã 2FA phải gồm 6 chữ số!", true);
            return;
        }

        btn.disabled = true;
        btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Đang xác thực 2FA...';

        try {
            const res = await fetch(`http://${nasIp}:8090/api/apple/2fa`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ code: code })
            });
            const data = await res.json();

            if (res.ok && data.status === 'success') {
                showToast("Xác thực 2FA thành công! Đã lưu phiên iCloud vĩnh viễn.");
                document.getElementById('apple-2fa-form').style.display = 'none';
                checkAppleStatus();
                syncAppleNowFromModal();
            } else {
                showToast(data.message || data.detail || "Mã 2FA không chính xác!", true);
            }
        } catch (err) {
            showToast("Lỗi kết nối xác thực 2FA!", true);
        } finally {
            btn.disabled = false;
            btn.innerHTML = 'Xác Nhận 2FA';
        }
    }

    async function logoutAppleAccount() {
        if (!confirm("Bạn có chắc chắn muốn đăng xuất tài khoản Apple ID khỏi NAS?")) return;
        try {
            await fetch(`http://${nasIp}:8090/api/apple/logout`, { method: 'POST' });
            showToast("Đã đăng xuất tài khoản Apple ID.");
            checkAppleStatus();
        } catch (err) {
            showToast("Lỗi khi đăng xuất!", true);
        }
    }

    async function syncAppleNowFromModal() {
        showToast("Đang kéo vị trí thật từ Apple Find My về NAS...");
        try {
            const res = await fetch(`http://${nasIp}:8090/api/apple/sync-now`, { method: 'POST' });
            if (res.ok) {
                const data = await res.json();
                showToast(data.message || "Đã đồng bộ xong dữ liệu từ Apple!");
                fetchBikeLivePosition(true);
            } else {
                showToast("Chưa kéo được báo cáo vị trí mới từ Apple.", true);
            }
        } catch (err) {
            showToast("Lỗi khi gọi đồng bộ Apple!", true);
        }
    }
"""

# Chèn JS trước hàm closeKeyModal hoặc cuối script
if "submitAppleLogin" not in html:
    html = html.replace("    // --- MODAL QUẢN LÝ KHÓA NẠP ESP32 ---", apple_js + "\n    // --- MODAL QUẢN LÝ KHÓA NẠP ESP32 ---")
    print("Injected Apple Authentication JS logic!")

# Cập nhật nguồn dữ liệu hiển thị đẹp mắt nếu là Real iCloud
source_patch_old = "document.getElementById('airtag-source').innerText = data.provider || 'Apple Find My Network';"
source_patch_new = """document.getElementById('airtag-source').innerText = data.provider || 'Apple Find My Network';
            const badgeStatus = document.getElementById('airtag-badge-status');
            const badgeText = document.getElementById('badge-text');
            if (data.provider && data.provider.includes('Real iCloud')) {
                document.getElementById('airtag-source').style.color = '#34d399';
                if (badgeStatus) {
                    badgeStatus.className = 'airtag-badge live-mode';
                    badgeStatus.querySelector('.live-dot').style.background = '#10b981';
                    badgeStatus.querySelector('.live-dot').style.boxShadow = '0 0 8px #10b981';
                }
                if (badgeText) badgeText.innerText = 'ONLINE (ICLOUD THẬT)';
            }"""

if source_patch_old in html:
    html = html.replace(source_patch_old, source_patch_new)
    print("Patched live source badge indicator!")

# Lưu lên NAS
with sftp.open('/home/nas152/graphhopper-data/index.html', 'w') as f:
    f.write(html)
print("Uploaded fully updated index.html with Apple ID Login to NAS successfully!")

sftp.close()
ssh.close()
