/* 教学解释独立于提取出的源码。更新后运行 node refresh-sources.cjs。 */
(function () {
    const root = 'Source/BrokenEdge/';
    const refs = {};

    function group(file, entries) {
        for (const [key, symbol] of Object.entries(entries)) refs[key] = {file: root + file + '.cpp', symbol};
    }

    group('GameMode/LobbyGameMode', {lobby: 'ALobbyGameMode::OnStartSession'});
    group('Extraction/ExtractionGameMode', {
        init: 'AExtractionGameMode::BeginPlay',
        spawn: 'AExtractionGameMode::ChoosePlayerStart_Implementation',
        tick: 'AExtractionGameMode::Tick',
        settlePlayer: 'AExtractionGameMode::SettlePlayer',
        extract: 'AExtractionGameMode::UpdateExtraction',
        finish: 'AExtractionGameMode::FinishRound',
        sessionEnd: 'AExtractionGameMode::SessionEnded',
        defeat: 'AExtractionGameMode::DefeatCombatant',
        downed: 'AExtractionGameMode::UpdateDowned',
        logout: 'AExtractionGameMode::Logout'
    });
    group('Extraction/ExtractionPersistence', {
        beginRaid: 'UExtractionPersistence::BeginRaid',
        activate: 'UExtractionPersistence::ActivateRaid',
        settle: 'UExtractionPersistence::Settle',
        commit: 'UExtractionPersistence::Commit',
        load: 'UExtractionPersistence::Load',
        recover: 'UExtractionPersistence::Recover',
        economy: 'UExtractionPersistence::Economy',
        deposit: 'UExtractionPersistence::Deposit'
    });
    group('Extraction/ExtractionPlayerController', {
        requestOpen: 'AExtractionPlayerController::RequestOpenInventory',
        serverOpen: 'AExtractionPlayerController::ServerOpenInventory_Implementation',
        requestItem: 'AExtractionPlayerController::RequestItem',
        serverItem: 'AExtractionPlayerController::ServerItemOperation_Implementation',
        itemResult: 'AExtractionPlayerController::ClientItemResult_Implementation',
        frag: 'AExtractionPlayerController::ServerThrowGrenade_Implementation'
    });
    group('Extraction/ExtractionInteractionComponent', {
        focus: 'UExtractionInteractionComponent::UpdateFocus',
        open: 'UExtractionInteractionComponent::Open',
        reveal: 'UExtractionInteractionComponent::AdvanceSearch',
        lootView: 'UExtractionInteractionComponent::RefreshLootView',
        execute: 'UExtractionInteractionComponent::Execute',
        medical: 'UExtractionInteractionComponent::StartMedical',
        interactionTick: 'UExtractionInteractionComponent::TickComponent',
        takeAll: 'UExtractionInteractionComponent::TakeAll'
    });
    group('Extraction/ExtractionLootLayout', {
        roll: 'UExtractionLootRecipe::RollPool',
        lootSpawn: 'UExtractionLootLayout::Spawn'
    });
    group('Extraction/ExtractionItems', {
        fits: 'ExtractionItems::Fits',
        transfer: 'ExtractionItems::Transfer',
        autoPlace: 'ExtractionItems::AutoPlace',
        sort: 'ExtractionItems::Sort',
        detach: 'ExtractionItems::DetachGear'
    });
    group('Extraction/ExtractionInventoryComponent', {
        syncWeapons: 'UExtractionInventoryComponent::SyncWeapons',
        invChanged: 'UExtractionInventoryComponent::Changed',
        invRep: 'UExtractionInventoryComponent::OnRep_Contents',
        clear: 'UExtractionInventoryComponent::ClearSettled',
        reload: 'UExtractionInventoryComponent::Reload'
    });
    group('Extraction/ExtractionCombatComponent', {
        shot: 'UExtractionCombatComponent::RequestShot',
        serverShot: 'UExtractionCombatComponent::ServerRequestShotVersioned_Implementation',
        rewind: 'UExtractionCombatComponent::ServerRewindFor',
        shotResult: 'UExtractionCombatComponent::ClientShotResult_Implementation',
        fx: 'UExtractionCombatComponent::MulticastShotEffects_Implementation',
        reloadBegin: 'UExtractionCombatComponent::BeginServerReload',
        reloadFinish: 'UExtractionCombatComponent::FinishServerReload',
        swap: 'UExtractionCombatComponent::BeginServerSwap'
    });
    group('Weapon/HitScanWeapon', {fire: 'AHitScanWeapon::Fire'});
    group('Weapon/ExtractionBullet', {bulletTick: 'AExtractionBullet::Tick'});
    group('Character/BTService_UpdateGuardContext', {guardContext: 'UBTService_UpdateGuardContext::TickNode'});
    group('Character/BTTask_EngageTarget', {
        guardEngage: 'UBTTask_EngageTarget::UpdateEngagement',
        guardMove: 'UBTTask_EngageTarget::UpdateMovement'
    });
    group('Component/LagCompensationComponent', {
        pose: 'ULagCompensationComponent::PoseAt',
        trace: 'ULagCompensationComponent::TraceWorldAt'
    });
    group('Extraction/ExtractionHealth', {
        damage: 'UExtractionHealth::Bullet',
        treat: 'UExtractionHealth::Treat',
        revived: 'UExtractionHealth::Revived',
        stim: 'UExtractionHealth::ApplyStim'
    });
    group('Extraction/ExtractionAbilities', {
        abilityRequest: 'UExtractionAbilities::Request',
        abilityBegin: 'UExtractionAbilities::ServerBegin_Implementation',
        abilityTick: 'UExtractionAbilities::TickComponent',
        abilityComplete: 'UExtractionAbilities::Complete',
        abilityCancel: 'UExtractionAbilities::Cancel'
    });
    group('Extraction/ExtractionGuard', {
        guardStart: 'AExtractionGuard::StartGuardAI',
        hear: 'AExtractionGuard::Hear',
        pursuit: 'AExtractionGuard::BuildPursuitDestination',
        cover: 'AExtractionGuard::FindTacticalPoint'
    });
    group('Extraction/ExtractionExit', {
        available: 'AExtractionExit::UnavailableReason',
        consume: 'AExtractionExit::Consume'
    });
    group('Extraction/ExtractionAccountComponent', {
        requestEconomy: 'UExtractionAccountComponent::RequestEconomy',
        serverEconomy: 'UExtractionAccountComponent::ServerEconomy_Implementation',
        accountRefresh: 'UExtractionAccountComponent::Refresh'
    });
    group('Extraction/ExtractionProgression', {
        progressExecute: 'ExtractionProgression::Execute',
        progressExtract: 'ExtractionProgression::Extract'
    });
    const step = (ref, side, edge, title, detail, input, output) => ({ref, side, edge, title, detail, input, output});
    const tech = (name, why) => ({name, why});
    const chapters = [
        {
            id: 'lifecycle',
            title: '一场对局如何开始',
            short: '对局生命周期',
            subtitle: '从大厅凭据，到服务器状态机',
            category: '编排',
            icon: '01',
            summary: '加载地图不等于开始对局。先准备出战记录，再等待角色、地图和凭据全部就绪，服务器才允许战斗。',
            roles: [['GameMode', '仅在服务器编排规则和结算。'], ['GameState', '向客户端复制整局 Phase、Deadline 和错误。'], ['PlayerState', '保存单人生命状态、出战身份和背包。'], ['Persistence', '跨关卡保留档案和出战记录。']],
            topics: [
                ['先准备，再激活', '大厅 OnStartSession 在会话成功启动后调用 BeginRaid，将所有成员的携带装备保存为 Original 并从大厅携行栏移出。Raid GUID 是这一局的凭据。随后 ServerTravel 进入战斗地图；这时记录仍是“准备中”，尚未允许战斗。'],
                ['出生不是一个固定坐标', 'ChoosePlayerStart 按小队分配不重复出生组，并检查区域加载、地面、坡度和胶囊阻挡。已经分配的出生点优先复用。无法安全出生时返回空，由等待阶段重试，而不是把玩家放到世界原点。'],
                ['状态与角色分开管理', 'Tick 在 Waiting 阶段等待成员和 Pawn 就绪，再调用 ActivateRaid 并设置 Active。Deadline 使用服务器绝对时间。Alive 与 Downed 都计入未结束人数；倒地仍可救援，不等于已结算。'],
                ['结束也是一个流程', 'FinishRound 先完成必要的取消或超时结算，再发布 Ended 并冻结玩家。结束会话异步完成后，SessionEnded 才触发回大厅；失败延后重试，防止重复旅行。存储不可用时先冻结，避免继续扩大未保存的物资变化。']
            ],
            tech: [tech('UE Gameplay Framework', '用 GameMode 执行规则，用可复制的 GameState / PlayerState 暴露读模型。'), tech('Seamless Travel', '在大厅与对局之间迁移玩家；不能把正在换控制器的旧对象当作有效玩家。'), tech('GameInstanceSubsystem', '存储对象跨 World 存活，与某个可销毁 Pawn 解耦。')],
            flows: [{
                title: '大厅 → 开局 → 结束', note: '下列步骤跨越关卡加载与多个 Tick，不是一次同步调用栈。', steps: [
                    step('lobby', 'server', 'callback', '会话启动回调', '确认会话启动成功，收集成员档案并准备出战；准备失败不进入战斗地图。', '会话回调、玩家档案', '出战凭据及 ServerTravel 请求'),
                    step('beginRaid', 'storage', 'call', '原子准备出战', '在候选状态中保存所有成员原装备与来源批次，成功提交后才返回 Raid ID。', 'Profiles', '准备中的 Raid、清空的大厅携行栏'),
                    step('init', 'server', 'travel', '新地图初始化', '加载地图定义，生成战利品和撤离点，校验出生组与撤离配置。', '地图 DataAsset', '地图配置是否有效'),
                    step('spawn', 'server', 'callback', '按队伍寻找出生点', '引擎出生回调检查已加载区域、落地与碰撞条件；失败可在等待阶段重试。', 'Controller、小队、地图出生组', '合法 PlayerStart 或 nullptr'),
                    step('tick', 'server', 'tick', '等待所有条件就绪', '周期检查当前玩家与出战记录，准备成功才激活；120 秒加载超时会结束等待。', 'Phase、Pawn、Raid', 'Active 或等待 / 结束'),
                    step('activate', 'storage', 'call', '持久化激活记录', '将准备记录标为 Active 并 Commit。外层 Tick 随后设置阶段、截止时间和撤离点起始时间。', 'Raid ID', '持久化的活动记录'),
                    step('finish', 'server', 'tick', '处理结束条件', '后续 Tick 检测无人存活或时间结束，完成未结算成员的必要写入，再发布 Ended。', '结束原因', '冻结玩家、等待结束会话'),
                    step('sessionEnd', 'server', 'callback', '返回大厅', '等待异步会话结束结果。失败延迟重试，成功才 ServerTravel 回大厅。', 'EndSession 回调结果', '大厅旅行请求')
                ]
            }],
            pitfalls: ['GameMode 不复制到客户端，不能让 UI 直接依赖它。', '直接打开地图的 PIE 测试没有大厅出战凭据，不能把它当作正常结算链路。', '状态机源码存在不代表所有多人旅行与断线边界已实测。'],
            questions: [['为什么背包放在 PlayerState 而不是 Pawn？', 'Pawn 会因死亡、观战或切换被销毁。PlayerState 表达玩家身份与对局状态，背包不应随可见角色的生命周期一起丢失；持久档案则进一步放在服务器子系统。'], ['准备与激活为什么分成两次写入？', '准备阶段锁定出战资产；地图加载失败时可取消准备并返还。激活后退出按活动对局规则结算，这让异常恢复能区分“还未开局”和“已经出战”。']],
            extra: ['logout'],
            tests: ['ExtractionSpawnLifecycleTests.cpp', 'ExtractionPersistenceTests.cpp']
        },
        {
            id: 'loot',
            title: '搜索不是打开一个列表',
            short: '搜索与拾取',
            subtitle: '服务器完整库存，与玩家可见视图',
            category: '交互',
            icon: '02',
            summary: '玩家搜到什么，由自己的搜索进度决定。客户端只收到当前允许看见的物品信息，真正转移仍由服务器裁决。',
            roles: [['LootLayout / Recipe', '按配置、权重和随机种子生成物资。'], ['LootActor', '保存服务器容器及版本。'], ['InteractionComponent', '选择焦点、搜索计时与物品事务。'], ['LootView', '只给当前玩家的脱敏容器快照。']],
            topics: [
                ['先生成世界，再产生交互', 'LootLayout 根据分组、点位权重和种子选择容器；Recipe 根据品质权重抽取物品。固定种子有助于复现布局，但不等于所有随机系统都共享一个种子。钥匙房的访问条件与普通散落物拾取不同。'],
                ['本地焦点不等于权限', 'UpdateFocus 根据距离和朝向选择目标，帮助玩家看到交互提示。RequestOpenInventory 带新会话号发往服务器；Open 再检查能否行动、距离、视线和门锁。散落物可直接拾取，容器进入搜索会话。'],
                ['每个人有自己的搜索记忆', 'AdvanceSearch 按槽位和网格顺序每秒揭示一件，用容器 GUID 与物品 GUID 记住该玩家揭示过什么。物品被另一玩家先取走时取消该条搜索；容器版本变化时刷新视图。'],
                ['隐藏信息发生在服务器', 'RefreshLootView 从真实容器复制快照后，擦除未揭示条目的 GUID、定义、数量、弹匣、耐久和附件。并非只在 UI 上画一个问号。搜索揭示和拾取是两件事；TakeAll 只转移已揭示且能放下的物品，剩余物品留在原处。']
            ],
            tech: [tech('FRandomStream + DataAsset', '把物资分布与权重从交互代码中分离，支持按种子复现。'), tech('Owner-only replication', '搜索视图仅发给拥有者，减少无关信息暴露。'), tech('Session + Revision', '会话号识别打开的是哪一次容器；版本号识别基于哪一份内容。')],
            flows: [{
                title: '打开容器 → 逐件搜索',
                note: '属性复制独立于 RPC 的返回；物品尚未揭示时，服务器也不允许取走它。',
                steps: [
                    step('focus', 'client', 'tick', '找到本地交互焦点', '距离与朝向评分决定提示哪个目标，尚未修改真实库存。', '角色视点、附近物体', 'Focus / DoorFocus'),
                    step('requestOpen', 'client', 'call', '请求打开容器', '推进本地会话号，向服务器提交目标与打开意图。', '目标 Actor、bOpen', 'ServerOpenInventory 请求'),
                    step('serverOpen', 'server', 'server-rpc', '服务器收到请求', 'RPC 实现把请求交给角色交互组件处理。', 'Session、目标', 'Open 调用'),
                    step('open', 'server', 'call', '验证并开启搜索', '拒绝旧会话，检查玩家状态与可达性，取消互斥动作并设置当前容器。', '会话、距离、门锁', 'InventoryOpen、Loot'),
                    step('reveal', 'server', 'tick', '按服务器时间揭示物品', '交互组件 Tick 调用 AdvanceSearch。到达 SearchEnd 后记录物品 GUID，再选择下一件。', 'Now、Searching、SearchEnd', 'Revealed 更新'),
                    step('lootView', 'server', 'call', '构造可见快照', '拷贝容器并清除未揭示物品的敏感字段；更新 SeenRevision。', 'Contents、个人 Revealed', '脱敏 LootView，随后仅向拥有者复制')
                ]
            }],
            pitfalls: ['其他玩家搜过，不意味着自己已经揭示。', 'UI 隐藏字段不能代替服务器信息裁剪。', '批量拾取是尽量搬走能装下的已揭示物品，不承诺清空整个容器。'],
            questions: [['为什么既需要会话号又需要版本号？', '会话号防止关闭后到达的旧操作影响新交互；版本号防止拿旧库存快照覆盖当前数据。两者解决不同时间尺度上的过期请求。'], ['为什么搜索记录使用 GUID？', '数组索引会在物品移动、删除或排序后改变。稳定身份允许按实际物品记忆搜索进度，并在物品消失时正确取消。']],
            extra: ['roll', 'lootSpawn', 'takeAll', 'interactionTick'],
            tests: ['ExtractionLootLayoutTests.cpp', 'ExtractionInventoryTests.cpp']
        },
        {
            id: 'inventory',
            title: '一件物品只能属于一处',
            short: '背包与装备',
            subtitle: '从网格算法，到带版本的物品事务',
            category: '数据',
            icon: '03',
            summary: 'UI 发的是“我想移动这件物品”，不是新的库存。服务器检查身份、版本、格位和数量，再一次性提交合法结果。',
            roles: [['ItemDefinition', '定义类型、尺寸、最大堆叠和兼容规则。'], ['FExtractionItem', '实例 GUID、数量、格位、弹匣、耐久和附件。'], ['FExtractionContainer', '物品列表、容器身份与 Revision。'], ['InventoryComponent', '权威库存与场景武器 Actor 的同步入口。']],
            topics: [
                ['种类、实例与来源批次', 'Definition 表示“这是什么”，Id 表示“这是哪一个”，Origins.Lot 表示“从哪里来”。同种子弹可以合堆，但来源批次数量也要合并；拆分、装填和消耗时同样维护来源。'],
                ['格子不是 UI 坐标', 'Fits 按旋转后的尺寸检查分区边界与矩形重叠，装备槽另按类别校验。AutoPlace 先尝试合并堆叠，再遍历格位与两个朝向。胸挂与背包有各自的口袋布局，不是任意连通的大矩形。'],
                ['先校验，再修改候选数据', 'RequestItem 携带 Sequence、Session 与双方 Revision。Execute 先挡住过期请求，再由 Transfer、Configure 等处理具体操作。Transfer 使用副本验证；无法放置时不留下半次移动。背包整理 Sort 不移动装备槽。'],
                ['还要让数据与场景一致', '转移成功后 SyncWeapons 根据物品身份复用或生成武器 Actor。如果生成失败，Execute 还原背包和容器快照并删除临时掉落物。胸挂或背包整包丢弃时，内部物品一起转移；安全箱不能像普通容器一样丢弃。']
            ],
            tech: [tech('GUID + 数据驱动', '定义可复用，实例可追踪，场景 Actor 只是装备的表现。'), tech('乐观版本校验', '请求携带读到的版本；版本变化即拒绝，刷新后重新决策。'), tech('候选副本与补偿清理', '数据验证失败不提交，Actor 同步失败还原数据并清理临时对象。')],
            flows: [{
                title: '拾取一件已揭示物品',
                note: '下面展示成功主路径；版本、距离、揭示状态与格位任一失败都会提前返回原因。',
                steps: [
                    step('requestItem', 'client', 'call', '封装操作意图', '为 UI 操作补齐递增序号、当前会话和两侧容器版本。', 'Item GUID、数量、目标格位', 'FExtractionItemRequest'),
                    step('serverItem', 'server', 'server-rpc', '进入权威操作入口', '服务器调用 Execute，并将错误字符串作为客户端回执。', 'Request', '操作结果字符串'),
                    step('execute', 'server', 'call', '检查操作资格', '检查 Sequence、InventoryRevision、Session、LootRevision、距离和搜索权限。', '请求与当前状态', '合法操作或提前拒绝'),
                    step('transfer', 'server', 'call', '移动、拆分或合并', '在容器副本上执行操作，验证数量、身份和占格后提交。', '来源 / 目标容器', '更新后的容器及版本'),
                    step('syncWeapons', 'server', 'call', '同步场景装备', '需要时生成或复用主副武器 Actor；失败时外层 Execute 还原原快照。', '当前库存', '武器 Actor 与槽位匹配'),
                    step('invChanged', 'server', 'call', '通知库存变化', '刷新备弹并请求网络更新，Loot 侧同时刷新搜索视图。', '更新后的 Contents', '拥有者将收到新快照'),
                    step('itemResult', 'client', 'client-rpc', '收到操作回执', '回执用于反馈失败原因；库存本身通过属性复制更新，不能假设二者总在同一帧到达。', 'Sequence、Error', '界面操作反馈'),
                    step('invRep', 'client', 'replication', '应用背包快照', 'OnRep 在属性变化到达时同步备弹显示，不执行新的服务器转移。', 'Contents 新快照', '客户端显示与服务器数据对齐')
                ]
            }],
            pitfalls: ['Reliable RPC 保证可靠投递，不代表请求可信，也不代替版本校验。', '拒绝请求也推进序号水位，不能用同一个旧序号反复尝试。', '回执和属性复制是两条更新路径，图中展示的是职责，不是固定到达顺序。'],
            questions: [['两名玩家同时拿同一件物品会怎样？', '服务器按收到的操作顺序处理。首个成功操作改变版本；另一个基于旧版本的操作被拒绝，刷新后会发现物品已经不在容器中。'], ['为什么移动物品后还可能回滚？', '库存合法不代表对应的武器 Actor 一定能成功生成。上层必须把数据与场景表现作为一个完整操作处理，失败时恢复原快照。']],
            extra: ['fits', 'autoPlace', 'sort', 'detach'],
            lab: 'inventory',
            tests: ['ExtractionInventoryTests.cpp', 'ExtractionEquipmentTests.cpp', 'ExtractionSwapPocketTests.cpp']
        },
        {
            id: 'combat',
            title: '按下开火之后，谁说了算',
            short: '联网枪械',
            subtitle: '预测手感，权威命中，回执校正',
            category: '网络',
            icon: '04',
            summary: '拥有者先播放射击，再由服务器校验并执行真实战斗逻辑。预测负责即时反馈，权威结果负责弹药和伤害。',
            roles: [['CombatComponent', '本地请求、校验、散布与射击回执。'], ['HitScanWeapon', '兼容入口；搜打撤有效配置分支生成实体弹丸。'], ['ExtractionBullet', '推进弹道并在服务器处理命中。'], ['LagCompensationComponent', '历史姿态采样、插值及回溯查询。']],
            topics: [
                ['先预测，再确认', 'RequestShot 生成递增 ShotId，非权威拥有者先登记 PendingShots 并执行 LocalFire。服务器检查武器归属、装配版本、瞄准方向、射速、弹药与动作互斥。被拒绝的射击同样会得到回执。'],
                ['不要只按类名理解弹道', '当前配置有效的搜打撤武器经过 AHitScanWeapon::Fire 后，会生成 AExtractionBullet；弹丸推进飞行、重力及命中。服务器负责权威结果，拥有者可生成预测表现。UseImmediateServerHitScan 这个兼容开关并不等于当前所有射击都用即时射线结算。'],
                ['回溯的是目标历史姿态', '客户端提交估算的 ViewTime；ServerRewindFor 将回溯时长限制在配置上限和服务器测得延迟允许的范围内。弹丸 Tick 结合累计时间查询历史姿态，PoseAt 在历史帧之间插值。历史查询需要和场景遮挡共同判断，不能把客户端时间直接视为真值。'],
                ['回执不能抹掉后续预测', 'ClientShotResult 清除已确认 ShotId 及更早请求，再用服务器弹药减去仍未确认的本地射击恢复显示。MulticastShotEffects 只给远端非拥有者播放效果，避免拥有者重复开火。装填与切枪由服务器定时器推进，不依赖专服播放动画或收到 AnimNotify。']
            ],
            tech: [tech('Server / Client / Multicast RPC', '分别承载请求、拥有者校正与远端表现；权限检查仍在服务器执行。'), tech('共享种子 + ShotId', '让本地与服务器构造一致的枪械散布输入。'), tech('历史姿态回溯', '有界时间窗口内查询目标过去的位置，效果需要网络条件实测。'), tech('FTimerManager', '装填与切枪逻辑不依赖渲染侧动画通知。')],
            flows: [{
                title: '射击请求与弹丸命中',
                note: '服务器射击回执确认的是请求和弹药；弹丸之后才可能命中。播放器依次展开两个分支，不表示回执必须等待命中。',
                steps: [
                    step('shot', 'client', 'call', '立即预测本地开火', '登记 PendingShots，播放 LocalFire，估算 ViewTime 并发出服务器请求。', '目标点、当前武器', 'ShotId、PendingShots、ViewTime'),
                    step('serverShot', 'server', 'server-rpc', '校验并接受射击', '验证序号、武器、配置、状态、方向、射速与弹药；有效则设置回溯参数并调用 Fire。', 'ShotId、Weapon、Target、Revision、ViewTime', '接受或拒绝，权威弹药'),
                    step('fire', 'server', 'call', '生成实体弹丸', '有效搜打撤配置下处理枪口遮挡，并为每颗弹丸设置参数与回溯时长。', '武器参数、最终射击方向', 'AExtractionBullet 实例'),
                    step('shotResult', 'client', 'client-rpc', '分支 A：校正弹药', '服务器发回当前弹药。清除已确认请求，再扣除仍在途的本地预测发数。', '确认序号、服务器 Ammo', '校正后的弹药显示'),
                    step('fx', 'client', 'multicast', '分支 A：远端播放效果', '非拥有者按最终端点播放效果；拥有者和服务器避免重复表现。', '最终端点、Ammo', '其他客户端开火表现'),
                    step('bulletTick', 'server', 'tick', '分支 B：推进飞行', '后续 Tick 推进弹丸，用场景碰撞和历史目标形状判断命中。', '速度、累计时间、回溯时长', '继续飞行或命中结果'),
                    step('trace', 'server', 'call', '查询历史目标', '在回溯组件中查询指定时刻的目标碰撞形状，选择最近有效命中。', 'Time、Start、End、Radius', 'FRewindHit'),
                    step('damage', 'server', 'call', '命中后的伤害入口', '服务器命中分支把伤害、骨骼与弹药交给创伤组件，计算护甲与实际掉血。', 'Damage、Bone、Ammo', '护甲耐久与生命变化')
                ]
            }],
            pitfalls: ['本地预测不等于延迟补偿，两者分别解决反馈等待与历史命中问题。', '射击被接受不等于命中目标，实体弹丸需要飞行时间。', '不能从源码推导“支持某延迟仍无误差”等效果数据，需专项实测。'],
            questions: [['为什么不直接用服务器弹药覆盖客户端？', '回执到达时，玩家可能又预测了几发。如果直接覆盖，会把这些尚未确认的消耗加回去。需要按 ShotId 删除已确认请求，再叠加仍在途的预测。'], ['为什么服务端还要限制 ViewTime？', '时间戳来自客户端，可以过旧或伪造。服务器以最大回溯窗口及测得的延迟限制补偿量，避免任意查询很久以前的目标。']],
            extra: ['rewind', 'pose', 'reloadBegin', 'reloadFinish', 'reload', 'swap'],
            tests: ['ExtractionDriverTests.cpp', 'ExtractionScarTests.cpp']
        }
    ];
    chapters.push(
        {
            id: 'survival',
            title: '生命归零，还没有结束',
            short: '生存与救援',
            subtitle: '伤害、创伤与可恢复的倒地状态',
            category: '玩法',
            icon: '05',
            summary: '生命值、护甲耐久、倒地血量和玩家生命状态各有职责。受伤不只是减去一个数，救援也不只是播一段动画。',
            roles: [['ExtractionHealth', '护甲、流血、骨折与治疗效果。'], ['PlayerState', 'Life、DownHealth、BleedAt。'], ['ExtractionAbilities', '救援锁、目标资格、施放与冷却。'], ['GameMode', '决定倒地、最终死亡及持久化结算。']],
            topics: [
                ['先护甲，再根据实际掉血产生创伤', 'Bullet 使用命中骨骼选择头盔或护甲槽，根据弹药穿透等级与耐久吸收伤害。护甲耐久存在物品实例内。随后调用角色 ReceiveDamage，再依据实际生命损失判断流血与腿部骨折，避免把已吸收的伤害算作创伤。'],
                ['倒地是一种独立状态', 'DefeatCombatant 先取消装填、切枪、搜索和医疗。当普通存活玩家仍有合格同队救援者时，进入 Downed，设置独立倒地血量与流血截止时间；否则尝试最终死亡结算。UpdateDowned 检测超时或已无救援者，再触发最终死亡。'],
                ['救援需要持续满足条件', 'ServerBegin 检查序号、动作互斥、距离、队友身份及冷却，并占用目标的 Rescuer，防止两个人同时完成一次救援。后续 Tick 继续检查目标、移动和下落条件；松开、受伤或失去资格会 Cancel 并释放占用。'],
                ['医疗物品与技能分两条路径', '药品通过 StartMedical 开始计时，完成时再次确认 Treat 能生效后才消耗。技能则通过 Abilities 的服务器动作时钟推进。救起会恢复指定血量并清除流血，但不会自动治好腿部骨折；不要把“救起”解释成全部状态重置。']
            ],
            tech: [tech('状态分层', 'Health、DownHealth 与 EExtractionLife 分别承担不同含义。'), tech('服务器绝对时钟', '复制动作完成时间，由服务器判定是否完成，客户端计算剩余时间用于显示。'), tech('双向救援占用', '救援者与目标关联，取消或销毁时释放，防止并行施救冲突。')],
            flows: [{
                title: '请求救援 → 重新站起',
                note: '救援前提是目标已经进入 Downed；其他死亡分支可在下方关键源码中查看。',
                steps: [
                    step('abilityRequest', 'client', 'call', '发起救援意图', '玩家请求 Revive，递增序号；客户端不自行增加队友血量。', '动作 Revive、目标', 'ServerBegin 请求'),
                    step('abilityBegin', 'server', 'server-rpc', '验证并占用目标', '确认队友、距离、状态和动作互斥，设置目标的 Rescuer 与 ActionEnd。', 'Sequence、Kind、Other', '救援进行中'),
                    step('abilityTick', 'server', 'tick', '持续复核资格', '目标失效、移动、下落或不能行动时取消；达到服务器截止时间才调用 Complete。', 'Now、ActionEnd、目标状态', '继续 / Cancel / Complete'),
                    step('abilityComplete', 'server', 'call', '恢复存活状态', 'Revive 分支设置 Life=Alive，清零倒地参数，恢复指定血量并通知创伤组件。', '有效倒地目标', 'Alive、ReviveHealth'),
                    step('revived', 'server', 'call', '清除流血', '清除 Bleeding 与 NextBleed 并请求网络更新；不清除 LegFracture。', '当前创伤状态', '流血停止，骨折保持'),
                    step('abilityCancel', 'server', 'call', '释放救援关系', 'Complete 结尾统一 Cancel，释放 Rescuer，清空 Action 与 Target；状态随后复制到客户端。', '完成的动作', '空闲动作、解除占用')
                ]
            }],
            pitfalls: ['倒地仍计入未结束人数，不能提前当作最终死亡结算。', '治疗允许移动施放，救援与烟雾施放对移动的取消规则不同。', 'Begin 时校验通过不够，持续动作还需要 Tick 和完成时复核。'],
            questions: [['为什么救援需要一把逻辑锁？', '两个人可能在相近时间请求救同一目标。Rescuer 记录当前占用者，服务器拒绝第二个冲突请求，并在完成、取消和销毁时释放。'], ['为什么创伤看实际掉血而不是原始伤害？', '护甲可能吸收大部分伤害。按实际生命变化计算创伤，才不会在几乎没有生命损失时仍按全额伤害触发流血或骨折。']],
            extra: ['damage', 'defeat', 'downed', 'medical', 'treat'],
            tests: ['ExtractionAbilityTests.cpp', 'ExtractionEquipmentTests.cpp']
        },
        {
            id: 'ai',
            title: '让战场对玩家作出反应',
            short: 'AI 与技能',
            subtitle: '行为树、感知与服务器施放',
            category: '行为',
            icon: '06',
            summary: '守卫依靠感知和黑板更新行为。技能通过服务器验证与计时生效，模型和蒙太奇负责表现，两者各司其职。',
            roles: [['ExtractionGuard', 'AIController，启动行为树、配置视觉并接收声音事件。'], ['Behavior Tree / Blackboard', '选择巡逻、搜索、追击和交战行为。'], ['ExtractionAbilities', '玩家治疗与烟雾的动作协议。'], ['Dart / Grenade Actor', '飞行、碰撞以及命中或起烟后的效果。']],
            topics: [
                ['当前 AI 由行为树驱动', 'StartGuardAI 只在服务器启动，验证所控制角色、行为树与黑板资源，然后 RunBehaviorTree。拥有角色后可能通过下一 Tick 重试初始化；失去控制或结束时停止行为树。C++ 负责启动与辅助计算，树资产负责行为组织，阅读源码不能替代查看树资产连接。'],
                ['声音与视觉进入同一个决策上下文', '视觉感知使用 AIPerception 的 Sight 配置。枪声调用 HearShot 通知范围内守卫；Hear 会检查是否可运行 AI、是否已有目标，并把 LastKnownLocation、LastSeenTime 和 SearchActive 写入黑板。消音器缩小枪声搜索半径。'],
                ['追击不能偷看隐藏目标', 'BuildPursuitDestination 使用最后可见位置与速度作有限提前量预测，并受导航路径预算约束。FindTacticalPoint 采样候选地面点，通过导航、视线和遮挡检查为横移或掩体提供位置，而不是简单朝目标直线移动。'],
                ['技能生效以服务器结果为准', '治疗技能自用时 ApplyStim；对队友发射时生成飞镖，真正命中才施加效果，打空会浪费这次施放。烟雾使用有重力和弹跳的投掷 Actor，高抛低抛改变初速度。破片手雷走独立控制器请求，不能把它画成 Abilities 的同一种动作。']
            ],
            tech: [tech('Behavior Tree + Blackboard', '将行为选择与共享上下文分开，C++ 提供可检查的决策辅助。'), tech('AIPerception + Navigation', '视觉发现目标，路径与遮挡约束搜索和战术位置。'), tech('Config 规则对象', '持续时间、冷却、速度与半径集中配置。'), tech('异步资源加载', '模型提前加载，技能效果不等待首次显示时同步加载资源。')],
            flows: [
                {
                    title: 'AI：启动与枪声搜索',
                    note: '启动与枪声是不同事件；黑板更新由行为树后续读取，不是 Hear 同步调用某个移动任务。',
                    steps: [
                        step('guardStart', 'server', 'callback', '启动守卫行为树', 'OnPossess 后的初始化验证角色与树资源，再 RunBehaviorTree。', 'Pawn、BehaviorTree', '运行中的行为树及黑板'),
                        step('hear', 'server', 'event', '枪声事件写入黑板', '玩家开枪后 HearShot 通知附近守卫；Hear 更新最后已知位置并进入搜索状态。', '声音位置、导航投影', 'LastKnownLocation、SearchActive'),
                        step('guardContext', 'server', 'tick', '服务更新视觉上下文', '行为树 Service 读取当前视觉感知目标，再补充烟雾和视线过滤；统一更新黑板，丢失目标时进入有限时长搜索。', 'AIPerception、Blackboard', 'TargetActor、最后可见位置、反应时间'),
                        step('pursuit', 'server', 'branch', '失去视野分支：追击点', 'Service 在刚丢失目标时调用此辅助函数，使用最后可见数据并检查导航预算。', 'LastSeenLocation、最后可见速度', '合法追击点或失败'),
                        step('guardEngage', 'server', 'tick', '交战分支：更新交战状态', 'TickTask 调用 UpdateEngagement，检查目标、反应时间与射击节奏，通过 Combat->FireGuard 接入同一权威射击校验。', '黑板目标、反应时间、NextFireTime', '交战与射击请求'),
                        step('guardMove', 'server', 'call', '更新交战中的移动', '交战任务根据换弹、受伤及间隔选择掩体或横移，并发出导航移动请求。', '目标、当前武器与受伤状态', 'MoveToLocation 或保持位置'),
                        step('cover', 'server', 'call', '采样战术位置', 'UpdateMovement 需要新位置时调用 FindTacticalPoint，检查地面、路径和暴露情况。', 'Threat、bCover', '候选战术位置')
                    ]
                },
                {
                    title: '技能：治疗或投掷烟雾', note: 'Revive、Heal、Smoke 是当前能力动作；破片手雷有独立入口。', steps: [
                        step('abilityRequest', 'client', 'call', '提交动作', '带目标与高低抛意图发送请求，序号识别新旧消息。', 'Kind、Other、bLowThrow', 'ServerBegin'),
                        step('abilityBegin', 'server', 'server-rpc', '校验与计时', '确认动作、冷却和目标，设置 ActionEnd。', '当前状态、规则参数', '进行中的技能动作'),
                        step('abilityTick', 'server', 'tick', '等待完成时刻', '持续复核资格，受限移动或失效会取消。', '服务器时间', 'Complete 或 Cancel'),
                        step('abilityComplete', 'server', 'call', '分支应用效果', '自用治疗直接 ApplyStim；队友治疗生成飞镖；Smoke 生成投掷 Actor。随后设置对应冷却并清理动作。', 'Action、bHealDart、LowThrow', '效果 / 弹体及冷却'),
                        step('stim', 'server', 'branch', '自用分支：更新治疗时钟', '记录快速回血、持续回血和止痛的截止时间；后续 Health Tick 推进，不是一次立即回满。', '当前规则、Now', '治疗与止痛状态')
                    ]
                }
            ],
            pitfalls: ['黑板写入与行为树执行之间是事件与调度关系，不是普通函数直调。', 'AI 行为树资产的完整结构未在本页面导出，不把源码辅助函数当作整棵树。', '烟雾和治疗有表现模型，但模型加载成功不等于技能已经生效。'],
            questions: [['失去视野后为什么使用最后可见数据？', '持续查询隐藏目标的实时位置会让 AI 获得不合理信息。使用历史感知数据与有限预测，能保持搜索行为可解释，并限制追击距离。'], ['为什么技能不用动画通知作为最终结算点？', '专用服务器不依赖渲染。服务器时钟保证规则推进，蒙太奇用于客户端表现，避免动画未播放导致技能永不完成。']],
            extra: ['frag'],
            tests: ['GuardBTTaskTests.cpp', 'ExtractionAbilityTests.cpp']
        },
        {
            id: 'extraction',
            title: '走进撤离点，只是第一步',
            short: '条件撤离',
            subtitle: '条件 → 停留 → 持久化 → 离场',
            category: '结算',
            icon: '07',
            summary: '撤离不是碰到触发器就销毁角色。服务器先检查条件并计时，写入仓库成功后才占用名额、清空背包并转入观战。',
            roles: [['ExtractionExit', '区域、开放时段、电闸与名额。'], ['GameMode', '每帧检查玩家位置和撤离进度。'], ['PlayerState', 'ExtractExitId、ExtractAt、BlockedReason。'], ['Persistence', '最终资产与任务进度提交。']],
            topics: [
                ['区域与条件分别检查', 'Contains 将玩家位置转换到撤离点局部坐标判断范围。UnavailableReason 依次检查定义与开局时间、名额、开放时间、电闸及关闭时间。GameMode 再检查当前玩家是否满足丢包条件：要求没有装备背包，不是背包内没有物品。'],
                ['计时绑定具体撤离点', '条件满足后记录 ExtractExitId 与 ExtractAt=Now+HoldSeconds。离开所有撤离区域会清空进度，换点重新计时；重叠区域优先保持当前选中的点，避免偷偷借用另一处倒计时。'],
                ['最后一秒仍要等待保存', '达到截止时间先冻结玩家，然后 SettlePlayer 捕获枪械弹药并调用持久化 Settle。失败就保留角色和库存，不消耗名额、不显示成功；存储故障会让后续 GameMode Tick 冻结玩家并暂停有效战斗。'],
                ['成功、死亡与断线是不同结局', '成功撤离带出全部物资并积累本局任务；其他结局只保留安全箱与其中物品。出战未激活时的取消走返还流程。退出与断线并不等同于成功撤离；仓库满了通过 Overflow 保留放不下的物品。']
            ],
            tech: [tech('服务器空间判定', '客户端提示不决定撤离成功，权威位置与状态决定资格。'), tech('复制绝对截止时间', '客户端用同步服务器时间显示倒计时，服务器做最终判定。'), tech('持久化先于离场', '只有资产写入成功才发布 Extracted 并清理 Pawn。')],
            flows: [{
                title: '停留完成 → 安全离场',
                note: '播放器会展开持久化内部调用，再返回 UpdateExtraction 展示后续清理。',
                steps: [
                    step('extract', 'server', 'tick', '检查玩家所处区域', '活动阶段 Tick 调用 UpdateExtraction，只有存活且有效的玩家进入判定。', '玩家位置、Life、Now', '当前撤离点或清空进度'),
                    step('available', 'server', 'call', '获取不可用原因', '检查名额、时间、电闸；外层再检查该玩家的背包条件。', '撤离定义、UsedCapacity、Now', '空字符串或阻止原因'),
                    step('extract', 'server', 'tick', '服务器停留计时', '记录当前点及截止时间。离区或条件失败会清空计时；达到截止时间才冻结并提交。', 'ExtractExitId、ExtractAt', '待提交撤离'),
                    step('settlePlayer', 'server', 'call', '捕获当前装备并提交', 'CaptureWeapons 把当前弹匣写回实例，然后汇总本局任务数据交给 Settle。', '库存、Raid、Profile、进度', '持久化结果'),
                    step('settle', 'storage', 'call', '构造结算候选状态', '确认成员未结算，整理带出物、任务与来源批次，提交整份候选状态。', 'Outcome=Extracted、Inventory', '候选档案及出战记录'),
                    step('commit', 'storage', 'call', '写日志并刷盘', '验证并写入带长度与 CRC 的记录；Flush 成功后替换内存快照。', 'Candidate', '成功或 Faulted'),
                    step('consume', 'server', 'return', '成功返回后占用名额', '回到 UpdateExtraction，只有 SettlePlayer 成功才 Consume 并发布 Extracted。', '成功的结算结果', 'UsedCapacity 增加、Life=Extracted'),
                    step('clear', 'server', 'call', '清理已结算背包', 'ClearSettled 防止再次掉落，外层接着进入观战并销毁角色。', '已保存物资', '库存清空、角色离场')
                ]
            }],
            pitfalls: ['“丢包撤”要求丢弃背包装备本体，清空背包内容还不够。', '倒计时归零并不是成功回执，必须等待持久化返回。', '安全箱保留与正常死亡掉落应按来源身份共同维护，避免重复转移。'],
            questions: [['为什么先结算再销毁角色？', '如果先销毁，再发现写盘失败，玩家会失去局内角色而仓库也没有资产。先提交成功，再发布结果和清理，保留失败时的可恢复状态。'], ['撤离点人数限制什么时候消耗？', '在持久化结算成功之后。停留中或保存失败时不应占用成功撤离名额。']],
            extra: ['deposit'],
            lab: 'extraction',
            tests: ['ExtractionExitFeedbackTests.cpp', 'ExtractionExpansionTests.cpp']
        },
        {
            id: 'persistence',
            title: '把一次撤离，变成长期资产',
            short: '仓库与成长',
            subtitle: '幂等、日志与来源追踪',
            category: '持久化',
            icon: '08',
            summary: '交易、任务与撤离结果都要经过服务器持久化。设计重点是明确什么时候算成功，以及异常重启后哪些物资能返还。',
            roles: [['AccountComponent', '客户端档案快照与交易 RPC。'], ['Progression', '货币、经验、等级和任务规则。'], ['Persistence', '单写入者，验证与持久化整个候选状态。'], ['Origins / Exported', '追踪来源批次，恢复时扣除已带出部分。']],
            topics: [
                ['大厅交易同样不信任客户端', 'RequestEconomy 生成 Operation GUID 并携带 Profile.Revision。ServerEconomy 重新检查档案绑定与锁定状态，准备或出战期间不能交易。Economy 根据操作 ID 和请求签名识别重试，再验证版本并执行规则。'],
                ['交易与任务在同一候选状态处理', '购买检查等级、余额和空间，出售按物品及耐久等规则报价；任务有前置条件、进度与领奖状态。文件提交要求本人接任务后成功带出的合格来源批次，不是背包里有同名 Document 就能交。成功撤离才把局内进度写入长期成长。'],
                ['成功的边界是日志刷盘', 'Commit 先验证候选状态，序列化 JSON，再写入包含 Magic、版本、长度与 CRC 的记录并 Flush。只有成功后才替换内存 State。写入失败将存储标记 Faulted，暂停后续操作。CRC 用于完整性检测，不是加密或防篡改认证。'],
                ['崩溃恢复不能把原物资全返还', 'Load 校验记录序号和完整性，允许处理未写完的尾记录，但完整损坏记录会报错。Recover 处理未关闭出战：原始物资减去已被成员带出的 Exported 来源批次，只返还剩余部分，必要时更换拆分物品的实例 GUID。已结算成员不重复结算。']
            ],
            tech: [tech('幂等操作', '同一操作重复到达不重复发奖或入库；相同 ID 不同参数应拒绝。'), tech('追加日志 + CRC + Flush', '清晰定义提交边界，检测不完整或损坏记录。'), tech('来源批次数量守恒', '物品本体、弹匣和附件都参与来源计算，支持跨玩家转移后的恢复。')],
            flows: [
                {
                    title: '商店购买 → 档案刷新', note: '客户端提交意图，价格与资格由服务器重新计算。', steps: [
                        step('requestEconomy', 'client', 'call', '生成交易请求', '携带操作 GUID 与档案版本，发给服务器。', '商品、数量、Action', 'Operation、Revision'),
                        step('serverEconomy', 'server', 'server-rpc', '检查大厅权限', '刷新档案绑定和锁定状态，仅未准备的有效大厅玩家可操作。', '请求、Bound、Locked', 'Economy 调用或错误'),
                        step('economy', 'storage', 'call', '识别重试与版本冲突', '同 ID 同签名可识别为重试；合法新请求在候选状态上执行。', 'Profile、Operation、Signature', '交易候选与回执'),
                        step('progressExecute', 'server', 'call', '执行经济规则', '计算价格、等级门槛、空间或任务资格，修改候选数据。', 'Progression、Stash、Request', '成功的候选或整体拒绝'),
                        step('commit', 'storage', 'call', '持久化交易与回执', '候选状态和幂等回执一起落盘，成功才更新权威内存。', 'Candidate', '持久化结果'),
                        step('accountRefresh', 'server', 'return', '刷新拥有者快照', '返回 AccountComponent 后刷新 Profile 与 Message，随后通过属性复制更新本人界面。', '服务器档案', '新的本人档案视图')
                    ]
                },
                {
                    title: '结算幂等与重启恢复',
                    note: '恢复是启动时的另一条路径，不是每次正常结算都会执行 Recover。',
                    steps: [
                        step('settle', 'storage', 'call', '结算一次出战成员', '成员已有 Outcome 就返回成功；尚未结算时把物资、进度与 Exported 一起提交。', 'Raid、Profile、Outcome', '一次有效结算'),
                        step('commit', 'storage', 'call', '持久化边界', '验证、写入、刷盘、发布内存状态。若成功后回执丢失，重试不应再次入库。', '候选状态', '已提交状态'),
                        step('load', 'storage', 'event', '独立分支：服务器重启', '重启加载日志，验证完整记录，重建最后完整状态并进行恢复。', '日志文件', '最后完整快照'),
                        step('recover', 'storage', 'call', '恢复未关闭对局', '只处理尚未结算成员，按 Original 减 Exported 返还剩余来源，关闭记录并提交。', '未关闭 Raids、来源批次', 'Recovered / Aborted 结果')
                    ]
                }
            ],
            pitfalls: ['幂等结算不等于所有请求都可用新的操作 ID 安全重发。', '这是一套本地服务器日志存储，不应描述为分布式数据库事务。', '工程中存在故障注入与恢复测试；本页面没有重新运行这些 UE 测试。'],
            questions: [['保存成功但客户端没收到结果怎么办？', '重试应由服务器识别已处理操作或已结算成员，返回既有结果，而不是再次增加资产。提交状态与操作回执应处于同一个持久化边界。'], ['来源批次解决了什么普通 GUID 解决不了的问题？', 'GUID 只标识当前实例。堆叠、拆分、弹匣装填和玩家间转移后，原物资可能分散到多个实例。批次数量能追踪已带出部分，防止恢复时重复返还。']],
            extra: ['progressExtract'],
            lab: 'persistence',
            tests: ['ExtractionPersistenceTests.cpp', 'ExtractionProgressionTests.cpp']
        }
    );
    window.TUTORIAL = {refs, chapters};
})();
