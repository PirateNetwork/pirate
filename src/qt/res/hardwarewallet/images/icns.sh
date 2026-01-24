mkdir pirate.iconset
sips -z 16 16     pirate_plus.png --out pirate.iconset/icon_16x16.png
sips -z 32 32     pirate_plus.png --out pirate.iconset/icon_16x16@2x.png
sips -z 32 32     pirate_plus.png --out pirate.iconset/icon_32x32.png
sips -z 64 64     pirate_plus.png --out pirate.iconset/icon_32x32@2x.png
sips -z 128 128   pirate_plus.png --out pirate.iconset/icon_128x128.png
sips -z 256 256   pirate_plus.png --out pirate.iconset/icon_128x128@2x.png
sips -z 256 256   pirate_plus.png --out pirate.iconset/icon_256x256.png
sips -z 512 512   pirate_plus.png --out pirate.iconset/icon_256x256@2x.png
sips -z 512 512   pirate_plus.png --out pirate.iconset/icon_512x512.png
cp pirate_plus.png pirate.iconset/icon_512x512@2x.png
iconutil -c icns pirate.iconset
rm -R pirate.iconset
