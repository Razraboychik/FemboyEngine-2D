# Third-party notices

FemboyEngine2D is licensed under the Mozilla Public License 2.0 (see
[LICENSE](LICENSE)). The components below are distributed with it under
their own licenses; those licenses apply to the listed component and not
to the engine source.

This file is generated from [`third_party/notices.toml`](third_party/notices.toml)
by [`Documentation/Tools/GenerateThirdPartyNotices.py`](Documentation/Tools/GenerateThirdPartyNotices.py).
Do not edit it by hand; change the manifest and regenerate.

| Component | Version | License (SPDX) | Purpose |
|---|---|---|---|
| SDL3 | release-3.4.18 | Zlib | Window, input, platform layer and Vulkan surface |
| Box2D | v3.0.0 | MIT | 2D rigid-body physics |
| EnTT | vendored | MIT | ECS registry and views |
| toml++ | 3.4.0 | MIT | Project descriptor and scene TOML parser |
| FlatBuffers | 25.12.19 | Apache-2.0 | Localization and scene binary schemas |
| miniaudio | 0.11.23 | MIT-0 OR Unlicense | Audio device and mixer backend |
| Opus | vendored | BSD-3-Clause | Ogg Opus decoding and voice encoding |
| Vulkan-Headers | v1.4.366 | Apache-2.0 | Vulkan API declarations |
| glslang | 15.3.0 | BSD-3-Clause OR Apache-2.0 | Runtime GLSL to SPIR-V compilation |
| stb | vendored | MIT OR Unlicense | Image and font decoding |
| Dear ImGui | 1.93.1 | MIT | Editor user interface |
| ImGuizmo | vendored | MIT | Editor transform gizmo |
| ImPlot | vendored | MIT | Editor profiler plots |
| imnodes | vendored | MIT | Editor node graph canvas |
| LZMA SDK | 25.01 | Public Domain | FemArchiver distribution compression |
| RNNoise | 0.1.1 | BSD-3-Clause | Neural speech denoise tier for the voice chain |
| Monocypher | 4.0.2 | BSD-2-Clause OR CC0-1.0 | BLAKE2b, X25519 and EdDSA primitives for the network layer |
| HarfBuzz | 15.0.0 | MIT | OpenType shaping for complex scripts, ligatures and mark positioning |
| SheenBidi | 3.0.0 | Apache-2.0 | Unicode Bidirectional Algorithm (UAX #9) runs |
| IBM Plex Sans / IBM Plex Mono | IBM Plex 6.4.0 | OFL-1.1 | Default text family: regular, bold, italic, bold italic and mono slots |
| Intel XeSS SDK | de0fb9c | LicenseRef-Intel-XeSS | Optional XeSS upscaling and XeSS Frame Generation backend |
| AMD FidelityFX SDK | 7539ae8 | LicenseRef-AMD-FidelityFX | Optional FSR1/FSR2/FSR3 upscaling and frame-generation backend |
| NVIDIA DLSS SDK | 9981c63 | LicenseRef-NVIDIA-RTX-SDKs | Optional DLSS upscaling and DLSS Frame Generation backend |
| Noto Sans Arabic/Devanagari/Thai/SC test fonts | Google Fonts | OFL-1.1 | Complex-script shaping and full-glyph SDF tests |

## License texts

### SDL3 release-3.4.18

- SPDX: `Zlib`
- Copyright: Copyright (c) 1997-2025 Sam Lantinga
- Upstream: https://github.com/libsdl-org/SDL

```text
The upstream license text is not vendored in this checkout. SPDX: Zlib. See https://github.com/libsdl-org/SDL.
```

### Box2D v3.0.0

- SPDX: `MIT`
- Copyright: Copyright (c) 2022 Erin Catto
- Upstream: https://github.com/erincatto/box2d

```text
MIT License

Copyright (c) 2022 Erin Catto

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### EnTT vendored

- SPDX: `MIT`
- Copyright: Copyright (c) 2017-2026 Michele Caini, author of EnTT
- Upstream: https://github.com/skypjack/entt

```text
The MIT License (MIT)

Copyright (c) 2017-2026 Michele Caini, author of EnTT

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copy of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copy or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### toml++ 3.4.0

- SPDX: `MIT`
- Copyright: Copyright (c) Mark Gillard
- Upstream: https://github.com/marzer/tomlplusplus

```text
MIT License

Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### FlatBuffers 25.12.19

- SPDX: `Apache-2.0`
- Copyright: Copyright (c) 2025 Google Inc.
- Upstream: https://github.com/google/flatbuffers

```text
Apache License
                           Version 2.0, January 2004
                        http://www.apache.org/licenses/

   TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

   1. Definitions.

      "License" shall mean the terms and conditions for use, reproduction,
      and distribution as defined by Sections 1 through 9 of this document.

      "Licensor" shall mean the copyright owner or entity authorized by
      the copyright owner that is granting the License.

      "Legal Entity" shall mean the union of the acting entity and all
      other entities that control, are controlled by, or are under common
      control with that entity. For the purposes of this definition,
      "control" means (i) the power, direct or indirect, to cause the
      direction or management of such entity, whether by contract or
      otherwise, or (ii) ownership of fifty percent (50%) or more of the
      outstanding shares, or (iii) beneficial ownership of such entity.

      "You" (or "Your") shall mean an individual or Legal Entity
      exercising permissions granted by this License.

      "Source" form shall mean the preferred form for making modifications,
      including but not limited to software source code, documentation
      source, and configuration files.

      "Object" form shall mean any form resulting from mechanical
      transformation or translation of a Source form, including but
      not limited to compiled object code, generated documentation,
      and conversions to other media types.

      "Work" shall mean the work of authorship, whether in Source or
      Object form, made available under the License, as indicated by a
      copyright notice that is included in or attached to the work
      (an example is provided in the Appendix below).

      "Derivative Works" shall mean any work, whether in Source or Object
      form, that is based on (or derived from) the Work and for which the
      editorial revisions, annotations, elaborations, or other modifications
      represent, as a whole, an original work of authorship. For the purposes
      of this License, Derivative Works shall not include works that remain
      separable from, or merely link (or bind by name) to the interfaces of,
      the Work and Derivative Works thereof.

      "Contribution" shall mean any work of authorship, including
      the original version of the Work and any modifications or additions
      to that Work or Derivative Works thereof, that is intentionally
      submitted to Licensor for inclusion in the Work by the copyright owner
      or by an individual or Legal Entity authorized to submit on behalf of
      the copyright owner. For the purposes of this definition, "submitted"
      means any form of electronic, verbal, or written communication sent
      to the Licensor or its representatives, including but not limited to
      communication on electronic mailing lists, source code control systems,
      and issue tracking systems that are managed by, or on behalf of, the
      Licensor for the purpose of discussing and improving the Work, but
      excluding communication that is conspicuously marked or otherwise
      designated in writing by the copyright owner as "Not a Contribution."

      "Contributor" shall mean Licensor and any individual or Legal Entity
      on behalf of whom a Contribution has been received by Licensor and
      subsequently incorporated within the Work.

   2. Grant of Copyright License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      copyright license to reproduce, prepare Derivative Works of,
      publicly display, publicly perform, sublicense, and distribute the
      Work and such Derivative Works in Source or Object form.

   3. Grant of Patent License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      (except as stated in this section) patent license to make, have made,
      use, offer to sell, sell, import, and otherwise transfer the Work,
      where such license applies only to those patent claims licensable
      by such Contributor that are necessarily infringed by their
      Contribution(s) alone or by combination of their Contribution(s)
      with the Work to which such Contribution(s) was submitted. If You
      institute patent litigation against any entity (including a
      cross-claim or counterclaim in a lawsuit) alleging that the Work
      or a Contribution incorporated within the Work constitutes direct
      or contributory patent infringement, then any patent licenses
      granted to You under this License for that Work shall terminate
      as of the date such litigation is filed.

   4. Redistribution. You may reproduce and distribute copies of the
      Work or Derivative Works thereof in any medium, with or without
      modifications, and in Source or Object form, provided that You
      meet the following conditions:

      (a) You must give any other recipients of the Work or
          Derivative Works a copy of this License; and

      (b) You must cause any modified files to carry prominent notices
          stating that You changed the files; and

      (c) You must retain, in the Source form of any Derivative Works
          that You distribute, all copyright, patent, trademark, and
          attribution notices from the Source form of the Work,
          excluding those notices that do not pertain to any part of
          the Derivative Works; and

      (d) If the Work includes a "NOTICE" text file as part of its
          distribution, then any Derivative Works that You distribute must
          include a readable copy of the attribution notices contained
          within such NOTICE file, excluding those notices that do not
          pertain to any part of the Derivative Works, in at least one
          of the following places: within a NOTICE text file distributed
          as part of the Derivative Works; within the Source form or
          documentation, if provided along with the Derivative Works; or,
          within a display generated by the Derivative Works, if and
          wherever such third-party notices normally appear. The contents
          of the NOTICE file are for informational purposes only and
          do not modify the License. You may add Your own attribution
          notices within Derivative Works that You distribute, alongside
          or as an addendum to the NOTICE text from the Work, provided
          that such additional attribution notices cannot be construed
          as modifying the License.

      You may add Your own copyright statement to Your modifications and
      may provide additional or different license terms and conditions
      for use, reproduction, or distribution of Your modifications, or
      for any such Derivative Works as a whole, provided Your use,
      reproduction, and distribution of the Work otherwise complies with
      the conditions stated in this License.

   5. Submission of Contributions. Unless You explicitly state otherwise,
      any Contribution intentionally submitted for inclusion in the Work
      by You to the Licensor shall be under the terms and conditions of
      this License, without any additional terms or conditions.
      Notwithstanding the above, nothing herein shall supersede or modify
      the terms of any separate license agreement you may have executed
      with Licensor regarding such Contributions.

   6. Trademarks. This License does not grant permission to use the trade
      names, trademarks, service marks, or product names of the Licensor,
      except as required for reasonable and customary use in describing the
      origin of the Work and reproducing the content of the NOTICE file.

   7. Disclaimer of Warranty. Unless required by applicable law or
      agreed to in writing, Licensor provides the Work (and each
      Contributor provides its Contributions) on an "AS IS" BASIS,
      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
      implied, including, without limitation, any warranties or conditions
      of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
      PARTICULAR PURPOSE. You are solely responsible for determining the
      appropriateness of using or redistributing the Work and assume any
      risks associated with Your exercise of permissions under this License.

   8. Limitation of Liability. In no event and under no legal theory,
      whether in tort (including negligence), contract, or otherwise,
      unless required by applicable law (such as deliberate and grossly
      negligent acts) or agreed to in writing, shall any Contributor be
      liable to You for damages, including any direct, indirect, special,
      incidental, or consequential damages of any character arising as a
      result of this License or out of the use or inability to use the
      Work (including but not limited to damages for loss of goodwill,
      work stoppage, computer failure or malfunction, or any and all
      other commercial damages or losses), even if such Contributor
      has been advised of the possibility of such damages.

   9. Accepting Warranty or Additional Liability. While redistributing
      the Work or Derivative Works thereof, You may choose to offer,
      and charge a fee for, acceptance of support, warranty, indemnity,
      or other liability obligations and/or rights consistent with this
      License. However, in accepting such obligations, You may act only
      on Your own behalf and on Your sole responsibility, not on behalf
      of any other Contributor, and only if You agree to indemnify,
      defend, and hold each Contributor harmless for any liability
      incurred by, or claims asserted against, such Contributor by reason
      of your accepting any such warranty or additional liability.

   END OF TERMS AND CONDITIONS

   APPENDIX: How to apply the Apache License to your work.

      To apply the Apache License to your work, attach the following
      boilerplate notice, with the fields enclosed by brackets "[]"
      replaced with your own identifying information. (Don't include
      the brackets!)  The text should be enclosed in the appropriate
      comment syntax for the file format. We also recommend that a
      file or class name and description of purpose be included on the
      same "printed page" as the copyright notice for easier
      identification within third-party archives.

   Copyright [yyyy] [name of copyright owner]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
```

### miniaudio 0.11.23

- SPDX: `MIT-0 OR Unlicense`
- Copyright: Copyright (c) David Reid
- Upstream: https://github.com/mackron/miniaudio

```text
This software is available as a choice of the following licenses. Choose
whichever you prefer.

===============================================================================
ALTERNATIVE 1 - Public Domain (www.unlicense.org)
===============================================================================
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.

In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>

===============================================================================
ALTERNATIVE 2 - MIT No Attribution
===============================================================================
Copyright 2025 David Reid

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Opus vendored

- SPDX: `BSD-3-Clause`
- Copyright: Copyright 2001-2023 Xiph.Org, Skype Limited, Octasic, Jean-Marc Valin, Timothy B. Terriberry, CSIRO, Gregory Maxwell, Mark Borgerding, Erik de Castro Lopo, Mozilla, Amazon
- Upstream: https://github.com/xiph/opus

```text
Copyright 2001-2023 Xiph.Org, Skype Limited, Octasic,
                    Jean-Marc Valin, Timothy B. Terriberry,
                    CSIRO, Gregory Maxwell, Mark Borgerding,
                    Erik de Castro Lopo, Mozilla, Amazon

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of Internet Society, IETF or IETF Trust, nor the
names of specific contributors, may be used to endorse or promote
products derived from this software without specific prior written
permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Opus is subject to the royalty-free patent licenses which are
specified at:

Xiph.Org Foundation:
https://datatracker.ietf.org/ipr/1524/

Microsoft Corporation:
https://datatracker.ietf.org/ipr/1914/

Broadcom Corporation:
https://datatracker.ietf.org/ipr/1526/
```

### Vulkan-Headers v1.4.366

- SPDX: `Apache-2.0`
- Copyright: Copyright (c) 2015-2025 The Khronos Group Inc.
- Upstream: https://github.com/KhronosGroup/Vulkan-Headers

```text
The upstream license text is not vendored in this checkout. SPDX: Apache-2.0. See https://github.com/KhronosGroup/Vulkan-Headers.
```

### glslang 15.3.0

- SPDX: `BSD-3-Clause OR Apache-2.0`
- Copyright: Copyright (c) The Khronos Group Inc.
- Upstream: https://github.com/KhronosGroup/glslang

```text
The upstream license text is not vendored in this checkout. SPDX: BSD-3-Clause OR Apache-2.0. See https://github.com/KhronosGroup/glslang.
```

### stb vendored

- SPDX: `MIT OR Unlicense`
- Copyright: Copyright (c) Sean Barrett and contributors
- Upstream: https://github.com/nothings/stb

```text
The upstream license text is not vendored in this checkout. SPDX: MIT OR Unlicense. See https://github.com/nothings/stb.
```

### Dear ImGui 1.93.1

- SPDX: `MIT`
- Copyright: Copyright (c) 2014-2026 Omar Cornut
- Upstream: https://github.com/ocornut/imgui

```text
The MIT License (MIT)

Copyright (c) 2014-2026 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### ImGuizmo vendored

- SPDX: `MIT`
- Copyright: Copyright (c) 2016 Cedric Guillemet
- Upstream: https://github.com/CedricGuillemet/ImGuizmo

```text
The MIT License (MIT)

Copyright (c) 2016 Cedric Guillemet

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### ImPlot vendored

- SPDX: `MIT`
- Copyright: Copyright (c) 2020 Evan Pezent
- Upstream: https://github.com/epezent/implot

```text
MIT License

Copyright (c) 2020 Evan Pezent

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### imnodes vendored

- SPDX: `MIT`
- Copyright: Copyright (c) 2019 Johann Muszynski
- Upstream: https://github.com/Nelarius/imnodes

```text
MIT License

Copyright (c) 2019 Johann Muszynski

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### LZMA SDK 25.01

- SPDX: `Public Domain`
- Copyright: Copyright (c) Igor Pavlov
- Upstream: https://www.7-zip.org/sdk.html

```text
The upstream license text is not vendored in this checkout. SPDX: Public Domain. See https://www.7-zip.org/sdk.html.
```

### RNNoise 0.1.1

- SPDX: `BSD-3-Clause`
- Copyright: Copyright (c) 2017-2018 Mozilla, Xiph.Org, Jean-Marc Valin
- Upstream: https://github.com/xiph/rnnoise

```text
Copyright (c) 2017, Mozilla
Copyright (c) 2007-2017, Jean-Marc Valin
Copyright (c) 2005-2017, Xiph.Org Foundation
Copyright (c) 2003-2004, Mark Borgerding

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of the Xiph.Org Foundation nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### Monocypher 4.0.2

- SPDX: `BSD-2-Clause OR CC0-1.0`
- Copyright: Copyright (c) 2017-2023 Loup Vaillant and contributors
- Upstream: https://github.com/LoupVaillant/Monocypher

```text
Monocypher as a whole is dual-licensed.  Choose whichever licence you
want from the two licences listed below.

The first licence is a regular 2-clause BSD licence.  The second licence
is the CC-0 from Creative Commons. It is intended to release Monocypher
to the public domain.  The BSD licence serves as a fallback option.

See the individual files for specific information about who contributed
to what file during which years.  See below for special notes.

Licence 1 (2-clause BSD)
------------------------

Copyright (c) 2017-2023, Loup Vaillant
Copyright (c) 2017-2019, Michael Savage
Copyright (c) 2017-2023, Fabio Scotoni
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the
   distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


Licence 2 (CC-0)
----------------

> CREATIVE COMMONS CORPORATION IS NOT A LAW FIRM AND DOES NOT PROVIDE
> LEGAL SERVICES. DISTRIBUTION OF THIS DOCUMENT DOES NOT CREATE AN
> ATTORNEY-CLIENT RELATIONSHIP. CREATIVE COMMONS PROVIDES THIS
> INFORMATION ON AN "AS-IS" BASIS. CREATIVE COMMONS MAKES NO WARRANTIES
> REGARDING THE USE OF THIS DOCUMENT OR THE INFORMATION OR WORKS
> PROVIDED HEREUNDER, AND DISCLAIMS LIABILITY FOR DAMAGES RESULTING FROM
> THE USE OF THIS DOCUMENT OR THE INFORMATION OR WORKS PROVIDED
> HEREUNDER.

### Statement of Purpose

The laws of most jurisdictions throughout the world automatically confer
exclusive Copyright and Related Rights (defined below) upon the creator
and subsequent owner(s) (each and all, an "owner") of an original work
of authorship and/or a database (each, a "Work").

Certain owners wish to permanently relinquish those rights to a Work for
the purpose of contributing to a commons of creative, cultural and
scientific works ("Commons") that the public can reliably and without
fear of later claims of infringement build upon, modify, incorporate in
other works, reuse and redistribute as freely as possible in any form
whatsoever and for any purposes, including without limitation commercial
purposes. These owners may contribute to the Commons to promote the
ideal of a free culture and the further production of creative, cultural
and scientific works, or to gain reputation or greater distribution for
their Work in part through the use and efforts of others.

For these and/or other purposes and motivations, and without any
expectation of additional consideration or compensation, the person
associating CC0 with a Work (the "Affirmer"), to the extent that he or
she is an owner of Copyright and Related Rights in the Work, voluntarily
elects to apply CC0 to the Work and publicly distribute the Work under
its terms, with knowledge of his or her Copyright and Related Rights in
the Work and the meaning and intended legal effect of CC0 on those
rights.

1. **Copyright and Related Rights.** A Work made available under CC0 may
   be protected by copyright and related or neighboring rights
   ("Copyright and Related Rights"). Copyright and Related Rights
   include, but are not limited to, the following:

   - the right to reproduce, adapt, distribute, perform, display,
     communicate, and translate a Work;
   - moral rights retained by the original author(s) and/or
     performer(s); publicity and privacy rights pertaining to a person's
     image or likeness depicted in a Work;
   - rights protecting against unfair competition in regards to a Work,
     subject to the limitations in paragraph 4(a), below;
   - rights protecting the extraction, dissemination, use and reuse of
     data in a Work;
   - database rights (such as those arising under Directive 96/9/EC of
     the European Parliament and of the Council of 11 March 1996 on the
     legal protection of databases, and under any national
     implementation thereof, including any amended or successor version
     of such directive); and
   - other similar, equivalent or corresponding rights throughout the
     world based on applicable law or treaty, and any national
     implementations thereof.

2. **Waiver.** To the greatest extent permitted by, but not in
   contravention of, applicable law, Affirmer hereby overtly, fully,
   permanently, irrevocably and unconditionally waives, abandons, and
   surrenders all of Affirmer's Copyright and Related Rights and
   associated claims and causes of action, whether now known or unknown
   (including existing as well as future claims and causes of action),
   in the Work (i) in all territories worldwide, (ii) for the maximum
   duration provided by applicable law or treaty (including future time
   extensions), (iii) in any current or future medium and for any number
   of copies, and (iv) for any purpose whatsoever, including without
   limitation commercial, advertising or promotional purposes (the
   "Waiver"). Affirmer makes the Waiver for the benefit of each member
   of the public at large and to the detriment of Affirmer's heirs and
   successors, fully intending that such Waiver shall not be subject to
   revocation, rescission, cancellation, termination, or any other legal
   or equitable action to disrupt the quiet enjoyment of the Work by the
   public as contemplated by Affirmer's express Statement of Purpose.

3. **Public License Fallback.** Should any part of the Waiver for any
   reason be judged legally invalid or ineffective under applicable law,
   then the Waiver shall be preserved to the maximum extent permitted
   taking into account Affirmer's express Statement of Purpose. In
   addition, to the extent the Waiver is so judged Affirmer hereby
   grants to each affected person a royalty-free, non transferable, non
   sublicensable, non exclusive, irrevocable and unconditional license
   to exercise Affirmer's Copyright and Related Rights in the Work (i)
   in all territories worldwide, (ii) for the maximum duration provided
   by applicable law or treaty (including future time extensions), (iii)
   in any current or future medium and for any number of copies, and
   (iv) for any purpose whatsoever, including without limitation
   commercial, advertising or promotional purposes (the "License"). The
   License shall be deemed effective as of the date CC0 was applied by
   Affirmer to the Work. Should any part of the License for any reason
   be judged legally invalid or ineffective under applicable law, such
   partial invalidity or ineffectiveness shall not invalidate the
   remainder of the License, and in such case Affirmer hereby affirms
   that he or she will not (i) exercise any of his or her remaining
   Copyright and Related Rights in the Work or (ii) assert any
   associated claims and causes of action with respect to the Work, in
   either case contrary to Affirmer's express Statement of Purpose.

4. **Limitations and Disclaimers.**

   - No trademark or patent rights held by Affirmer are waived,
     abandoned, surrendered, licensed or otherwise affected by this
     document.
   - Affirmer offers the Work as-is and makes no representations or
     warranties of any kind concerning the Work, express, implied,
     statutory or otherwise, including without limitation warranties of
     title, merchantability, fitness for a particular purpose, non
     infringement, or the absence of latent or other defects, accuracy,
     or the present or absence of errors, whether or not discoverable,
     all to the greatest extent permissible under applicable law.
   - Affirmer disclaims responsibility for clearing rights of other
     persons that may apply to the Work or any use thereof, including
     without limitation any person's Copyright and Related Rights in the
     Work. Further, Affirmer disclaims responsibility for obtaining any
     necessary consents, permissions or other rights required for any
     use of the Work.
   - Affirmer understands and acknowledges that Creative Commons is not
     a party to this document and has no duty or obligation with respect
     to this CC0 or use of the Work.

Special notes
-------------

The files in `tests/externals/` were placed in the public domain by
their respective authors.  See the `AUTHORS.md` files in each directory.
```

### HarfBuzz 15.0.0

- SPDX: `MIT`
- Copyright: Copyright (c) 2010-2023 Google, Inc. and HarfBuzz contributors
- Upstream: https://github.com/harfbuzz/harfbuzz

```text
HarfBuzz is licensed under the so-called "Old MIT" license.  Details follow.
For parts of HarfBuzz that are licensed under different licenses see individual
files names COPYING in subdirectories where applicable.

Copyright © 2010-2022  Google, Inc.
Copyright © 2015-2020  Ebrahim Byagowi
Copyright © 2019,2020  Facebook, Inc.
Copyright © 2012,2015  Mozilla Foundation
Copyright © 2011  Codethink Limited
Copyright © 2008,2010  Nokia Corporation and/or its subsidiary(-ies)
Copyright © 2009  Keith Stribley
Copyright © 2011  Martin Hosken and SIL International
Copyright © 2007  Chris Wilson
Copyright © 2005,2006,2020,2021,2022,2023  Behdad Esfahbod
Copyright © 2004,2007,2008,2009,2010,2013,2021,2022,2023  Red Hat, Inc.
Copyright © 1998-2005  David Turner and Werner Lemberg
Copyright © 2016  Igalia S.L.
Copyright © 2022  Matthias Clasen
Copyright © 2018,2021  Khaled Hosny
Copyright © 2018,2019,2020  Adobe, Inc
Copyright © 2013-2015  Alexei Podtelezhnikov

For full copyright notices consult the individual files in the package.


Permission is hereby granted, without written agreement and without
license or royalty fees, to use, copy, modify, and distribute this
software and its documentation for any purpose, provided that the
above copyright notice and the following two paragraphs appear in
all copies of this software.

IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
```

### SheenBidi 3.0.0

- SPDX: `Apache-2.0`
- Copyright: Copyright (C) 2016-2025 Muhammad Tayyab Akram
- Upstream: https://github.com/Tehreer/SheenBidi

```text
Apache License
                           Version 2.0, January 2004
                        http://www.apache.org/licenses/

   TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

   1. Definitions.

      "License" shall mean the terms and conditions for use, reproduction,
      and distribution as defined by Sections 1 through 9 of this document.

      "Licensor" shall mean the copyright owner or entity authorized by
      the copyright owner that is granting the License.

      "Legal Entity" shall mean the union of the acting entity and all
      other entities that control, are controlled by, or are under common
      control with that entity. For the purposes of this definition,
      "control" means (i) the power, direct or indirect, to cause the
      direction or management of such entity, whether by contract or
      otherwise, or (ii) ownership of fifty percent (50%) or more of the
      outstanding shares, or (iii) beneficial ownership of such entity.

      "You" (or "Your") shall mean an individual or Legal Entity
      exercising permissions granted by this License.

      "Source" form shall mean the preferred form for making modifications,
      including but not limited to software source code, documentation
      source, and configuration files.

      "Object" form shall mean any form resulting from mechanical
      transformation or translation of a Source form, including but
      not limited to compiled object code, generated documentation,
      and conversions to other media types.

      "Work" shall mean the work of authorship, whether in Source or
      Object form, made available under the License, as indicated by a
      copyright notice that is included in or attached to the work
      (an example is provided in the Appendix below).

      "Derivative Works" shall mean any work, whether in Source or Object
      form, that is based on (or derived from) the Work and for which the
      editorial revisions, annotations, elaborations, or other modifications
      represent, as a whole, an original work of authorship. For the purposes
      of this License, Derivative Works shall not include works that remain
      separable from, or merely link (or bind by name) to the interfaces of,
      the Work and Derivative Works thereof.

      "Contribution" shall mean any work of authorship, including
      the original version of the Work and any modifications or additions
      to that Work or Derivative Works thereof, that is intentionally
      submitted to Licensor for inclusion in the Work by the copyright owner
      or by an individual or Legal Entity authorized to submit on behalf of
      the copyright owner. For the purposes of this definition, "submitted"
      means any form of electronic, verbal, or written communication sent
      to the Licensor or its representatives, including but not limited to
      communication on electronic mailing lists, source code control systems,
      and issue tracking systems that are managed by, or on behalf of, the
      Licensor for the purpose of discussing and improving the Work, but
      excluding communication that is conspicuously marked or otherwise
      designated in writing by the copyright owner as "Not a Contribution."

      "Contributor" shall mean Licensor and any individual or Legal Entity
      on behalf of whom a Contribution has been received by Licensor and
      subsequently incorporated within the Work.

   2. Grant of Copyright License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      copyright license to reproduce, prepare Derivative Works of,
      publicly display, publicly perform, sublicense, and distribute the
      Work and such Derivative Works in Source or Object form.

   3. Grant of Patent License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      (except as stated in this section) patent license to make, have made,
      use, offer to sell, sell, import, and otherwise transfer the Work,
      where such license applies only to those patent claims licensable
      by such Contributor that are necessarily infringed by their
      Contribution(s) alone or by combination of their Contribution(s)
      with the Work to which such Contribution(s) was submitted. If You
      institute patent litigation against any entity (including a
      cross-claim or counterclaim in a lawsuit) alleging that the Work
      or a Contribution incorporated within the Work constitutes direct
      or contributory patent infringement, then any patent licenses
      granted to You under this License for that Work shall terminate
      as of the date such litigation is filed.

   4. Redistribution. You may reproduce and distribute copies of the
      Work or Derivative Works thereof in any medium, with or without
      modifications, and in Source or Object form, provided that You
      meet the following conditions:

      (a) You must give any other recipients of the Work or
          Derivative Works a copy of this License; and

      (b) You must cause any modified files to carry prominent notices
          stating that You changed the files; and

      (c) You must retain, in the Source form of any Derivative Works
          that You distribute, all copyright, patent, trademark, and
          attribution notices from the Source form of the Work,
          excluding those notices that do not pertain to any part of
          the Derivative Works; and

      (d) If the Work includes a "NOTICE" text file as part of its
          distribution, then any Derivative Works that You distribute must
          include a readable copy of the attribution notices contained
          within such NOTICE file, excluding those notices that do not
          pertain to any part of the Derivative Works, in at least one
          of the following places: within a NOTICE text file distributed
          as part of the Derivative Works; within the Source form or
          documentation, if provided along with the Derivative Works; or,
          within a display generated by the Derivative Works, if and
          wherever such third-party notices normally appear. The contents
          of the NOTICE file are for informational purposes only and
          do not modify the License. You may add Your own attribution
          notices within Derivative Works that You distribute, alongside
          or as an addendum to the NOTICE text from the Work, provided
          that such additional attribution notices cannot be construed
          as modifying the License.

      You may add Your own copyright statement to Your modifications and
      may provide additional or different license terms and conditions
      for use, reproduction, or distribution of Your modifications, or
      for any such Derivative Works as a whole, provided Your use,
      reproduction, and distribution of the Work otherwise complies with
      the conditions stated in this License.

   5. Submission of Contributions. Unless You explicitly state otherwise,
      any Contribution intentionally submitted for inclusion in the Work
      by You to the Licensor shall be under the terms and conditions of
      this License, without any additional terms or conditions.
      Notwithstanding the above, nothing herein shall supersede or modify
      the terms of any separate license agreement you may have executed
      with Licensor regarding such Contributions.

   6. Trademarks. This License does not grant permission to use the trade
      names, trademarks, service marks, or product names of the Licensor,
      except as required for reasonable and customary use in describing the
      origin of the Work and reproducing the content of the NOTICE file.

   7. Disclaimer of Warranty. Unless required by applicable law or
      agreed to in writing, Licensor provides the Work (and each
      Contributor provides its Contributions) on an "AS IS" BASIS,
      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
      implied, including, without limitation, any warranties or conditions
      of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
      PARTICULAR PURPOSE. You are solely responsible for determining the
      appropriateness of using or redistributing the Work and assume any
      risks associated with Your exercise of permissions under this License.

   8. Limitation of Liability. In no event and under no legal theory,
      whether in tort (including negligence), contract, or otherwise,
      unless required by applicable law (such as deliberate and grossly
      negligent acts) or agreed to in writing, shall any Contributor be
      liable to You for damages, including any direct, indirect, special,
      incidental, or consequential damages of any character arising as a
      result of this License or out of the use or inability to use the
      Work (including but not limited to damages for loss of goodwill,
      work stoppage, computer failure or malfunction, or any and all
      other commercial damages or losses), even if such Contributor
      has been advised of the possibility of such damages.

   9. Accepting Warranty or Additional Liability. While redistributing
      the Work or Derivative Works thereof, You may choose to offer,
      and charge a fee for, acceptance of support, warranty, indemnity,
      or other liability obligations and/or rights consistent with this
      License. However, in accepting such obligations, You may act only
      on Your own behalf and on Your sole responsibility, not on behalf
      of any other Contributor, and only if You agree to indemnify,
      defend, and hold each Contributor harmless for any liability
      incurred by, or claims asserted against, such Contributor by reason
      of your accepting any such warranty or additional liability.

   END OF TERMS AND CONDITIONS

   APPENDIX: How to apply the Apache License to your work.

      To apply the Apache License to your work, attach the following
      boilerplate notice, with the fields enclosed by brackets "[]"
      replaced with your own identifying information. (Don't include
      the brackets!)  The text should be enclosed in the appropriate
      comment syntax for the file format. We also recommend that a
      file or class name and description of purpose be included on the
      same "printed page" as the copyright notice for easier
      identification within third-party archives.

   Copyright [yyyy] [name of copyright owner]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
```

### IBM Plex Sans / IBM Plex Mono IBM Plex 6.4.0

- SPDX: `OFL-1.1`
- Copyright: Copyright (c) 2017 IBM Corp. with Reserved Font Name "Plex"
- Upstream: https://github.com/IBM/plex

```text
Copyright © 2017 IBM Corp. with Reserved Font Name "Plex"

This Font Software is licensed under the SIL Open Font License, Version 1.1.

This license is copied below, and is also available with a FAQ at: http://scripts.sil.org/OFL


-----------------------------------------------------------
SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007
-----------------------------------------------------------

PREAMBLE
The goals of the Open Font License (OFL) are to stimulate worldwide
development of collaborative font projects, to support the font creation
efforts of academic and linguistic communities, and to provide a free and
open framework in which fonts may be shared and improved in partnership
with others.

The OFL allows the licensed fonts to be used, studied, modified and
redistributed freely as long as they are not sold by themselves. The
fonts, including any derivative works, can be bundled, embedded,
redistributed and/or sold with any software provided that any reserved
names are not used by derivative works. The fonts and derivatives,
however, cannot be released under any other type of license. The
requirement for fonts to remain under this license does not apply
to any document created using the fonts or their derivatives.

DEFINITIONS
"Font Software" refers to the set of files released by the Copyright
Holder(s) under this license and clearly marked as such. This may
include source files, build scripts and documentation.

"Reserved Font Name" refers to any names specified as such after the
copyright statement(s).

"Original Version" refers to the collection of Font Software components as
distributed by the Copyright Holder(s).

"Modified Version" refers to any derivative made by adding to, deleting,
or substituting -- in part or in whole -- any of the components of the
Original Version, by changing formats or by porting the Font Software to a
new environment.

"Author" refers to any designer, engineer, programmer, technical
writer or other person who contributed to the Font Software.

PERMISSION & CONDITIONS
Permission is hereby granted, free of charge, to any person obtaining
a copy of the Font Software, to use, study, copy, merge, embed, modify,
redistribute, and sell modified and unmodified copies of the Font
Software, subject to the following conditions:

1) Neither the Font Software nor any of its individual components,
in Original or Modified Versions, may be sold by itself.

