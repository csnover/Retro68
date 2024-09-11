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
DEFAULT_PREFIX="$(pwd -P)/toolchain/"

##################### Command-line Options

BUILD_68K=1
BUILD_CARBON=1
BUILD_PALM=
BUILD_PPC=1
BUILD_THIRDPARTY=1
CLEAN_AFTER_BUILD=
HOST_CMAKE_FLAGS=()
HOST_C_COMPILER=
HOST_CXX_COMPILER=
INTERFACES_KIND=multiversal
OVERWRITE=
PREFIX="${DEFAULT_PREFIX}"
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
	echo "    --host-c-compiler         specify C compiler (needed on Mac OS X 10.4)"
	echo "    --host-cxx-compiler       specify C++ compiler (needed on Mac OS X 10.4)"
	echo "    --multiversal             use the open-source multiversal interfaces (default: autodetect)"
	echo "    --ninja                   use Ninja for CMake builds"
	echo "    --no-68k                  disable support for 68K Macs"
	echo "    --no-carbon               disable Carbon CFM support"
	echo "    --no-ppc                  disable classic PowerPC CFM support"
	echo "    --no-thirdparty           do not rebuild gcc & third party libraries"
	echo "    --overwrite               allow builds on top of existing prefix"
	echo "    --palm                    enable support for 68K PalmOS"
	echo "    --prefix=                 the path to install the toolchain to"
	echo "    --universal               use Apple's universal interfaces (default: autodetect)"
	echo "    --verbose                 increase verbosity of the script"
	echo "    --yes                     build without confirmation prompt"
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
			HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="-DCMAKE_C_COMPILER=${ARG#*=}"
			HOST_C_COMPILER="${ARG#*=}"
			;;
		--host-cxx-compiler=*)
			HOST_CMAKE_FLAGS[${#HOST_CMAKE_FLAGS[@]}]="-DCMAKE_CXX_COMPILER=${ARG#*=}"
			HOST_CXX_COMPILER="${ARG#*=}"
			;;
		--multiversal)
			INTERFACES_KIND=multiversal
			;;
		--ninja)
			USE_NINJA=1
			;;
		--no-68k)
			BUILD_68K=
			;;
		--no-carbon)
			BUILD_CARBON=
			;;
		--no-ppc)
			BUILD_PPC=
			BUILD_CARBON=
			;;
		--no-thirdparty)
			BUILD_THIRDPARTY=
			;;
		--overwrite)
			OVERWRITE=1
			;;
		--palm)
			BUILD_PALM=1
			;;
		--prefix=*)
			PREFIX="${ARG#*=}"
			command -v realpath >/dev/null && PREFIX="$(realpath "${PREFIX}")"
			;;
		--universal)
			INTERFACES_KIND=universal
			;;
		--verbose)
			VERBOSE=1
			;;
		--yes)
			YES=1
			;;
		*)
			echo "unknown option ${ARG}"
			usage
			exit 1
			;;
	esac
done

BUILD_MAC=${BUILD_68K}${BUILD_PPC}

##################### Prerequisites check

if [[ -n ${BUILD_MAC} && "${INTERFACES_KIND}" = "multiversal" && ! -d "${SRC}/multiversal" ]]; then
	echo "Could not find directory '${SRC}/multiversal'."
	echo "It looks like you did not clone the git submodules."
	echo "Please run:"
	echo "    git submodule update --init"
	exit 1
fi

##################### Sanity checks

if [[ "$(pwd -P || true)" = "${SRC}" ]]; then
	echo "Please do not invoke $(basename "$0") from the source directory."
	echo "Instead, create a separate build directory:"
	echo "    cd .."
	echo "    mkdir Retro68-build"
	echo "    cd Retro68-build"
	echo "    ../$(basename "${SRC}")/$(basename "$0")"
	exit 1
fi

if [[ "${PREFIX}" != "${DEFAULT_PREFIX}" && -d "${PREFIX}" && -n ${BUILD_THIRDPARTY} ]]; then
	if [[ ! -w "${PREFIX}" ]]; then
		echo "${PREFIX} is not writable, cannot install to there."
		exit 1
	elif [[ -z ${OVERWRITE} && -n "$(ls -A "${PREFIX}" || true)" ]]; then
		echo "${PREFIX} is not empty, cannot install to there."
		exit 1
	fi
fi

function missing_tools()
{
	local check="$1"
	local target="$2"
	[[ -n ${check} && ( ! -x "${PREFIX}/${target}/bin/nm" || ! -x "${PREFIX}/bin/${target}-gcc" ) ]]
}

[[ -z ${BUILD_THIRDPARTY} ]] && if \
	[[ ! -d "${PREFIX}" ]] \
	|| missing_tools "${BUILD_PALM}" m68k-none-palmos \
	|| missing_tools "${BUILD_68K}" m68k-apple-macos \
	|| missing_tools "${BUILD_PPC}" powerpc-apple-macos ; then
	BUILD_THIRDPARTY=1
	echo "Not all third-party components have been built yet, ignoring --no-thirdparty."
