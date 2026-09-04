#!/bin/bash
# Quick sanity check for the gz-sim golf cart: verify sim runs and cart moves.
set -u
source /opt/ros/${ROS_DISTRO}/setup.bash
source install/setup.bash

# Start sim headless in background.
ros2 launch golfcart_gazebo sim.launch.py headless:=true \
  > /tmp/sim_check_launch.log 2>&1 &
LPID=$!
sleep 15

echo "=== TOPICS ==="
timeout -s KILL 5 ros2 topic list 2>/dev/null | grep -iE 'cmd_vel|odom|scan|imu|gps' | sort

echo "=== SIM TIME (twice, should advance) ==="
timeout -s KILL 3 gz topic -e -n 1 -t /world/golf_course/clock 2>/dev/null | grep -A4 'sim {' | head -6 || echo "no sim clock"
sleep 2
timeout -s KILL 3 gz topic -e -n 1 -t /world/golf_course/clock 2>/dev/null | grep -A4 'sim {' | head -6 || echo "no sim clock 2"

echo "=== WHEEL + ODOM (command velocity, check movement) ==="
timeout -s KILL 15 python3 - <<'PYEOF'
import rclpy, time
from rclpy.node import Node
from nav_msgs.msg import Odometry
from sensor_msgs.msg import JointState
from geometry_msgs.msg import Twist
class N(Node):
    def __init__(self):
        super().__init__("n")
        self.pub = self.create_publisher(Twist, "/cmd_vel", 10)
        self.pos=None; self.js=None
        self.create_subscription(Odometry,"/odom",lambda m:setattr(self,"pos",m.pose.pose.position.x),10)
        self.create_subscription(JointState,"/joint_states",self.jscb,10)
    def jscb(self,m):
        d=dict(zip(m.name,m.velocity)); self.js=d
rclpy.init(); n=N()
t0=time.time()
while time.time()-t0<3: rclpy.spin_once(n,timeout_sec=0.1)
x0=n.pos
tw=Twist(); tw.linear.x=0.5
t1=time.time()
while time.time()-t1<3:
    rclpy.spin_once(n,timeout_sec=0.1); n.pub.publish(tw)
print("ODOM_X_START:",x0)
print("ODOM_X_END:",n.pos)
print("WHEEL_V:",n.js.get("rear_left_wheel_joint"),n.js.get("rear_right_wheel_joint"))
n.destroy_node(); rclpy.shutdown()
PYEOF

echo "=== DONE ==="
kill -9 $LPID 2>/dev/null
pkill -9 -f gz 2>/dev/null
pkill -9 -f ros2 2>/dev/null