2) Original or Modified Versions of the Font Software may be bundled,
redistributed and/or sold with any software, provided that each copy
contains the above copyright notice and this license. These can be
included either as stand-alone text files, human-readable headers or
in the appropriate machine-readable metadata fields within text or
binary files as long as those fields can be easily viewed by the user.

3) No Modified Version of the Font Software may use the Reserved Font
Name(s) unless explicit written permission is granted by the corresponding
Copyright Holder. This restriction only applies to the primary font name as
presented to the users.

4) The name(s) of the Copyright Holder(s) or the Author(s) of the Font
Software shall not be used to promote, endorse or advertise any
Modified Version, except to acknowledge the contribution(s) of the
Copyright Holder(s) and the Author(s) or with their explicit written
permission.

5) The Font Software, modified or unmodified, in part or in whole,
must be distributed entirely under this license, and must not be
distributed under any other license. The requirement for fonts to
remain under this license does not apply to any document created
using the Font Software.

TERMINATION
This license becomes null and void if any of the above conditions are
not met.

DISCLAIMER
THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL THE
COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL
DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM
OTHER DEALINGS IN THE FONT SOFTWARE.
```

### Intel XeSS SDK de0fb9c

- SPDX: `LicenseRef-Intel-XeSS`
- Copyright: Copyright (C) 2025 Intel Corporation
- Upstream: https://github.com/intel/xess

```text
Intel Simplified Software License (Version October 2022)

