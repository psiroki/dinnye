#!/bin/bash

# platforms.txt should have lines in the following format:
# MIYOO mm /path/to/dir/containing/miyommini/cross/compile/docker/makefile
# PORTMASTER a32 /path/to/dir/containing/armhf/cross/compile/docker/makefile
# (so the parent directory of the Makefile, not the path to the Makefile itself)

set -eu

trapERR() {
    ss=$? bc="$BASH_COMMAND" ln="$BASH_LINENO"
    echo ">> '$bc' has failed on line $ln, status is $ss <<" >&2
    exit $ss
}

# Arrange to call trapERR when an error is raised
trap trapERR ERR    

SCRIPT_DIR="$(dirname $0)"
PROJECT_ROOT="$(realpath "$SCRIPT_DIR/..")"
if [ "$#" -gt 0 ]; then
  ONLY_TARGET="$1"
else
  ONLY_TARGET=
fi

while read -r line && [ -n "$line" ]; do
  TAG="$(echo $line | cut -d ' ' -f 1)"
  SUFFIX="$(echo $line | cut -d ' ' -f 2)"
  TCD="$(realpath $(echo $line | cut -d ' ' -f 3-))"
  cd "$PROJECT_ROOT"
  mkdir -p build/platforms/$SUFFIX
  BUILD_DIR="$PROJECT_ROOT/build/platforms/$SUFFIX"
  if [ -z "$ONLY_TARGET" ] || [ "$ONLY_TARGET" = "$SUFFIX" ]; then
    PACKER_SCRIPT_DIR="$PROJECT_ROOT/platforms/packers/$TAG-$SUFFIX"
    if [ "$TAG" == "WEB" ]; then
      cd "$BUILD_DIR"
      cmake "$PROJECT_ROOT" -DCMAKE_TOOLCHAIN_FILE="$TCD"
      make
    else
      cd "$TCD"
      make "WORKSPACE_DIR=$PROJECT_ROOT" "DOCKER_CMD=/bin/bash /workspace/platforms/internal_build_helper.sh build/platforms/$SUFFIX $TAG" "INTERACTIVE=0" shell
      if [ -d "$PACKER_SCRIPT_DIR" ]; then
        (cd "$PACKER_SCRIPT_DIR"; make PROJECT_ROOT="$PROJECT_ROOT" BUILD_DIR="$BUILD_DIR")
      else
        echo "No packer found for $TAG-$SUFFIX (expected $PACKER_SCRIPT_DIR)"
      fi
    fi
  fi
done < "$SCRIPT_DIR/platforms.txt"
