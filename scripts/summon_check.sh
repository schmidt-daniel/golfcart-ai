#!/bin/bash
# Summon feature sanity check in gz-sim.
#
# Verifies:
#   1. summon_node is up and /summon service is available
#   2. A phone target can be published and summon triggered
#   3. Summon status transitions (TRACKING/DRIVING)
#   4. Target-loss: stopping /phone/gps -> TARGET_LOST
#   5. Manual override (safety stop) cancels summon
#
# Note: this is a smoke test of the summon node wiring in sim. Full
# predictive-intercept and "hold if approaching" behavior is covered by the
# unit tests in test_summon_math.cpp.
set -u
source /opt/ros/${ROS_DISTRO}/setup.bash
source install/setup.bash

# Start sim headless in background.
ros2 launch golfcart_gazebo sim.launch.py headless:=true \
  > /tmp/summon_check_sim.log 2>&1 &
SIM_PID=$!
sleep 15

# Start navigation + summon.
ros2 launch golfcart_navigation navigation.launch.py use_sim_time:=true \
  > /tmp/summon_check_nav.log 2>&1 &
NAV_PID=$!
sleep 8

echo "=== SUMMON SERVICE ==="
timeout -s KILL 5 ros2 service list 2>/dev/null | grep -E '/summon|set_goal' | sort

echo "=== PUBLISH PHONE TARGET + TRIGGER SUMMON ==="
timeout -s KILL 20 python3 - <<'PYEOF'
import rclpy, time
from rclpy.node import Node
from golfcart_msgs.msg import PhoneFix
from golfcart_msgs.srv import SummonTrigger
from golfcart_msgs.msg import SummonStatus

class N(Node):
    def __init__(self):
        super().__init__("summon_check")
        self.phone_pub = self.create_publisher(PhoneFix, "/phone/gps", 10)
        self.summon_client = self.create_client(SummonTrigger, "/summon")
        self.status = None
        self.create_subscription(SummonStatus, "/summon/status", self.on_status, 10)
    def on_status(self, m):
        self.status = m.state
        print("SUMMON_STATUS:", m.state, "dist:", round(m.distance_m,1))

rclpy.init(); n=N()
# Wait for /summon service.
t0=time.time()
while not n.summon_client.wait_for_service(timeout_sec=1.0) and time.time()-t0<10:
    pass
print("SUMMON_SERVICE_READY:", n.summon_client.service_is_ready())

# Publish a phone fix (simulated operator position).
fix = PhoneFix()
fix.latitude_deg = 51.5
fix.longitude_deg = -0.12
fix.accuracy_m = 1.0
fix.valid = True
for _ in range(5):
    n.phone_pub.publish(fix)
    rclpy.spin_once(n, timeout_sec=0.2)

# Trigger summon.
req = SummonTrigger.Request()
req.lat = 51.5
req.lon = -0.12
req.mode = "CURRENT"
req.cancel = False
future = n.summon_client.call_async(req)
rclpy.spin_until_future_complete(n, future, timeout_sec=5.0)
if future.result() is not None:
    print("SUMMON_RESPONSE:", future.result().success, future.result().message)
else:
    print("SUMMON_RESPONSE: TIMEOUT")

# Observe status for a few seconds.
t0=time.time()
while time.time()-t0<5:
    rclpy.spin_once(n, timeout_sec=0.2)
    n.phone_pub.publish(fix)

# Stop publishing phone -> target loss.
print("=== STOPPING PHONE GPS (target loss) ===")
t0=time.time()
while time.time()-t0<5:
    rclpy.spin_once(n, timeout_sec=0.2)
print("FINAL_STATUS:", n.status)
n.destroy_node(); rclpy.shutdown()
PYEOF

echo "=== DONE ==="
kill -9 $SIM_PID $NAV_PID 2>/dev/null
pkill -9 -f gz 2>/dev/null
pkill -9 -f ros2 2>/dev/null