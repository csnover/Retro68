#!/usr/bin/env bash
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
CMAKE_GENERATOR=
HOST_CMAKE_FLAGS=()
HOST_C_COMPILER=
HOST_CXX_COMPILER=
INTERFACES_KIND=multiversal
PREFIX="$DEFAULT_PREFIX"

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
	echo "    --palm                    enable support for 68K PalmOS"
	echo "    --prefix=                 the path to install the toolchain to"
	echo "    --universal               use Apple's universal interfaces (default: autodetect)"
}

for ARG in "$@"; do
	case "$ARG" in
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
			CMAKE_GENERATOR=-GNinja
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
		--palm)
			BUILD_PALM=1
			;;
		--prefix=*)
			PREFIX="${ARG#*=}"
			which realpath >/dev/null 2>&1 && PREFIX="$(realpath "$PREFIX")"
			;;
		--universal)
			INTERFACES_KIND=universal
			;;
		*)
			echo "unknown option $ARG"
			usage
			exit 1
			;;
	esac
done

BUILD_MAC=$BUILD_68K$BUILD_PPC

##################### Prerequisites check

if [[ $BUILD_MAC && "$INTERFACES_KIND" = "multiversal" && ! -d "$SRC/multiversal" ]]; then
	echo "Could not find directory '$SRC/multiversal'."
	echo "It looks like you did not clone the git submodules."
	echo "Please run:"
	echo "    git submodule update --init"
	exit 1
fi

##################### Sanity checks

if [[ "$(pwd -P)" = "$SRC" ]]; then
	echo "Please do not invoke $(basename "$0") from the source directory."
	echo "Instead, create a separate build directory:"
	echo "    cd .."
	echo "    mkdir Retro68-build"
	echo "    cd Retro68-build"
	echo "    ../$(basename "$SRC")/$(basename "$0")"
	exit 1
fi

if [[ "$PREFIX" != "$DEFAULT_PREFIX" && -d "$PREFIX" && $BUILD_THIRDPARTY ]]; then
	if [[ ! -w "$PREFIX" ]]; then
		echo "$PREFIX is not writable, cannot install to there."
		exit 1
	elif [[ "$(ls -A "$PREFIX")" ]]; then
		echo "$PREFIX is not empty, cannot install to there."
		exit 1
	fi
fi

[[ ! $BUILD_THIRDPARTY ]] && if \
	[[ ! -d "$PREFIX" ]] \
	|| [[ $BUILD_PALM && ( ! -d binutils-build-palm || ! -d gcc-build-palm ) ]] \
	|| [[ $BUILD_68K  && ( ! -d binutils-build      || ! -d gcc-build      ) ]] \
	|| [[ $BUILD_PPC  && ( ! -d binutils-build-ppc  || ! -d gcc-build-ppc  ) ]] \
	|| [[ $BUILD_MAC  &&   ! -d hfsutils ]]; then
	BUILD_THIRDPARTY=1
	echo "Not all third-party components have been built yet, ignoring --no-thirdparty."
fi

### Running on a Power Mac (tested with 10.4 Tiger)
if [ "$(uname -m)" = "Power Macintosh" ]; then
	# The default compiler won't work,
	# check whether the compiler has been explictly specified
	# on the command line
	if [[ ! $BUILD_THIRDPARTY && ( -z "$HOST_CXX_COMPILER" || -z "$HOST_C_COMPILER" ) ]]; then
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

if [[ -d "$SRC/CIncludes" || -d "$SRC/RIncludes" ]]; then
	echo
	echo "### WARNING:"
	echo "### Different from previous versions, Retro68 now expects to find"
	echo "### header files and libraries inside the InterfacesAndLibraries diretory."
	echo
fi

if [[ $BUILD_MAC ]]; then
	. "$SRC/interfaces-and-libraries.sh"
	INTERFACES_DIR="$SRC/InterfacesAndLibraries" locateAndCheckInterfacesAndLibraries
fi

##################### Final confirmation