Intel(R) Xe Super Sampling (XeSS) SDK: Copyright (C) 2025 Intel Corporation

Use and Redistribution. You may use and redistribute the software, which is provided in binary form only, (the "Software"), without modification, provided the following conditions are met:

* 	Redistributions must reproduce the above copyright notice and these terms of use in the Software and in the documentation and/or other materials provided with the distribution.
* 	Neither the name of Intel nor the names of its suppliers may be used to endorse or promote products derived from this Software without specific prior written permission.
* 	No reverse engineering, decompilation, or disassembly of the Software is permitted, nor any modification or alteration of the Software or its operation at any time, including during execution.

No other licenses. Except as provided in the preceding section, Intel grants no licenses or other rights by implication, estoppel or otherwise to, patent, copyright, trademark, trade name, service mark or other intellectual property licenses or rights of Intel.

Third party software.  "Third Party Software" means the files (if any) listed in the "third-party-software.txt" or other similarly-named text file that may be included with the Software. Third Party Software, even if included with the distribution of the Software, may be governed by separate license terms, including without limitation, third party license terms, open source software notices and terms, and/or other Intel software license terms. These separate license terms solely govern Your use of the Third Party Software.

DISCLAIMER. THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT ARE DISCLAIMED. THIS SOFTWARE IS NOT INTENDED FOR USE IN SYSTEMS OR APPLICATIONS WHERE FAILURE OF THE SOFTWARE MAY CAUSE PERSONAL INJURY OR DEATH AND YOU AGREE THAT YOU ARE FULLY RESPONSIBLE FOR ANY CLAIMS, COSTS, DAMAGES, EXPENSES, AND ATTORNEYS' FEES ARISING OUT OF ANY SUCH USE, EVEN IF ANY CLAIM ALLEGES THAT INTEL WAS NEGLIGENT REGARDING THE DESIGN OR MANUFACTURE OF THE SOFTWARE.

LIMITATION OF LIABILITY. IN NO EVENT WILL INTEL BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

No support.  Intel may make changes to the Software, at any time without notice, and is not obligated to support, update or provide training for the Software.

Termination. Your right to use the Software is terminated in the event of your breach of this license.

Feedback.  Should you provide Intel with comments, modifications, corrections, enhancements or other input (“Feedback”) related to the Software, Intel will be free to use, disclose, reproduce, license or otherwise distribute or exploit the Feedback in its sole discretion without any obligations or restrictions of any kind, including without limitation, intellectual property rights or licensing obligations.

Compliance with laws.  You agree to comply with all relevant laws and regulations governing your use, transfer, import or export (or prohibition thereof) of the Software.

Governing law.  All disputes will be governed by the laws of the United States of America and the State of Delaware without reference to conflict of law principles and subject to the exclusive jurisdiction of the state or federal courts sitting in the State of Delaware, and each party agrees that it submits to the personal jurisdiction and venue of those courts and waives any objections. THE UNITED NATIONS CONVENTION ON CONTRACTS FOR THE INTERNATIONAL SALE OF GOODS (1980) IS SPECIFICALLY EXCLUDED AND WILL NOT APPLY TO THE SOFTWARE.
```

### AMD FidelityFX SDK 7539ae8

- SPDX: `LicenseRef-AMD-FidelityFX`
- Copyright: Copyright (C) Advanced Micro Devices, Inc.
- Upstream: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK

```text
The following license applies to all files except as noted below.

Copyright (C)  Advanced Micro Devices, Inc. All rights reserved.

REDISTRIBUTION: Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to install, reproduce, copy and distribute copies of the Software, in binary form only, and to permit persons to whom the Software is provided to do the same, provided that the following conditions are met:

No reverse engineering, decompilation, or disassembly of this Software is permitted.

Redistributions must reproduce the above copyright notice, this permission notice, and the following disclaimers and notices in the Software documentation and/or other materials provided with the Software.

DISCLAIMER: THE USE OF THE SOFTWARE IS AT YOUR SOLE RISK.  THE SOFTWARE
IS PROVIDED "AS IS" AND WITHOUT WARRANTY OF ANY KIND AND COPYRIGHT
HOLDER AND ITS LICENSORS EXPRESSLY DISCLAIM ALL WARRANTIES, EXPRESS AND
IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
COPYRIGHT HOLDER AND ITS LICENSORS DO NOT WARRANT THAT THE SOFTWARE WILL
MEET YOUR REQUIREMENTS, OR THAT THE OPERATION OF THE SOFTWARE WILL BE
UNINTERRUPTED OR ERROR-FREE.  THE ENTIRE RISK ASSOCIATED WITH THE USE OF
THE SOFTWARE IS ASSUMED BY YOU.  FURTHERMORE, COPYRIGHT HOLDER AND ITS
LICENSORS DO NOT WARRANT OR MAKE ANY REPRESENTATIONS REGARDING THE USE
OR THE RESULTS OF THE USE OF THE SOFTWARE IN TERMS OF ITS CORRECTNESS,
ACCURACY, RELIABILITY, CURRENTNESS, OR OTHERWISE.

DISCLAIMER: UNDER NO CIRCUMSTANCES INCLUDING NEGLIGENCE, SHALL COPYRIGHT HOLDER AND ITS LICENSORS OR ITS DIRECTORS, OFFICERS, EMPLOYEES OR AGENTS ("AUTHORIZED REPRESENTATIVES") BE LIABLE FOR ANY INCIDENTAL, INDIRECT,
SPECIAL OR CONSEQUENTIAL DAMAGES (INCLUDING DAMAGES FOR LOSS OF BUSINESS
PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, AND THE
LIKE) ARISING OUT OF THE USE, MISUSE OR INABILITY TO USE THE SOFTWARE,
BREACH OR DEFAULT, INCLUDING THOSE ARISING FROM INFRINGEMENT OR ALLEGED
INFRINGEMENT OF ANY PATENT, TRADEMARK, COPYRIGHT OR OTHER INTELLECTUAL
PROPERTY RIGHT EVEN IF COPYRIGHT HOLDER AND ITS AUTHORIZED
REPRESENTATIVES HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.  IN
NO EVENT SHALL COPYRIGHT HOLDER OR ITS AUTHORIZED REPRESENTATIVES TOTAL
LIABILITY FOR ALL DAMAGES, LOSSES, AND CAUSES OF ACTION (WHETHER IN
CONTRACT, TORT (INCLUDING NEGLIGENCE) OR OTHERWISE) EXCEED THE AMOUNT OF
US$10.

These source files

