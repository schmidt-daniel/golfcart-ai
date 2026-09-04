import rclpy, time
from rclpy.node import Node
from golfcart_msgs.msg import GpsFix
from sensor_msgs.msg import LaserScan
class P(Node):
    def __init__(self):
        super().__init__("p")
        self.gps = self.create_publisher(GpsFix, "/gps/fix", 10)
        self.scan = self.create_publisher(LaserScan, "/scan", 10)
rclpy.init(); n=P()
for lat, lon in [(0.5,0.5),(0.6,0.6),(3.0,3.0),(0.7,0.7),(0.8,0.8),(4.0,4.0)]:
    g = GpsFix(); g.valid=True; g.latitude_deg=lat; g.longitude_deg=lon
    n.gps.publish(g)
    s = LaserScan(); s.header.stamp = n.get_clock().now().to_msg()
    n.scan.publish(s)
    time.sleep(0.5)
print("published")
n.destroy_node(); rclpy.shutdown()
