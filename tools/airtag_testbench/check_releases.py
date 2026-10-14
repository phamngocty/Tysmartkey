import urllib.request
import json

url = "https://api.github.com/repos/kenhtaymay/openhaystack/releases"
req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
try:
    with urllib.request.urlopen(req) as resp:
        releases = json.loads(resp.read().decode("utf-8"))
        for r in releases:
            tag = r.get("tag_name")
            name = r.get("name")
            body = r.get("body", "")
            assets = [a.get("name") + " (" + a.get("browser_download_url") + ")" for a in r.get("assets", [])]
            print(f"=== TAG: {tag} ({name}) ===")
            print(f"Body: {body[:300]}")
            print(f"Assets: {assets}")
except Exception as e:
    print(f"Error: {e}")
