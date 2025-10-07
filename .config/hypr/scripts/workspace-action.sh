#!/usr/bin/env bash
curr_workspace="$(hyprctl activeworkspace -j | jq -r ".id")"
dispatcher="$1"
shift

if [[ -z "${dispatcher}" || "${dispatcher}" == "--help" || "${dispatcher}" == "-h" || -z "$1" ]]; then
  echo "Usage: $0 <dispatcher> <target>"
  exit 1
fi
if [[ "$1" == *"+"* || "$1" == *"-"* ]]; then
  hyprctl dispatch "${dispatcher}" "$1"
elif [[ "$1" =~ ^[0-9]+$ ]]; then
  target_workspace=$(((($curr_workspace - 1) / 10 ) * 10 + $1))
  hyprctl dispatch "${dispatcher}" "${target_workspace}"
else
 hyprctl dispatch "${dispatcher}" "$1"
 exit 1
fi
