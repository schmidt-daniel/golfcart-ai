# Map Editor → ROS Integration (Course Reload)

Let the cart pick up a newly deployed course Zip without restarting.

> **Status:** Implemented (small version). The core editor→ROS integration
> (course Zip → `CourseMap` → Nav2 `static_layer`) was already in place; this
> adds a live-reload service so a re-deployed course is picked up on demand.

## Purpose

The map editor exports a course as a Zip bundle. `course_registry_node` scans
a directory of Zips and publishes the `CourseMap` + costmap. Previously, a
newly deployed Zip required a node restart to be seen. This feature adds a
`/course/reload` service so the registry re-scans on demand.

## How it works

- **`course_registry_node`** already scans `courses_dir` for `*.zip`, publishes
  `/course/list`, serves `/course/select`, and publishes `CourseMap` on
  `/course/map` + the costmap on `/map` (Nav2 `static_layer`).
- **`CourseReload.srv`** (no request fields) + the **`/course/reload`** service
  re-run `scan_courses()` + `publish_list()`, so a newly deployed Zip appears
  in `/course/list` without a restart.

## Usage

```bash
# After copying a new course Zip into the courses dir:
ros2 service call /course/reload golfcart_msgs/srv/CourseReload "{}"
```

## Files

- `src/golfcart_msgs/srv/CourseReload.srv` — the service definition
- `src/golfcart_navigation/src/course_registry_node.cpp` — the `/course/reload`
  service handler