echo "Confirm configuration"
echo "====================="
(
	b="${BUILD_68K:+ 68K,}${BUILD_PPC:+ PPC,}${BUILD_CARBON:+ Carbon,}${BUILD_PALM:+ Palm,}"
	echo "Build:${b%%,}${BUILD_THIRDPARTY:+ (including third-party)}"
)
echo -n "Clean after build: " ; [[ $CLEAN_AFTER_BUILD ]] && echo "Yes" || echo "No"
echo "Host C compiler: ${HOST_C_COMPILER:-Default}"
echo "Host C++ compiler: ${HOST_CXX_COMPILER:-Default}"
[[ $BUILD_MAC ]] && echo "Interface kind: $INTERFACES_KIND"
echo "Install prefix: $PREFIX"
echo -n "Use Ninja for CMake: " ; [[ $CMAKE_GENERATOR ]] && echo "Yes" || echo "No"
echo
echo "Press 'y' to continue, or any other key to abort."
read -r -s -n 1

if [ "$REPLY" != "y" ]; then
	echo "Aborted."
	exit 0
fi

##################### Third-Party components: binutils, gcc, hfsutils

function build_binutils()
{
	local directory="$1"
	shift
	if [[ -z "$directory" ]]; then
		echo "Missing directory for binutils build"
		exit 1
	fi

	mkdir -p "$directory"
	pushd "$directory" >/dev/null
	"$SRC/binutils/configure" "--prefix=$PREFIX" --disable-doc "$@"
	make "-j$BUILD_JOBS"
	make install
	popd >/dev/null

	[[ $CLEAN_AFTER_BUILD ]] && rm -rf "$directory"
}

function build_gcc()
{
	local directory="$1"
	shift
	if [[ -z "$directory" ]]; then
		echo "Missing directory for gcc build"
		exit 1
	fi

	mkdir -p "$directory"
	pushd "$directory" >/dev/null
	export target_configargs="--disable-nls --enable-libstdcxx-dual-abi=no --disable-libstdcxx-verbose"
	"$SRC/gcc/configure" "--prefix=$PREFIX" --enable-languages=c,c++ --disable-libssp "$@" MAKEINFO=missing
	# There seems to be a build failure in parallel builds; ignore any errors and try again without -j8.
	make "-j$BUILD_JOBS" || make
	make install
	unset target_configargs
	popd >/dev/null

	[[ $CLEAN_AFTER_BUILD ]] && rm -rf "$directory"
}

function enable_elf2mac()
{
	local target="$1"

	# Move the real linker aside and install symlinks to Elf2Mac
	# (Elf2Mac is built by cmake below)
	mv "$PREFIX/bin/$target-ld" "$PREFIX/bin/$target-ld.real"
	mv "$PREFIX/$target/bin/ld" "$PREFIX/$target/bin/ld.real"
	ln -s Elf2Mac "$PREFIX/bin/$target-ld"
	ln -s ../../bin/Elf2Mac "$PREFIX/$target/bin/ld"
}

if [[ $BUILD_THIRDPARTY ]]; then
	[[ "$PREFIX" = "$DEFAULT_PREFIX" ]] && rm -rf "$PREFIX"
	mkdir -p "$PREFIX"
	PREFIX="$(cd "$PREFIX" && pwd -P)"

	if [[ "$(uname)" = "Darwin" ]]; then
		# present-day Mac users are likely to install dependencies
		# via the homebrew package manager
		if [[ -d "/opt/homebrew" ]]; then
			export CPPFLAGS="$CPPFLAGS -I/opt/homebrew/include"
			export LDFLAGS="$LDFLAGS -L/opt/homebrew/lib"
		fi
		# or they could be using MacPorts. Default install
		# location is /opt/local
		if [[ -d "/opt/local/include" ]]; then
			export CPPFLAGS="$CPPFLAGS -I/opt/local/include"
			export LDFLAGS="$LDFLAGS -L/opt/local/lib"
		fi

		export CPPFLAGS="$CPPFLAGS -I/usr/local/include"
		export LDFLAGS="$CPPFLAGS -L/usr/local/lib"
	fi

	export CC="$HOST_C_COMPILER"
	export CXX="$HOST_CXX_COMPILER"

	if [[ $BUILD_68K ]]; then
		build_binutils binutils-build --target=m68k-apple-macos
		build_gcc gcc-build --target=m68k-apple-macos --with-arch=m68k --with-cpu=m68000
		enable_elf2mac m68k-apple-macos
	fi

	if [[ $BUILD_PALM ]]; then
		build_binutils binutils-build-palm --target=m68k-none-palmos
		build_gcc gcc-build-palm --target=m68k-none-palmos --with-arch=m68k --with-cpu=m68000
		enable_elf2mac m68k-none-palmos
	fi

	if [[ $BUILD_PPC ]]; then
		build_binutils binutils-build-ppc --disable-plugins --target=powerpc-apple-macos
		build_gcc gcc-build-ppc --target=powerpc-apple-macos --disable-lto
	fi

	unset CC
	unset CXX
	unset CPPFLAGS
	unset LDFLAGS

	if [[ $BUILD_MAC ]]; then
		mkdir -p "$PREFIX/lib"
		mkdir -p "$PREFIX/share/man/man1"
		mkdir hfsutils
		pushd hfsutils >/dev/null
		"$SRC/hfsutils/configure" "--prefix=$PREFIX" "--mandir=$PREFIX/share/man" --enable-devlibs
		make
		make install
		popd >/dev/null

		[[ $CLEAN_AFTER_BUILD ]] && rm -rf hfsutils
	fi
