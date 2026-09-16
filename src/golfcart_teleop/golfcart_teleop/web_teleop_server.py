#!/usr/bin/env python3
"""Simple HTTP server to serve the web teleop page.

Serves the static web/ directory over HTTP so the operator can open the
teleop page in a browser. The page connects to rosbridge_server over
WebSocket (port 9090) for ROS 2 communication.

Also serves /trips (the round-history JSON log) so the trip history page can
display past rounds.
"""

import http.server
import json
import os
import socketserver

import rclpy
from rclpy.node import Node


class TripHandler(http.server.SimpleHTTPRequestHandler):
    """Serves static files + the /trips round-history endpoint."""

    trips_file = '/var/lib/golfcart/trips.json'

    def do_GET(self):
        if self.path == '/trips' or self.path == '/trips/':
            self._serve_trips()
            return
        return super().do_GET()

    def _serve_trips(self):
        try:
            with open(self.trips_file) as f:
                # JSON Lines -> JSON array.
                entries = [json.loads(line) for line in f if line.strip()]
        except FileNotFoundError:
            entries = []
        except json.JSONDecodeError:
            entries = []
        body = json.dumps(entries).encode('utf-8')
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class WebServerNode(Node):
    def __init__(self):
        super().__init__('web_teleop_server')
        self.declare_parameter('port', 8080)
        self.declare_parameter('web_dir', '')
        self.declare_parameter('trips_file', '/var/lib/golfcart/trips.json')
        port = self.get_parameter('port').value
        web_dir = self.get_parameter('web_dir').value
        trips_file = self.get_parameter('trips_file').value

        # Default to the installed web/ directory if not overridden.
        if not web_dir:
            web_dir = os.path.join(
                os.path.dirname(os.path.abspath(__file__)), '..', 'web')

        web_dir = os.path.abspath(web_dir)
        TripHandler.trips_file = trips_file
        self.get_logger().info(f'Serving web teleop from {web_dir} on port {port}')

        handler = lambda *args, **kwargs: TripHandler(
            *args, directory=web_dir, **kwargs)

        self.httpd = socketserver.TCPServer(('', port), handler)
        self.get_logger().info(f'Open http://<host>:{port}/ in a browser')

        # Serve in a background thread so the ROS node can spin.
        import threading
        self._thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self._thread.start()

    def destroy_node(self):
        self.httpd.shutdown()
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = WebServerNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