- `3rdpartynotice.md`
- `ClearMediaCache.bat`
- `docs\getting-started\building-samples.md`
- `docs\getting-started\index.md`
- `docs\getting-started\media\component-dependencies-dark.png`
- `docs\getting-started\media\component-dependencies.png`
- `docs\getting-started\media\config_init.jpg`
- `docs\getting-started\media\lighting-ui.jpg`
- `docs\getting-started\media\limiter-ui.jpg`
- `docs\getting-started\media\magnifier-ui.jpg`
- `docs\getting-started\media\particle-ui.jpg`
- `docs\getting-started\media\postprocessing-ui.jpg`
- `docs\getting-started\media\shadowraster-ui.jpg`
- `docs\getting-started\media\skydome-ui.jpg`
- `docs\getting-started\media\taa-ui.jpg`
- `docs\getting-started\running-samples.md`
- `docs\getting-started\sdk-structure.md`
- `docs\license.md`
- `docs\media\AMD_FidelityFX_Lockup_Black.png`
- `docs\media\AMD_FidelityFX_SDK_Colored.jpg`
- `docs\media\fidelityfxsdk-logo-rescaled.png`
- `docs\samples\denoiser.md`
- `docs\samples\index.md`
- `docs\samples\media\blur\blur.jpg`
- `docs\samples\media\blur\blur.png`
- `docs\samples\media\brixelizergi\sample.png`
- `docs\samples\media\contrast-adaptive-sharpening\cas-sample.png`
- `docs\samples\media\denoiser\fsr-ray-regeneration-sample.png`
- `docs\samples\media\hybrid-reflections\hybrid-reflections.jpg`
- `docs\samples\media\hybrid-reflections\hybrid-reflections.png`
- `docs\samples\media\hybrid-shadows\hybrid-shadows-sample.jpg`
- `docs\samples\media\hybrid-shadows\hybrid-shadows-sample.png`
- `docs\samples\media\lens\lens-sample_resized.jpg`
- `docs\samples\media\lens\lens-sample.jpg`
- `docs\samples\media\luma-preserving-mapper\LPM.jpg`
- `docs\samples\media\luma-preserving-mapper\LPM.png`
- `docs\samples\media\luma-preserving-mapper\NoLPM.jpg`
- `docs\samples\media\luma-preserving-mapper\NoLPM.png`
- `docs\samples\media\parallel-sort\parallel-sort_resized.jpg`
- `docs\samples\media\parallel-sort\parallel-sort.jpg`
- `docs\samples\media\radiance-caching\nrc-preview-sample.png`
- `docs\samples\media\single-pass-downsampler\spd-sample.jpg`
- `docs\samples\media\stochastic-screen-space-reflections\sssr-sample_resized.jpg`
- `docs\samples\media\stochastic-screen-space-reflections\sssr-sample.jpg`
- `docs\samples\media\super-resolution\frame-interpolation-debug-overlay.svg`
- `docs\samples\media\super-resolution\fsr3-sample_resized.jpg`
- `docs\samples\media\super-resolution\fsr3-sample.jpg`
- `docs\samples\media\super-resolution\upscaler-debug-overlay.svg`
- `docs\samples\media\variable-shading\variable-shading.jpg`
- `docs\samples\media\variable-shading\variable-shading.png`
- `docs\samples\radiance-cache.md`
- `docs\samples\super-resolution.md`
- `docs\sdk.css`
- `docs\tools\index.md`
- `docs\tools\media-delivery.md`
- `Kits\Cauldron2\dx12\Cauldron_vs2022.vcxproj`
- `Kits\Cauldron2\dx12\Cauldron_vs2022.vcxproj.filters`
- `Kits\Cauldron2\dx12\vcpkg.json`
- `Kits\Cauldron2\framework\config\cauldronconfig.json`
- `Kits\Cauldron2\framework\core\backend_interface.h`
- `Kits\Cauldron2\framework\core\component.cpp`
- `Kits\Cauldron2\framework\core\component.h`
- `Kits\Cauldron2\framework\core\components\animationcomponent.cpp`
- `Kits\Cauldron2\framework\core\components\animationcomponent.h`
- `Kits\Cauldron2\framework\core\components\cameracomponent.cpp`
- `Kits\Cauldron2\framework\core\components\cameracomponent.h`
- `Kits\Cauldron2\framework\core\components\lightcomponent.cpp`
- `Kits\Cauldron2\framework\core\components\lightcomponent.h`
- `Kits\Cauldron2\framework\core\components\meshcomponent.cpp`
- `Kits\Cauldron2\framework\core\components\meshcomponent.h`
- `Kits\Cauldron2\framework\core\components\particlespawnercomponent.cpp`
- `Kits\Cauldron2\framework\core\components\particlespawnercomponent.h`
- `Kits\Cauldron2\framework\core\contentloader.h`
- `Kits\Cauldron2\framework\core\contentmanager.cpp`
- `Kits\Cauldron2\framework\core\contentmanager.h`
- `Kits\Cauldron2\framework\core\entity.cpp`
- `Kits\Cauldron2\framework\core\entity.h`
- `Kits\Cauldron2\framework\core\framework.cpp`
- `Kits\Cauldron2\framework\core\framework.h`
- `Kits\Cauldron2\framework\core\inputmanager.cpp`
- `Kits\Cauldron2\framework\core\inputmanager.h`
- `Kits\Cauldron2\framework\core\loaders\gltfloader.cpp`
- `Kits\Cauldron2\framework\core\loaders\gltfloader.h`
- `Kits\Cauldron2\framework\core\loaders\particleloader.cpp`
- `Kits\Cauldron2\framework\core\loaders\particleloader.h`
- `Kits\Cauldron2\framework\core\loaders\textureloader.cpp`
- `Kits\Cauldron2\framework\core\loaders\textureloader.h`
- `Kits\Cauldron2\framework\core\scene.cpp`
- `Kits\Cauldron2\framework\core\scene.h`
- `Kits\Cauldron2\framework\core\taskmanager.cpp`
- `Kits\Cauldron2\framework\core\taskmanager.h`
- `Kits\Cauldron2\framework\core\uibackend.cpp`
- `Kits\Cauldron2\framework\core\uibackend.h`
- `Kits\Cauldron2\framework\core\uimanager.cpp`
- `Kits\Cauldron2\framework\core\uimanager.h`
- `Kits\Cauldron2\framework\core\win\framework_win.cpp`
- `Kits\Cauldron2\framework\core\win\framework_win.h`
- `Kits\Cauldron2\framework\core\win\inputmanager_win.cpp`
- `Kits\Cauldron2\framework\core\win\inputmanager_win.h`
- `Kits\Cauldron2\framework\core\win\uibackend_win.cpp`
- `Kits\Cauldron2\framework\core\win\uibackend_win.h`
- `Kits\Cauldron2\framework\misc\assert.h`
- `Kits\Cauldron2\framework\misc\corecounts.cpp`
- `Kits\Cauldron2\framework\misc\corecounts.h`
- `Kits\Cauldron2\framework\misc\fileio.cpp`
- `Kits\Cauldron2\framework\misc\fileio.h`
- `Kits\Cauldron2\framework\misc\helpers.h`
- `Kits\Cauldron2\framework\misc\log.cpp`
- `Kits\Cauldron2\framework\misc\log.h`
- `Kits\Cauldron2\framework\misc\math.cpp`
- `Kits\Cauldron2\framework\misc\math.h`
- `Kits\Cauldron2\framework\misc\sync.h`
- `Kits\Cauldron2\framework\misc\threadsafe_queue.h`
- `Kits\Cauldron2\framework\misc\threadsafe_ringbuffer.h`
- `Kits\Cauldron2\framework\render\animation.cpp`
- `Kits\Cauldron2\framework\render\animation.h`
- `Kits\Cauldron2\framework\render\backend_shader_reloader\backend_shader_reloader_common.cpp`
- `Kits\Cauldron2\framework\render\backend_shader_reloader\backend_shader_reloader_common.h`
- `Kits\Cauldron2\framework\render\backend_shader_reloader\backend_shader_reloader.cpp`
- `Kits\Cauldron2\framework\render\backend_shader_reloader\backend_shader_reloader.h`
- `Kits\Cauldron2\framework\render\backend_shader_reloader\CMakeLists.txt`
- `Kits\Cauldron2\framework\render\backend_shader_reloader\command_execution.cpp`
- `Kits\Cauldron2\framework\render\backend_shader_reloader\command_execution.h`
- `Kits\Cauldron2\framework\render\backend_shader_reloader\native_backend_shader_reloader.cpp`
- `Kits\Cauldron2\framework\render\backend_shader_reloader\native_backend_shader_reloader.h`
- `Kits\Cauldron2\framework\render\buffer.cpp`
- `Kits\Cauldron2\framework\render\buffer.h`
- `Kits\Cauldron2\framework\render\color_conversion.cpp`
- `Kits\Cauldron2\framework\render\color_conversion.h`
- `Kits\Cauldron2\framework\render\commandlist.cpp`
- `Kits\Cauldron2\framework\render\commandlist.h`
- `Kits\Cauldron2\framework\render\copyresource.cpp`
- `Kits\Cauldron2\framework\render\copyresource.h`
- `Kits\Cauldron2\framework\render\device.cpp`
- `Kits\Cauldron2\framework\render\device.h`
- `Kits\Cauldron2\framework\render\dx12\buffer_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\buffer_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\commandlist_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\commandlist_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\copyresource_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\copyresource_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\defines_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\device_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\device_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\dynamicbufferpool_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\dynamicbufferpool_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\gpuresource_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\gpuresource_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\indirectworkload_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\indirectworkload_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\parameterset_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\parameterset_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\pipelinedesc_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\pipelinedesc_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\pipelineobject_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\pipelineobject_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\profiler_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\profiler_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\resourceview_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\resourceview_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\resourceviewallocator_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\resourceviewallocator_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\rootsignature_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\rootsignature_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\rootsignaturedesc_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\rootsignaturedesc_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\rtresources_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\rtresources_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\sampler_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\sampler_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\swapchain_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\swapchain_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\texture_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\texture_dx12.h`
- `Kits\Cauldron2\framework\render\dx12\uploadheap_dx12.cpp`
- `Kits\Cauldron2\framework\render\dx12\uploadheap_dx12.h`
- `Kits\Cauldron2\framework\render\dynamicbufferpool.cpp`
- `Kits\Cauldron2\framework\render\dynamicbufferpool.h`
- `Kits\Cauldron2\framework\render\dynamicresourcepool.cpp`
- `Kits\Cauldron2\framework\render\dynamicresourcepool.h`
- `Kits\Cauldron2\framework\render\gpuresource.cpp`
- `Kits\Cauldron2\framework\render\gpuresource.h`
- `Kits\Cauldron2\framework\render\indirectworkload.h`
- `Kits\Cauldron2\framework\render\material.cpp`
- `Kits\Cauldron2\framework\render\material.h`
- `Kits\Cauldron2\framework\render\mesh.cpp`
- `Kits\Cauldron2\framework\render\mesh.h`
- `Kits\Cauldron2\framework\render\parameterset.cpp`
- `Kits\Cauldron2\framework\render\parameterset.h`
- `Kits\Cauldron2\framework\render\particle.cpp`
- `Kits\Cauldron2\framework\render\particle.h`
- `Kits\Cauldron2\framework\render\pipelinedesc.cpp`
- `Kits\Cauldron2\framework\render\pipelinedesc.h`
- `Kits\Cauldron2\framework\render\pipelineobject.h`
- `Kits\Cauldron2\framework\render\profiler.cpp`
- `Kits\Cauldron2\framework\render\profiler.h`
- `Kits\Cauldron2\framework\render\rasterview.cpp`
- `Kits\Cauldron2\framework\render\rasterview.h`
- `Kits\Cauldron2\framework\render\renderdefines.h`
- `Kits\Cauldron2\framework\render\rendermodule.cpp`
- `Kits\Cauldron2\framework\render\rendermodule.h`
- `Kits\Cauldron2\framework\render\rendermodules\fpslimiter\fpslimiterrendermodule.cpp`
- `Kits\Cauldron2\framework\render\rendermodules\fpslimiter\fpslimiterrendermodule.h`
- `Kits\Cauldron2\framework\render\rendermodules\raytracing\raytracingrendermodule.cpp`
- `Kits\Cauldron2\framework\render\rendermodules\raytracing\raytracingrendermodule.h`
- `Kits\Cauldron2\framework\render\rendermodules\rsr\runtimeshaderrecompilerrendermodule.cpp`
- `Kits\Cauldron2\framework\render\rendermodules\rsr\runtimeshaderrecompilerrendermodule.h`
- `Kits\Cauldron2\framework\render\rendermodules\skinning\skinningrendermodule.cpp`
- `Kits\Cauldron2\framework\render\rendermodules\skinning\skinningrendermodule.h`
- `Kits\Cauldron2\framework\render\rendermodules\swapchain\swapchainrendermodule.cpp`
- `Kits\Cauldron2\framework\render\rendermodules\swapchain\swapchainrendermodule.h`
- `Kits\Cauldron2\framework\render\rendermodules\tonemapping\tonemappingrendermodule.cpp`
- `Kits\Cauldron2\framework\render\rendermodules\tonemapping\tonemappingrendermodule.h`
- `Kits\Cauldron2\framework\render\rendermodules\ui\uirendermodule.cpp`
- `Kits\Cauldron2\framework\render\rendermodules\ui\uirendermodule.h`
- `Kits\Cauldron2\framework\render\resourceresizedlistener.cpp`
- `Kits\Cauldron2\framework\render\resourceresizedlistener.h`
- `Kits\Cauldron2\framework\render\resourceview.cpp`
- `Kits\Cauldron2\framework\render\resourceview.h`
- `Kits\Cauldron2\framework\render\resourceviewallocator.cpp`
- `Kits\Cauldron2\framework\render\resourceviewallocator.h`
- `Kits\Cauldron2\framework\render\rootsignature.h`
- `Kits\Cauldron2\framework\render\rootsignaturedesc.cpp`
- `Kits\Cauldron2\framework\render\rootsignaturedesc.h`
- `Kits\Cauldron2\framework\render\rtresources.h`
- `Kits\Cauldron2\framework\render\sampler.h`
- `Kits\Cauldron2\framework\render\shaderbuilder.h`
- `Kits\Cauldron2\framework\render\shaderbuilderhelper.cpp`
- `Kits\Cauldron2\framework\render\shaderbuilderhelper.h`
- `Kits\Cauldron2\framework\render\shadowmapresourcepool.cpp`
- `Kits\Cauldron2\framework\render\shadowmapresourcepool.h`
- `Kits\Cauldron2\framework\render\swapchain.cpp`
- `Kits\Cauldron2\framework\render\swapchain.h`
- `Kits\Cauldron2\framework\render\texture.cpp`
- `Kits\Cauldron2\framework\render\texture.h`
- `Kits\Cauldron2\framework\render\uploadheap.cpp`
- `Kits\Cauldron2\framework\render\uploadheap.h`
- `Kits\Cauldron2\framework\render\vk\buffer_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\buffer_vk.h`
- `Kits\Cauldron2\framework\render\vk\commandlist_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\commandlist_vk.h`
- `Kits\Cauldron2\framework\render\vk\copyresource_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\copyresource_vk.h`
- `Kits\Cauldron2\framework\render\vk\device_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\device_vk.h`
- `Kits\Cauldron2\framework\render\vk\dynamicbufferpool_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\dynamicbufferpool_vk.h`
- `Kits\Cauldron2\framework\render\vk\gpuresource_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\gpuresource_vk.h`
- `Kits\Cauldron2\framework\render\vk\helpers.cpp`
- `Kits\Cauldron2\framework\render\vk\helpers.h`
- `Kits\Cauldron2\framework\render\vk\indirectworkload_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\indirectworkload_vk.h`
- `Kits\Cauldron2\framework\render\vk\parameterset_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\parameterset_vk.h`
- `Kits\Cauldron2\framework\render\vk\pipelinedesc_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\pipelinedesc_vk.h`
- `Kits\Cauldron2\framework\render\vk\pipelineobject_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\pipelineobject_vk.h`
- `Kits\Cauldron2\framework\render\vk\profiler_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\profiler_vk.h`
- `Kits\Cauldron2\framework\render\vk\resourceview_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\resourceview_vk.h`
- `Kits\Cauldron2\framework\render\vk\resourceviewallocator_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\resourceviewallocator_vk.h`
- `Kits\Cauldron2\framework\render\vk\rootsignature_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\rootsignature_vk.h`
- `Kits\Cauldron2\framework\render\vk\rootsignaturedesc_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\rootsignaturedesc_vk.h`
- `Kits\Cauldron2\framework\render\vk\rtresources_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\rtresources_vk.h`
- `Kits\Cauldron2\framework\render\vk\sampler_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\sampler_vk.h`
- `Kits\Cauldron2\framework\render\vk\swapchain_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\swapchain_vk.h`
- `Kits\Cauldron2\framework\render\vk\texture_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\texture_vk.h`
- `Kits\Cauldron2\framework\render\vk\uploadheap_vk.cpp`
- `Kits\Cauldron2\framework\render\vk\uploadheap_vk.h`
- `Kits\Cauldron2\framework\render\win\shaderbuilder_win.cpp`
- `Kits\Cauldron2\framework\shaders\autoexposure.hlsl`
- `Kits\Cauldron2\framework\shaders\builddistortionfield.hlsl`
- `Kits\Cauldron2\framework\shaders\commonintersect.hlsl`
- `Kits\Cauldron2\framework\shaders\computeskinning.hlsl`
- `Kits\Cauldron2\framework\shaders\copytexture.hlsl`
- `Kits\Cauldron2\framework\shaders\fidelityfx\ffx_common_types.h`
- `Kits\Cauldron2\framework\shaders\fidelityfx\ffx_core_cpu.h`
- `Kits\Cauldron2\framework\shaders\fidelityfx\ffx_core_gpu_common_half.h`
- `Kits\Cauldron2\framework\shaders\fidelityfx\ffx_core_gpu_common.h`
- `Kits\Cauldron2\framework\shaders\fidelityfx\ffx_core_hlsl.h`
- `Kits\Cauldron2\framework\shaders\fidelityfx\ffx_core_portability.h`
- `Kits\Cauldron2\framework\shaders\fidelityfx\ffx_core.h`
- `Kits\Cauldron2\framework\shaders\fidelityfx\parallelsort\ffx_parallelsort_common.h`
- `Kits\Cauldron2\framework\shaders\fidelityfx\parallelsort\ffx_parallelsort.h`
- `Kits\Cauldron2\framework\shaders\fidelityfx\spd\ffx_spd.h`
- `Kits\Cauldron2\framework\shaders\fpslimiter.hlsl`
- `Kits\Cauldron2\framework\shaders\fullscreen.hlsl`
- `Kits\Cauldron2\framework\shaders\hudlessblit.hlsl`
- `Kits\Cauldron2\framework\shaders\lensdistortion.h`
- `Kits\Cauldron2\framework\shaders\lightfunctionscommon.hlsl`
- `Kits\Cauldron2\framework\shaders\lightingcommon.h`
- `Kits\Cauldron2\framework\shaders\particlecommon.h`
- `Kits\Cauldron2\framework\shaders\rasterlightfunctions.hlsl`
- `Kits\Cauldron2\framework\shaders\raytracingcommon.hlsl`
- `Kits\Cauldron2\framework\shaders\raytracinglightfunctions.hlsl`
- `Kits\Cauldron2\framework\shaders\shadercommon.h`
- `Kits\Cauldron2\framework\shaders\shadowFiltering.h`
- `Kits\Cauldron2\framework\shaders\surfacerendercommon.h`
- `Kits\Cauldron2\framework\shaders\tonemappercommon.h`
- `Kits\Cauldron2\framework\shaders\tonemappers.hlsl`
- `Kits\Cauldron2\framework\shaders\tonemapping.hlsl`
- `Kits\Cauldron2\framework\shaders\transferFunction.h`
- `Kits\Cauldron2\framework\shaders\transformVS.hlsl`
- `Kits\Cauldron2\framework\shaders\ui.hlsl`
- `Kits\Cauldron2\framework\shaders\uicommon.h`
- `Kits\Cauldron2\framework\shaders\upscaler.h`
- `Kits\Cauldron2\rendermodules\animatedtextures\animatedtexturesrendermodule.cpp`
- `Kits\Cauldron2\rendermodules\animatedtextures\animatedtexturesrendermodule.h`
- `Kits\Cauldron2\rendermodules\animatedtextures\shaders\AnimatedTexture.hlsl`
- `Kits\Cauldron2\rendermodules\gbuffer\config\gbufferrendermodule.json`
- `Kits\Cauldron2\rendermodules\gbuffer\gbufferrendermodule.cpp`
- `Kits\Cauldron2\rendermodules\gbuffer\gbufferrendermodule.h`
- `Kits\Cauldron2\rendermodules\gbuffer\shaders\gbufferps.hlsl`
- `Kits\Cauldron2\rendermodules\gpuparticle\ffx_assert.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\ffx_error.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\ffx_interface.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\ffx_parallelsort_private.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\ffx_parallelsort.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\ffx_types.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\ffx_util.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\gpuparticlerendermodule.cpp`
- `Kits\Cauldron2\rendermodules\gpuparticle\gpuparticlerendermodule.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\parallelsort.cpp`
- `Kits\Cauldron2\rendermodules\gpuparticle\parallelsort.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\parallelsort\ffx_parallelsort_callbacks_glsl.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\parallelsort\ffx_parallelsort_callbacks_hlsl.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\parallelsort\ffx_parallelsort_common.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\parallelsort\ffx_parallelsort_reduce.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\parallelsort\ffx_parallelsort_resources.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\parallelsort\ffx_parallelsort_scan_add.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\parallelsort\ffx_parallelsort_scan.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\parallelsort\ffx_parallelsort_scatter.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\parallelsort\ffx_parallelsort_setup_indirect_args.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\parallelsort\ffx_parallelsort_sum.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\parallelsort\ffx_parallelsort.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\shaders\parallelsort_common_ffx.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\shaders\parallelsort_reduce_pass.hlsl`
- `Kits\Cauldron2\rendermodules\gpuparticle\shaders\parallelsort_scan_add_pass.hlsl`
- `Kits\Cauldron2\rendermodules\gpuparticle\shaders\parallelsort_scan_pass.hlsl`
- `Kits\Cauldron2\rendermodules\gpuparticle\shaders\parallelsort_scatter_pass.hlsl`
- `Kits\Cauldron2\rendermodules\gpuparticle\shaders\parallelsort_setup_indirect_args.hlsl`
- `Kits\Cauldron2\rendermodules\gpuparticle\shaders\parallelsort_sum_pass.hlsl`
- `Kits\Cauldron2\rendermodules\gpuparticle\shaders\particleemit.hlsl`
- `Kits\Cauldron2\rendermodules\gpuparticle\shaders\particlesimulation.hlsl`
- `Kits\Cauldron2\rendermodules\gpuparticle\shaders\particlesimulationcommon.h`
- `Kits\Cauldron2\rendermodules\gpuparticle\shaders\simulationbindings.h`
- `Kits\Cauldron2\rendermodules\lighting\lightingrendermodule.cpp`
- `Kits\Cauldron2\rendermodules\lighting\lightingrendermodule.h`
- `Kits\Cauldron2\rendermodules\lighting\shaders\lighting.hlsl`
- `Kits\Cauldron2\rendermodules\rastershadow\config\rastershadowrendermodule.json`
- `Kits\Cauldron2\rendermodules\rastershadow\rastershadowrendermodule.cpp`
- `Kits\Cauldron2\rendermodules\rastershadow\rastershadowrendermodule.h`
- `Kits\Cauldron2\rendermodules\rastershadow\shaders\rastershadowps.hlsl`
- `Kits\Cauldron2\rendermodules\rendermoduleregistry.cpp`
- `Kits\Cauldron2\rendermodules\rendermoduleregistry.h`
- `Kits\Cauldron2\rendermodules\skydome\config\skydomerendermodule.json`
- `Kits\Cauldron2\rendermodules\skydome\shaders\skydome.hlsl`
- `Kits\Cauldron2\rendermodules\skydome\shaders\skydomecommon.h`
- `Kits\Cauldron2\rendermodules\skydome\shaders\skydomeproc.hlsl`
- `Kits\Cauldron2\rendermodules\skydome\skydomerendermodule.cpp`
- `Kits\Cauldron2\rendermodules\skydome\skydomerendermodule.h`
- `Kits\Cauldron2\rendermodules\taa\config\taarendermodule.json`
- `Kits\Cauldron2\rendermodules\taa\shaders\taa.hlsl`
- `Kits\Cauldron2\rendermodules\taa\shaders\taacommon.h`
- `Kits\Cauldron2\rendermodules\taa\shaders\taaPost.hlsl`
- `Kits\Cauldron2\rendermodules\taa\taarendermodule.cpp`
- `Kits\Cauldron2\rendermodules\taa\taarendermodule.h`
- `Kits\Cauldron2\rendermodules\translucency\config\translucencyrendermodule.json`
- `Kits\Cauldron2\rendermodules\translucency\shaders\particlerender.hlsl`
- `Kits\Cauldron2\rendermodules\translucency\shaders\particlerendercommon.h`
- `Kits\Cauldron2\rendermodules\translucency\shaders\translucencyps.hlsl`
- `Kits\Cauldron2\rendermodules\translucency\translucencyrendermodule.cpp`
- `Kits\Cauldron2\rendermodules\translucency\translucencyrendermodule.h`
- `Kits\Cauldron2\vk\Cauldron_vs2022.vcxproj`
- `Kits\Cauldron2\vk\Cauldron_vs2022.vcxproj.filters`
- `Kits\Cauldron2\vk\vcpkg.json`
- `Kits\FidelityFX\api\include\dx12\ffx_api_dx12.h`
- `Kits\FidelityFX\api\include\dx12\ffx_api_dx12.hpp`
- `Kits\FidelityFX\api\include\ffx_api_loader.h`
- `Kits\FidelityFX\api\include\ffx_api_matrix_helpers.h`
- `Kits\FidelityFX\api\include\ffx_api_matrix_helpers.hpp`
- `Kits\FidelityFX\api\include\ffx_api_types.h`
- `Kits\FidelityFX\api\include\ffx_api.h`
- `Kits\FidelityFX\api\include\ffx_api.hpp`
- `Kits\FidelityFX\api\include\vk\ffx_api_vk.h`
- `Kits\FidelityFX\api\include\vk\ffx_api_vk.hpp`
- `Kits\FidelityFX\api\internal\ffx_api_helper.h`
- `Kits\FidelityFX\api\internal\ffx_api.cpp`
- `Kits\FidelityFX\api\internal\ffx_assert.cpp`
- `Kits\FidelityFX\api\internal\ffx_assert.h`
- `Kits\FidelityFX\api\internal\ffx_backends.h`
- `Kits\FidelityFX\api\internal\ffx_blob_compression.h`
- `Kits\FidelityFX\api\internal\ffx_blob_decompress.h`
- `Kits\FidelityFX\api\internal\ffx_error.h`
- `Kits\FidelityFX\api\internal\ffx_interface.h`
- `Kits\FidelityFX\api\internal\ffx_internal_types.h`
- `Kits\FidelityFX\api\internal\ffx_message.cpp`
- `Kits\FidelityFX\api\internal\ffx_message.h`
- `Kits\FidelityFX\api\internal\ffx_object_management.cpp`
- `Kits\FidelityFX\api\internal\ffx_object_management.h`
- `Kits\FidelityFX\api\internal\ffx_provider.h`
- `Kits\FidelityFX\api\internal\ffx_query_fallback.cpp`
- `Kits\FidelityFX\api\internal\ffx_util.h`
- `Kits\FidelityFX\api\internal\gpu\ffx_common_types.h`
- `Kits\FidelityFX\api\internal\gpu\ffx_core_cpu.h`
- `Kits\FidelityFX\api\internal\gpu\ffx_core_gpu_common_half.h`
- `Kits\FidelityFX\api\internal\gpu\ffx_core_gpu_common.h`
- `Kits\FidelityFX\api\internal\gpu\ffx_core_hlsl.h`
- `Kits\FidelityFX\api\internal\gpu\ffx_core_portability.h`
- `Kits\FidelityFX\api\internal\gpu\ffx_core.h`
- `Kits\FidelityFX\backend\dx12\d3dx12.h`
- `Kits\FidelityFX\backend\dx12\ffx_backends_dx12.cpp`
- `Kits\FidelityFX\backend\dx12\ffx_dx12.cpp`
- `Kits\FidelityFX\backend\dx12\ffx_dx12.h`
- `Kits\FidelityFX\backend\vk\ffx_backends_vk.cpp`
- `Kits\FidelityFX\backend\vk\ffx_vk.cpp`
- `Kits\FidelityFX\backend\vk\ffx_vk.h`
- `Kits\FidelityFX\denoisers\include\ffx_denoiser.h`
- `Kits\FidelityFX\denoisers\include\ffx_denoiser.hpp`
- `Kits\FidelityFX\docs\getting-started\ffx-api.md`
- `Kits\FidelityFX\docs\getting-started\index.md`
- `Kits\FidelityFX\docs\getting-started\media\component-dependencies-dark.png`
- `Kits\FidelityFX\docs\getting-started\media\component-dependencies.png`
- `Kits\FidelityFX\docs\getting-started\media\config_init.jpg`
- `Kits\FidelityFX\docs\getting-started\media\lighting-ui.jpg`
- `Kits\FidelityFX\docs\getting-started\media\limiter-ui.jpg`
- `Kits\FidelityFX\docs\getting-started\media\magnifier-ui.jpg`
- `Kits\FidelityFX\docs\getting-started\media\particle-ui.jpg`
- `Kits\FidelityFX\docs\getting-started\media\postprocessing-ui.jpg`
- `Kits\FidelityFX\docs\getting-started\media\shadowraster-ui.jpg`
- `Kits\FidelityFX\docs\getting-started\media\skydome-ui.jpg`
- `Kits\FidelityFX\docs\getting-started\media\taa-ui.jpg`
- `Kits\FidelityFX\docs\getting-started\naming-guidelines.md`
- `Kits\FidelityFX\docs\getting-started\sdk-structure.md`
- `Kits\FidelityFX\docs\license.md`
- `Kits\FidelityFX\docs\media\AMD_FSR_SDK_Lockup_RGB_Blk.png`
- `Kits\FidelityFX\docs\media\AMD_FSR_SDK_Lockup_RGB_Wht.png`
- `Kits\FidelityFX\docs\techniques\denoising.md`
- `Kits\FidelityFX\docs\techniques\frame-interpolation-api.md`
- `Kits\FidelityFX\docs\techniques\frame-interpolation-ml.md`
- `Kits\FidelityFX\docs\techniques\frame-interpolation-swap-chain.md`
- `Kits\FidelityFX\docs\techniques\frame-interpolation.md`
- `Kits\FidelityFX\docs\techniques\index.md`
- `Kits\FidelityFX\docs\techniques\media\cross.png`
- `Kits\FidelityFX\docs\techniques\media\denoiser\fsr-ray-regeneration-brdf-lut.jpg`
- `Kits\FidelityFX\docs\techniques\media\denoiser\fsr-ray-regeneration-debug-view.png`
- `Kits\FidelityFX\docs\techniques\media\denoiser\fsr-ray-regeneration-denoised-sample.png`
- `Kits\FidelityFX\docs\techniques\media\denoiser\fsr-ray-regeneration-dispatch.png`
- `Kits\FidelityFX\docs\techniques\media\denoiser\fsr-ray-regeneration-noisy-sample.png`
- `Kits\FidelityFX\docs\techniques\media\denoiser\fsr-ray-regeneration-sample.png`
- `Kits\FidelityFX\docs\techniques\media\frame-interpolation-swapchain\frame-interpolation-swap-chain.svg`
- `Kits\FidelityFX\docs\techniques\media\frame-interpolation\frame-interpolation-debug-overlay.svg`
- `Kits\FidelityFX\docs\techniques\media\frame-pacing\fixed-high.svg`
- `Kits\FidelityFX\docs\techniques\media\frame-pacing\fixed-low.svg`
- `Kits\FidelityFX\docs\techniques\media\frame-pacing\pacing-overview.svg`
- `Kits\FidelityFX\docs\techniques\media\frame-pacing\variable-inside.svg`
- `Kits\FidelityFX\docs\techniques\media\radiance-cache\banner.jpg`
- `Kits\FidelityFX\docs\techniques\media\radiance-cache\path-tracer-schematic-querying.png`
- `Kits\FidelityFX\docs\techniques\media\radiance-cache\path-tracer-schematic-self-train.png`
- `Kits\FidelityFX\docs\techniques\media\radiance-cache\path-tracer-schematic-subpaths.png`
- `Kits\FidelityFX\docs\techniques\media\radiance-cache\pipeline-one-pass.png`
- `Kits\FidelityFX\docs\techniques\media\radiance-cache\pipeline-two-pass.png`
- `Kits\FidelityFX\docs\techniques\media\radiance-cache\sub-path-eqn.png`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-interpolation\01_FSR3_DataFlow.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-interpolation\02_FSR3_Flow.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-interpolation\03_FSR3_FlowAsyncQueue.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-interpolation\04_FSR3_UiCallback.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-interpolation\05_FSR3_NoFG_UiCallback.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-interpolation\06_FSR3_UiTex.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-interpolation\07_FSR3_NoFG_UiTex.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-interpolation\08_FSR3_HUDLess.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-ml\fsr4-sample.jpg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-ml\pipeline-placement.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-temporal\fsr2-sample_resized.jpg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-temporal\fsr2-sample.jpg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-temporal\fsr3-sample_resized.jpg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-temporal\fsr3-sample.jpg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-temporal\jitter-space.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-temporal\motion-vectors.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-temporal\overview.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-temporal\pipeline-placement.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-upscaler\overview.svg`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-upscaler\upscaler-debug-fsr4.png`
- `Kits\FidelityFX\docs\techniques\media\super-resolution-upscaler\upscaler-debug-overlay.svg`
- `Kits\FidelityFX\docs\techniques\media\tick.png`
- `Kits\FidelityFX\docs\techniques\radiance-cache.md`
- `Kits\FidelityFX\docs\techniques\super-resolution-ml.md`
- `Kits\FidelityFX\docs\techniques\super-resolution-temporal.md`
- `Kits\FidelityFX\docs\techniques\super-resolution-upscaler.md`
- `Kits\FidelityFX\docs\whats-new\index.md`
- `Kits\FidelityFX\docs\whats-new\version_1_0.md`
- `Kits\FidelityFX\docs\whats-new\version_1_1_1.md`
- `Kits\FidelityFX\docs\whats-new\version_1_1_2.md`
- `Kits\FidelityFX\docs\whats-new\version_1_1_3.md`
- `Kits\FidelityFX\docs\whats-new\version_1_1_4.md`
- `Kits\FidelityFX\docs\whats-new\version_1_1.md`
- `Kits\FidelityFX\docs\whats-new\version_2_0_0.md`
- `Kits\FidelityFX\docs\whats-new\version_2_1_0.md`
- `Kits\FidelityFX\docs\whats-new\version_2_2_0.md`
- `Kits\FidelityFX\docs\whats-new\version_2_3_0.md`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\antilag2\ffx_antilag2_dx12.h`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\ffx_provider_fsr3framegenerationswapchain_dx12.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainDebugPacingPS.h`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainDebugPacingVS.h`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainDX12_DebugPacing.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainDX12_DebugPacing.h`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainDX12_Helpers.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainDX12_Helpers.h`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainDX12_UiComposition.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainDX12_UiComposition.h`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainDX12.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainDX12.h`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainUiCompositionPremulPS.h`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainUiCompositionPremulVS.h`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainUiCompositionPS.h`
- `Kits\FidelityFX\framegeneration\fsr3\dx12\FrameInterpolationSwapchainUiCompositionVS.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\ffx_frameinterpolation.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\ffx_opticalflow.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\ffx_provider_fsr3framegeneration.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\ffx_provider_fsr3framegenerationswapchain.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation_callbacks_hlsl.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation_common.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation_compute_inpainting_pyramid.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation_debug_view.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation_disocclusion_mask.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation_game_motion_vector_field.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation_inpainting.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation_optical_flow_vector_field.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation_reconstruct_dilated_velocity_and_previous_depth.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation_reconstruct_previous_depth.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation_resources.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation_setup.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\frameinterpolation\ffx_frameinterpolation.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\opticalflow\ffx_opticalflow_callbacks_hlsl.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\opticalflow\ffx_opticalflow_common.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\opticalflow\ffx_opticalflow_compute_luminance_pyramid.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\opticalflow\ffx_opticalflow_compute_optical_flow_v5.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\opticalflow\ffx_opticalflow_compute_scd_divergence.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\opticalflow\ffx_opticalflow_filter_optical_flow_v5.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\opticalflow\ffx_opticalflow_generate_scd_histogram.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\opticalflow\ffx_opticalflow_prepare_luma.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\opticalflow\ffx_opticalflow_resources.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\opticalflow\ffx_opticalflow_scale_optical_flow_advanced_v5.h`
- `Kits\FidelityFX\framegeneration\fsr3\include\gpu\spd\ffx_spd.h`
- `Kits\FidelityFX\framegeneration\fsr3\internal\ffx_frameinterpolation_private.h`
- `Kits\FidelityFX\framegeneration\fsr3\internal\ffx_frameinterpolation_shaderblobs.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\internal\ffx_frameinterpolation_shaderblobs.h`
- `Kits\FidelityFX\framegeneration\fsr3\internal\ffx_frameinterpolation.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\internal\ffx_opticalflow_private.h`
- `Kits\FidelityFX\framegeneration\fsr3\internal\ffx_opticalflow_shaderblobs.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\internal\ffx_opticalflow_shaderblobs.h`
- `Kits\FidelityFX\framegeneration\fsr3\internal\ffx_opticalflow.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\internal\ffx_provider_fsr3framegeneration.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_frameinterpolation_compute_inpainting_pyramid_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_frameinterpolation_debug_view_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_frameinterpolation_disocclusion_mask_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_frameinterpolation_game_motion_vector_field_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_frameinterpolation_inpainting_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_frameinterpolation_optical_flow_vector_field_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_frameinterpolation_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_frameinterpolation_reconstruct_and_dilate_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_frameinterpolation_reconstruct_previous_depth_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_frameinterpolation_setup_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_opticalflow_compute_luminance_pyramid_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_opticalflow_compute_optical_flow_advanced_pass_v5.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_opticalflow_compute_scd_divergence_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_opticalflow_filter_optical_flow_pass_v5.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_opticalflow_generate_scd_histogram_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_opticalflow_prepare_luma_pass.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\ffx_opticalflow_scale_optical_flow_advanced_pass_v5.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\FrameInterpolationSwapchainDebugPacing.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\internal\shaders\FrameInterpolationSwapchainUiComposition.hlsl`
- `Kits\FidelityFX\framegeneration\fsr3\vk\ffx_provider_framegenerationswapchain_vk.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainDebugPacingPS.h`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainDebugPacingVS.h`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainUiCompositionPremulPS.h`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainUiCompositionPremulVS.h`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainUiCompositionPS.h`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainUiCompositionVS.h`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainVK_DebugPacing.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainVK_DebugPacing.h`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainVK_Helpers.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainVK_Helpers.h`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainVK_UiComposition.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainVK_UiComposition.h`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainVK.cpp`
- `Kits\FidelityFX\framegeneration\fsr3\vk\FrameInterpolationSwapchainVK.h`
- `Kits\FidelityFX\framegeneration\include\dx12\ffx_api_framegeneration_dx12.h`
- `Kits\FidelityFX\framegeneration\include\dx12\ffx_api_framegeneration_dx12.hpp`
- `Kits\FidelityFX\framegeneration\include\ffx_framegeneration_api_types.h`
- `Kits\FidelityFX\framegeneration\include\ffx_framegeneration.h`
- `Kits\FidelityFX\framegeneration\include\ffx_framegeneration.hpp`
- `Kits\FidelityFX\framegeneration\include\vk\ffx_api_framegeneration_vk.h`
- `Kits\FidelityFX\framegeneration\include\vk\ffx_api_framegeneration_vk.hpp`
- `Kits\FidelityFX\framegeneration\internal\ffx_framegeneration_internal.h`
- `Kits\FidelityFX\radiancecache\include\ffx_radiancecache.h`
- `Kits\FidelityFX\radiancecache\include\ffx_radiancecache.hpp`
- `Kits\FidelityFX\readme.md`
- `Kits\FidelityFX\signedbin\amd_fidelityfx_denoiser_dx12.dll`
- `Kits\FidelityFX\signedbin\amd_fidelityfx_framegeneration_dx12.dll`
- `Kits\FidelityFX\signedbin\amd_fidelityfx_framegeneration_vk.dll`
- `Kits\FidelityFX\signedbin\amd_fidelityfx_loader_dx12.dll`
- `Kits\FidelityFX\signedbin\amd_fidelityfx_loader_dx12.lib`
- `Kits\FidelityFX\signedbin\amd_fidelityfx_loader_vk.dll`
- `Kits\FidelityFX\signedbin\amd_fidelityfx_loader_vk.lib`
- `Kits\FidelityFX\signedbin\amd_fidelityfx_radiancecache_dx12.dll`
- `Kits\FidelityFX\signedbin\amd_fidelityfx_upscaler_dx12.dll`
- `Kits\FidelityFX\signedbin\amd_fidelityfx_upscaler_vk.dll`
- `Kits\FidelityFX\upscalers\fsr3\include\ffx_fsr2.h`
- `Kits\FidelityFX\upscalers\fsr3\include\ffx_fsr3upscaler.h`
- `Kits\FidelityFX\upscalers\fsr3\include\ffx_provider_fsr2.h`
- `Kits\FidelityFX\upscalers\fsr3\include\ffx_provider_fsr3upscale.h`
- `Kits\FidelityFX\upscalers\fsr3\include\ffx_spd.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr1\ffx_fsr1.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_accumulate.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_callbacks_hlsl.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_common.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_compute_luminance_pyramid.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_depth_clip.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_lock.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_postprocess_lock_status.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_rcas.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_reconstruct_previous_depth.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_reproject.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_resources.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_sample.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_tcr_autogen.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr2\ffx_fsr2_upsample.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3\ffx_fsr3_callbacks_hlsl.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3\ffx_fsr3_common.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3\ffx_fsr3_resources.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_accumulate.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_callbacks_hlsl.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_common.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_debug_view.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_luma_instability.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_luma_pyramid.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_prepare_inputs.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_prepare_reactivity.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_rcas.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_reproject.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_resources.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_sample.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_shading_change_pyramid.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_shading_change.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\fsr3upscaler\ffx_fsr3upscaler_upsample.h`
- `Kits\FidelityFX\upscalers\fsr3\include\gpu\spd\ffx_spd.h`
- `Kits\FidelityFX\upscalers\fsr3\internal\ffx_fsr2_maximum_bias.h`
- `Kits\FidelityFX\upscalers\fsr3\internal\ffx_fsr2_private.h`
- `Kits\FidelityFX\upscalers\fsr3\internal\ffx_fsr2_shaderblobs.cpp`
- `Kits\FidelityFX\upscalers\fsr3\internal\ffx_fsr2_shaderblobs.h`
- `Kits\FidelityFX\upscalers\fsr3\internal\ffx_fsr2.cpp`
- `Kits\FidelityFX\upscalers\fsr3\internal\ffx_fsr3_private.h`
- `Kits\FidelityFX\upscalers\fsr3\internal\ffx_fsr3upscaler_private.h`
- `Kits\FidelityFX\upscalers\fsr3\internal\ffx_fsr3upscaler_shaderblobs.cpp`
- `Kits\FidelityFX\upscalers\fsr3\internal\ffx_fsr3upscaler_shaderblobs.h`
- `Kits\FidelityFX\upscalers\fsr3\internal\ffx_fsr3upscaler.cpp`
- `Kits\FidelityFX\upscalers\fsr3\internal\ffx_provider_fsr2.cpp`
- `Kits\FidelityFX\upscalers\fsr3\internal\ffx_provider_fsr3upscale.cpp`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr2_accumulate_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr2_autogen_reactive_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr2_compute_luminance_pyramid_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr2_debug_blit.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr2_depth_clip_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr2_lock_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr2_rcas_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr2_reconstruct_previous_depth_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr2_tcr_autogen_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr3upscaler_accumulate_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr3upscaler_autogen_reactive_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr3upscaler_debug_view_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr3upscaler_luma_instability_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr3upscaler_luma_pyramid_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr3upscaler_prepare_inputs_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr3upscaler_prepare_reactivity_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr3upscaler_rcas_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr3upscaler_shading_change_pass.hlsl`
- `Kits\FidelityFX\upscalers\fsr3\internal\shaders\ffx_fsr3upscaler_shading_change_pyramid_pass.hlsl`
- `Kits\FidelityFX\upscalers\include\ffx_upscale.h`
- `Kits\FidelityFX\upscalers\include\ffx_upscale.hpp`
- `Kits\OpenSource\amd\acs\amd_acs_ARM64_2022_MD.lib`
- `Kits\OpenSource\amd\acs\amd_acs_ARM64_2022_MDd.lib`
- `Kits\OpenSource\amd\acs\amd_acs_ARM64_2022_MT.lib`
- `Kits\OpenSource\amd\acs\amd_acs_ARM64_2022_MTd.lib`
- `Kits\OpenSource\amd\acs\amd_acs_ARM64.dll`
- `Kits\OpenSource\amd\acs\amd_acs_ARM64.exp`
- `Kits\OpenSource\amd\acs\amd_acs_ARM64.lib`
- `Kits\OpenSource\amd\acs\amd_acs_ARM64d.exp`
- `Kits\OpenSource\amd\acs\amd_acs_ARM64d.lib`
- `Kits\OpenSource\amd\acs\amd_acs_x64_2019_MD.lib`
- `Kits\OpenSource\amd\acs\amd_acs_x64_2019_MDd.lib`
- `Kits\OpenSource\amd\acs\amd_acs_x64_2019_MT.lib`
- `Kits\OpenSource\amd\acs\amd_acs_x64_2019_MTd.lib`
- `Kits\OpenSource\amd\acs\amd_acs_x64.dll`
- `Kits\OpenSource\amd\acs\amd_acs_x64.exp`
- `Kits\OpenSource\amd\acs\amd_acs_x64.lib`
- `Kits\OpenSource\amd\acs\amd_acs_x64d.exp`
- `Kits\OpenSource\amd\acs\amd_acs_x64d.lib`
- `Kits\OpenSource\amd\acs\amd_acs.h`
- `Kits\OpenSource\amd\acs\LICENSE.txt`
- `Kits\OpenSource\amd\acs\README.md`
- `Kits\OpenSource\amd\ags\ags_shader_intrinsics_dx12.hlsl`
- `Kits\OpenSource\amd\ags\amd_ags_x64.dll`
- `Kits\OpenSource\amd\ags\amd_ags_x64.lib`
- `Kits\OpenSource\amd\ags\amd_ags.chm`
- `Kits\OpenSource\amd\ags\amd_ags.h`
- `Kits\OpenSource\amd\ags\LICENSE.txt`
- `Kits\OpenSource\amd\ags\README.md`
- `Kits\OpenSource\amd\antilag2\ffx_antilag2_dx12.h`
- `Kits\OpenSource\amd\antilag2\LICENSE.txt`
- `Kits\OpenSource\amd\memoryallocator\D3D12MemAlloc.cpp`
- `Kits\OpenSource\amd\memoryallocator\D3D12MemAlloc.h`
- `Kits\OpenSource\amd\memoryallocator\license.txt`
- `Kits\OpenSource\amd\memoryallocator\memoryallocator.cpp`
- `Kits\OpenSource\amd\memoryallocator\memoryallocator.h`
- `Kits\OpenSource\amd\memoryallocator\vk_mem_alloc.h`
- `Kits\OpenSource\imgui\backends\imgui_impl_dx12.cpp`
- `Kits\OpenSource\imgui\backends\imgui_impl_dx12.h`
- `Kits\OpenSource\imgui\backends\imgui_impl_win32.cpp`
- `Kits\OpenSource\imgui\backends\imgui_impl_win32.h`
- `Kits\OpenSource\imgui\docs\BACKENDS.md`
- `Kits\OpenSource\imgui\docs\CHANGELOG.txt`
- `Kits\OpenSource\imgui\docs\CONTRIBUTING.md`
- `Kits\OpenSource\imgui\docs\EXAMPLES.md`
- `Kits\OpenSource\imgui\docs\FAQ.md`
- `Kits\OpenSource\imgui\docs\FONTS.md`
- `Kits\OpenSource\imgui\docs\README.md`
- `Kits\OpenSource\imgui\docs\TODO.txt`
- `Kits\OpenSource\imgui\imconfig.h`
- `Kits\OpenSource\imgui\imgui_demo.cpp`
- `Kits\OpenSource\imgui\imgui_draw.cpp`
- `Kits\OpenSource\imgui\imgui_internal.h`
- `Kits\OpenSource\imgui\imgui_tables.cpp`
- `Kits\OpenSource\imgui\imgui_widgets.cpp`
- `Kits\OpenSource\imgui\imgui.cpp`
- `Kits\OpenSource\imgui\imgui.h`
- `Kits\OpenSource\imgui\imstb_rectpack.h`
- `Kits\OpenSource\imgui\imstb_textedit.h`
- `Kits\OpenSource\imgui\imstb_truetype.h`
- `Kits\OpenSource\imgui\LICENSE.txt`
- `Kits\OpenSource\imgui\misc\cpp\imgui_stdlib.cpp`
- `Kits\OpenSource\imgui\misc\cpp\imgui_stdlib.h`
- `Kits\OpenSource\imgui\misc\cpp\README.txt`
- `Kits\OpenSource\imgui\misc\debuggers\imgui.gdb`
- `Kits\OpenSource\imgui\misc\debuggers\imgui.natstepfilter`
- `Kits\OpenSource\imgui\misc\debuggers\imgui.natvis`
- `Kits\OpenSource\imgui\misc\debuggers\README.txt`
- `Kits\OpenSource\imgui\misc\fonts\binary_to_compressed_c.cpp`
- `Kits\OpenSource\imgui\misc\fonts\Cousine-Regular.ttf`
- `Kits\OpenSource\imgui\misc\fonts\DroidSans.ttf`
- `Kits\OpenSource\imgui\misc\fonts\Karla-Regular.ttf`
- `Kits\OpenSource\imgui\misc\fonts\ProggyClean.ttf`
- `Kits\OpenSource\imgui\misc\fonts\ProggyTiny.ttf`
- `Kits\OpenSource\imgui\misc\fonts\Roboto-Medium.ttf`
- `Kits\OpenSource\imgui\misc\freetype\imgui_freetype.cpp`
- `Kits\OpenSource\imgui\misc\freetype\imgui_freetype.h`
- `Kits\OpenSource\imgui\misc\freetype\README.md`
- `Kits\OpenSource\imgui\misc\README.txt`
- `Kits\OpenSource\imgui\misc\single_file\imgui_single_file.h`
- `Kits\OpenSource\nlohmann\json_fwd.hpp`
- `Kits\OpenSource\nlohmann\json.hpp`
- `Kits\OpenSource\nlohmann\LICENSE.MIT`
- `Kits\OpenSource\nlohmann\version.txt`
- `Kits\OpenSource\stb\LICENSE.MIT`
- `Kits\OpenSource\stb\stb_image_write.h`
- `Kits\OpenSource\stb\stb_image.h`
- `Kits\OpenSource\vectormath\common.hpp`
- `Kits\OpenSource\vectormath\docs\VectorMath-Library-Overview.pdf`
- `Kits\OpenSource\vectormath\LICENSE.txt`
- `Kits\OpenSource\vectormath\README.md`
- `Kits\OpenSource\vectormath\scalar\matrix.hpp`
- `Kits\OpenSource\vectormath\scalar\quaternion.hpp`
- `Kits\OpenSource\vectormath\scalar\vector.hpp`
- `Kits\OpenSource\vectormath\scalar\vectormath.hpp`
- `Kits\OpenSource\vectormath\sse\boolinvec.hpp`
- `Kits\OpenSource\vectormath\sse\floatinvec.hpp`
- `Kits\OpenSource\vectormath\sse\internal.hpp`
- `Kits\OpenSource\vectormath\sse\matrix.hpp`
- `Kits\OpenSource\vectormath\sse\quaternion.hpp`
- `Kits\OpenSource\vectormath\sse\vecidx.hpp`
- `Kits\OpenSource\vectormath\sse\vector.hpp`
- `Kits\OpenSource\vectormath\sse\vectormath.hpp`
- `Kits\OpenSource\vectormath\vec2d.hpp`
- `Kits\OpenSource\vectormath\vectormath.hpp`
- `readme.md`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\config\denoiserconfig.json`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\debugoverlayrendermodule.cpp`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\debugoverlayrendermodule.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\denoiserrendermodule.cpp`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\denoiserrendermodule.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\denoisersettingshandler.cpp`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\denoisersettingshandler.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\dpiawarescaling.manifest`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\FidelityFX_Denoiser_Sample_2022.props`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\FidelityFX_Denoiser_Sample_2022.sln`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\FidelityFX_Denoiser_Sample_2022.vcxproj`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\FidelityFX_Denoiser_Sample_2022.vcxproj.filters`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\fsrapirendermodule.cpp`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\fsrapirendermodule.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\fsrsettingshandler.cpp`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\fsrsettingshandler.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\main.cpp`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\pathtracingrendermodule.cpp`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\pathtracingrendermodule.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\resources\cauldron.rc`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\resources\GPUOpenChip.ico`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\resources\resource.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\sample.cpp`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\sample.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\sampleautomation.cpp`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\sampleautomation.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\Basis.hlsli`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\Bindings.hlsli`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\Color.hlsli`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\DebugOverlay.cs.hlsl`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\DebugOverlayData.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\DenoisingCompose.cs.hlsl`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\DenoisingData.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\DenoisingPrepare.cs.hlsl`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\Dispatch.hlsli`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\Geometry.hlsli`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\Lighting.hlsli`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\Material.hlsli`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\Numeric.hlsli`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\PackedData.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\PathTracing.cs.hlsl`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\PathTracing.hlsli`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\PathTracingData.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\Random.hlsli`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\Sampling.hlsli`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\Shadow.hlsli`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\Transformation.hlsli`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\Types.h`
- `Samples\Denoisers\FidelityFX_Denoiser\dx12\shaders\UnitVector.hlsli`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\config.json`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\D3DBase.h`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\D3DPipeline.cpp`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\D3DPipeline.h`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\dpiawarescaling.manifest`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\DXC.cpp`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\DXC.h`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\FidelityFX_NRC_2022.props`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\FidelityFX_NRC_2022.sln`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\FidelityFX_NRC_2022.vcxproj`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\FidelityFX_NRC_2022.vcxproj.filters`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\RenderManager.cpp`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\RenderManager.h`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\resources\cauldron.rc`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\resources\GPUOpenChip.ico`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\resources\resource.h`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\ShaderGraph.cpp`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\ShaderGraph.h`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\Basis.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\bsdf\Beckmann.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\bsdf\BSDF.hlsli`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\bsdf\GGX.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\bsdf\Simple.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\Camera.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\Cel.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\color\Color.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\color\Grade.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\Constants.hlsli`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\Ctor.hlsli`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\FrameCtx.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\Half.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\Hash.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\Hatch.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\integrator\PT.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\light\Planar.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\Math.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\RadianceCache.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\random\Halton.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\random\OwenSobol.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\random\PCG.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\random\Random.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\Ray.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\texture\Simple.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\tracable\Boolean.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\tracable\Intersector.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\tracable\KIFS.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\tracable\Primitive.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\tracable\SimpleCSG.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\Transform.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\common\Triplanar.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\scene\Composite.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\scene\Display.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\scene\Meta.json`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\shaders\scene\Renderer.hlsl`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\utils\Assert.h`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\utils\Backtrace.cpp`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\utils\Backtrace.h`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\utils\FilesystemUtils.cpp`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\utils\FilesystemUtils.h`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\utils\HighResTimer.h`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\utils\Json.cpp`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\utils\Json.h`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\utils\Log.cpp`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\utils\Log.h`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\utils\StringUtils.cpp`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\utils\StringUtils.h`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\vcpkg.json`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\Win32.cpp`
- `Samples\RadianceCaches\FidelityFX_NRC\dx12\Win32.h`
- `Samples\Upscalers\FidelityFX_FSR\dx12\config\fsrapiconfig.json`
- `Samples\Upscalers\FidelityFX_FSR\dx12\dpiawarescaling.manifest`
- `Samples\Upscalers\FidelityFX_FSR\dx12\FidelityFX_FSR_2022.props`
- `Samples\Upscalers\FidelityFX_FSR\dx12\FidelityFX_FSR_2022.sln`
- `Samples\Upscalers\FidelityFX_FSR\dx12\FidelityFX_FSR_2022.vcxproj`
- `Samples\Upscalers\FidelityFX_FSR\dx12\FidelityFX_FSR_2022.vcxproj.filters`
- `Samples\Upscalers\FidelityFX_FSR\dx12\resources\cauldron.rc`
- `Samples\Upscalers\FidelityFX_FSR\dx12\resources\GPUOpenChip.ico`
- `Samples\Upscalers\FidelityFX_FSR\dx12\resources\resource.h`
- `Samples\Upscalers\FidelityFX_FSR\internal\fsrapirendermodule.cpp`
- `Samples\Upscalers\FidelityFX_FSR\internal\fsrapirendermodule.h`
- `Samples\Upscalers\FidelityFX_FSR\internal\fsrsettingshandler.cpp`
- `Samples\Upscalers\FidelityFX_FSR\internal\fsrsettingshandler.h`
- `Samples\Upscalers\FidelityFX_FSR\internal\main.cpp`
- `Samples\Upscalers\FidelityFX_FSR\internal\sample.cpp`
- `Samples\Upscalers\FidelityFX_FSR\internal\sample.h`
- `Samples\Upscalers\FidelityFX_FSR\vk\config\fsrapiconfig.json`
- `Samples\Upscalers\FidelityFX_FSR\vk\dpiawarescaling.manifest`
- `Samples\Upscalers\FidelityFX_FSR\vk\FidelityFX_FSR_2022.props`
- `Samples\Upscalers\FidelityFX_FSR\vk\FidelityFX_FSR_2022.sln`
- `Samples\Upscalers\FidelityFX_FSR\vk\FidelityFX_FSR_2022.vcxproj`
- `Samples\Upscalers\FidelityFX_FSR\vk\FidelityFX_FSR_2022.vcxproj.filters`
- `Samples\Upscalers\FidelityFX_FSR\vk\fsrapirendermodule.cpp`
- `Samples\Upscalers\FidelityFX_FSR\vk\resources\cauldron.rc`
- `Samples\Upscalers\FidelityFX_FSR\vk\resources\GPUOpenChip.ico`
- `Samples\Upscalers\FidelityFX_FSR\vk\resources\resource.h`
- `Tools\media_delivery\MediaDelivery.exe`
- `UpdateMedia.bat`

