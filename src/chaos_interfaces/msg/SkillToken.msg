# One discrete team-skill decision, as published by an agent or accepted by a
# skill runtime. The vocabulary lives in chaos_assets; this message carries a
# single chosen token plus the context that produced it.

# The chosen skill, e.g. "navigate_to", "takeoff", "scan_area", "hold", "done".
string token
# JSON-encoded arguments of the skill (target pose, altitude, area id...).
# Empty means the skill takes no arguments.
string arguments_json
# ROS time when the decision was produced by the planner. The acceptance
# deadlines of a request use Unix UTC; this stamp is an observation.
builtin_interfaces/Time decided_at
# Identifier of the producing agent (asset id or "operator").
string source
