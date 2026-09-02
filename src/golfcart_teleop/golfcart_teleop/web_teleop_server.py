#!/usr/bin/env python3
"""Simple HTTP server to serve the web teleop page.

Serves the static web/ directory over HTTP so the operator can open the
teleop page in a browser. The page connects to rosbridge_server over
WebSocket (port 9090) for ROS 2 communication.
"""

import http.server
import os
import socketserver

import rclpy
from rclpy.node import Node


class WebServerNode(Node):
    def __init__(self):
        super().__init__('web_teleop_server')
        self.declare_parameter('port', 8080)
        self.declare_parameter('web_dir', '')
        port = self.get_parameter('port').value
        web_dir = self.get_parameter('web_dir').value

        # Default to the installed web/ directory if not overridden.
        if not web_dir:
            web_dir = os.path.join(
                os.path.dirname(os.path.abspath(__file__)), '..', 'web')

        web_dir = os.path.abspath(web_dir)
        self.get_logger().info(f'Serving web teleop from {web_dir} on port {port}')

        handler = lambda *args, **kwargs: http.server.SimpleHTTPRequestHandler(
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
