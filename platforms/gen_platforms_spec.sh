#!/bin/bash

set -eu

trapERR() {
    ss=$? bc="$BASH_COMMAND" ln="$BASH_LINENO"
    echo ">> '$bc' has failed on line $ln, status is $ss <<" >&2
    exit $ss
}

# Arrange to call trapERR when an error is raised
trap trapERR ERR    

ARMEL="$(realpath "$1")"
GPUARMHF="$(realpath "$2")"
MIYOOMINI="$(realpath "$3")"
AARCH64="$(realpath "$4")"

echo "BITTBOY bb $ARMEL"
echo "MIYOOA30 ma30 $GPUARMHF"
echo "MIYOO mm $MIYOOMINI"
#echo "RG35XX22 garlic $ARMEL"
echo "PORTMASTER a64 $AARCH64"
