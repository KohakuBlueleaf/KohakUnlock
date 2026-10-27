#!/bin/sh
# Shared by the real boot gate and CPU-only tests. Missing values fail closed.
kohak_trial_handoff()
{
    [ "$#" -eq 3 ] && [ "$1" = 0 ] && [ "$2" = 0 ] && [ "$3" = Y ]
}
