#!/bin/sh
# Sourced by the tools/apple scripts. Reads KEY=value lines from the repo's
# .env.local (see .env.example) for any KEY not already set in the environment,
# then checks that AC_JAR and PRODOS_SRC are set and exist.
_env_root=$(cd "$(dirname "$0")/../.." && pwd)
if [ -f "$_env_root/.env.local" ]; then
  while IFS= read -r _line || [ -n "$_line" ]; do
    case $_line in ''|\#*) continue ;; esac
    _k=${_line%%=*}; _v=${_line#*=}
    eval "[ -n \"\${$_k+set}\" ] || $_k=\"\$_v\""
  done < "$_env_root/.env.local"
fi
for _k in AC_JAR PRODOS_SRC; do
  eval "_v=\${$_k}"
  if [ -z "$_v" ]; then
    echo "$0: $_k is not set. Put it in $_env_root/.env.local (see .env.example) or export it." >&2
    exit 2
  fi
  if [ ! -f "$_v" ]; then
    echo "$0: $_k=$_v does not exist." >&2
    exit 2
  fi
done
ac="java -jar $AC_JAR"
src=$PRODOS_SRC
