"""已废弃：烟雾蒙太奇改由远端统一的技能蒙太奇系统管理。

历史：
    本脚本曾把 AM_ExtractionSmokeThrow_1P/3P 绑到 C++ 的
    FirstPersonSmokeMontage / SmokeThrowMontage 两个属性上。

为什么要废弃：
    合并 origin/ue5.8-20260930 后，远端引入了统一的技能表现系统
    （ABrokenEdgeCharacter::PlayAbilityMontage + UExtractionAbilities::OnRep_Action），
    属性改名为 FirstPersonSmokeMontage / SmokeMontage，并且由原版动画
    Support_C101_SmokeScreen_M_1P_Fire / _3P_Fire 驱动。继续用本脚本会
    写不存在的 SmokeThrowMontage 而直接抛异常。

现在的做法：
    绑定入口是 Scripts/Stinger/configure_ability_montages.py，它负责
    治疗 / 救援 / 烟雾三组蒙太奇的创建与绑定，幂等。

本文件保留为空壳仅作说明，避免有人照着旧命令再跑一遍。
"""
