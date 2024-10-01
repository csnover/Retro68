#!/usr/bin/env bash
# shellcheck enable=all disable=SC2310
#   Copyright 2014 Wolfgang Thaller.
#
#   This file is part of Retro68.
#
#   Retro68 is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   Retro68 is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with Retro68.  If not, see <http://www.gnu.org/licenses/>.

set -ueo pipefail

BUILD_JOBS="$(nproc || echo 8)"
SRC="$(cd "$(dirname "$0")" && pwd -P)"

##################### Command-line Options

BUILD_68K=1
BUILD_CARBON=1
BUILD_PALM=
BUILD_PPC=1
CLEAN_AFTER_BUILD=
HOST_CMAKE_FLAGS=()
PREFIX="$(pwd -P)/toolchain/"
USE_NINJA=
VERBOSE=
YES=

function usage()
{
	echo "Usage: $0 [options]"
	echo
	echo "Options: "
	echo "    --clean-after-build       remove intermediate build files right after building"
	echo "    --help                    show this help message"
	echo "    --verbose                 increase verbosity of the script"
	echo "    --yes                     build without confirmation prompt"
	echo "    -DCMAKE_C_COMPILER=       specify C compiler (needed on Mac OS X 10.4)"
	echo "    -DCMAKE_CXX_COMPILER=     specify C++ compiler (needed on Mac OS X 10.4)"
	echo "    -DCMAKE_INSTALL_PREFIX=   the path to install the toolchain to"
	echo "    -DRETRO_68K=OFF           disable support for 68K Macs"
	echo "    -DRETRO_CARBON=OFF        disable Carbon CFM support"
	echo "    -DRETRO_LIBINTERFACE=     Mac OS interface ([multiversal]/universal)"
	echo "    -DRETRO_PALMOS=ON         enable support for 68K Palm OS"
	echo "    -DRETRO_PPC=OFF           disable classic PowerPC CFM support"
	echo "    -DRETRO_THIRDPARTY=OFF    do not rebuild gcc & third party libraries"
	echo "    -GNinja                   use Ninja for CMake builds"
}

for ARG in "$@"; do
	case "${ARG}" in
		--clean-after-build)
			CLEAN_AFTER_BUILD=1
			;;
		--help)
			usage
			exit 0
			;;
		--host-c-compiler=*)
			echo "${ARG} deprecated: Use -DCMAKE_C_COMPILER"
			HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="-DCMAKE_C_COMPILER=${ARG#*=}"
			;;
		--host-cxx-compiler=*)
			echo "${ARG} deprecated: Use -DCMAKE_CXX_COMPILER"
			HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="-DCMAKE_CXX_COMPILER=${ARG#*=}"
			;;
		--multiversal)
			echo "${ARG} deprecated: Use -DRETRO_LIBINTERFACE=multiversal"
			HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="-DRETRO_LIBINTERFACE=multiversal"
			;;
		--ninja)
			echo "${ARG} deprecated: Use -GNinja"
			HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="-GNinja"
			USE_NINJA=1
			;;
		--no-68k)
			echo "${ARG} deprecated: Use -DRETRO_68K=OFF"
			HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="-DRETRO_68K=OFF"
			BUILD_68K=
			;;
		--no-carbon)
			echo "${ARG} deprecated: Use -DRETRO_CARBON=OFF"
			HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="-DRETRO_CARBON=OFF"
			BUILD_CARBON=
			;;
		--no-ppc)
			echo "${ARG} deprecated: Use -DRETRO_PPC=OFF -DRETRO_CARBON=OFF"
			HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="-DRETRO_PPC=OFF"
			HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="-DRETRO_CARBON=OFF"
			BUILD_PPC=
			BUILD_CARBON=
			;;
		--no-thirdparty)
			echo "${ARG} deprecated: Use -DRETRO_THIRDPARTY=OFF"
			HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="-DRETRO_THIRDPARTY=OFF"
			;;
		--prefix=*)
			echo "${ARG} deprecated: Use -DCMAKE_INSTALL_PREFIX"
			PREFIX="${ARG#*=}"
			;;
		--universal)
			echo "${ARG} deprecated: Use -DRETRO_LIBINTERFACE=universal"
			;;
		--verbose)
			VERBOSE=1
			;;
		--yes)
			YES=1
			;;
		# This is temporary until this script is a total pass-through to CMake
		-DRETRO_68K=OFF|-DRETRO_68K=NO|-DRETRO_68K=0|-DRETRO_68K=FALSE|-DRETRO_68K=N|-DRETRO_68K=IGNORE)
			BUILD_68K=
			;;
		-DRETRO_PPC=OFF|-DRETRO_PPC=NO|-DRETRO_PPC=0|-DRETRO_PPC=FALSE|-DRETRO_PPC=N|-DRETRO_PPC=IGNORE)
			BUILD_PPC=
			;;
		-DRETRO_CARBON=OFF|-DRETRO_CARBON=NO|-DRETRO_CARBON=0|-DRETRO_CARBON=FALSE|-DRETRO_CARBON=N|-DRETRO_CARBON=IGNORE)
			BUILD_CARBON=
			;;
		-DRETRO_PALMOS=1|-DRETRO_PALMOS=ON|-DRETRO_PALMOS=YES|-DRETRO_PALMOS=TRUE|-DRETRO_PALMOS=Y)
			BUILD_PALM=1
			;;
		-DCMAKE_INSTALL_PREFIX=*)
			PREFIX="${ARG#*=}"
			;;
		-GNinja)
			USE_NINJA=1
			;;
		-*)
			HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="${ARG}"
			;;
		*)
			echo "unknown option ${ARG}"
			usage
			exit 1
			;;
	esac
