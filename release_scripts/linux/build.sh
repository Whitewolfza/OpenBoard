#!/bin/bash
# --------------------------------------------------------------------
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
# ---------------------------------------------------------------------

initializeVariables()
{
  APPLICATION_NAME="OpenBoard"
  STANDARD_QT_USED=false

  # Root directory
  SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
  PROJECT_ROOT="$SCRIPT_PATH/../.."
  BUILD_DIR="$PROJECT_ROOT/build/linux/release"
  PRODUCT_PATH="$BUILD_DIR/product"

  # Qt installation path. This may vary across machines
  QT_PATH="${QT_PATH:-/home/dev/Qt/6.9.3/gcc_64}"
  PLUGINS_PATH="${PLUGINS_PATH:-$QT_PATH/plugins}"
  GUI_TRANSLATIONS_DIRECTORY_PATH="${GUI_TRANSLATIONS_DIRECTORY_PATH:-$QT_PATH/translations}"
  QMAKE_PATH="${QMAKE_PATH:-$QT_PATH/bin/qmake}"
  LRELEASES="${LRELEASES:-$QT_PATH/bin/lrelease}"

  NOTIFY_CMD=`which notify-send`
  ZIP_PATH=`which zip`

  if [ -z $ARCHITECTURE ]; then
    ARCHITECTURE=`uname -m`
    if [ $ARCHITECTURE == "x86_64" ]; then
        ARCHITECTURE="amd64"
    elif [$ARCHITECTURE == "armv7l" ]; then
        $ARCHITECTURE="armhf"
        QT_PATH="/usr/lib/arm-linux-gnueabihf/qt5"
    fi
  fi
}

configureQtPaths(){
    if [ ! -x "$QMAKE_PATH" ]; then
        QMAKE_PATH=`which qmake6 2>/dev/null`
    fi
    if [ ! -x "$QMAKE_PATH" ]; then
        QMAKE_PATH=`which qmake 2>/dev/null`
    fi

    if [ -x "$QMAKE_PATH" ]; then
        if [ ! -d "$QT_PATH" ]; then
            QT_PATH=`$QMAKE_PATH -query QT_INSTALL_PREFIX 2>/dev/null`
        fi
        if [ ! -d "$PLUGINS_PATH" ]; then
            PLUGINS_PATH=`$QMAKE_PATH -query QT_INSTALL_PLUGINS 2>/dev/null`
        fi
        if [ ! -d "$GUI_TRANSLATIONS_DIRECTORY_PATH" ]; then
            GUI_TRANSLATIONS_DIRECTORY_PATH=`$QMAKE_PATH -query QT_INSTALL_TRANSLATIONS 2>/dev/null`
        fi
        if [ ! -x "$LRELEASES" ]; then
            QT_BIN_PATH=`$QMAKE_PATH -query QT_INSTALL_BINS 2>/dev/null`
            if [ -x "$QT_BIN_PATH/lrelease" ]; then
                LRELEASES="$QT_BIN_PATH/lrelease"
            fi
        fi
    fi
}

notifyError(){
    if [ -e "$NOTIFY_CMD" ]; then
        $NOTIFY_CMD -t 0 -i "/usr/share/icons/oxygen/64x64/status/dialog-error.png" "$1"
    fi
    printf "\033[31merror:\033[0m $1\n"
    exit 1
}

notifyProgress(){
    if [ -e "$NOTIFY_CMD" ]; then
        $NOTIFY_CMD "$1" "$2"
    fi
    printf "\033[32m--> Achieved task:\033[0m $1:\n\t$2\n"
}

checkDir(){
    if [ ! -d "$1" ]; then
        notifyError "Directory not found : $1"
    fi
}

checkExecutable(){
    if [ ! -e "$1" ]; then
        notifyError "$1 command not found"
    fi
}

buildWithStandardQt(){
  # if both Qt4 and Qt5 are installed, choose Qt5
  export QT_SELECT=5
  STANDARD_QT=`which qmake`
  if [ $? == "0" ]; then
    QT_VERSION=`$STANDARD_QT --version | grep -i "Using Qt version" | sed -e "s/Using Qt version \(.*\) in.*/\1/"`
    if [ `echo $QT_VERSION | sed -e "s/\.//g"` -gt 480 ]; then
        notifyProgress "Standard QT" "A recent enough qmake has been found. Using this one instead of custom one"
        STANDARD_QT_USED=true
        QMAKE_PATH=$STANDARD_QT
        LRELEASES=`which lrelease`
        PLUGINS_PATH="$STANDARD_QT/../plugins"
    fi
  fi
}

createBuildContext() {
    BUILD_CONTEXT="buildContext"
    echo $ARCHITECTURE > $BUILD_CONTEXT
}



# Check command-line arguments to force an architecture

for var in "$@"
do
   if [ $var == "i386" ]; then
      ARCHITECTURE="i386"
   fi
   if [ $var == "amd64" ]; then
      ARCHITECTURE="amd64"
   fi
done


initializeVariables
configureQtPaths
createBuildContext

cd $PROJECT_ROOT

# check of directories and executables
checkDir $QT_PATH
checkDir $PLUGINS_PATH
checkDir $GUI_TRANSLATIONS_DIRECTORY_PATH


checkExecutable $QMAKE_PATH
checkExecutable $LRELEASES
checkExecutable $ZIP_PATH

# cleaning the build directory
rm -rf $BUILD_DIR

# Generate translations
notifyProgress "QT" "Internationalization"
$LRELEASES ${APPLICATION_NAME}.pro
if [ -f "$GUI_TRANSLATIONS_DIRECTORY_PATH/translations.pro" ] && [ -w "$GUI_TRANSLATIONS_DIRECTORY_PATH" ]; then
    cd $GUI_TRANSLATIONS_DIRECTORY_PATH
    $LRELEASES translations.pro
    cd -
fi

notifyProgress "${APPLICATION_NAME}" "Building ${APPLICATION_NAME}"

if [ "$ARCHITECTURE" == "amd64" ] || [ "$ARCHITECTURE" == "x86_64" ]; then
    $QMAKE_PATH ${APPLICATION_NAME}.pro -spec linux-g++-64
else
    $QMAKE_PATH ${APPLICATION_NAME}.pro -spec linux-g++
fi


make -j4 release-install

if [ ! -e "$PRODUCT_PATH/${APPLICATION_NAME}" ]; then
    notifyError "${APPLICATION_NAME} build failed"
else
    notifyProgress "Finished building OpenBoard. You may now run the packaging script."
fi
