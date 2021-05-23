export APP_VERSION="5.1.1"

echo -n "Building amd64 deb..........."
debdir=bin/pirate-qt-ubuntu1804-v$APP_VERSION
mkdir -p $debdir > /dev/null
mkdir    $debdir/DEBIAN
mkdir -p $debdir/usr/local/bin

cat zcutil/deb/control | sed "s/RELEASE_VERSION/$APP_VERSION/g" > $debdir/DEBIAN/control

cp release/pirate-qt-linux                   $debdir/usr/local/bin/pirate-qt

mkdir -p $debdir/usr/share/pixmaps/
cp zcutil/deb/pirate.xpm           $debdir/usr/share/pixmaps/

mkdir -p $debdir/usr/share/applications
cp zcutil/deb/desktopentry    $debdir/usr/share/applications/piratewallet-lite.desktop

dpkg-deb --build $debdir >/dev/null
cp $debdir.deb                 release/pirate-qt-ubuntu1804-v$APP_VERSION.deb
rm ./bin -rf
echo "[OK]"


echo -n "Building arrch64 deb..........."
debdir=bin/pirate-qt-arrch64-v$APP_VERSION
mkdir -p $debdir > /dev/null
mkdir    $debdir/DEBIAN
mkdir -p $debdir/usr/local/bin

cat zcutil/deb/control | sed "s/RELEASE_VERSION/$APP_VERSION/g" > $debdir/DEBIAN/control

cp release/pirate-qt-arm                   $debdir/usr/local/bin/pirate-qt

mkdir -p $debdir/usr/share/pixmaps/
cp zcutil/deb/pirate.xpm           $debdir/usr/share/pixmaps/

mkdir -p $debdir/usr/share/applications
cp zcutil/deb/desktopentry    $debdir/usr/share/applications/piratewallet-lite.desktop

dpkg-deb --build $debdir >/dev/null
cp $debdir.deb                 release/pirate-qt-aarch64-v$APP_VERSION.deb
rm ./bin -rf
echo "[OK]"
