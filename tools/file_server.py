#!/usr/bin/env python3
"""
简单的文件传输服务器（支持上传和下载）
在板子上运行，Windows 浏览器访问 http://192.168.1.100:8000

上传：选择文件 → 点击 Upload
下载：点击文件名
"""
import http.server
import os
import cgi

UPLOAD_DIR = "/root"

class FileTransferHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=UPLOAD_DIR, **kwargs)

    def do_POST(self):
        """处理文件上传"""
        form = cgi.FieldStorage(
            fp=self.rfile,
            headers=self.headers,
            environ={"REQUEST_METHOD": "POST"}
        )
        file_item = form["file"]
        if file_item.filename:
            # 安全处理文件名
            filename = os.path.basename(file_item.filename)
            path = os.path.join(UPLOAD_DIR, filename)
            with open(path, "wb") as f:
                f.write(file_item.file.read())
            self.send_response(200)
            self.send_header("Content-type", "text/html")
            self.end_headers()
            self.wfile.write(f"OK: {filename} uploaded ({os.path.getsize(path)} bytes)".encode())
        else:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b"No file received")

    def do_GET(self):
        """列出文件 + 上传表单"""
        if self.path == "/":
            self.send_response(200)
            self.send_header("Content-type", "text/html")
            self.end_headers()
            self.wfile.write(b"<html><body>")
            self.wfile.write(b"<h2>AutoCore File Transfer</h2>")
            # 上传表单
            self.wfile.write(b"""
            <h3>Upload to Board:</h3>
            <form method="post" enctype="multipart/form-data">
              <input type="file" name="file">
              <input type="submit" value="Upload">
            </form>
            <hr>
            <h3>Download from Board:</h3>
            <ul>
            """)
            for f in sorted(os.listdir(UPLOAD_DIR)):
                if os.path.isfile(os.path.join(UPLOAD_DIR, f)):
                    size = os.path.getsize(os.path.join(UPLOAD_DIR, f))
                    self.wfile.write(f'<li><a href="/{f}">{f}</a> ({size} bytes)</li>'.encode())
            self.wfile.write(b"</ul></body></html>")
        else:
            super().do_GET()

if __name__ == "__main__":
    port = 8000
    print(f"AutoCore File Server on http://0.0.0.0:{port}")
    print(f"Directory: {UPLOAD_DIR}")
    http.server.HTTPServer(("0.0.0.0", port), FileTransferHandler).serve_forever()
