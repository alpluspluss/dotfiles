<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
    <match target="font">
        <edit name="rgba" mode="assign">
            <const>none</const>
        </edit>
    </match>
    
    <alias binding="strong">
        <family>sans-serif</family>
        <prefer>
            <family>Rubik</family>
        </prefer>
    </alias>

    <alias binding="strong">
        <family>monospace</family>
        <prefer>
            <family>JetBrains Mono</family>
        </prefer>
    </alias>

    <match target="pattern">
        <test qual="any" name="family">
            <string>monospace</string>
        </test>
        <edit name="size" mode="assign">
            <double>11</double>
        </edit>
    </match>
    
    <match target="pattern">
        <test qual="any" name="family">
            <string>ui-monospace</string>
        </test>
        <edit name="size" mode="assign">
            <double>11</double>
        </edit>
    </match>
    
    <alias binding="strong">
        <family>serif</family>
        <prefer>
            <family>Noto Serif CJK SC</family>
            <family>Noto Serif CJK TC</family>
            <family>Noto Serif CJK JP</family>
            <family>Noto Serif CJK KR</family>
            <family>Noto Serif CJK HK</family>
        </prefer>
    </alias>
    
    <alias binding="strong">
        <family>ui-monospace</family>
        <prefer>
            <family>JetBrains Mono</family>
        </prefer>
    </alias>
    
    <alias binding="strong">
        <family>SFMono-Regular</family>
        <prefer>
            <family>JetBrains Mono</family>
        </prefer>
    </alias>
</fontconfig>
