export APP_VERSION="5.9.3"

echo -n "Building amd64 deb..........."
debdir=bin/pirate-qt-ubuntu1804-v$APP_VERSION
mkdir -p $debdir > /dev/null
mkdir    $debdir/DEBIAN
mkdir -p $debdir/usr/local/bin

cat zcutil/deb/control_amd64 | sed "s/RELEASE_VERSION/$APP_VERSION/g" > $debdir/DEBIAN/control

cp release/pirate-qt-linux                   $debdir/usr/local/bin/pirate-qt

# Bundle the embedded tor/i2pd daemons and their crash-safety watchdog as
# siblings of pirate-qt (see doc/tor.md, doc/i2p.md), if this build produced them.
for bin in pirate-tor pirate-i2pd pirate-networking; do
  if [ -f artifacts/bin/$bin ]; then
    cp artifacts/bin/$bin $debdir/usr/local/bin/$bin
    chmod +x $debdir/usr/local/bin/$bin
  fi
done

mkdir -p $debdir/usr/share/pixmaps/
cp zcutil/deb/pirate.xpm           $debdir/usr/share/pixmaps/

mkdir -p $debdir/usr/share/applications
cp zcutil/deb/desktopentry    $debdir/usr/share/applications/pirate-qt.desktop

dpkg-deb --build $debdir >/dev/null
cp $debdir.deb                 release/pirate-qt-ubuntu1804-v$APP_VERSION.deb
rm ./bin -rf
echo "[OK]"


echo -n "Building aarch64 deb..........."
debdir=bin/pirate-qt-arrch64-v$APP_VERSION
mkdir -p $debdir > /dev/null
mkdir    $debdir/DEBIAN
mkdir -p $debdir/usr/local/bin

cat zcutil/deb/control_aarch64 | sed "s/RELEASE_VERSION/$APP_VERSION/g" > $debdir/DEBIAN/control

cp release/pirate-qt-arm                   $debdir/usr/local/bin/pirate-qt

# Bundle the embedded tor/i2pd daemons and their crash-safety watchdog as
# siblings of pirate-qt (see doc/tor.md, doc/i2p.md), if this build produced them.
for bin in pirate-tor pirate-i2pd pirate-networking; do
  if [ -f artifacts/bin/$bin ]; then
    cp artifacts/bin/$bin $debdir/usr/local/bin/$bin
    chmod +x $debdir/usr/local/bin/$bin
  fi
done

mkdir -p $debdir/usr/share/pixmaps/
cp zcutil/deb/pirate.xpm           $debdir/usr/share/pixmaps/

mkdir -p $debdir/usr/share/applications
cp zcutil/deb/desktopentry    $debdir/usr/share/applications/piratewallet-qt.desktop

dpkg-deb --build $debdir >/dev/null
cp $debdir.deb                 release/pirate-qt-aarch64-v$APP_VERSION.deb
rm ./bin -rf
echo "[OK]"
