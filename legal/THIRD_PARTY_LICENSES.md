# Third-Party Licenses

The full license texts for each bundled third-party component are reproduced
here. If a license requires the verbatim text to accompany the binary, the
same text is also placed under `legal/licenses/<component>.txt`.

## Microsoft Visual C++ Redistributable (x64)

The Windows installer embeds Microsoft's official x64 Visual C++
Redistributable as an installation prerequisite. Redistribution is governed
by the Microsoft Visual Studio license terms; see Microsoft's documentation
on [redistributing Visual C++ files](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170).

## Qt6 — LGPL v3

BoundFree EEG Native links Qt6 dynamically. Under the GNU LGPL v3, recipients
of this binary distribution are entitled to the corresponding source of Qt6
and the object files / combined-work notice required to relink against a
modified Qt6.

  Qt is Copyright (C) The Qt Company Ltd and other contributors.
  Licensed under the GNU LGPL v3. The full text of the LGPL v3 is at
  https://www.gnu.org/licenses/lgpl-3.0.txt and is reproduced in
  `licenses/LGPL-3.0.txt`.

  The combined-works notice: BoundFree EEG Native ("bf_app") is a combined
  work that uses the dynamically-linked Qt6 libraries. The source of the
  BoundFree proprietary host application is available from BoundFree under
  the BoundFree-proprietary license. The Qt6 library source is available
  from https://www.qt.io/download and under the LGPL v3 source-offer
  obligations.

## pocketfft (hdronly variant) — BSD-3-Clause

```
Copyright (C) 2010-2024 Max-Planck-Society
Copyright (C) 2019-2020 Peter Bell

For the odd-sized DCT-IV transforms:
  Copyright (C) 2003, 2007-14 Matteo Frigo
  Copyright (C) 2003, 2007-14 Massachusetts Institute of Technology

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
* Neither the name of the copyright holder nor the names of its contributors
  may be used to endorse or promote products derived from this software
  without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

## MNE-Python — BSD-3-Clause

MNE-Python is Copyright (c) 2011-2024, MNE Developers. Licensed under the
BSD-3-Clause license. See https://github.com/mne-tools/mne-python.

## NumPy — BSD-3-Clause

NumPy is Copyright (c) 2005-2024, NumPy Developers. Licensed under the
BSD-3-Clause license. See https://numpy.org.

## h5py — BSD-3-Clause

h5py is Copyright (c) 2008-2024, Andrew Collette and contributors. Licensed
under the BSD-3-Clause license. See https://www.h5py.org.

## pyedflib — BSD-2-Clause

pyedflib is Copyright (c) 2010-2024, the pyedflib contributors. Licensed
under the BSD-2-Clause license. See https://github.com/holgern/pyedflib.

## matplotlib — Matplotlib license

matplotlib versions 1.3.0 and later are licensed under the Matplotlib license
agreement (PSF-based). See https://matplotlib.org/stable/users/license.html.

## PyInstaller bootloader — GPLv2 with linking exception

The PyInstaller bootloader bundled into the frozen sidecar is licensed under
the GPLv2 with a special exception that permits linking with non-GPL
programs. See https://pyinstaller.org and the file
`licenses/PyInstaller-exception.txt`.

## SciPy — BSD-3-Clause (transitive via MNE)

SciPy is Copyright (c) 2001-2002 Enthought, Inc.; 2003-2024 SciPy
Developers. Licensed under the BSD-3-Clause license. Bundled transitively via
MNE; not directly invoked by the sidecar.

## pandas — BSD-3-Clause (transitive via MNE)

pandas is Copyright (c) 2008-2011, AQR Capital Management, LLC, Lambda Foundry,
Inc. and PyData Development Team. Licensed under the BSD-3-Clause license.
Bundled transitively via MNE; not directly invoked by the sidecar.
