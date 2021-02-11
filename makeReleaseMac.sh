#!/bin/sh

PACKAGE_DIR="TreasureChest.app"
mkdir ${PACKAGE_DIR}
mkdir ${PACKAGE_DIR}/Contents
mkdir ${PACKAGE_DIR}/Contents/MacOS
mkdir ${PACKAGE_DIR}/Contents/Frameworks
mkdir ${PACKAGE_DIR}/Contents/Resources

binaries=("pirate-qt-mac")
alllibs=()
for binary in "${binaries[@]}";
do
    # do the work in the destination directory
    cp ${binary} ${PACKAGE_DIR}/Contents/MacOS/
    cp zcutil/res/Info.plist ${PACKAGE_DIR}/Contents/
    cp zcutil/res/PkgInfo ${PACKAGE_DIR}/Contents/
    cp src/qt/res/icons/pirate.icns ${PACKAGE_DIR}/Contents/Resources/logo.icns
    # find the dylibs to copy for komodod
    DYLIBS=`otool -L ${PACKAGE_DIR}/Contents/MacOS/${binary} | grep "/usr/local" | awk -F' ' '{ print $1 }'`
    echo "copying ${DYLIBS} to ${PACKAGE_DIR}"
    # copy the dylibs to the srcdir
    for dylib in ${DYLIBS}; do cp -rf ${dylib} ${PACKAGE_DIR}/Contents/Frameworks; done
done

libraries=("libgcc_s.1.dylib" "libgomp.1.dylib" "libidn2.0.dylib" "libstdc++.6.dylib")

for binary in "${libraries[@]}";
do
    # find the dylibs to copy for komodod
    DYLIBS=`otool -L ${PACKAGE_DIR}/Contents/Frameworks/${binary} | grep "/usr/local" | awk -F' ' '{ print $1 }'`
    echo "copying ${DYLIBS} to ${PACKAGE_DIR}/Contents/Frameworks"
    # copy the dylibs to the srcdir
    for dylib in ${DYLIBS}; do cp -rf ${dylib} ${PACKAGE_DIR}/Contents/Frameworks; alllibs+=(${dylib}); done
done

indirectlibraries=("libintl.8.dylib" "libunistring.2.dylib")

for binary in "${indirectlibraries[@]}";
do
    # Need to undo this for the dylibs when we are done
    chmod 755 src/${binary}
    # find the dylibs to copy for komodod
    DYLIBS=`otool -L ${PACKAGE_DIR}/Contents/Frameworks/${binary} | grep "/usr/local" | awk -F' ' '{ print $1 }'`
    echo "copying indirect ${DYLIBS} to ${PACKAGE_DIR}/Contents/Frameworks"
    # copy the dylibs to the dest dir
    for dylib in ${DYLIBS}; do cp -rf ${dylib} ${PACKAGE_DIR}/Contents/Frameworks; alllibs+=(${dylib}); done
done

for binary in "${binaries[@]}";
do
    # modify komodod to point to dylibs
    echo "modifying ${binary} to use local libraries"
    for dylib in "${alllibs[@]}"
    do
        echo "Next lib is ${dylib} "
        install_name_tool -change ${dylib} @executable_path/../Frameworks/`basename ${dylib}` ${PACKAGE_DIR}/Contents/MacOS/${binary}
    done
    chmod +x ${PACKAGE_DIR}/Contents/MacOS/${binary}
done

for binary in "${alllibs[@]}";
do
    # modify libraries to point to dylibs
    echo "modifying ${binary} to use local libraries"
    for dylib in "${alllibs[@]}"
    do
        echo "Next lib is ${dylib} "
        install_name_tool -change ${dylib} @executable_path/../Frameworks/`basename ${dylib}` ${PACKAGE_DIR}/Contents/Frameworks/`basename ${binary}`
    done
done


create-dmg --volname "pirate-qt-mac" --volicon "zcutil/res/logo.icns" --window-pos 200 120 --icon "TreasureChest.app" 200 190  --app-drop-link 600 185 --hide-extension "TreasureChest.app"  --window-size 800 400 --hdiutil-quiet --background zcutil/res/dmgbg.png  pirate-qt-mac.dmg TreasureChest.app