done

# This is temporary until this script is a total pass-through to CMake
HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="-DCMAKE_INSTALL_PREFIX=${PREFIX}"
if [[ -n "${USE_NINJA}" ]]; then
	HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="-GNinja"
fi

function yes_no()
{
	[[ -n $1 ]] && echo "Yes" || echo "No"
}

echo "Confirm configuration"
echo "====================="
(
	b="${BUILD_68K:+" 68K,"}${BUILD_PPC:+" PPC,"}${BUILD_CARBON:+" Carbon,"}${BUILD_PALM:+" Palm,"}"
	echo "Build:${b%%,}${BUILD_THIRDPARTY:+" (including third-party)"}"
)
echo "Clean after build: $(yes_no "${CLEAN_AFTER_BUILD}" || true)"
echo "Install prefix: ${PREFIX}"
echo "Use Ninja for CMake: $(yes_no "${USE_NINJA}" || true)"
echo

if [[ -z ${YES} ]]; then
	echo "Press 'y' to continue, or any other key to abort."
	read -r -s -n 1

	if [[ ${REPLY} != "y" ]]; then
		echo "Aborted."
		exit 0
	fi
fi

echo "Building host-based tools..."
cmake -S "${SRC}" -B build-host --fresh "${HOST_CMAKE_FLAGS[@]}"
cmake --build build-host "-j${BUILD_JOBS}" && cmake --install build-host
[[ -n ${CLEAN_AFTER_BUILD} ]] && rm -rf build-host
echo 'subdirs("build-host")' > CTestTestfile.cmake
echo "Built host-based tools"

function crosscompile()
{
	local what="$1"
	local directory="$2"
	local abi="$3"
	shift 3

	echo "Building target ${what} for ${abi}..."
	[[ -n ${VERBOSE} ]] && set -x
	cmake -S "${SRC}" -B "${directory}" --fresh \
		"-DCMAKE_TOOLCHAIN_FILE=${PREFIX}/share/cmake/Retro.toolchain.cmake" \
		${USE_NINJA:+"-GNinja"} \
		"-DRETRO_ABI=${abi}" \
		"$@"
	cmake --build "${directory}" "-j${BUILD_JOBS}" && cmake --install "${directory}"
	[[ -n ${VERBOSE} ]] && set +x
	echo "subdirs(\"${directory}\")" >> CTestTestfile.cmake
	[[ -n ${CLEAN_AFTER_BUILD} ]] && rm -rf "${directory}"
	echo "Built target ${what} for ${abi}"
}

function build_library()
{
	local directory="$1"
	shift 1

	crosscompile "libraries" "${directory}-libs" "$@" -DRETRO_BOOTSTRAP=ON
	crosscompile "sample applications" "${directory}-apps" "$@"
}

[[ -n ${BUILD_68K} ]] && build_library build-target m68k-apple-macos
[[ -n ${BUILD_PALM} ]] && build_library build-target-palm m68k-none-palmos
[[ -n ${BUILD_PPC} ]] && build_library build-target-ppc powerpc-apple-macos
[[ -n ${BUILD_CARBON} ]] && build_library build-target-carbon powerpc-apple-macos -DRETRO_CARBON=ON

echo
echo "==============================================================================="
echo "Done building Retro68."
if [[ "$(command -v Rez >/dev/null || true)" != "${PREFIX}/bin/Rez" ]]; then
	echo "You might want to add ${PREFIX}/bin to your PATH."
fi

[[ -n ${BUILD_68K} ]] && echo "You will find 68K sample applications in build-target-apps/Samples/."
[[ -n ${BUILD_PPC} ]] && echo "You will find PowerPC sample applications in build-target-ppc-apps/Samples/."
[[ -n ${BUILD_CARBON} ]] && echo "You will find Carbon sample applications in build-target-carbon-apps/Samples/."
