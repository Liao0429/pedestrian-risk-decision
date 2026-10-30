#!/usr/bin/env python3
"ROI HTTP server - 接收网页画框坐标,写入JSON"
from http.server import HTTPServer, BaseHTTPRequestHandler
import json, os, time

CONFIG = "/home/pico/daima/luckfox_pico_rtsp_yolov5_demo/config/default_config.json"

class H(BaseHTTPRequestHandler):
    def do_OPTIONS(self):
        self.send_response(200); self.send_header('Access-Control-Allow-Origin','*'); self.send_header('Access-Control-Allow-Methods','POST,GET,OPTIONS'); self.send_header('Access-Control-Allow-Headers','Content-Type'); self.end_headers()
    def do_GET(self):
        self.send_response(200); self.send_header('Access-Control-Allow-Origin','*'); self.end_headers()
        if os.path.exists(CONFIG):
            with open(CONFIG) as f: cfg=json.load(f)
            self.wfile.write(json.dumps(cfg.get('roi_zone',{}),indent=2,ensure_ascii=False).encode())
        else: self.wfile.write(b'{}')
    def do_POST(self):
        n=int(self.headers.get('Content-Length',0)); body=self.rfile.read(n)
        try:
            roi=json.loads(body); cfg={}
            if os.path.exists(CONFIG):
                with open(CONFIG) as f: cfg=json.load(f)
            cfg['roi_zone']=roi
            with open(CONFIG,'w') as f: json.dump(cfg,f,indent=2,ensure_ascii=False)
            self.send_response(200); self.send_header('Access-Control-Allow-Origin','*'); self.end_headers()
            self.wfile.write(b'{"ok":true}')
        except Exception as e:
            self.send_response(400); self.send_header('Access-Control-Allow-Origin','*'); self.end_headers()
            self.wfile.write(json.dumps({"error":str(e)}).encode())
    def log_message(self,*a): pass

HTTPServer(('0.0.0.0',8088),H).serve_forever()