are subject to the following license:


Copyright (C)  Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### NVIDIA DLSS SDK 9981c63

- SPDX: `LicenseRef-NVIDIA-RTX-SDKs`
- Copyright: Copyright (c) NVIDIA Corporation
- Upstream: https://github.com/NVIDIA/DLSS

```text
NVIDIA RTX SDKs LICENSE

This license is a legal agreement between you and NVIDIA Corporation ("NVIDIA") and governs the use of the NVIDIA RTX software development kits, including the DLSS SDK, NGX SDK, RTXGI SDK, RTXDI SDK, RTX Video SDK, RTX Dynamic Vibrance SDK and/or NRD SDK, if and when made available to you under this license (in each case, the “SDK”).
This license can be accepted only by an adult of legal age of majority in the country in which the SDK is used. If you are under the legal age of majority, you must ask your parent or legal guardian to consent to this license. If you are entering this license on behalf of a company or other legal entity, you represent that you have legal authority and “you” will mean the entity you represent.
By using the SDK, you affirm that you have reached the legal age of majority, you accept the terms of this license, and you take legal and financial responsibility for the actions of your permitted users.

You agree to use the SDK only for purposes that are permitted by (a) this license, and (b) any applicable law, regulation or generally accepted practices or guidelines in the relevant jurisdictions.

1. LICENSE. Subject to the terms of this license and the terms in the supplement attached, NVIDIA hereby grants you a non-exclusive, non-transferable license, without the right to sublicense (except as expressly provided in this license) to:
a. Install and use the SDK,
b. Modify and create derivative works of sample source code delivered in the SDK, and
c. Distribute any software and materials within the SDK, other than developer tools provided for your internal use, as incorporated in object code format into a software application subject to the distribution requirements indicated in this license.

2. DISTRIBUTION REQUIREMENTS. These are the distribution requirements for you to exercise the grants above:
a.	An application must have material additional functionality, beyond the included portions of the SDK.
b.	The following notice shall be included in modifications and derivative works of source code distributed: “This software contains source code provided by NVIDIA Corporation.”
c.	You agree to distribute the SDK subject to the terms at least as protective as the terms of this license, including (without limitation) terms relating to the license grant, license restrictions and protection of NVIDIA’s intellectual property rights. Additionally, you agree that you will protect the privacy, security and legal rights of your application users.
d.	You agree to notify NVIDIA in writing of any known or suspected distribution or use of the SDK not in compliance with the requirements of this license, and to enforce the terms of your agreements with respect to the distributed portions of the SDK.

3. AUTHORIZED USERS. You may allow employees and contractors of your entity or of your subsidiary(ies) to access and use the SDK from your secure network to perform work on your behalf. If you are an academic institution you may allow users enrolled or employed by the academic institution to access and use the SDK from your secure network. You are responsible for the compliance with the terms of this license by your authorized users.

4. LIMITATIONS. Your license to use the SDK is restricted as follows:
a.	You may not reverse engineer, decompile or disassemble, or remove copyright or other proprietary notices from any portion of the SDK or copies of the SDK.
b.	Except as expressly provided in this license, you may not copy, sell, rent, sublicense, transfer, distribute, modify, or create derivative works of any portion of the SDK. For clarity, you may not distribute or sublicense the SDK as a stand-alone product.
c.	 Unless you have an agreement with NVIDIA for this purpose, you may not indicate that an application created with the SDK is sponsored or endorsed by NVIDIA.
d.	 You may not bypass, disable, or circumvent any technical limitation, encryption, security, digital rights management or authentication mechanism in the SDK.
e.	You may not use the SDK in any manner that would cause it to become subject to an open source software license. As examples, licenses that require as a condition of use, modification, and/or distribution that the SDK be: (i) disclosed or distributed in source code form; (ii) licensed for the purpose of making derivative works; or (iii) redistributable at no charge.
f.	 Unless you have an agreement with NVIDIA for this purpose, you may not use the SDK with any system or application where the use or failure of the system or application can reasonably be expected to threaten or result in personal injury, death, or catastrophic loss. Examples include use in avionics, navigation, military, medical, life support or other life critical applications. NVIDIA does not design, test or manufacture the SDK for these critical uses and NVIDIA shall not be liable to you or any third party, in whole or in part, for any claims or damages arising from such uses.
g.	You agree to defend, indemnify and hold harmless NVIDIA and its affiliates, and their respective employees, contractors, agents, officers and directors, from and against any and all claims, damages, obligations, losses, liabilities, costs or debt, fines, restitutions and expenses (including but not limited to attorney’s fees and costs incident to establishing the right of indemnification) arising out of or related to your use of the SDK outside of the scope of this license, or not in compliance with its terms.

5. UPDATES. NVIDIA may, at its option, make available patches, workarounds or other updates to this SDK. Unless the updates are provided with their separate governing terms, they are deemed part of the SDK licensed to you as provided in this license. Further, NVIDIA may, at its option, automatically update the SDK or other software in the system, except for those updates that you may opt-out via the SDK API. You agree that the form and content of the SDK that NVIDIA provides may change without prior notice to you. While NVIDIA generally maintains compatibility between versions, NVIDIA may in some cases make changes that introduce incompatibilities in future versions of the SDK.

6. PRE-RELEASE VERSIONS. SDK versions identified as alpha, beta, preview, early access or otherwise as pre-release may not be fully functional, may contain errors or design flaws, and may have reduced or different security, privacy, availability, and reliability standards relative to commercial versions of NVIDIA software and materials. You may use a pre-release SDK version at your own risk, understanding that these versions are not intended for use in production or business-critical systems. NVIDIA may choose not to make available a commercial version of any pre-release SDK. NVIDIA may also choose to abandon development and terminate the availability of a pre-release SDK at any time without liability.

7. THIRD-PARTY COMPONENTS. The SDK may include third-party components with separate legal notices or terms as may be described in proprietary notices accompanying the SDK. If and to the extent there is a conflict between the terms in this license and the third-party license terms, the third-party terms control only to the extent necessary to resolve the conflict.

8. OWNERSHIP.

8.1 NVIDIA reserves all rights, title and interest in and to the SDK not expressly granted to you under this license. NVIDIA and its suppliers hold all rights, title and interest in and to the SDK, including their respective intellectual property rights. The SDK is copyrighted and protected by the laws of the United States and other countries, and international treaty provisions.

8.2 Subject to the rights of NVIDIA and its suppliers in the SDK, you hold all rights, title and interest in and to your applications and your derivative works of the sample source code delivered in the SDK including their respective intellectual property rights.

9. FEEDBACK. You may, but are not obligated to, provide Feedback to NVIDIA. “Feedback” means all suggestions, fixes, modifications, feature requests or other feedback regarding the SDK. Feedback, even if designated as confidential by you, shall not create any confidentiality obligation for NVIDIA. If you provide Feedback, you hereby grant NVIDIA, its affiliates and its designees a non-exclusive, perpetual, irrevocable, sublicensable, worldwide, royalty-free, fully paid-up and transferable license, under your intellectual property rights, to publicly perform, publicly display, reproduce, use, make, have made, sell, offer for sale, distribute (through multiple tiers of distribution), import, create derivative works of and otherwise commercialize and exploit the Feedback at NVIDIA’s discretion. You will not give Feedback (i) that you have reason to believe is subject to any restriction that impairs the exercise of the grant stated in this section, such as third-party intellectual property rights or (ii) subject to license terms which seek to require any product incorporating or developed using such Feedback, or other intellectual property of NVIDIA or its affiliates, to be licensed to or otherwise shared with any third party.

10. NO WARRANTIES. THE SDK IS PROVIDED AS-IS. TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW NVIDIA AND ITS AFFILIATES EXPRESSLY DISCLAIM ALL WARRANTIES OF ANY KIND OR NATURE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, WARRANTIES OF MERCHANTABILITY, NON-INFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, USAGE OF TRADE AND COURSE OF DEALING. NVIDIA DOES NOT WARRANT THAT THE SDK WILL MEET YOUR REQUIREMENTS OR THAT THE OPERATION THEREOF WILL BE UNINTERRUPTED OR ERROR-FREE, OR THAT ALL ERRORS WILL BE CORRECTED.

11. LIMITATIONS OF LIABILITY. TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW NVIDIA AND ITS AFFILIATES SHALL NOT BE LIABLE FOR ANY (I) SPECIAL, INCIDENTAL, PUNITIVE OR CONSEQUENTIAL DAMAGES, OR FOR DAMAGES FOR  (A) ANY LOST PROFITS, PROJECT DELAYS, LOSS OF USE, LOSS OF DATA OR LOSS OF GOODWILL, OR (B) THE COSTS OF PROCURING SUBSTITUTE PRODUCTS, ARISING OUT OF OR IN CONNECTION WITH THIS LICENSE OR THE USE OR PERFORMANCE OF THE SDK, WHETHER SUCH LIABILITY ARISES FROM ANY CLAIM BASED UPON BREACH OF CONTRACT, BREACH OF WARRANTY, TORT (INCLUDING NEGLIGENCE), PRODUCT LIABILITY OR ANY OTHER CAUSE OF ACTION OR THEORY OF LIABILITY, EVEN IF NVIDIA HAS PREVIOUSLY BEEN ADVISED OF, OR COULD REASONABLY HAVE FORESEEN, THE POSSIBILITY OF SUCH DAMAGES. IN NO EVENT WILL NVIDIA’S AND ITS AFFILIATES TOTAL CUMULATIVE LIABILITY UNDER OR ARISING OUT OF THIS LICENSE EXCEED US$10.00. THE NATURE OF THE LIABILITY OR THE NUMBER OF CLAIMS OR SUITS SHALL NOT ENLARGE OR EXTEND THIS LIMIT.

12. TERMINATION. Your rights under this license will terminate automatically without notice from NVIDIA if you fail to comply with any term and condition of this license or if you commence or participate in any legal proceeding against NVIDIA with respect to the SDK. NVIDIA may terminate this license with advance written notice to you, if NVIDIA decides to no longer provide the SDK in a country or, in NVIDIA’s sole discretion, the continued use of it is no longer commercially viable. Upon any termination of this license, you agree to promptly discontinue use of the SDK and destroy all copies in your possession or control. Your prior distributions in accordance with this license are not affected by the termination of this license. All provisions of this license will survive termination, except for the license granted to you.

13. APPLICABLE LAW. This license will be governed in all respects by the laws of the United States and of the State of Delaware, without regard to the conflicts of laws principles. The United Nations Convention on Contracts for the International Sale of Goods is specifically disclaimed. You agree to all terms of this license in the English language. The state or federal courts residing in Santa Clara County, California shall have exclusive jurisdiction over any dispute or claim arising out of this license. Notwithstanding this, you agree that NVIDIA shall still be allowed to apply for injunctive remedies or urgent legal relief in any jurisdiction.

14. NO ASSIGNMENT. This license and your rights and obligations thereunder may not be assigned by you by any means or operation of law without NVIDIA’s permission. Any attempted assignment not approved by NVIDIA in writing shall be void and of no effect. NVIDIA may assign, delegate or transfer this license and its rights and obligations, and if to a non-affiliate you will be notified.

15. EXPORT. The SDK is subject to United States export laws and regulations. You agree to comply with all applicable U.S. and international export laws, including the Export Administration Regulations (EAR) administered by the U.S. Department of Commerce and economic sanctions administered by the U.S. Department of Treasury’s Office of Foreign Assets Control (OFAC). These laws include restrictions on destinations, end-users and end-use. By accepting this license, you confirm that you are not currently residing in a country or region currently embargoed by the U.S. and that you are not otherwise prohibited from receiving the SDK.

16. GOVERNMENT USE. The SDK, documentation and technology (“Protected Items”) are “Commercial products” as this term is defined at 48 C.F.R. 2.101, consisting of “commercial computer software” and “commercial computer software documentation” as such terms are used in, respectively, 48 C.F.R. 12.212 and 48 C.F.R. 227.7202 & 252.227-7014(a)(1). Before any Protected Items are supplied to the U.S. Government, you will (i) inform the U.S. Government in writing that the Protected Items are and must be treated as commercial computer software and commercial computer software documentation developed at private expense; (ii) inform the U.S. Government that the Protected Items are provided subject to the terms of the Agreement; and (iii) mark the Protected Items as commercial computer software and commercial computer software documentation developed at private expense. In no event will you permit the U.S. Government to acquire rights in Protected Items beyond those specified in 48 C.F.R. 52.227-19(b)(1)-(2) or 252.227-7013(c) except as expressly approved by NVIDIA in writing.

17. NOTICES. You agree that any notices that NVIDIA sends you electronically, such as via email, will satisfy any legal communication requirements. Please direct your legal notices or other correspondence to NVIDIA Corporation, 2788 San Tomas Expressway, Santa Clara, California 95051, United States of America, Attention: Legal Department.

18. ENTIRE AGREEMENT. This license is the final, complete and exclusive agreement between the parties relating to the subject matter of this license and supersedes all prior or contemporaneous understandings and agreements relating to this subject matter, whether oral or written. If any court of competent jurisdiction determines that any provision of this license is illegal, invalid or unenforceable, the remaining provisions will remain in full force and effect. Any amendment or waiver under this license shall be in writing and signed by representatives of both parties.

19. LICENSING. If the distribution terms in this license are not suitable for your organization, or for any questions regarding this license, please contact NVIDIA at nvidia-rtx-license-questions@nvidia.com.
(v. March 14, 2024)


NVIDIA RTX SUPPLEMENT TO SOFTWARE LICENSE AGREEMENT FOR NVIDIA SOFTWARE DEVELOPMENT KITS
The terms in this supplement govern your use of the NVIDIA RTX SDKs, including the DLSS SDK, NGX SDK, RTXGI SDK, RTXDI SDK, RTX Video SDK, RTX Dynamic Vibrance SDK and/or NRD SDK, if and when made available to you (in each case, the “SDK”) under the terms of your license agreement (“Agreement”) as modified by this supplement. Capitalized terms used but not defined below have the meaning assigned to them in the Agreement.
This supplement is an exhibit to the Agreement and is incorporated as an integral part of the Agreement. In the event of conflict between the terms in this supplement and the terms in the Agreement, the terms in this supplement govern.

1. Interoperability. Your applications that incorporate, or are based on, the SDK must be fully interoperable with compatible GPU hardware products designed by NVIDIA or its affiliates. Further, the DLSS SDK, NGX SDK and RTX Dynamic Vibrance SDK are licensed for you to develop applications only for their use in systems with NVIDIA GPUs.

2. Game License. You may, but are not obligated to, provide your game or related content (“Game Content”) to NVIDIA. If you provide Game Content, you hereby grant NVIDIA, its affiliates and its designees a non-exclusive, perpetual, irrevocable, worldwide, royalty-free, fully paid-up license, to use the Game Content to improve NVIDIA DLSS SDK and DLSS Model Training.

3. Limitations for the DLSS SDK, NGX SDK,RTX Video SDK and RTX Dynamic Vibrance SDK. Your applications that incorporate, or are based on, the DLSS SDK, NGX SDK, RTX Video SDK or RTX Dynamic Vibrance SDK may be deployed in a cloud service that runs on systems that consume NVIDIA vGPU software, and any other cloud service use of such SDKs or their functionality is outside of the scope of the Agreement. For the purpose of this section, cloud services include application service providers or service bureaus, operators of hosted/virtual system environments, or hosting, time sharing or providing any other type of service to others.

4. Notification for the DLSS SDK, NGX SDK and RTX Dynamic Vibrance SDK. You are required to notify NVIDIA prior to commercial release of an application (including a plug-in to a commercial application) that incorporates, or is based on, the DLSS SDK, NGX SDK or RTX Dynamic Vibrance SDK. Please send notifications to: https://developer.nvidia.com/sw-notification and provide the following information in the email: company name, publisher and developer name, NVIDIA SDK used, application name, platform (i.e. PC, Linux), scheduled ship date, and weblink to product/video.

5. Audio and Video Encoders and Decoders. You acknowledge and agree that it is your sole responsibility to obtain any additional third-party licenses required to make, have made, use, have used, sell, import, and offer for sale your products or services that include or incorporate any third-party software and content relating to audio and/or video encoders and decoders from, including but not limited to, Microsoft, Thomson, Fraunhofer IIS, Sisvel S.p.A., MPEG-LA, and Coding Technologies. NVIDIA does not grant to you under this Agreement any necessary patent or other rights with respect to any audio and/or video encoders and decoders.

6. SDK Terms.
6.1 Over the Air Updates. By installing or using the SDK you agree that NVIDIA can make over-the-air updates of the SDK in systems that have the SDK installed, including (without limitation) for quality, stability or performance improvements or to support new hardware.

6.2 SDK Integration. If you publicly release a DLSS integration in an end user game or application that presents material stability, performance, image quality, or other technical issues impacting the user experience, you will work to quickly address the integration issues. In the case issues are not addressed, NVIDIA reserves the right, as a last resort, to temporarily disable the DLSS integration until the issues can be fixed.

7. Marketing.
7.1 Marketing Activities. Your license to the SDK(s) under the Agreement is subject to your compliance with the following marketing terms:
(a)  Identification by You in the DLSS SDK, NGX SDK, RTX Video SDK or Dynamic Vibrance SDK.  During the term of the Agreement, NVIDIA agrees that you may identify NVIDIA on your websites, printed collateral, trade-show displays and other retail packaging materials, as the supplier of the DLSS SDK,  NGX SDK, RTX Video SDK or Dynamic Vibrance SDK for the applications that were developed with use of such SDKs, provided that all such references to NVIDIA will be subject to NVIDIA's prior review and written approval, which will not be unreasonably withheld or delayed.
(b)  NVIDIA Trademark Placement in Applications with the DLSS SDK, NGX SDK, or RTX Video SDK.  For applications that incorporate the DLSS SDK or NGX SDK or portions thereof, you must attribute the use of the applicable SDK and include the NVIDIA Marks on splash screens, in the about box of the application (if present), and in credits for game applications.
(c) NVIDIA Trademark Placement in Applications with a licensed SDK, other than the DLSS SDK, RTX Video SDK or NGX SDK. For applications that incorporates and/or makes use of a licensed SDK, other than the DLSS SDK, RTX Video SDK or NGX SDK, you must attribute the use of the applicable SDK and include the NVIDIA Marks on the credit screen for applications that have such credit screen, or where a credit screen is not present prominently in end user documentation for the application.
(d) Identification by NVIDIA in the DLSS SDK, NGX SDK, RTX Video SDK or Dynamic Vibrance SDK.  You agree that NVIDIA may identify you on NVIDIA's websites, printed collateral, trade-show displays, and other retail packaging materials as an individual or entity that produces products and services which incorporate the DLSS SDK, NGX SDK, RTX Video SDK or Dynamic Vibrance SDK as applicable. To the extent that you provide NVIDIA with input or usage requests with regard to the use of your logo or materials, NVIDIA will use commercially reasonable efforts to comply with such requests.  For the avoidance of doubt, NVIDIA’s rights pursuant to this section shall survive any expiration or termination of the Agreement with respect to existing applications which incorporate the DLSS SDK, RTX Video SDK or NGX SDK.
(e) Applications Marketing Material in the DLSS SDK, NGX SDK, RTX Video SDK or Dynamic Vibrance SDK. You may provide NVIDIA with screenshots, imagery, and video footage of applications representative of your use of the NVIDIA DLSS SDK or NGX SDKs in your application (collectively, “Assets”). You hereby grant to NVIDIA the right to create and display self-promotional demo materials using the Assets, and after release of the application to the public to distribute, sub-license, and use the Assets to promote and market the NVIDIA RTX SDKs.  To the extent you provide NVIDIA with input or usage requests with regard to the use of your logo or materials, NVIDIA will use commercially reasonable efforts to comply with such requests.  For the avoidance of doubt, NVIDIA’s rights pursuant to this section shall survive any termination of the Agreement with respect to applications which incorporate the NVIDIA RTX SDK.

7.2 Trademark Ownership and Licenses. Trademarks are owned and licenses as follows:
(a)	Ownership of Trademarks.  Each party owns the trademarks, logos, and trade names (collectively "Marks") for their respective products or services, including without limitation in applications, and the NVIDIA RTX SDKs. Each party agrees to use the Marks of the other only as permitted in this exhibit.

(b)	Trademark License to NVIDIA.  You grant to NVIDIA a non-exclusive, non-sub licensable, non-transferable (except as set forth in the assignment provision of the Agreement), worldwide license to refer to you and your applications, and to use your Marks on NVIDIA's marketing materials and on NVIDIA's website (subject to any reasonable conditions of you) solely for NVIDIA’s marketing activities set forth in this exhibit Sections 7 (d)-(e) above.  NVIDIA will follow your specifications for your Marks as to style, color, and typeface as reasonably provided to NVIDIA.

(c)	Trademark License to You.  NVIDIA grants to you a non-exclusive, non-sub licensable, non-transferable (except as set forth in the assignment provision of the Agreement), worldwide license, subject to the terms of this exhibit and the Agreement, to use NVIDIA RTX™, NVIDIA GeForce RTX™ in combination with GeForce products, and/or NVIDIA Quadro RTX™  in combination with Quadro products (collectively, the “NVIDIA Marks”) on your marketing materials and on your website (subject to any reasonable conditions of NVIDIA) solely for your marketing activities set forth in this exhibit Sections 7.1 (a)-(c) above.  For the avoidance of doubt, you will not and will not permit others to use any NVIDIA Mark for any other goods or services, or in a way that tarnishes, degrades, disparages or reflects adversely any of the NVIDIA Marks or NVIDIA’s business or reputation, or that dilutes or otherwise harms the value, reputation or distinctiveness of or NVIDIA’s goodwill in any NVIDIA Mark. In addition to the termination rights set forth in the Agreement, NVIDIA may terminate this trademark license at any time upon written notice to you. You will follow NVIDIA's use guidelines and specifications for NVIDIA's Marks as to style, color and typeface as provided in NVIDIA Marks and submit a sample of each proposed use of NVIDIA's Marks at least two (2) weeks prior to the desired implementation of such use to obtain NVIDIA's prior written approval (which approval will not be unreasonably withheld or delayed).  If NVIDIA does not respond within ten (10) business days of your submission of such sample, the sample will be deemed unapproved. All goodwill associated with use of NVIDIA Marks will inure to the sole benefit of NVIDIA. For the RTX Video SDK, contact NVIDIA at nvidia-rtx-video-sdk-license-questions@nvidia.com.

7.3 Use Guidelines. Use of the NVIDIA Marks is subject to the following guidelines:
(a)	Business Practices.  You covenant that you will: (a)  conduct business with respect to NVIDIA’s products in a manner that reflects favorably at all times on the good name, goodwill and reputation of such products; (b) avoid deceptive, misleading or unethical practices that are detrimental to NVIDIA, its customers, or end users; (c) make no false or misleading representations with regard to NVIDIA or its products; and (d) not publish or employ or cooperate in the publication or employment of any misleading or deceptive advertising or promotional materials.

(b)	No Combination Marks or Similar Marks.  You agree not to (a) combine NVIDIA Marks with any other content without NVIDIA’s prior written approval, or (b) use any other trademark, trade name, or other designation of source which creates a likelihood of confusion with NVIDIA Marks.

(v. March 14, 2024)
```

