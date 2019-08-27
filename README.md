microcom
========

microcom is a minimalistic terminal program for communicating with devices over
a serial connection (e.g. embedded systems, switches, modems). It features
connection via RS232 serial interfaces (including setting of transfer rates) as
well as in "Telnet mode" as specified in [RFC 2217].

[RFC 2217]: https://tools.ietf.org/html/rfc2217


Installation
------------

microcom depends on the [readline] library.

If you just cloned this repository, you also need to install [autoconf],
[automake], and the [autoconf archive] first. Then change to the project's root
directy and do:

```
autoreconf -i
```

If you extracted microcom from a release tarball, this previous step should not
be needed.

Now continue with building and installing microcom:

```
./configure
make
sudo make install
```

By default, microcom is installed into `/usr/local/bin/`. Use `./configure
--prefix=YOURPATH` to change that, and see `./configure --help` for more
options related to building and installation.

[readline]: https://tiswww.case.edu/php/chet/readline/rltop.html
[autoconf]: https://www.gnu.org/software/autoconf/
[automake]: https://www.gnu.org/software/automake/
[autoconf archive]: https://www.gnu.org/software/autoconf-archive/


Usage
-----

The typical usage with TTY devices looks like this:

```
microcom --speed=115200 --port=/dev/ttyS0
```

To connect to remote serial ports via RFC 2217, use the ``--telnet`` option instead:

```
microcom --speed=115200 --telnet=somehost:port
```

For the full list of options, see `microcom --help`.

During the connection, you can get to the microcom menu by pressing `Ctrl-\`.
Various options are available there, like setting flow  control, RTS and DTR.
See ``help`` for a full list.


License and Contributing
------------------------

microcom is free software and distributable under the GNU General Public
License, version 2. See the file `COPYING` in this repository for more
information.

Changes to microcom must be certified to be compatible with this license. For
this purpose, we use the Developer's Certificate of Origin 1.1; see the file
`DCO` in this repository.  If you can certify that the DCO applies for your
changes, add a line like the following:

```
Signed-off-by: Random J Developer <random@developer.example.org>
```

… containing your real name and e-mail address at the end of the patch
description (Git can do this for you when you use `git commit -s`).
Then send your patches to <oss-tools@pengutronix.de>, or, if you use GitHub,
open a pull-request on <https://github.com/pengutronix/microcom>.
If you send patches, please prefix your subject with "[PATCH microcom]" (for
example, see the `git-config` manpage for the option `format.subjectPrefix`).
