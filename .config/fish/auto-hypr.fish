# auto start Hyprland on tty1 upon login
if test -z "$DISPLAY" ;and test "$XDG_VTNR" -eq 1
    mkdir -p ~/.cache
    exec Hyprland > ~/.cache/hyprland.log 2>&1
end
