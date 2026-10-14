import paramiko
import sys

sys.stdout.reconfigure(encoding='utf-8')
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect('192.168.1.114', username='nas152', password='271000', timeout=5)

cmd = "docker exec findmy_backend_service python -c 'import findmy; print(dir(findmy))'"
stdin, stdout, stderr = ssh.exec_command(cmd)
print("findmy exports:", stdout.read().decode('utf-8', errors='ignore').strip())

cmd2 = "docker exec findmy_backend_service python -c 'from findmy import AppleAccount, RemoteAnisetteProvider; print(AppleAccount, RemoteAnisetteProvider)'"
stdin, stdout, stderr = ssh.exec_command(cmd2)
print("Classes:", stdout.read().decode('utf-8', errors='ignore').strip())

ssh.close()
