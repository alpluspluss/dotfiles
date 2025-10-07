#!/bin/bash

WALLPAPER_DIR="$HOME/wallpapers"
mkdir -p "$WALLPAPER_DIR"

WALLPAPER=$(yad --file --title="Select Wallpaper" --filename="$WALLPAPER_DIR/")
[[ -z "$WALLPAPER" ]] && exit 1

swww img "$WALLPAPER" --transition-type fade --transition-duration 1
matugen image "$WALLPAPER"
pkill waybar && sleep 0.1 && waybar &
notify-send "Wallpaper Set" "Theme updated" -i "$WALLPAPER"
