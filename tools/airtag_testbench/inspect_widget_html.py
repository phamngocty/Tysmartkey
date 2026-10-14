import paramiko
import sys

sys.stdout.reconfigure(encoding='utf-8')
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect('192.168.1.114', username='nas152', password='271000', timeout=5)

sftp = ssh.open_sftp()
with sftp.open('/home/nas152/graphhopper-data/index.html', 'r') as f:
    html = f.read().decode('utf-8', errors='ignore')

# Print lines 870 to 930
lines = html.splitlines()
print("\n--- Widget HTML (Lines 870-930) ---")
for i in range(869, min(940, len(lines))):
    print(f"{i+1}: {lines[i]}")

sftp.close()
ssh.close()
