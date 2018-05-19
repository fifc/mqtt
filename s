#!/usr/bin/zsh
set -x

if [[ -z "${SEA_HOST}" ]]; then
  SEA_HOST=ffox.top
fi

scp src/.libs/libmsq_auth.so.0.0.0 ${SEA_HOST}:/g/pkg/lib/libmsq_auth.so
