"""Archive completed checks and a previously recorded manual visual review."""
from pathlib import Path
import json,shutil
project=Path(__file__).resolve().parents[2];saved=project/'Saved/Stinger';docs=project/'Docs/Stinger'
for name in ['configure_view_fit.py','import_view_arms.py','validate_view_fit.py','validate_multiplayer_view.py',
             'promote_view_fit.py','record_view_fit_reports.py','prepare_capped_view_arms.py','audit_view_rig.py']:
    path=project/'Scripts/Stinger'/name
    compile(path.read_text(encoding='utf-8'),str(path),'exec')
files={'view_fit_motion_validation.json':'ViewFitMotionValidation.json','pie_validation.json':'ViewFitPIEValidation.json',
    'view_fit_visual_review.json':'ViewFitVisualReview.json','view_fit_configuration.json':'ViewFitConfiguration.json',
    'view_fit_montage_timings.json':'ViewFitMontageTimings.json','view_arms_topology.json':'ViewFitTopology.json',
    'view_fit_multiplayer_views.json':'ViewFitMultiplayerViews.json','view_fit_promotion.json':'ViewFitPromotion.json'}
motion=json.loads((saved/'view_fit_motion_validation.json').read_text(encoding='utf-8'))
pie=json.loads((saved/'pie_validation.json').read_text(encoding='utf-8'))
review=json.loads((saved/'view_fit_visual_review.json').read_text(encoding='utf-8'))
multi=json.loads((saved/'view_fit_multiplayer_views.json').read_text(encoding='utf-8'))
assert motion['result']==pie['result']==multi['result']=='passed'
assert review['result']=='accepted'
assert review['frame_directory']==motion['frame_directory'], 'Visual review must match the archived capture'
for source,target in files.items():shutil.copy2(saved/source,docs/target)
summary={'branch':'ue5.8-20260930','engine':'5.8.3','map':'/Game/Characters/Stinger/Maps/L_StingerTest',
    'character':'BP_StingerCharacter','parent':'BrokenEdgeCharacter','build':'succeeded',
    'motion_checks':len(motion['checks']),'captured_frames':len(motion['samples']),'capture_fps':motion['fps'],
    'pie_checks':len(pie['checks']),'multiplayer_view_checks':len(multi['checks']),
    'visual_review':review,'third_person':'original project retargeted animations, explicitly retained for this stage',
    'limitations':['AK47 fitting only','zero-scale jump clip remains isolated','all 307 mapped clips not visually reviewed','no independent-process network test']}
(docs/'ViewFitSummary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
shutil.copy2(saved/'view_fit_build.txt',docs/'ViewFitBuild.txt')
capture=docs/'ViewFitCapture';capture.mkdir(exist_ok=True)
for phase in ['idle','aim','reload','reload_toggle','sprint','swap','third_person','first_person','death']:
    matches=[(i,s) for i,s in enumerate(motion['samples']) if s['phase']==phase]
    index,_=matches[len(matches)//2]
    shutil.copy2(Path(motion['frame_directory'])/('%05d.png'%(index+1)),capture/(phase+'.png'))
for i in range(2):shutil.copy2(saved/('view_2p_'+str(i)+'.png'),capture/('multiplayer_'+str(i)+'.png'))
print('STINGER_VIEW_FIT_REPORTS_RECORDED')
