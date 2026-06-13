#!/usr/bin/env python2
"""
AutoCore File Server - 支持上传/下载
用法: python file_server.py
然后浏览器访问 http://192.168.1.100:8000
"""
import SimpleHTTPServer
import BaseHTTPServer
import cgi
import os

PORT = 8000
DIR = "/root"

class Handler(SimpleHTTPServer.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        SimpleHTTPServer.SimpleHTTPRequestHandler.__init__(self, *args, **kwargs)

    def do_POST(self):
        form = cgi.FieldStorage(
            fp=self.rfile,
            headers=self.headers,
            environ={"REQUEST_METHOD": "POST"})
        file_item = form["file"]
        if file_item.filename:
            filename = os.path.basename(file_item.filename)
            path = os.path.join(DIR, filename)
            with open(path, "wb") as f:
                f.write(file_item.file.read())
            self.send_response(200)
            self.send_header("Content-type", "text/plain")
            self.end_headers()
            self.wfile.write("OK: %s (%d bytes)" % (filename, os.path.getsize(path)))
        else:
            self.send_response(400)
            self.end_headers()
            self.wfile.write("No file")

    def do_GET(self):
        if self.path == "/":
            self.send_response(200)
            self.send_header("Content-type", "text/html")
            self.end_headers()
            self.wfile.write("<html><body>")
            self.wfile.write("<h2>AutoCore File Transfer</h2>")
            self.wfile.write('<form method="post" enctype="multipart/form-data">')
            self.wfile.write('<input type="file" name="file">')
            self.wfile.write('<input type="submit" value="Upload">')
            self.wfile.write('</form><hr><ul>')
            for f in sorted(os.listdir(DIR)):
                path = os.path.join(DIR, f)
                if os.path.isfile(path):
                    size = os.path.getsize(path)
                    self.wfile.write('<li><a href="/%s">%s</a> (%d bytes)</li>' % (f, f, size))
            self.wfile.write("</ul></body></html>")
        else:
            SimpleHTTPServer.SimpleHTTPRequestHandler.do_GET(self)

    def translate_path(self, path):
        return os.path.join(DIR, path.lstrip("/"))

httpd = BaseHTTPServer.HTTPServer(("0.0.0.0", PORT), Handler)
print("AutoCore File Server at http://0.0.0.0:%d" % PORT)
print("Directory: %s" % DIR)
httpd.serve_forever()
