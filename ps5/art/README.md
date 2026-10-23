# Title art

The home screen's art for Kong³, made for this title by kongatime and shipped
with the title. The background was redone without the pattern of the
PlayStation's button symbols (Sony's marks) the first one had:

| File | Becomes |
| --- | --- |
| `icon-source.webp` (1254x1254) | `ps5/sce_sys/icon0.png`, 512x512; and, as a rounded tile, the launcher's mark (`ps5/assets/launcher/rpcs3-logo.png`, `mark.png`) |
| `background-source.webp` (1672x941) | `ps5/sce_sys/pic0.dds` and `pic1.dds`, 3840x2160 BC7 |

`ps5/tools/title-art.py` writes the home screen's art (Pillow and etcpak), and
`ps5/tools/launcher-art.py --logo-only` the launcher's mark; run them after
changing a source and commit what they write.
