#!/bin/sh
set -eu
if [ ! -f config.lua ]; then
  cp -R /opt/yurots-seed/data .
  cp /opt/yurots-seed/config.lua .
  printf '\nip = "127.0.0.1"\n' >> config.lua
fi
# The original Windows data pack mixes filename casing, while Npc::load lowercases it.
for file in data/npc/*.xml; do
  lower=$(printf '%s' "$file" | tr '[:upper:]' '[:lower:]')
  if [ "$file" != "$lower" ] && [ ! -e "$lower" ]; then cp "$file" "$lower"; fi
done
exec yurots