fi

### Running on a Power Mac (tested with 10.4 Tiger)
if [[ "$(uname -m || true)" = "Power Macintosh" ]]; then
	# The default compiler won't work,
	# check whether the compiler has been explictly specified
	# on the command line
	if [[ -z ${BUILD_THIRDPARTY} && ( -z ${HOST_CXX_COMPILER} || -z ${HOST_C_COMPILER} ) ]]; then
		echo "**** Apple's version of GCC on Power Macs is too old."
		echo "**** Please explicitly specify the C and C++ compilers"
		echo "**** using the --host-c-compiler and --host-cxx-compiler options."
		echo "**** You can install a usable compiler using tigerbrew."
		exit 1
	fi

	# work around a problem with building gcc-7 with homebrew's gcc-5
	export gcc_cv_c_no_fpie=no
fi

##################### Locate and check Interfaces & Libraries

if [[ -d "${SRC}/CIncludes" || -d "${SRC}/RIncludes" ]]; then
	echo
	echo "### WARNING:"
	echo "### Different from previous versions, Retro68 now expects to find"
	echo "### header files and libraries inside the InterfacesAndLibraries diretory."
	echo
fi

if [[ -n ${BUILD_MAC} ]]; then
	. "${SRC}/interfaces-and-libraries.sh"
	INTERFACES_DIR="${SRC}/InterfacesAndLibraries" locateAndCheckInterfacesAndLibraries
fi

##################### Final confirmation

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
echo "Host C compiler: ${HOST_C_COMPILER:-"Default"}"
echo "Host C++ compiler: ${HOST_CXX_COMPILER:-"Default"}"
[[ -n ${BUILD_MAC} ]] && echo "Interface kind: ${INTERFACES_KIND}"
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

##################### Third-Party components: binutils, gcc

function start_build()
{
	local directory="$1"
	[[ ! -d "${directory}" ]] && mkdir -p "${directory}"
	pushd "${directory}" >/dev/null
	[[ -n ${VERBOSE} ]] && set -x
}

function end_build()
{
	local directory="$1"
	[[ -n ${VERBOSE} ]] && set +x
	popd >/dev/null
	[[ -n ${CLEAN_AFTER_BUILD} ]] && rm -rf "${directory}"
}

function build_binutils()
{
	local directory="$1"
	shift

	echo "Building binutils..."
	start_build "${directory}"
	[[ ! -f Makefile ]] && "${SRC}/binutils/configure" "--prefix=${PREFIX}" --disable-doc "$@"
	make "-j${BUILD_JOBS}"
	make install
	end_build "${directory}"
	echo "Built binutils"
}

function build_gcc()
{
	local directory="$1"
	shift

	echo "Building gcc..."
	start_build "${directory}"
	export target_configargs="--disable-nls --enable-libstdcxx-dual-abi=no --disable-libstdcxx-verbose"
	[[ ! -f Makefile ]] && "${SRC}/gcc/configure" "--prefix=${PREFIX}" \
		--enable-languages=c,c++ --disable-libssp "$@" MAKEINFO=missing
	# There seems to be a build failure in parallel builds; ignore any errors and try again without -j8.
	make "-j${BUILD_JOBS}" || make
	make install
	unset target_configargs
	end_build "${directory}"
	echo "Built gcc"
}

function enable_elf2mac()
{
	local target="$1"

	echo "Enabling Elf2Mac..."
	# Move the real linker aside and install symlinks to Elf2Mac
	# (Elf2Mac is built by cmake below)
	[[ -n ${VERBOSE} ]] && set -x
	mv "${PREFIX}/bin/${target}-ld"{,.real}
	mv "${PREFIX}/${target}/bin/ld"{,.real}
	ln -s Elf2Mac "${PREFIX}/bin/${target}-ld"
	ln -s ../../bin/Elf2Mac "${PREFIX}/${target}/bin/ld"
	[[ -n ${VERBOSE} ]] && set +x
	echo "Enabled Elf2Mac"
}

