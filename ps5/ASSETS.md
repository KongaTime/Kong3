# Assets

The assets the title ships, written from `ps5/assets.json` by
`ps5/tools/build-assets.py --notices`. Only assets with a clear licence are
shipped: in PS5_VulkanTemplate, the asset pack's other files (the `assets`
submodule) stay out, and the samples that use them get the replacements here.

| Path under `/app0/assets/` | Asset | Author | Licence | Samples |
| --- | --- | --- | --- | --- |
| `Roboto-Medium.ttf` | [Roboto Medium](https://fonts.google.com/specimen/Roboto) | Christian Robertson (Google Fonts) | Apache-2.0 | all |
| `models/ps5/lantern` | [Lantern (glTF sample model)](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Lantern) (parent nodes centre it and scale it to 1.6, 5.5 and 8 tall, one .gltf for each sample) | Microsoft (sbtron) | CC0-1.0 | rpcs3 |
| `fonts/Inter-Regular.ttf` | [Inter Regular](https://github.com/rsms/inter) (a static instance of the variable font (google/fonts at 7085eb89a950), subset to Latin, by ps5/tools/launcher-art.py) | The Inter Project Authors (Rasmus Andersson) | OFL-1.1 | rpcs3 |
| `fonts/Inter-Medium.ttf` | [Inter Medium](https://github.com/rsms/inter) (a static instance of the variable font (google/fonts at 7085eb89a950), subset to Latin, by ps5/tools/launcher-art.py) | The Inter Project Authors (Rasmus Andersson) | OFL-1.1 | rpcs3 |
| `fonts/Inter-SemiBold.ttf` | [Inter SemiBold](https://github.com/rsms/inter) (a static instance of the variable font (google/fonts at 7085eb89a950), subset to Latin, by ps5/tools/launcher-art.py) | The Inter Project Authors (Rasmus Andersson) | OFL-1.1 | rpcs3 |
| `fonts/Inter-Bold.ttf` | [Inter Bold](https://github.com/rsms/inter) (a static instance of the variable font (google/fonts at 7085eb89a950), subset to Latin, by ps5/tools/launcher-art.py) | The Inter Project Authors (Rasmus Andersson) | OFL-1.1 | rpcs3 |
| `launcher/rpcs3-logo.png` | [The launcher's logo](https://github.com/KongaTime/ps5_rpcs3title) (drawn by ps5/tools/launcher-art.py: kongatime's crowned 3 (ps5/art/icon-source.webp) as a rounded tile, and the word RPCS3 drawn with Orbitron Medium (OFL-1.1, The Orbitron Project Authors), which is not shipped) | kongatime (Kong³) | MIT | rpcs3 |
| `launcher/mark.png` | [The launcher intro's mark](https://github.com/KongaTime/ps5_rpcs3title) (drawn by ps5/tools/launcher-art.py: kongatime's crowned 3 (ps5/art/icon-source.webp) as a rounded tile, 450 pixels square) | kongatime (Kong³) | MIT | rpcs3 |
| `launcher/intro.wav` | [The launcher intro's sound](https://github.com/KongaTime/ps5_rpcs3title) (synthesised by ps5/tools/intro-sound.py from sine and saw waves and noise; nothing sampled) | kongatime (Kong³) | MIT | rpcs3 |
| `launcher/sounds` | [The menus' sounds](https://github.com/KongaTime/ps5_rpcs3title) (synthesised by ps5/tools/menu-sounds.py from sine and saw waves and noise; nothing sampled) | kongatime (Kong³) | MIT | rpcs3 |
| `launcher/trash.png` | [The launcher's Delete icon](https://github.com/KongaTime/ps5_rpcs3title) (drawn by ps5/tools/launcher-art.py) | kongatime (Kong³) | MIT | rpcs3 |
| `launcher/patch.png` | [The launcher's Patches icon](https://github.com/KongaTime/ps5_rpcs3title) (drawn by ps5/tools/launcher-art.py) | kongatime (Kong³) | MIT | rpcs3 |
