"""Read-only source icon extraction. Weapon IDs are grounded in RecFunctionTable evidence."""
import hashlib
import json
from pathlib import Path
import zipfile

root=Path(__file__).resolve().parents[2]
archive=Path('D:/司晨/源代码和素材/三角洲解包资产/网盘上传压缩包/可用资源_DeltaForce_UI_001.zip')
out=root/'Saved/Extraction/UISources';out.mkdir(parents=True,exist_ok=True)
selection={
    'AK':('ItemIconWhite_RTI/18010000006.png','source weapon ID'),
    'SCARH':('ItemIconWhite_RTI/18010000021.png','source weapon ID'),
    'Ammo':('ItemIcon/1_1/Icon_Bullets_762X39_1347.png','caliber image; project ammunition balance'),
    'AmmoAP':('ItemIcon/1_1/Icon_Bullets_762X39_1438.png','caliber image; project ammunition balance'),
    'Ammo76251':('ItemIcon/1_1/Icon_Bullets_762x51_1552.png','caliber image; project ammunition balance'),
    'Tools':('ItemIcon/2_2/Icon_props_collect_toolbox_1095.png','representative source toolbox for project Tools item'),
    'Electronics':('ItemIcon/2_1/Icon_props_collect_graphicscard_946.png','representative source electronics for project aggregate item'),
}
records=[]
with zipfile.ZipFile(archive) as z:
    for item,(suffix,status) in selection.items():
        name=next(n for n in z.namelist() if n.endswith('/UI/UIIcon/'+suffix))
        data=z.read(name);file=out/('T_Item_'+item+'.png');file.write_bytes(data)
        records.append({'item':item,'archive':archive.name,'entry':name,'sha256':hashlib.sha256(data).hexdigest(),
                        'file':file.name,'status':status})
(out/'Manifest.json').write_text(json.dumps(records,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('UI_SOURCE_ICONS',len(records))
