#!/usr/bin/env bash
# Local mirror of the CI `lint` gate (.github/workflows/build.yml).
#
# Run from anywhere: tools/check.sh
# Gates, in order: clang-format, cppcheck, configure, build (also generates
# PIO headers), clang-tidy. Each gate runs even if an earlier one fails; the
# script exits non-zero if ANY gate failed.
#
# Requires: clang-format, cppcheck, clang-tidy, cmake, an arm-none-eabi
# toolchain, and the Pico SDK submodule (rp2040-firmware/pico-sdk/).
set -uo pipefail
cd "$(dirname "$0")/.."

FIRMWARE_SRCS=(
    rp2040-firmware/main.cpp
    rp2040-firmware/position.cpp
    rp2040-firmware/position.h
    rp2040-firmware/quadrature_encoder.cpp
    rp2040-firmware/quadrature_encoder.h
    rp2040-firmware/usb_device.cpp
    rp2040-firmware/usb_device.h
    rp2040-firmware/ws2812_led.cpp
    rp2040-firmware/ws2812_led.h
    rp2040-firmware/tusb_config.h
)

FIRMWARE_TUS=(
    rp2040-firmware/main.cpp
    rp2040-firmware/position.cpp
    rp2040-firmware/quadrature_encoder.cpp
    rp2040-firmware/usb_device.cpp
    rp2040-firmware/ws2812_led.cpp
)

rc=0
gate() { printf '\n== %s ==\n' "$1"; }
result() { if [ "$1" -eq 0 ]; then echo "  OK"; else echo "  FAIL"; rc=1; fi; }

gate "clang-format"
clang-format --dry-run --Werror "${FIRMWARE_SRCS[@]}"
result $?

gate "cppcheck"
# Suppressions:
#   missingInclude / missingIncludeSystem: pico-sdk + tinyusb headers come via
#     cross-compile paths cppcheck can't resolve;
#   unusedFunction / unusedStructMember: tinyusb extern "C" callbacks and the
#     vendor-protocol opcode/sentinel constants look "unused" to whole-program
#     analysis because consumers are in the linuxcnc-hal/test-script side.
cppcheck --enable=all --std=c++23 --language=c++ \
    -I rp2040-firmware \
    --suppress=missingInclude --suppress=missingIncludeSystem \
    --suppress=checkersReport --suppress=normalCheckLevelMaxBranches \
    --suppress=unmatchedSuppression \
    --suppress=unusedFunction --suppress=unusedStructMember \
    --error-exitcode=2 \
    "${FIRMWARE_SRCS[@]}"
result $?

gate "configure (compile DB for clang-tidy)"
cmake -S rp2040-firmware -B rp2040-firmware/build-lint \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null
result $?

gate "build (also generates PIO headers)"
cmake --build rp2040-firmware/build-lint -j"$(nproc 2>/dev/null || echo 4)" >/dev/null
result $?

gate "clang-tidy (rp2040 firmware TUs)"
# clang-tidy needs the arm-none-eabi C++ stdlib include paths since the
# compile DB targets arm-none-eabi but clang ships only a host stdlib.
# Discover them from g++ -v and feed them via --extra-arg=-isystem.
if command -v arm-none-eabi-g++ >/dev/null 2>&1; then
    ARM_INCS=$(arm-none-eabi-g++ -E -x c++ -v - </dev/null 2>&1 \
        | sed -n '/^#include <\.\.\.>/,/^End/p' \
        | grep -E '^ /' \
        | awk '{print "--extra-arg=-isystem"$1}')
    N=$(nproc 2>/dev/null || echo 4)
    printf '%s\n' "${FIRMWARE_TUS[@]}" |
        xargs -P "$N" -n 1 clang-tidy \
            -p rp2040-firmware/build-lint --quiet \
            --warnings-as-errors='*' \
            --header-filter='rp2040-firmware/[^/]+\.h$' \
            $ARM_INCS
    result $?
else
    echo "  SKIP (no arm-none-eabi-g++)"
fi

echo
if [ "$rc" -eq 0 ]; then echo "ALL GATES GREEN"; else echo "GATES FAILED (rc=$rc)"; fi
exit "$rc"
