# install-app

This is a simple C++ utility to install applications from archives to your
desired destination with automatic symlink creation and desktop entry generation.

It extracts and installs from various archive formats and automatically detects
application name from archive filename then creates symlinks to `/usr/local/bin` for easy command-line access.

Additionally, `.desktop` files will be generated for GUI launchers with icons and executable
auto-detection.

## Supported Formats

- `.tar`
- `.tar.gz`
- `.tar.bz2`
- `.tar.xz`
- `.zip`
- `.deb`
- `.rpm`

## Installing

Simply clone this directory then build and install with CMake. Do note that
this application depends on [libarchive](https://archlinux.org/packages/core/x86_64/libarchive/).

```shell
# assuming you have cloned the dotfiles repository
$ cd bin/install-app
$ cmake --build build
# this may prompt you to enter sudo password as the program needs to be 
# installed to `/usr/bin`
$ sudo cmake --install build
```

## License

This project is licensed under the MIT License. See [LICENSE.txt](LICENSE.txt) for more details.
