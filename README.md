# CryFS

CryFS encrypts your files, so you can safely store them anywhere. It works well together with cloud services like Dropbox, iCloud, OneDrive and others.
See [https://www.cryfs.org](https://www.cryfs.org).

Install latest release
======================

Linux
------

CryFS is available through apt, but depending on which version of Ubuntu or Debian you're using, you may get an old version.

    sudo apt install cryfs

The following should work on Arch and Arch-based distros:

    sudo pacman -S cryfs

If you use homebrew-core, using the following instruction you should be able to install CrysFS:

    brew install cryfs/tap/cryfs

Additionally, the following would work for any Linux distro with the Nix package manager:

    nix-env -iA nixpkgs.cryfs

OSX
----

CryFS is distributed via Homebrew, MacPorts, and Nix.

If you use Homebrew:

    brew install --cask macfuse
    brew install cryfs/tap/cryfs

If you use MacPorts:

    port install cryfs

For Nix, the macOS build for cryfs is available in the Nixpkgs channel 21.05
and later:

    brew install --cask macfuse # or download from https://osxfuse.github.io/
    nix-env -iA nixpkgs.cryfs

Windows (experimental)
----------------------

CryFS has experimental Windows support since the 0.10 release series. To install it, do:

1. Install [DokanY](https://github.com/dokan-dev/dokany/releases)
   It's recommended to install the matching version DokanY a given CryFS version was built with. Other versions may work but we have seen issues.
   * CryFS 1.0: DokanY 2.2.0.1000
   * CryFS 0.11: DokanY 1.2.2.1001
2. Install [Microsoft Visual C++ Redistributable for Visual Studio 2022](https://support.microsoft.com/en-us/help/2977003/the-latest-supported-visual-c-downloads)
4. Install [CryFS](https://www.cryfs.org/#download)

GUI
===
There are some GUI applications with CryFS support. You usually have to install the GUI **and** also CryFS itself for it to work.
- [SiriKali](https://mhogomchungu.github.io/sirikali/)
- [Plasma Vault](https://www.kde.org/announcements/plasma-5.11.0.php) in KDE Plasma >= 5.11

Stability / Production readiness
====================
For non-concurrent use, CryFS 0.10 or later is stable, but has a couple of known issues that can corrupt your file system.
They don't happen in normal day to day use, but can happen if you don't pay attention or aren't aware of them.

- If you kill the CryFS process while it was in the middle of writing data (either intentionally or unintentionally by losing power to your PC), your file system could get corrupted.
  CryFS does not do journaling. Note that in 0.10.x, read accesses into a CryFS file system can cause writes because file timestamps get updated. So if you're unlucky, your file system
  could get corrupted if you lose power while you were reading files as well. Read accesses aren't an issue in CryFS 0.11.x or later anymore, because it mounts the filesystem with `noatime` by default.
- The same corruption mentioned above can happen when CryFS is trying to write data but your disk ran out of space, causing the write to fail.
- CryFS does not currently support concurrent access, i.e. accessing a file system from multiple devices at the same time.
  CryFS works very well for storing data in a cloud and using it from multiple devices, but you need to make sure that only one CryFS process is active at any point in time, and you also need
  to make sure that the cloud synchronization client (e.g. Dropbox) finishes its synchronization before you switch devices. There are some ideas on how concurrent access could be supported in
  future versions, but it's a hard problem to solve. If you do happen to access the file system from multiple devices at the same time, it will likely go well most of the time, but it can corrupt your file system.
- In addition to the scenarios above that can corrupt your file system, note that there is currently no fsck-like tool for CryFS that could recover your data. Such a tool is in development together with the Rust rewrite of CryFS.
  Until that is ready, a corrupted file system will most likely cause a loss of all of your data.

If the scenarios mentioned above don't apply to you, then you can consider CryFS 0.10.x and later as stable. The 0.9.x versions are not recommended anymore.

Building from source
====================

Requirements
------------
  - Git (for getting the source code)
  - GCC version >= 7 or Clang >= 7
  - CMake version >= 3.25
  - Ninja
  - pkg-config (on Unix)
  - Boost development headers/libraries
  - range-v3
  - spdlog
  - libFUSE version >= 2.9 (including development headers), on Mac OS X instead install macFUSE from https://osxfuse.github.io/
  - Python >= 3.5
  - OpenMP
  - GTest/GMock development files if building tests
  - libcurl development files only if building with `CRYFS_UPDATE_CHECKS=ON`

You can use the following commands to install these requirements

    # Ubuntu
    $ sudo apt install git python3 g++ cmake ninja-build libomp-dev pkg-config libfuse-dev fuse \
        libboost-filesystem-dev libboost-thread-dev libboost-chrono-dev libboost-program-options-dev \
        librange-v3-dev libspdlog-dev libgtest-dev libgmock-dev

    # Fedora
    $ sudo dnf install git python3 gcc-c++ cmake ninja-build pkgconf fuse-devel libomp-devel \
        boost-devel range-v3-devel spdlog-devel gtest-devel gmock-devel

    # Macintosh
    # TODO Update the package list
    $ brew install cmake ninja pkg-config libomp macfuse boost range-v3 spdlog googletest

Build & Install
---------------
 1. Clone repository

        $ git clone <repo-url> cryfs
        $ cd cryfs

 2. Build

        $ cmake --preset release
        $ cmake --build --preset release
        
    The executable will be generated at `build/release/src/cryfs-cli/cryfs`.

 3. Install

        $ sudo cmake --install build/release

The checked-in presets are:
 - **dev**: Debug build, tests enabled, compile commands exported, update checks disabled
 - **release**: RelWithDebInfo build, tests disabled, update checks disabled

Important CMake options:
 - **BUILD_TESTING**=[ON|OFF]: Whether to build the test cases. Default: OFF.
 - **CRYFS_UPDATE_CHECKS**=[ON|OFF]: Enable online update/security checks. Default: OFF.
 - **DISABLE_OPENMP**=[ON|OFF]: Disable OpenMP support. Default: OFF.


Run tests
---------
Use the development preset:

    $ cmake --preset dev
    $ cmake --build --preset dev
    $ ctest --preset dev

Run benchmarks
--------------
Use the benchmark preset:

    $ cmake --preset bench
    $ cmake --build --preset bench --target cryfs-bench
    $ build/bench/bench/cryfs-bench

Building on Windows (experimental)
----------------------------------
1. Install DokanY 2.2.0.1000. Other versions may not work.
2. Build the project with CMake and set `DOKAN_PATH` to the Dokan installation directory.

Disclaimer
----------------------

In the event of a password leak, you are strongly advised to create a new filesystem and copy all the data over from the previous one. Then, remove all copies of the compromised filesystem and config file(e.g, from the "previous versions" feature of your cloud system) to prevent access to the key (and, as a result, your data) using the leaked password.
