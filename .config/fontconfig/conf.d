<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
    <match target="font">
        <edit name="rgba" mode="assign">
            <const>none</const>
        </edit>
    </match>
    
    <!-- Set default fonts with CJK fallback -->
    <alias>
        <family>sans-serif</family>
        <prefer>
            <family>Rubik</family>
            <family>Noto Sans CJK SC</family>
            <family>Noto Sans CJK TC</family>
            <family>Noto Sans CJK JP</family>
            <family>Noto Sans CJK KR</family>
            <family>Noto Sans CJK HK</family>
        </prefer>
    </alias>
    
    <alias>
        <family>monospace</family>
        <prefer>
            <family>JetBrains Mono</family>
            <family>Noto Sans Mono CJK SC</family>
            <family>Noto Sans Mono CJK TC</family>
            <family>Noto Sans Mono CJK JP</family>
            <family>Noto Sans Mono CJK KR</family>
            <family>Noto Sans Mono CJK HK</family>
        </prefer>
    </alias>
    
    <alias>
        <family>serif</family>
        <prefer>
            <family>Noto Serif CJK SC</family>
            <family>Noto Serif CJK TC</family>
            <family>Noto Serif CJK JP</family>
            <family>Noto Serif CJK KR</family>
            <family>Noto Serif CJK HK</family>
        </prefer>
    </alias>
</fontconfig>
