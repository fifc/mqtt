#!/usr/bin/zsh
set -x

if [[ -z "${SEA_HOST}" ]]; then
  SEA_HOST=ffox.top
fi

scp auth_plugin.so ${SEA_HOST}:/g/pkg/lib
