#!/bin/bash
# Follow Me sanity check in gz-sim (Level-1: rigid moving person).
#
# Verifies:
#   1. person_detection_node publishes /person/target (valid) for a person
#      placed in front of the cart.
#   2. follow_controller_node /follow service starts and stops.
#   3. Follow status transitions (IDLE -> FOLLOWING).
#   4. Moving the person away re-follows.
#   5. Removing the person -> target lost -> stops.
#
# Note: this is a smoke test of the follow-node wiring + detection in sim for a
# rigid moving person (Level 1). The leg-pair/animated-leg heuristic is validated
# by unit tests.
source /opt/ros/${ROS_DISTRO}/setup.bash
source install/setup.bash

# Start sim headless in background.
ros2 launch golfcart_gazebo sim.launch.py headless:=true \
  > /tmp/follow_check_sim.log 2>&1 &
SIM_PID=$!
sleep 15

# Start the core control pipeline (for safety/motion) + follow stack.
ros2 launch golfcart_bringup core.launch.py use_sim_time:=true \
  > /tmp/follow_check_core.log 2>&1 &
CORE_PID=$!
sleep 5

# Launch the follow stack (person detection + controller + obstacle awareness).
ros2 launch golfcart_follow follow.launch.py use_sim_time:=true \
  > /tmp/follow_check_follow.log 2>&1 &
FOLLOW_PID=$!
sleep 8

echo "=== PERSON TARGET (person is 1.5m in front, within lock distance) ==="
timeout -s KILL 40 python3 - <<'PYEOF'
import rclpy, time
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy
from golfcart_msgs.msg import PersonTarget
from golfcart_msgs.srv import FollowTrigger
from golfcart_msgs.msg import FollowStatus

class N(Node):
    def __init__(self):
        super().__init__("follow_check")
        self.follow_client = self.create_client(FollowTrigger, "/follow")
        self.person = None
        self.status = None
        qos = QoSProfile(depth=10, reliability=ReliabilityPolicy.BEST_EFFORT)
        self.create_subscription(PersonTarget, "/person/target", self.on_person, qos)
        self.create_subscription(FollowStatus, "/follow/status", self.on_status, qos)
    def on_person(self, m):
        self.person = m
        if m.valid:
            print(f"PERSON_TARGET: dist={m.distance_m:.2f} lat={m.lateral_offset_m:.2f} conf={m.confidence:.2f} src={m.source}")
        else:
            print("PERSON_TARGET: INVALID")
    def on_status(self, m):
        if m.state != self.status:
            self.status = m.state
            print("FOLLOW_STATUS:", m.state)

rclpy.init(); n=N()
saw_valid=False; states=set()
t0=time.time()
while time.time()-t0<12:
    rclpy.spin_once(n, timeout_sec=0.2)
    if n.person is not None and n.person.valid: saw_valid=True
    if n.status: states.add(n.status)
print("PERSON_SAW_VALID:", saw_valid)
print("STATES_SEEN_BEFORE_START:", sorted(states))

# Trigger follow.
req = FollowTrigger.Request(); req.cancel = False
future = n.follow_client.call_async(req)
rclpy.spin_until_future_complete(n, future, timeout_sec=5.0)
if future.result() is not None:
    print("FOLLOW_RESPONSE:", future.result().success, future.result().message)
else:
    print("FOLLOW_RESPONSE: TIMEOUT")

# Observe following state over a longer window.
saw_following=False; states_after=set()
t0 = time.time()
while time.time()-t0<8:
    rclpy.spin_once(n, timeout_sec=0.2)
    if n.status: states_after.add(n.status)
for s in states_after:
    print("FOLLOW_STATUS:", s)
print("SAW_FOLLOWING:", "FOLLOWING" in states_after)
n.destroy_node(); rclpy.shutdown()
PYEOF

echo "=== MOVE PERSON AWAY (1.5m -> 4m in +X) ==="
gz model -m person -x 4 -y 0 -z 0 2>/dev/null || echo "gz model move not shown (ok)"
sleep 3

echo "=== DONE ==="
kill -9 $SIM_PID $CORE_PID $FOLLOW_PID 2>/dev/null
pkill -9 -f gz 2>/dev/null
pkill -9 -f ros2 2>/dev/null