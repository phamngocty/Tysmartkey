import paramiko
import sys

sys.stdout.reconfigure(encoding='utf-8')
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect('192.168.1.114', username='nas152', password='271000', timeout=5)

cmd = """docker exec findmy_backend_service python -c "
from findmy import RemoteAnisetteProvider
provider = RemoteAnisetteProvider('http://anisette:6969')
headers = provider.get_headers()
print('Anisette headers received:', list(headers.keys()))
" """
stdin, stdout, stderr = ssh.exec_command(cmd)
print("Anisette Test:\n", stdout.read().decode('utf-8', errors='ignore').strip())
err = stderr.read().decode('utf-8', errors='ignore').strip()
if err:
    print("Error:\n", err)

ssh.close()
