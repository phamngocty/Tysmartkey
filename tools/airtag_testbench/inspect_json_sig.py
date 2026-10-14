import paramiko
import sys

sys.stdout.reconfigure(encoding='utf-8')
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect('192.168.1.114', username='nas152', password='271000', timeout=5)

cmd = """docker exec findmy_backend_service python -c "
import inspect
from findmy import AppleAccount
print('from_json:', inspect.signature(AppleAccount.from_json))
print('to_json:', inspect.signature(AppleAccount.to_json))
" """
stdin, stdout, stderr = ssh.exec_command(cmd)
print("from_json / to_json signatures:\n", stdout.read().decode('utf-8', errors='ignore').strip())

ssh.close()
