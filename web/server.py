#!/usr/bin/env python3
"""
Simple HTTP server for AFOS web emulator
Run this script to start a local web server
"""

import http.server
import socketserver
import os
import sys

PORT = 8000

class MyHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # Add CORS headers for local development
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', '*')
        # Add MIME type for ISO files
        if self.path.endswith('.iso'):
            self.send_header('Content-Type', 'application/octet-stream')
        super().end_headers()

    def log_message(self, format, *args):
        # Custom log format
        sys.stderr.write("%s - - [%s] %s\n" %
                        (self.address_string(),
                         self.log_date_time_string(),
                         format % args))

if __name__ == "__main__":
    # Change to the script's directory
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    
    # Check if afos.iso exists
    if not os.path.exists('afos.iso'):
        print("WARNING: afos.iso not found!")
        print("Please run 'make web' from the project root to build the ISO.")
        print()
    
    Handler = MyHTTPRequestHandler
    
    with socketserver.TCPServer(("", PORT), Handler) as httpd:
        print(f"AFOS Web Server starting on http://localhost:{PORT}")
        print(f"Serving directory: {os.getcwd()}")
        print()
        print("Press Ctrl+C to stop the server")
        print()
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped.")

