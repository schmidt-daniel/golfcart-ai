# Published by course_registry_node when a course is selected/loaded.
# Carries the zip path so course_session_node can parse the full hole data
# (distances, boundary, features) directly from the course Zip.
string course_id
string zip_path
builtin_interfaces/Time timestamp