### Noto Sans Arabic/Devanagari/Thai/SC test fonts Google Fonts

- SPDX: `OFL-1.1`
- Copyright: Copyright (c) Google Inc. and Noto contributors
- Upstream: https://github.com/google/fonts

```text
Copyright 2022 The Noto Project Authors (https://github.com/notofonts/arabic)

This Font Software is licensed under the SIL Open Font License, Version 1.1.
This license is copied below, and is also available with a FAQ at:
https://scripts.sil.org/OFL


-----------------------------------------------------------
SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007
-----------------------------------------------------------

PREAMBLE
The goals of the Open Font License (OFL) are to stimulate worldwide
development of collaborative font projects, to support the font creation
efforts of academic and linguistic communities, and to provide a free and
open framework in which fonts may be shared and improved in partnership
with others.

The OFL allows the licensed fonts to be used, studied, modified and
redistributed freely as long as they are not sold by themselves. The
fonts, including any derivative works, can be bundled, embedded,
redistributed and/or sold with any software provided that any reserved
names are not used by derivative works. The fonts and derivatives,
however, cannot be released under any other type of license. The
requirement for fonts to remain under this license does not apply
to any document created using the fonts or their derivatives.

DEFINITIONS
"Font Software" refers to the set of files released by the Copyright
Holder(s) under this license and clearly marked as such. This may
include source files, build scripts and documentation.

"Reserved Font Name" refers to any names specified as such after the
copyright statement(s).

"Original Version" refers to the collection of Font Software components as
distributed by the Copyright Holder(s).

"Modified Version" refers to any derivative made by adding to, deleting,
or substituting -- in part or in whole -- any of the components of the
Original Version, by changing formats or by porting the Font Software to a
new environment.

"Author" refers to any designer, engineer, programmer, technical
writer or other person who contributed to the Font Software.

PERMISSION & CONDITIONS
Permission is hereby granted, free of charge, to any person obtaining
a copy of the Font Software, to use, study, copy, merge, embed, modify,
redistribute, and sell modified and unmodified copies of the Font
Software, subject to the following conditions:

1) Neither the Font Software nor any of its individual components,
in Original or Modified Versions, may be sold by itself.

2) Original or Modified Versions of the Font Software may be bundled,
redistributed and/or sold with any software, provided that each copy
contains the above copyright notice and this license. These can be
included either as stand-alone text files, human-readable headers or
in the appropriate machine-readable metadata fields within text or
binary files as long as those fields can be easily viewed by the user.

3) No Modified Version of the Font Software may use the Reserved Font
Name(s) unless explicit written permission is granted by the corresponding
Copyright Holder. This restriction only applies to the primary font name as
presented to the users.

4) The name(s) of the Copyright Holder(s) or the Author(s) of the Font
Software shall not be used to promote, endorse or advertise any
Modified Version, except to acknowledge the contribution(s) of the
Copyright Holder(s) and the Author(s) or with their explicit written
permission.

5) The Font Software, modified or unmodified, in part or in whole,
must be distributed entirely under this license, and must not be
distributed under any other license. The requirement for fonts to
remain under this license does not apply to any document created
using the Font Software.

TERMINATION
This license becomes null and void if any of the above conditions are
not met.

DISCLAIMER
THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL THE
COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL
DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM
OTHER DEALINGS IN THE FONT SOFTWARE.
```

## Engine license

The engine itself is distributed under the Mozilla Public License 2.0.
The complete source of every MPL-covered file is available from the project repository published with each release.
