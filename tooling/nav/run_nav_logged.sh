#!/usr/bin/env bash
# run_nav.sh with its output also saved to /tmp/robot_nav_<mode>.log (for troubleshooting).
bash "$(dirname "$0")/run_nav.sh" "$@" 2>&1 | tee "/tmp/robot_nav_${1:-slam}.log"
