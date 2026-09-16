# Trip / round summary from trip_logger_node.
# Published on /trip/summary for the current round, and used to build the
# round history log.
bool   active           # true when a round is in progress
string course_name      # active course (empty if none)
string tee_name         # active tee (empty if none)
float64 distance_m      # distance driven this round (m)
float64 energy_wh       # energy used this round (Wh)
float64 duration_s      # round duration (s)
float64 avg_speed_mps   # average speed this round (m/s)
builtin_interfaces/Time timestamp