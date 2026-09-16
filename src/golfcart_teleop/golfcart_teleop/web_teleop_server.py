#!/usr/bin/env python3
"""Simple HTTP server to serve the web teleop page.

Serves the static web/ directory over HTTP so the operator can open the
teleop page in a browser. The page connects to rosbridge_server over
WebSocket (port 9090) for ROS 2 communication.

Also serves /trips (the round-history JSON log) so the trip history page can
display past rounds, and /camera.mjpeg (an MJPEG stream of /camera/image) so
the hazard camera view can show the live feed.
"""

import http.server
import io
import json
import os
import socketserver
import threading

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image


class TripHandler(http.server.SimpleHTTPRequestHandler):
    """Serves static files + the /trips and /camera.mjpeg endpoints."""

    trips_file = '/var/lib/golfcart/trips.json'
    # Latest JPEG frame + lock, set by the ROS node's camera subscription.
    latest_jpeg = None
    jpeg_lock = threading.Lock()

    def do_GET(self):
        if self.path == '/trips' or self.path == '/trips/':
            self._serve_trips()
            return
        if self.path == '/camera.mjpeg' or self.path == '/camera.mjpeg/':
            self._serve_camera()
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

    def _serve_camera(self):
        """Serve the latest camera frame as a multipart MJPEG stream."""
        self.send_response(200)
        self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=frame')
        self.send_header('Cache-Control', 'no-cache')
        self.end_headers()
        try:
            while True:
                with TripHandler.jpeg_lock:
                    jpeg = TripHandler.latest_jpeg
                if jpeg is None:
                    # No frame yet; keep the connection open and retry.
                    threading.Event().wait(0.2)
                    continue
                self.wfile.write(b'--frame\r\n')
                self.wfile.write(b'Content-Type: image/jpeg\r\n')
                self.wfile.write(b'Content-Length: ' +
                                 str(len(jpeg)).encode() + b'\r\n\r\n')
                self.wfile.write(jpeg)
                self.wfile.write(b'\r\n')
                self.wfile.flush()
                threading.Event().wait(0.2)
        except (BrokenPipeError, ConnectionResetError):
            pass


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

        # Subscribe to the camera feed and keep the latest JPEG for the MJPEG
        # endpoint. Degrades gracefully: no camera -> no frames -> blank view.
        self._camera_sub = self.create_subscription(
            Image, 'camera/image', self._on_camera, 1)

        handler = lambda *args, **kwargs: TripHandler(
            *args, directory=web_dir, **kwargs)

        self.httpd = socketserver.TCPServer(('', port), handler)
        self.get_logger().info(f'Open http://<host>:{port}/ in a browser')

        # Serve in a background thread so the ROS node can spin.
        self._thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self._thread.start()

    def _on_camera(self, msg):
        """Convert the latest RGB frame to JPEG for the MJPEG stream."""
        try:
            from PIL import Image as PILImage
            if msg.encoding != 'rgb8' or not msg.data:
                return
            w, h = msg.width, msg.height
            if w == 0 or h == 0 or len(msg.data) < w * h * 3:
                return
            img = PILImage.frombytes('RGB', (w, h), bytes(msg.data))
            buf = io.BytesIO()
            img.save(buf, format='JPEG', quality=70)
            with TripHandler.jpeg_lock:
                TripHandler.latest_jpeg = buf.getvalue()
        except Exception:
            # Never let a bad frame crash the server.
            pass

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
