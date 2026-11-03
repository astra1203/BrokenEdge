"""Create /Game/UI/Fonts/F_DeltaForce from the official UI fonts.

Regular = FZLTHJW (方正兰亭黑, official body text), Bold = FZLTZHJW_ZH (方正兰亭中黑).
Source: 原始资源ZIP/10_界面字体_001.zip, DeltaForce/Content/UI/Fonts/*.ufont (plain TTF).
ExtractionUIStyle::Font / ApplyFont load this asset at runtime.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/import_official_fonts.py" -NullRHI -unattended -nosound
"""
import zipfile
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir())
SOURCES = [Path(r'D:\司晨\源代码和素材\三角洲解包资产\原始资源ZIP\10_界面字体_001.zip'),
           Path(r'\\Desktop-m9pa1sf\三角洲解包资产\原始资源ZIP\10_界面字体_001.zip')]
FACES = [('Regular', 'DeltaForce/Content/UI/Fonts/FZLTHJW.ufont'),
         ('Bold', 'DeltaForce/Content/UI/Fonts/FZLTZHJW_ZH.ufont')]
OUT = ROOT / 'Saved/Extraction/Fonts'

archive = zipfile.ZipFile(next(p for p in SOURCES if p.exists()))
OUT.mkdir(parents=True, exist_ok=True)
files = []
for typeface, member in FACES:
    target = OUT / (Path(member).stem + '.ttf')
    target.write_bytes(archive.read(member))
    files.append(str(target))
font = unreal.StingerAssetLibrary.create_font_from_files(files, [t for t, _ in FACES], '/Game/UI/Fonts', 'F_DeltaForce')
assert font, 'font creation failed'
print('OFFICIAL_FONT_CREATED', font.get_path_name(), files)
