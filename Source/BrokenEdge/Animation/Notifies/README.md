# 动画通知 C++ 示例（UE 5.5.4）

| 类型 | 示例文件 | 何时执行 | 常见用途 |
| --- | --- | --- | --- |
| `UAnimNotify` | [AnimNotify_Example.h](AnimNotify_Example.h) / [.cpp](AnimNotify_Example.cpp) | 播放经过通知点时调用 `Notify` | 脚步声、装填完成 |
| `UAnimNotifyState` | [AnimNotifyState_Example.h](AnimNotifyState_Example.h) / [.cpp](AnimNotifyState_Example.cpp) | 区间开始 `NotifyBegin`、区间内更新 `NotifyTick`、区间结束 `NotifyEnd` | 攻击检测窗口、持续特效 |

普通通知的“一次”指每次触发通知点，不是角色整个生命周期只执行一次。循环播放动画可以重复触发。状态通知的 Tick 跟随动画更新，不是独立定时器。

## 使用步骤

1. 编译 `BrokenEdgeEditor`。首次添加带 `UCLASS` 的类，建议关闭编辑器后完整编译，再打开项目。
2. 打开一段动画序列或动画蒙太奇，找到 **Notifies（通知）** 轨道。
3. 在某一时刻右键，通过 **Add Notify** 选择 `Example: Single Notify`。选中通知，在 Details 中修改 `Message`。
4. 在轨道右键，通过 **Add Notify State** 选择 `Example: Notify State`，拖动两端设置持续区间。可配置 `SocketName`（例如骨架中存在的 `hand_r`）、`DebugRadius` 和 `DebugColor`。
5. 播放动画。单次通知打印 `[Notify]`；状态通知开始打印 `[Begin]`，持续期间绘制绿色调试球，结束打印 `[End]`。球体只是演示 Tick 的可视化效果。
6. 在 Output Log 搜索 `LogAnimNotifyExample` 或 `LogAnimNotifyStateExample`。需要查看逐次 Tick 时，在控制台输入 `log LogAnimNotifyStateExample Verbose`；恢复默认使用 `log LogAnimNotifyStateExample Log`。

## 回调参数与扩展方式

- `MeshComp`：此次播放动画的骨骼网格组件。使用 `MeshComp->GetOwner()` 找到当前角色；动画编辑器预览时可能是预览 Actor，转换成具体角色类型前必须检查。
- `Animation`：触发此次通知的动画资源。
- `EventReference`：此次通知事件的上下文。示例覆盖的是 UE 5.5.4 带该参数的接口，未使用已弃用的旧重载。
- `TotalDuration`：通知区间的持续时长参数；动画播放速率等会影响现实时间中的持续长度，不应把它直接当成固定墙钟计时器。
- `FrameDeltaTime`：此次动画更新的时间间隔。

若要接入战斗逻辑，可在回调中获取角色的战斗组件，再调用该组件的方法。此示例独立于项目现有战斗逻辑，便于先理解执行顺序。

通知对象属于动画资源，同一资源用于多个角色时可能共享通知对象。不要把当前角色指针、累计时间或命中列表保存在通知对象的普通成员变量中；应由角色/组件持有，并考虑同一角色同时播放多个通知区间的情况。

真正的攻击窗口还需要在角色死亡、动作取消、组件销毁等路径进行清理；不能把所有恢复逻辑都只依赖通知结束。通知本身也不会自动复制为网络事件，涉及伤害等权威逻辑时需要由服务器侧玩法代码控制，并保证相应动画在服务器上更新。

## 对应引擎源码

本例依据本机 UE 5.5.4 的 `Engine/Source/Runtime/Engine/Classes/Animation/AnimNotifies/AnimNotify.h` 和 `AnimNotifyState.h` 编写。

`Kismet/GameplayStaticsTypes.h` 定义轨迹预测等辅助类型，不是动画通知基类所在文件。项目已有 `Core`、`CoreUObject`、`Engine` 模块依赖，无需修改 Build.cs。
