"""生成烟团团块贴图：核心实、边缘蓬松。

为什么不用解包的那张：
    T_EFX_SmokeSubUV_41_6X6_02.png 是 6x6 序列图集，alpha 均值只有 10/255
    （几乎全透明），RGB 也只是稀疏细节。把它同时当颜色和遮罩用，
    烟团里没有任何实心区域，叠加后必然很淡。

这里程序生成一张团块：中心接近满值形成不透光的核，边缘用多个偏心高斯
造成不规则蓬松轮廓。**不做全局归一化**，否则各团相互削弱又会被拉平。

产物写到 Saved/SmokeSources/T_PuffBlob.png，由 build_smoke_material.py 导入。
"""
import math
import random
from pathlib import Path

from PIL import Image, ImageFilter

PROJECT = Path(__file__).resolve().parents[2]
OUT = PROJECT / 'Saved/SmokeSources/T_PuffBlob.png'
SIZE = 512
CENTER = SIZE / 2


def add_blob(field, cx, cy, radius, weight):
    """把一张偏心高斯斑累加进累积场，超出 1 的部分稍后统一截断。"""
    inv = 1.0 / (2.0 * radius * radius)
    for y in range(SIZE):
        dy2 = (y - cy) ** 2
        row = field[y]
        for x in range(SIZE):
            d = (x - cx) ** 2 + dy2
            if d > radius * radius * 9:   # 3 sigma 之外无贡献，跳过大半像素
                continue
            row[x] += weight * math.exp(-d * inv)


def build():
    rng = random.Random(20261027)
    field = [[0.0] * SIZE for _ in range(SIZE)]
    # 核心：窄半径 + 适中权重，让只有中心一圈饱和，不是整块实心。
    add_blob(field, CENTER, CENTER, SIZE * 0.17, 0.95)
    # 二级团：撑开体积，衰减到一半左右，形成从核到边缘的过渡带。
    add_blob(field, CENTER, CENTER, SIZE * 0.30, 0.42)
    # 外围蓬松：偏心小球，制造不规则边缘而不是光滑圆。
    for i in range(11):
        angle = i / 11 * math.tau + rng.uniform(-.25, .25)
        dist = SIZE * rng.uniform(0.21, 0.29)
        add_blob(field, CENTER + math.cos(angle) * dist, CENTER + math.sin(angle) * dist,
                 SIZE * rng.uniform(0.09, 0.15), rng.uniform(.22, .40))

    img = Image.new('L', (SIZE, SIZE))
    px = img.load()
    for y in range(SIZE):
        row = field[y]
        for x in range(SIZE):
            # 不做归一化：直接截断。中心饱和成实心核，外圈按累加值自然衰减。
            px[x, y] = int(min(row[x], 1.0) * 255)
    # 只轻微模糊，保留团块的内部起伏，避免糊成一圈光晕。
    img = img.filter(ImageFilter.GaussianBlur(SIZE * 0.006))
    out = Image.merge('RGBA', (img, img, img, img))
    out.save(OUT)
    return img


def report(img):
    px = img.load()
    total = SIZE * SIZE
    opaque = sum(1 for y in range(SIZE) for x in range(SIZE) if px[x, y] > 200)
    half = sum(1 for y in range(SIZE) for x in range(SIZE) if px[x, y] > 100)
    print('PUFF built {0} center={1} opaque={2:.1%} half={3:.1%}'.format(
        OUT.name, px[SIZE // 2, SIZE // 2], opaque / total, half / total))


if __name__ == '__main__':
    report(build())
