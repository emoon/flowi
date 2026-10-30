# Third-party licenses

flowi itself is MIT (see [LICENSE](../LICENSE)). The eleven libraries vendored under
`external/` and the third-party files copied into `src` and `examples/` keep their own
licenses, reproduced in the files listed below. All of them permit redistribution in source and binary form provided their notices are retained.

| Library | License | Copyright | License text |
|---|---|---|---|
| [clay](https://github.com/nicbarker/clay) (**modified fork**, see below) | zlib/libpng | 2024 Nic Barker | `external/clay/clay.h` (end of file) |
| [FreeType 2](https://freetype.org/) (2.13.3) | FreeType License (FTL) — see below | 1996-2024 David Turner, Robert Wilhelm, Werner Lemberg and others | `external/freetype2/LICENSE.TXT`, `external/freetype2/docs/FTL.TXT` |
| [fzy](https://github.com/jhawthorn/fzy) | MIT | 2014 John Hawthorn | `external/fzy/LICENSE` |
| [libarchive](https://github.com/libarchive/libarchive) | BSD 2-Clause | Tim Kientzle and contributors | `external/libarchive/COPYING` |
| [lhasa](https://github.com/fragglet/lhasa) | ISC | 2011-2025 Simon Howard | `external/lhasa/COPYING.md` |
| [mimalloc](https://github.com/microsoft/mimalloc) | MIT | 2018-2025 Microsoft Corporation, Daan Leijen | `external/mimalloc/LICENSE` |
| [nanosvg](https://github.com/memononen/nanosvg) | zlib/libpng | 2013-14 Mikko Mononen; SVG parser derived from Anti-Grain Geometry 2.4, 2002-2004 Maxim Shemanarev | `external/nanosvg/nanosvg.h` (header notice) |
| [stb](https://github.com/nothings/stb) (`stb_image`, `stb_image_write`, `stb_image_resize2`) (`stb_image.h` **modified**, see below) | Public domain (Unlicense) or MIT, at your option | Sean Barrett | end of each `external/stb/*.h` |
| unlzx | Public domain | Erik Meusel, Dan Fraser; LZX by Jonathan Forbes and Tomi Poutanen | notice at the top of `external/unlzx/unlzx.c` |
| [utest.h](https://github.com/sheredom/utest.h) | Unlicense | dedicated to the public domain | `external/utest/utest.h` (header notice) |
| [xxHash](https://github.com/Cyan4973/xxHash) | BSD 2-Clause | 2012-2021 Yann Collet | `external/xxhash/LICENSE` |

## clay is a modified fork, not an upstream drop

`external/clay/` started as clay v0.14 and has since been edited in place. The
divergences are marked in `clay.h` with `FLOWI:` and `flowi divergence` comments — extra config
structs and layout behaviour flowi needs. `clay.h` opens with the "altered version"
notice that the zlib license (clause 2) requires. Do not assume the vendored copy matches
upstream v0.14; diff before re-basing. See
[ADR-0012](adr/0012-fork-clay-as-em-layout.md).

The other ten are upstream copies, except `stb_image.h` (see below). libarchive, lhasa and unlzx back the optional VFS archive
drivers and are compiled only when `FLOWI_VFS_DRIVER_LIBARCHIVE`, `FLOWI_VFS_DRIVER_LHASA` or
`FLOWI_VFS_DRIVER_UNLZX` is on. fzy is a partial vendor — only `match.c`,
`match.h`, `bonus.h` and `config*.h`, with a flowi-authored `CMakeLists.txt`
alongside and two portability edits (`<strings.h>` guarded for Windows, flattened
`config.h` include path); its MIT license permits this.

## stb_image.h carries a local APNG patch

`external/stb/stb_image.h` (v2.30) is patched in place to decode animated PNG
(`stbi__apng_directory` and the APNG frame loading path). It is no longer identical
to upstream; diff against upstream stb_image.h v2.30 before re-basing.

## Third-party code outside `external/`

These files are copied into flowi's own source tree and keep their upstream
notices in the file. "Modified" means edited after copying.

| Code | Where | License | Copyright | Modified |
|---|---|---|---|---|
| [TLSF](http://tlsf.baisoku.org) 3.1 | `src/core/tlsf.c`, `tlsf.h` | BSD 3-Clause (notice in both files; reproduced below) | 2006-2016 Matthew Conte | Not tracked; treat as modified |
| [stb_sprintf](https://github.com/nothings/stb) v1.10 | `src/core/sprintf.c` | Public domain (Unlicense) or MIT, at your option (notice at end of the stb section) | 2017 Sean Barrett; RAD Game Tools and contributors listed in the file | Modified: flowi arena wrappers appended after the upstream code |
| [json.h](https://github.com/sheredom/json.h) | `src/core/json.c` | Unlicense (public domain) | Neil Henning (sheredom) | Modified: adapted to flowi code style |
| [branchless-utf8](https://github.com/skeeto/branchless-utf8) | `src/core/utf8.h` | Unlicense (public domain) | Christopher Wellons | Modified: embedded in flowi's UTF-8 helpers |
| [teeny-sha1](https://github.com/CTrabant/teeny-sha1) | `src/core/hash.c` (SHA1 section) | MIT (full notice in the file) | 2017 CTrabant | Modified: flowi types and hex output wrapper |
| MD5 (Colin Plumb) | `src/core/hash.c` (MD5 section) | Public domain | 1993 Colin Plumb (no copyright claimed) | Modified: flowi API wrapper |
| SHA256 ([Brad Conte](https://github.com/B-Con/crypto-algorithms)) | `src/core/hash.c` (SHA256 section) | Public domain | Brad Conte | Modified: flowi API wrapper |
| [stb_textedit](https://github.com/nothings/stb) v1.14 | `src/ui/stb_textedit.h` | Public domain (Unlicense) or MIT, at your option (end of file) | Sean Barrett | Modified: compiler diagnostic pragmas added |
| [Roboto Regular](https://github.com/googlefonts/roboto) 2.001047 | `examples/data/Roboto-Regular.ttf` | Apache License 2.0 | 2015 Google Inc. | Not modified |

The Roboto license text ships beside the font in
`examples/data/Roboto-LICENSE.txt`. The example embeds the font in its
binary, so redistributing that binary needs the same notice.

The TLSF notice (BSD 3-Clause):

> Copyright (c) 2006-2016, Matthew Conte. All rights reserved.
>
> Redistribution and use in source and binary forms, with or without modification,
> are permitted provided that the following conditions are met: Redistributions of
> source code must retain the above copyright notice, this list of conditions and
> the following disclaimer. Redistributions in binary form must reproduce the above
> copyright notice, this list of conditions and the following disclaimer in the
> documentation and/or other materials provided with the distribution. Neither the
> name of the copyright holder nor the names of its contributors may be used to
> endorse or promote products derived from this software without specific prior
> written permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
> ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
> WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
> DISCLAIMED. IN NO EVENT SHALL MATTHEW CONTE BE LIABLE FOR ANY DIRECT, INDIRECT,
> INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
> LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
> PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
> LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
> OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
> ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Not vendored: zlib

flowi links the system zlib (`find_package(ZLIB REQUIRED)`); the only zlib
code in this repository is the gzip decoder bundled inside FreeType
(`external/freetype2/src/gzip/`), covered by the FreeType entry above.

## Unresolved: MicroKnight bitmap font

The 8x8 "MicroKnight" bitmap font embedded in
`src/render/software/sw_glyph_cache.c` is an Amiga ANSI-art-scene font. Its
author and license could not be established from this repository or from public
sources checked so far, so no credit is given here and it is not covered by the
licenses above. It needs provenance or replacement before a release.

## FreeType: which arm

FreeType 2 is dual-licensed under the FreeType License (FTL) and GPLv2. flowi uses
it under the **FTL**. The FTL asks that use of FreeType be credited in the product
documentation — this file is that credit:

> Portions of this software are copyright © 2024 The FreeType Project
> (www.freetype.org). All rights reserved.
