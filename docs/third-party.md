# Third-party material

The top-level BSD-2-Clause license covers project-authored files where indicated. It does not replace the notices and licenses in vendored material.

| Material | Pinned upstream | Location / license |
|---|---|---|
| Lilu SDK | Acidanthera Lilu 1.7.2 DEBUG SDK | `vendor/LiluSDK/`; license and individual file notices retained |
| MacKernelSDK | commit `3f750085caa17ec3a7880f11c11bf4f48cd6a164` | `vendor/MacKernelSDK/`; APSL 2.0 license and individual file notices retained |
| Linux reference | Linux v6.8 `drivers/cpufreq/intel_pstate.c` | Not bundled; optional differential test extracts reference functions only from a file supplied locally by the tester |
| Apple kernel / Boot KC | Exact target described in `kernel-profile.json` | Not bundled; local profile/native-body tests read the tester's own files and keep extracted bytes temporary |

The Linux source used for the recorded differential run had SHA256 `4398e093550a25f8c0d68c870093243816ef7d3fcc6ab0f74447648a1bf84186`.

The Lilu DEBUG 1.7.2 archive used for the SDK had SHA256 `e95df95d82e8b151047abf359ecd9c853755c59293e19dfcc3907da8fbd8cf89`.

The MacKernelSDK source archive used for the pinned commit had SHA256 `f028466e8a18cae2e95724152f915a44afc6352a8a80ae3d85567c55b6c1d60d`.
