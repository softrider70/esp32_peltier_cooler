#!/usr/bin/env python3
"""
DSM API für ESP32 Cooler Integration (Python HTTP-Server)
Empfang und Abruf von Energie-Verbrauchsdaten auf Port 8080

POST: Daten von ESP32 empfangen und speichern
GET: Daten an ESP32 zurückgeben
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import os
import datetime
from urllib.parse import urlparse, parse_qs

# Konfiguration
BASE_DIR = '/volume1/web/espcooler_data'
SESSIONS_DIR = os.path.join(BASE_DIR, 'sessions')
STATS_DIR = os.path.join(BASE_DIR, 'stats')
PORT = 8080

# Verzeichnisse erstellen falls nicht vorhanden
os.makedirs(SESSIONS_DIR, exist_ok=True)
os.makedirs(STATS_DIR, exist_ok=True)

class APIHandler(BaseHTTPRequestHandler):
    def _set_headers(self, status=200):
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, GET')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def do_OPTIONS(self):
        self._set_headers(200)

    def do_POST(self):
        # POST: Daten empfangen und speichern
        try:
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)
            data = json.loads(post_data.decode('utf-8'))

            if not data:
                self._set_headers(400)
                self.wfile.write(json.dumps({'error': 'Invalid JSON'}).encode('utf-8'))
                return

            # Sessions speichern
            if 'sessions' in data and isinstance(data['sessions'], list):
                for session in data['sessions']:
                    timestamp = session.get('timestamp', 0)
                    dt = datetime.datetime.fromtimestamp(timestamp)
                    filename = dt.strftime('%Y%m%d_%H%M%S') + '_session.json'
                    filepath = os.path.join(SESSIONS_DIR, filename)
                    with open(filepath, 'w') as f:
                        json.dump(session, f, indent=2)

            # Stats speichern
            if 'stats' in data:
                date = datetime.datetime.now().strftime('%Y-%m-%d')
                filename = date + '_stats.json'
                filepath = os.path.join(STATS_DIR, filename)
                with open(filepath, 'w') as f:
                    json.dump(data['stats'], f, indent=2)

            self._set_headers(200)
            self.wfile.write(json.dumps({'success': True, 'message': 'Data saved'}).encode('utf-8'))

        except Exception as e:
            self._set_headers(500)
            self.wfile.write(json.dumps({'error': str(e)}).encode('utf-8'))

    def do_GET(self):
        # GET: Daten abrufen
        try:
            parsed = urlparse(self.path)
            params = parse_qs(parsed.query)
            action = params.get('action', [''])[0]

            if action == 'list' or action == '':
                # Alle Sessions zurückgeben
                sessions = []
                files = sorted(os.listdir(SESSIONS_DIR), reverse=True)
                for filename in files:
                    if filename.endswith('_session.json'):
                        with open(os.path.join(SESSIONS_DIR, filename)) as f:
                            sessions.append(json.load(f))

                self._set_headers(200)
                self.wfile.write(json.dumps({'sessions': sessions}).encode('utf-8'))

            elif action == 'stats':
                # Alle Stats zurückgeben
                stats = []
                files = sorted(os.listdir(STATS_DIR), reverse=True)
                for filename in files:
                    if filename.endswith('_stats.json'):
                        with open(os.path.join(STATS_DIR, filename)) as f:
                            stats.append(json.load(f))

                self._set_headers(200)
                self.wfile.write(json.dumps({'stats': stats}).encode('utf-8'))

            elif action == 'all':
                # Sessions + Stats zurückgeben
                sessions = []
                files = sorted(os.listdir(SESSIONS_DIR), reverse=True)
                for filename in files:
                    if filename.endswith('_session.json'):
                        with open(os.path.join(SESSIONS_DIR, filename)) as f:
                            sessions.append(json.load(f))

                stats = []
                files = sorted(os.listdir(STATS_DIR), reverse=True)
                for filename in files:
                    if filename.endswith('_stats.json'):
                        with open(os.path.join(STATS_DIR, filename)) as f:
                            stats.append(json.load(f))

                self._set_headers(200)
                self.wfile.write(json.dumps({'sessions': sessions, 'stats': stats}).encode('utf-8'))

            else:
                self._set_headers(405)
                self.wfile.write(json.dumps({'error': 'Method not allowed'}).encode('utf-8'))

        except Exception as e:
            self._set_headers(500)
            self.wfile.write(json.dumps({'error': str(e)}).encode('utf-8'))

    def log_message(self, format, *args):
        # Logging deaktivieren
        pass

def run_server():
    server = HTTPServer(('0.0.0.0', PORT), APIHandler)
    print(f'Server läuft auf Port {PORT}')
    server.serve_forever()

if __name__ == '__main__':
    run_server()
