"""Inspect retained original environment parameters without changing any map."""
import argparse
from restoration_common import WORK,extract_recorded,run_converter,write_json


parser=argparse.ArgumentParser();parser.add_argument('--tag',required=True);args=parser.parse_args()
out=WORK/'EnvironmentRuns'/args.tag
root='/Game/Maps/SpaceCenter_Ma1/ClientConfig/'
packages=[root+p for p in ['SpaceCenter_TODLight','SpaceCenter_Lighting_Occlusion','Environment_Effect','SpaceCenter_GlobalReflection_HLevel_HD']]
packages+=['/Game/Maps/SpaceCenter_Ma1/LightMapBakeLevel/SpaceCenterH_03_HDData/LocalLightBakeVolume_SpaceCenterH_03_HD']
extract_recorded(packages,out)
results=run_converter('inspect',packages,out/'Inspect',partial=True)
write_json(out/'Report.json',{'packages':packages,'errors':[p['package'] for p in results if p.get('error') or p.get('failures')],
    'classes':sorted({o.get('Type') for p in results for o in p.get('objects',[])})})
