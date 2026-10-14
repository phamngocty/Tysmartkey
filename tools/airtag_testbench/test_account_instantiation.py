import paramiko
import sys

sys.stdout.reconfigure(encoding='utf-8')
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect('192.168.1.114', username='nas152', password='271000', timeout=5)

cmd = """docker exec findmy_backend_service python -c "
from findmy import AppleAccount, RemoteAnisetteProvider
provider = RemoteAnisetteProvider('http://anisette:6969')
acc = AppleAccount(provider)
print('AppleAccount initialized successfully with Anisette:', acc)
" """
stdin, stdout, stderr = ssh.exec_command(cmd)
print("AppleAccount Test:\n", stdout.read().decode('utf-8', errors='ignore').strip())
err = stderr.read().decode('utf-8', errors='ignore').strip()
if err:
    print("Error:\n", err)

ssh.close()
