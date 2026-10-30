import http.server, socketserver, subprocess, os, json, threading, time

FFMPEG = r"G:\Hermes Portable\apps\windows\hermes-desktop-bundled\resources\agent-payload\tools\ffmpeg-9.0.1-win32-x64\bin\ffmpeg.exe"
RTSP   = "rtsp://192.168.0.136:554/live/0"
SNAP   = r"G:\PedestrianRiskDecision\tools\rtsp_snapshot.jpg"
HTML   = r"G:\PedestrianRiskDecision\tools\roi_live.html"
BOARD  = "192.168.0.136"
PORT   = 8088
INTERVAL = 2.0  # seconds between frame grabs

# ---- background frame grabber ----
def grabber():
    while True:
        subprocess.run([FFMPEG, "-y", "-rtsp_transport", "tcp", "-i", RTSP,
                        "-frames:v", "1", "-q:v", "3", SNAP],
                       capture_output=True, timeout=12)
        time.sleep(INTERVAL)

threading.Thread(target=grabber, daemon=True).start()
time.sleep(3)  # wait for first frame
print(f"✅ Frame grabber running ({INTERVAL}s) — {os.path.getsize(SNAP)}B")

# ---- HTTP server ----
class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=r"G:\PedestrianRiskDecision\tools", **kwargs)

    def do_GET(self):
        if self.path == '/' or self.path == '/index.html':
            self.path = '/roi_live.html'
        elif self.path.startswith('/snapshot'):
            self.path = '/rtsp_snapshot.jpg'
        elif self.path == '/roi':
            self.send_response_json(get_roi_config())
            return
        super().do_GET()

    def do_POST(self):
        if self.path == '/roi':
            try:
                n = int(self.headers['Content-Length']); body = self.rfile.read(n)
                roi = json.loads(body)
                send_to_board(roi)
                self.send_response_json({"ok": True, "msg": "ROI sent to board"})
            except Exception as e:
                self.send_response_json({"error": str(e)}, 500)
        else:
            self.send_response(404); self.end_headers()

    def send_response_json(self, data, code=200):
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())

    def do_OPTIONS(self):
        self.send_response(200)
        for h in ['Access-Control-Allow-Origin','Access-Control-Allow-Methods','Access-Control-Allow-Headers']:
            self.send_header(h, '*')
        self.end_headers()

def get_roi_config():
    return {"message": "roi config on board — open http://192.168.0.136:8088/roi for current"}

def send_to_board(roi):
    """POST the ROI config to the board's running roi_server.py, which writes JSON and restarts"""
    import urllib.request
    try:
        req = urllib.request.Request(
            f"http://{BOARD}:8088/roi",
            data=json.dumps(roi).encode(),
            headers={"Content-Type": "application/json"},
            method="POST")
        urllib.request.urlopen(req, timeout=5)
    except Exception as e:
        print(f"  Board relay error: {e}")

socketserver.TCPServer.allow_reuse_address = True
httpd = socketserver.TCPServer(("0.0.0.0", PORT), H)
print(f"🚀 打开 http://localhost:{PORT}/  — 本机 ffmpeg 抓 RTSP 帧 + 画框")
httpd.serve_forever()