elif [[ $BUILD_MAC ]]; then # SKIP_THIRDPARTY
	removeInterfacesAndLibraries
fi # SKIP_THIRDPARTY

##################### Build host-based components: MakePEF, MakeImport, ConvertObj, Rez, ...

echo "Building host-based tools..."

mkdir build-host
pushd build-host >/dev/null
cmake "$SRC" "-DCMAKE_INSTALL_PREFIX=$PREFIX" -DCMAKE_BUILD_TYPE=Debug "${HOST_CMAKE_FLAGS[@]}" "$CMAKE_GENERATOR"
popd >/dev/null
cmake --build build-host --target install

echo 'subdirs("build-host")' > CTestTestfile.cmake

# make tools (such as MakeImport and the compilers) available for later commands
export PATH="$PREFIX/bin:$PATH"

##################### Set up Interfaces & Libraries

if [[ $BUILD_MAC ]]; then
	if [[ "$INTERFACES_KIND" = "multiversal" ]]; then
		(cd "$SRC/multiversal" && ruby make-multiverse.rb -G CIncludes -o "$PREFIX/multiversal")
		mkdir -p "$PREFIX/multiversal/libppc"
		cp "$SRC/ImportLibraries"/*.a "$PREFIX/multiversal/libppc/"
	fi

	setUpInterfacesAndLibraries
	linkInterfacesAndLibraries "$INTERFACES_KIND"
fi

##################### Build target libraries and samples

function build_cmake()
{
	local kind="$1"
	local directory="$2"
	local toolchain="${3:-}"

	if [[ -z "$directory" ]]; then
		echo "Missing directory for $kind target library build"
		exit 1
	fi

	echo "Building target libraries and samples for $kind..."
	# Build target-based components for 68K
	mkdir -p "$directory"
	pushd "$directory" >/dev/null

	cmake "$SRC" "-DCMAKE_TOOLCHAIN_FILE=../build-host/cmake/intree$toolchain.toolchain.cmake" \
				 -DCMAKE_BUILD_TYPE=Release \
				 -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
				 "$CMAKE_GENERATOR"
	popd >/dev/null
	cmake --build "$directory" --target install
	echo "subdirs(\"$directory\")" >> CTestTestfile.cmake
}

[[ $BUILD_68K ]] && build_cmake 68K build-target
[[ $BUILD_PALM ]] && build_cmake PalmOS build-target-palm
[[ $BUILD_PPC ]] && build_cmake PowerPC build-target-ppc ppc
[[ $BUILD_CARBON ]] && build_cmake Carbon build-target-carbon carbon

echo
echo "==============================================================================="
echo "Done building Retro68."
if [ "$(which Rez)" != "$PREFIX/bin/Rez" ]; then
	echo "You might want to add $PREFIX/bin to your PATH."
fi

[[ $BUILD_68K ]] && echo "You will find 68K sample applications in build-target/Samples/."
[[ $BUILD_PPC ]] && echo "You will find PowerPC sample applications in build-target-ppc/Samples/."
[[ $BUILD_CARBON ]] && echo "You will find Carbon sample applications in build-target-carbon/Samples/."
