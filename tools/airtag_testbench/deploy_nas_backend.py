import paramiko
import sys

sys.stdout.reconfigure(encoding='utf-8')
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect('192.168.1.114', username='nas152', password='271000', timeout=5)

sftp = ssh.open_sftp()
sftp.put(r'c:\Users\phamn\Documents\PlatformIO\Tysmartkey\tools\airtag_testbench\backend_testbench.py', '/home/nas152/airtag_testbench/backend_testbench.py')
print('Uploaded backend_testbench.py to NAS successfully!')
sftp.close()

stdin, stdout, stderr = ssh.exec_command('docker restart findmy_backend_service')
print('Restart container output:', stdout.read().decode('utf-8', errors='ignore').strip())
err = stderr.read().decode('utf-8', errors='ignore').strip()
if err:
    print('Error:', err)

ssh.close()
