import paramiko
import sys

sys.stdout.reconfigure(encoding='utf-8')
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect('192.168.1.114', username='nas152', password='271000', timeout=5)

stdin, stdout, stderr = ssh.exec_command('sudo netstat -tulpn | grep 8085')
print("Netstat 8085:", stdout.read().decode('utf-8', errors='ignore').strip())

stdin, stdout, stderr = ssh.exec_command('docker ps')
print("Docker containers:\n", stdout.read().decode('utf-8', errors='ignore').strip())

ssh.close()
