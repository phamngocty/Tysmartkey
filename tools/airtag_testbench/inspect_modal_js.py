import paramiko
import sys

sys.stdout.reconfigure(encoding='utf-8')
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect('192.168.1.114', username='nas152', password='271000', timeout=5)

sftp = ssh.open_sftp()
with sftp.open('/home/nas152/graphhopper-data/index.html', 'r') as f:
    html = f.read().decode('utf-8', errors='ignore')

lines = html.splitlines()
print("\n--- Modal & JS lines ---")
for i, l in enumerate(lines):
    if 'openKeyModal' in l or 'airtag-modal' in l or 'pollAirtagLive' in l:
        print(f"{i+1}: {l.strip()[:100]}")

sftp.close()
ssh.close()
