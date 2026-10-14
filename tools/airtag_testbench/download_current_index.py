import paramiko
import sys

sys.stdout.reconfigure(encoding='utf-8')
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect('192.168.1.114', username='nas152', password='271000', timeout=5)

sftp = ssh.open_sftp()
sftp.get('/home/nas152/graphhopper-data/index.html', r'C:\Users\phamn\.gemini\antigravity-ide\brain\558de474-fceb-43ea-820d-bef55c4250e8\scratch\current_nas_index.html')
print('Downloaded current_nas_index.html successfully!')
sftp.close()
ssh.close()