if [[ -n ${BUILD_THIRDPARTY} ]]; then
	[[ -z ${OVERWRITE} && "${PREFIX}" = "${DEFAULT_PREFIX}" ]] && rm -rf "${PREFIX}"
	[[ ! -d "${PREFIX}" ]] && mkdir -p "${PREFIX}"
	# Make path absolute for configure scripts even if realpath had been
	# unavailable
	PREFIX="$(cd "${PREFIX}" && pwd -P)"

	if [[ "$(uname || true)" = "Darwin" ]]; then
		# present-day Mac users are likely to install dependencies
		# via the homebrew package manager
		if [[ -d "/opt/homebrew" ]]; then
			export CPPFLAGS="${CPPFLAGS} -I/opt/homebrew/include"
			export LDFLAGS="${LDFLAGS} -L/opt/homebrew/lib"
		fi
		# or they could be using MacPorts. Default install
		# location is /opt/local
		if [[ -d "/opt/local/include" ]]; then
			export CPPFLAGS="${CPPFLAGS} -I/opt/local/include"
			export LDFLAGS="${LDFLAGS} -L/opt/local/lib"
		fi

		export CPPFLAGS="${CPPFLAGS} -I/usr/local/include"
		export LDFLAGS="${LDFLAGS} -L/usr/local/lib"
	fi

	export CC="${HOST_C_COMPILER}"
	export CXX="${HOST_CXX_COMPILER}"

	if [[ -n ${BUILD_68K} ]]; then
		build_binutils binutils-build --target=m68k-apple-macos
		build_gcc gcc-build --target=m68k-apple-macos --with-arch=m68k --with-cpu=m68000
		enable_elf2mac m68k-apple-macos
	fi

	if [[ -n ${BUILD_PALM} ]]; then
		build_binutils binutils-build-palm --target=m68k-none-palmos
		build_gcc gcc-build-palm --target=m68k-none-palmos --with-arch=m68k --with-cpu=m68000
		enable_elf2mac m68k-none-palmos
	fi

	if [[ -n ${BUILD_PPC} ]]; then
		build_binutils binutils-build-ppc --disable-plugins --target=powerpc-apple-macos
		build_gcc gcc-build-ppc --target=powerpc-apple-macos --disable-lto
	fi

	unset CC
	unset CXX
	unset CPPFLAGS
	unset LDFLAGS
elif [[ -n ${BUILD_MAC} ]]; then # SKIP_THIRDPARTY
	removeInterfacesAndLibraries
fi # SKIP_THIRDPARTY

##################### Build host-based components: MakePEF, MakeImport, ConvertObj, Rez, ...

echo "Building host-based tools..."

[[ -n ${VERBOSE} ]] && set -x
cmake -S "${SRC}" -B build-host --fresh \
	"-DCMAKE_INSTALL_PREFIX=${PREFIX}" \
	-DCMAKE_BUILD_TYPE=Debug \
	"${HOST_CMAKE_FLAGS[@]}" \
	${USE_NINJA:+"-GNinja"} \
	${BUILD_PALM:+"-DRETRO_PALMOS=ON"} \
	${BUILD_PPC:+"-DRETRO_PPC=ON"} \
	${BUILD_68K:+"-DRETRO_68K=ON"} \
	${BUILD_CARBON:+"-DRETRO_CARBON=ON"}
cmake --build build-host --target install "-j${BUILD_JOBS}"
[[ -n ${VERBOSE} ]] && set +x
[[ -n ${CLEAN_AFTER_BUILD} ]] && rm -rf build-host
echo 'subdirs("build-host")' > CTestTestfile.cmake
echo "Built host-based tools"

# make tools (such as MakeImport and the compilers) available for later commands
export PATH="${PREFIX}/bin:${PATH}"

##################### Set up Interfaces & Libraries

if [[ -n ${BUILD_MAC} ]]; then
	if [[ "${INTERFACES_KIND}" = "multiversal" ]]; then
		([[ -n ${VERBOSE} ]] && set -x ; cd "${SRC}/multiversal" && ruby make-multiverse.rb -G CIncludes -o "${PREFIX}/multiversal")
		mkdir -p "${PREFIX}/multiversal/libppc"
		cp "${SRC}/ImportLibraries"/*.a "${PREFIX}/multiversal/libppc/"
	fi

	setUpInterfacesAndLibraries
	linkInterfacesAndLibraries "${INTERFACES_KIND}"
fi

##################### Build target libraries and samples

function build_library()
{
	local directory="$1"
	local abi="$2"
	shift 2

	echo "Building target libraries and samples for ${abi}..."
	[[ -n ${VERBOSE} ]] && set -x
	cmake -S "${SRC}" -B "${directory}" --fresh \
		"-DCMAKE_TOOLCHAIN_FILE=${PREFIX}/share/cmake/Retro.toolchain.cmake" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
		${USE_NINJA:+"-GNinja"} \
		"-DRETRO_ABI=${abi}" \
		"$@"
	cmake --build "${directory}" --target install
	[[ -n ${VERBOSE} ]] && set +x
	echo "subdirs(\"${directory}\")" >> CTestTestfile.cmake
	[[ -n ${CLEAN_AFTER_BUILD} ]] && rm -rf "${directory}"
	echo "Built target ${abi} libraries and samples"
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

[[ -n ${BUILD_68K} ]] && echo "You will find 68K sample applications in build-target/Samples/."
[[ -n ${BUILD_PPC} ]] && echo "You will find PowerPC sample applications in build-target-ppc/Samples/."
[[ -n ${BUILD_CARBON} ]] && echo "You will find Carbon sample applications in build-target-carbon/Samples/."
