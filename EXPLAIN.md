# X3F RAW 图像处理技术说明

## 概述

本文档详细说明 x3f_extract 工具中两个主要输出命令的色彩转换原理、色彩空间、白点、矩阵运算以及相关的 EXIF/DNG 标签。

---

## 1. DNG 输出：`-dng -linear-srgb`

### 1.1 命令功能

将 X3F RAW 文件转换为 DNG (Digital Negative) 格式，输出**线性 sRGB 色彩空间**的数据，已应用白平衡增益。

### 1.2 转换流程

```
X3F RAW (相机原生色彩空间)
    ↓ [应用白平衡增益]
RAW with WB
    ↓ [归一化到 [0, 1]]
Normalized RAW [0, 1]
    ↓ [色彩矩阵变换: raw_to_xyz]
XYZ (D65 白点)
    ↓ [色彩矩阵变换: xyz_to_srgb]
线性 sRGB (D65 白点)
    ↓ [缩放到 16-bit]
sRGB 16-bit [0, 65535]
    ↓ [写入 DNG]
DNG 文件 (TIFF 格式)
```

### 1.3 色彩空间

#### 输入色彩空间
- **名称**: X3F RAW（Foveon X3 传感器原生色彩空间）
- **白点**: 无（原始传感器数据）
- **像素类型**: `uint16_t`
- **像素范围**: 依赖黑电平和白电平（通常 0-4095 或 0-16383）

#### 输出色彩空间
- **名称**: 线性 sRGB
- **白点**: D65 (6504K)
- **伽马**: 1.0（线性，未应用伽马校正）
- **像素类型**: `uint16_t`
- **像素范围**: [0, 65535]

### 1.4 色彩矩阵

#### 矩阵 1: `raw_to_xyz` (RAW → XYZ D65)
**来源**: 从 X3F 文件的 CAMF 段读取，包含白平衡增益

**实现位置**: `x3f_meta.c:x3f_get_raw_to_xyz()`

**矩阵形式**:
```
| R_xyz |   | m00  m01  m02 |   | R_raw * wb_R |
| G_xyz | = | m10  m11  m12 | × | G_raw * wb_G |
| B_xyz |   | m20  m21  m22 |   | B_raw * wb_B |
```

其中 `wb_R`, `wb_G`, `wb_B` 是白平衡增益值。

**典型值示例** (Sigma dp2 Quattro, Auto 白平衡):
```
raw_to_xyz = [
   1.6744,  -0.6578,  -0.0088,
  -0.3011,   1.1098,   0.1916,
  -0.1490,   0.1652,   0.7642
]
```

#### 矩阵 2: `xyz_to_srgb` (XYZ D65 → sRGB)
**来源**: 标准 sRGB 色彩空间定义

**实现位置**: `x3f_matrix.c:x3f_XYZ_to_sRGB()`

**矩阵值**:
```
xyz_to_srgb = [
   3.2406, -1.5372, -0.4986,
  -0.9689,  1.8758,  0.0415,
   0.0557, -0.2040,  1.0570
]
```

#### 组合矩阵: `raw_to_srgb`
**计算**: `raw_to_srgb = xyz_to_srgb × raw_to_xyz`

**实现位置**: `x3f_output_dng.c:x3f_dump_raw_data_as_dng()`

**代码片段**:
```c
// 获取 raw_to_xyz (包含白平衡)
x3f_get_raw_to_xyz(x3f, wb, raw_to_xyz);

// 获取 xyz_to_srgb
x3f_XYZ_to_sRGB(xyz_to_srgb);

// 组合矩阵: raw -> sRGB
x3f_3x3_3x3_mul(xyz_to_srgb, raw_to_xyz, raw_to_srgb);

// 对每个像素应用矩阵
for (row = 0; row < height; row++) {
  for (col = 0; col < width; col++) {
    // 归一化到 [0, 1]
    input[0] = (src[0] - black[0]) / (white[0] - black[0]);
    input[1] = (src[1] - black[1]) / (white[1] - black[1]);
    input[2] = (src[2] - black[2]) / (white[2] - black[2]);

    // 应用色彩矩阵
    x3f_3x3_3x1_mul(raw_to_srgb, input, output);

    // 转换为 16-bit
    dst[0] = clamp(output[0] * 65535.0);
    dst[1] = clamp(output[1] * 65535.0);
    dst[2] = clamp(output[2] * 65535.0);
  }
}
```

### 1.5 DNG 标签 (TIFF/EXIF Tags)

#### PhotometricInterpretation (Tag 262)
- **TIFF 原始用途**: 描述像素数据的色彩解释方式（灰度、RGB、CMYK、YCbCr 等）
- **本项目使用值**: `2` (RGB)
- **实际用途**: 告诉 DNG 阅读器图像数据为 RGB 三通道格式
- **标准定义**: TIFF 6.0 基础标签

#### SamplesPerPixel (Tag 277)
- **TIFF 原始用途**: 指定每个像素包含的样本（通道）数量
- **本项目使用值**: `3`
- **实际用途**: 声明图像包含 R、G、B 三个颜色通道
- **标准定义**: TIFF 6.0 基础标签

#### BitsPerSample (Tag 258)
- **TIFF 原始用途**: 指定每个样本（通道）的位深度
- **本项目使用值**: `[16, 16, 16]`
- **实际用途**: 声明 R、G、B 三个通道均为 16 位精度（0-65535）
- **标准定义**: TIFF 6.0 基础标签

#### Compression (Tag 259)
- **TIFF 原始用途**: 指定图像数据的压缩算法
- **本项目使用值**: `1` (无压缩) 或 `8` (ZIP/Deflate 压缩)
- **实际用途**:
  - `1`: 原始未压缩数据，文件大但读写快
  - `8`: ZIP 压缩，文件小但需要解压缩（使用 `-compress` 参数时）
- **标准定义**: TIFF 6.0 基础标签

#### DNGVersion (Tag 50706)
- **DNG 原始用途**: 声明 DNG 格式规范的版本号
- **本项目使用值**: `[1, 4, 0, 0]`
- **实际用途**: 告诉阅读器本文件遵循 DNG 1.4.0.0 规范
- **标准定义**: DNG Specification 1.0+
- **格式**: 4 字节数组 [major, minor, tertiary, quaternary]

#### UniqueCameraModel (Tag 50708)
- **DNG 原始用途**: 相机型号的唯一标识符，用于区分不同相机的色彩特性
- **本项目使用值**: 从 X3F 文件 PROP 段读取（如 "SIGMA dp2 Quattro"）
- **实际用途**: 帮助 RAW 处理软件识别相机并加载对应的色彩配置文件
- **标准定义**: DNG Specification 1.0+

#### ColorMatrix1 (Tag 50721)
- **DNG 原始用途**: 定义从相机原生色彩空间到 XYZ(D50) 的 3x3 色彩转换矩阵
- **本项目使用值**: `srgb_to_xyz_d50` 矩阵（sRGB → XYZ D50）
- **实际用途**:
  - **标准用途**: 将相机 RAW 数据转换到标准 XYZ 色彩空间
  - **本项目特殊性**: 由于数据已预处理为线性 sRGB，此矩阵作为**参考矩阵**，描述当前数据的色彩空间特性（sRGB）而非相机原生色彩空间
- **标准定义**: DNG Specification 1.0+
- **矩阵维度**: 9 个浮点数（按行优先顺序）
- **实现**:
```c
// sRGB -> XYZ (D65)
x3f_sRGB_to_XYZ(srgb_to_xyz_d65);

// Bradford 色适应: D65 -> D50
x3f_Bradford_D65_to_D50(d65_to_d50);

// 组合: sRGB -> XYZ(D50)
x3f_3x3_3x3_mul(d65_to_d50, srgb_to_xyz_d65, srgb_to_xyz_d50);

TIFFSetField(f_out, TIFFTAG_COLORMATRIX1, 9, srgb_to_xyz_d50);
```

**矩阵值**:
```
srgb_to_xyz_d50 = [
   0.4361,  0.3851,  0.1431,
   0.2225,  0.7169,  0.0606,
   0.0139,  0.0971,  0.7142
]
```

#### CameraCalibration1 (Tag 50723)
- **DNG 原始用途**: 相机传感器校准矩阵，用于补偿传感器的色彩响应非线性、通道串扰等因素
- **本项目使用值**: 单位对角矩阵 `[1, 0, 0, 0, 1, 0, 0, 0, 1]`
- **实际用途**:
  - **标准用途**: 在应用 ColorMatrix 前对 RAW 数据进行校准调整
  - **本项目特殊性**: 数据已完整预处理，无需额外校准，使用单位矩阵表示"无校准"
- **标准定义**: DNG Specification 1.0+
- **矩阵维度**: 9 个浮点数（通常为对角矩阵）

#### AsShotNeutral (Tag 50728)
- **DNG 原始用途**: 描述拍摄时选择的白平衡，用三个数值表示中性灰在相机色彩空间中的坐标
- **本项目使用值**: `[1.0, 1.0, 1.0]`
- **实际用途**:
  - **标准用途**: 指导 RAW 处理软件应用白平衡（通常需要求倒数作为增益）
  - **本项目特殊性**: 白平衡已在转换时应用，设为 `[1, 1, 1]` 表示"无需额外白平衡调整"
- **标准定义**: DNG Specification 1.0+
- **格式**: 3 个浮点数 [R, G, B]，表示中性色的相对值

#### CalibrationIlluminant1 (Tag 50778)
- **DNG 原始用途**: 指定 ColorMatrix1 和 CameraCalibration1 对应的标准光源类型
- **本项目使用值**: `21` (D65)
- **实际用途**: 声明矩阵是针对 D65 标准光源（6504K 日光）定义的
- **标准定义**: DNG Specification 1.0+
- **可选值**:
  - `1`: 日光 (Daylight)
  - `17`: 标准光源 A (2856K, 白炽灯)
  - `18`: 标准光源 B (4874K)
  - `19`: 标准光源 C (6774K)
  - `20`: D50 (5003K)
  - `21`: D55 (5503K)
  - `22`: D65 (6504K)
  - `23`: D75 (7504K)

#### ImageDescription (Tag 270)
- **TIFF 原始用途**: 存储图像的文本描述信息，供人类阅读
- **本项目使用值**: `"Preprocessed linear sRGB with white balance applied. Camera Calibration matrix is for reference only."`
- **实际用途**:
  - 告知用户数据已经过预处理
  - 说明 ColorMatrix 和 CameraCalibration 仅作参考
  - 避免软件误以为是标准 RAW 而重新应用色彩转换
- **标准定义**: TIFF 6.0 基础标签
- **字符编码**: ASCII 字符串

#### ActiveArea (Tag 50829)
- **DNG 原始用途**: 定义传感器上真正包含有效图像数据的矩形区域（排除黑边、光学黑等）
- **本项目使用值**: `[top, left, bottom, right]`（从 X3F 文件读取的裁剪区域）
- **实际用途**:
  - 告诉 RAW 处理软件哪些像素是有效的图像数据
  - 排除传感器边缘的无效区域
- **标准定义**: DNG Specification 1.0+
- **格式**: 4 个整数 [top, left, bottom, right]，单位为像素
- **坐标系**: 左上角为原点 (0, 0)

#### BlackLevel (Tag 50714)
- **DNG 原始用途**: 指定相机传感器的黑电平值（零曝光时的输出值）
- **本项目使用值**: `[0, 0, 0]`
- **实际用途**:
  - **标准用途**: RAW 处理软件减去黑电平以获得真实信号
  - **本项目特殊性**: 数据已在转换时减去黑电平，设为 0 表示"已处理"
- **标准定义**: DNG Specification 1.0+
- **格式**: 每个通道一个值，可以是标量或数组
- **典型值**: 传感器相关，通常为几百到几千（对于 12-14 bit RAW）

#### WhiteLevel (Tag 50717)
- **DNG 原始用途**: 指定相机传感器的白电平值（饱和值）
- **本项目使用值**: `[65535, 65535, 65535]`
- **实际用途**:
  - **标准用途**: 指示像素值的最大有效范围
  - **本项目特殊性**: 数据已归一化并缩放到 16-bit 全范围
- **标准定义**: DNG Specification 1.0+
- **格式**: 每个通道一个整数值
- **典型值**: 依赖 ADC 位深度（12-bit: 4095, 14-bit: 16383, 16-bit: 65535）

### 1.6 参考矩阵的软件处理行为

由于本项目输出的 DNG 文件已预处理为**线性 sRGB**，而非标准的相机 RAW 数据，ColorMatrix 等标签的含义与标准 DNG 不同。这会影响不同软件的处理方式。

#### 1.6.1 标准 DNG 的处理流程

在标准 DNG 文件中，RAW 处理软件的典型流程：

```
相机 RAW 数据
    ↓ [减去 BlackLevel]
Normalized RAW
    ↓ [应用 CameraCalibration1]
Calibrated RAW
    ↓ [应用白平衡 (基于 AsShotNeutral)]
White-balanced RAW
    ↓ [应用 ColorMatrix1]
XYZ (D50)
    ↓ [转换到工作色彩空间]
工作色彩空间 (sRGB/Adobe RGB/ProPhoto RGB)
    ↓ [用户调整：曝光、对比度、色彩等]
最终图像
```

#### 1.6.2 本项目 DNG 的实际数据流

```
线性 sRGB 数据 (已完成所有转换)
    ↓ [软件可能尝试应用矩阵，但...]
取决于软件实现
```

#### 1.6.3 主流软件的处理行为

##### Adobe Lightroom / Camera Raw

**行为**:
- **读取 ImageDescription**: 可能识别出这是预处理数据
- **检查 AsShotNeutral = [1,1,1]**: 识别白平衡已应用
- **ColorMatrix 处理**:
  - 如果识别到 PhotometricInterpretation = RGB，可能直接显示
  - 如果尝试应用 ColorMatrix，由于是 sRGB → XYZ 而非 RAW → XYZ，结果仍然是 XYZ，然后转回 sRGB，理论上应该接近原图
- **Camera Profile**: 显示 "Linear sRGB" 或 "Embedded"

**推荐用法**:
- 应该能正常显示图像
- 色彩调整工具（白平衡、色调曲线）仍然可用
- 避免使用相机配置文件切换（因为数据已不是 RAW）

##### RawTherapee

**行为**:
- **读取 DNG 标签**: 尝试标准 DNG 处理流程
- **ColorMatrix 应用**:
  - 由于 ColorMatrix1 是 sRGB → XYZ(D50)
  - 数据本身已经是 sRGB
  - 应用后得到 XYZ(D50)，然后软件转换回工作色彩空间
  - **可能结果**: 轻微色彩偏差（由于 D65 → D50 的色适应）
- **白平衡调整**: 由于 AsShotNeutral = [1,1,1]，可能显示"As Shot"已为中性

**推荐用法**:
- 检查颜色配置文件设置
- 使用 "Neutral" 或 "Linear" 处理配置
- 手动调整白平衡和色彩可能需要重新校准

##### darktable

**行为**:
- **Input Color Profile**: 尝试使用 ColorMatrix1
- **白平衡模块**:
  - 读取 AsShotNeutral = [1,1,1]
  - 可能显示白平衡已设置为"相机预设"
- **色彩管理**:
  - 由于数据是线性 sRGB，如果软件正确识别 PhotometricInterpretation
  - 应该可以正确显示

**推荐用法**:
- 在输入颜色配置文件模块选择 "linear sRGB"
- 禁用或跳过白平衡模块（已应用）
- 直接使用输出颜色配置和色调映射

##### Python rawpy / LibRaw

**行为**:
- **LibRaw 处理**:
  - 读取 DNG 元数据
  - 默认会尝试应用 dcraw 算法
  - **关键**: 如果使用 `rawpy.imread(dng_file)` 的 `.raw_image` 或 `.raw_colors` 属性
    - 得到的是 16-bit RGB 数据（已转换，线性 sRGB）
    - 不应该再应用色彩矩阵

**推荐用法**:
```python
import rawpy
import numpy as np

# 读取 DNG
with rawpy.imread('file.dng') as raw:
    # 获取已处理的 RGB 数据（线性 sRGB）
    rgb = raw.postprocess(
        use_camera_wb=False,      # 不应用白平衡（已应用）
        use_auto_wb=False,        # 不自动白平衡
        output_color=rawpy.ColorSpace.raw,  # 使用原始数据
        no_auto_bright=True,      # 不自动亮度调整
        gamma=(1, 1),             # 线性，不应用伽马
        output_bps=16             # 16-bit 输出
    )

    # rgb 现在是线性 sRGB [0, 65535]
    # 可以直接进行后续处理
```

##### ExifTool

**行为**:
- **仅读取元数据**: 不处理像素数据
- **显示所有标签**: 包括 ColorMatrix1, CameraCalibration1, ImageDescription 等
- **有助于验证**: 检查标签是否正确写入

**使用示例**:
```bash
exiftool -a -G1 -s file.dng | grep -E "(ColorMatrix|Calibration|AsShotNeutral|ImageDescription)"
```

#### 1.6.4 矩阵组合的实际效果

假设软件尝试应用标准 DNG 处理流程：

**数据状态**: 线性 sRGB (D65)

**软件应用**:
1. ColorMatrix1: sRGB → XYZ(D50)
2. 软件内部: XYZ(D50) → sRGB 或其他工作色彩空间

**实际变换**:
```
线性 sRGB (D65)
    ↓ [ColorMatrix1: sRGB→XYZ, D65→D50]
XYZ (D50)
    ↓ [软件: XYZ→sRGB, D50→D65]
线性 sRGB (D65)  [应该回到原始状态]
```

**理论上**: 如果软件正确实现，应该能还原原始 sRGB 数据

**实际上**:
- 大多数软件会正确处理
- 可能有轻微色温偏移（由于 D65 ↔ D50 往返转换的数值误差）
- 某些软件可能不支持非标准 DNG，显示警告或错误

#### 1.6.5 为什么使用参考矩阵而非省略

**选择 1**: 完全省略 ColorMatrix 等标签
- ❌ 违反 DNG 规范（必需标签）
- ❌ 许多软件会拒绝打开文件
- ❌ 无法传达色彩空间信息

**选择 2**: 使用单位矩阵
- ✅ 符合规范
- ⚠️ 软件可能误以为是未校准的 RAW
- ⚠️ 可能导致色彩处理错误

**选择 3**: 使用正确的参考矩阵 (本项目采用)
- ✅ 符合 DNG 规范
- ✅ 传达正确的色彩空间信息（sRGB）
- ✅ 配合 ImageDescription 说明特殊性
- ✅ 大多数软件能正确处理或至少不会严重失真

#### 1.6.6 用户建议

**如果图像在某软件中显示色彩异常**:

1. **检查软件设置**:
   - 确认输入颜色配置为 "linear sRGB" 或 "embedded"
   - 禁用自动白平衡
   - 检查是否有 "使用嵌入色彩配置" 选项

2. **导出为 TIFF**:
   ```bash
   # 如果软件无法正确处理，导出为标准 TIFF
   exiftool -b -PreviewImage file.dng > preview.tiff
   # 或使用 ImageMagick
   convert file.dng output.tiff
   ```

3. **使用专业工具**:
   - Adobe DNG Converter (可能能正确识别)
   - dcraw with 合适参数
   - 自定义 Python/rawpy 脚本

4. **验证数据正确性**:
   ```python
   # 检查像素值范围和色彩
   import rawpy
   with rawpy.imread('file.dng') as raw:
       rgb = raw.postprocess(gamma=(1,1), output_bps=16)
       print(f"Min: {rgb.min()}, Max: {rgb.max()}, Mean: {rgb.mean()}")
   ```

---

## 2. JPEG 输出：`-jpg-from-raw`

### 2.1 命令功能

将 X3F RAW 文件直接转换为 JPEG 格式，包含完整的图像处理流程：白平衡、色彩转换、自动曝光、ACES 色调映射、伽马校正和锐化。

### 2.2 转换流程

```
X3F RAW (相机原生色彩空间)
    ↓ [应用白平衡增益]
RAW with WB
    ↓ [归一化到 [0, 1]]
Normalized RAW [0, 1]
    ↓ [色彩矩阵变换: raw_to_xyz]
XYZ (D65 白点)
    ↓ [色彩矩阵变换: xyz_to_srgb]
线性 sRGB (D65 白点) [0, 1]
    ↓ [自动曝光调整]
Exposed sRGB [0, ∞]
    ↓ [ACES 色调映射]
Tone-mapped sRGB [0, 1]
    ↓ [伽马校正 2.2]
Gamma-corrected sRGB [0, 1]
    ↓ [锐化]
Sharpened sRGB [0, 1]
    ↓ [转换为 8-bit]
sRGB 8-bit [0, 255]
    ↓ [JPEG 编码]
JPEG 文件
```

### 2.3 色彩空间

#### 输入色彩空间
- **名称**: X3F RAW
- **白点**: 无
- **像素类型**: `uint16_t`
- **像素范围**: [black_level, white_level]

#### 中间色彩空间
- **名称**: 线性 sRGB
- **白点**: D65
- **伽马**: 1.0（线性）
- **像素类型**: `float`
- **像素范围**: [0.0, 1.0] (色彩转换后) → [0.0, ∞] (曝光后)

#### 输出色彩空间
- **名称**: sRGB (非线性，带伽马)
- **白点**: D65
- **伽马**: 2.2
- **像素类型**: `uint8_t`
- **像素范围**: [0, 255]

### 2.4 色彩矩阵

使用与 DNG 输出相同的矩阵，但支持三种目标色彩空间：

#### 选项 1: sRGB (默认)
**转换路径**: RAW → XYZ(D65) → sRGB

**矩阵**:
```c
// raw_to_xyz (包含白平衡)
x3f_get_raw_to_xyz(x3f, wb, raw_to_xyz);

// xyz_to_srgb
double xyz_to_srgb[9] = {
   3.2406, -1.5372, -0.4986,
  -0.9689,  1.8758,  0.0415,
   0.0557, -0.2040,  1.0570
};

// 组合
x3f_3x3_3x3_mul(xyz_to_srgb, raw_to_xyz, raw_to_target);
```

#### 选项 2: Adobe RGB (`-color AdobeRGB`)
**转换路径**: RAW → XYZ(D65) → Adobe RGB

**矩阵**:
```c
double xyz_to_adobe[9] = {
   2.0416, -0.5650, -0.3447,
  -0.9689,  1.8758,  0.0415,
   0.0138, -0.1183,  1.0154
};
```

#### 选项 3: ProPhoto RGB (`-color ProPhotoRGB`)
**转换路径**: RAW → XYZ(D65) → XYZ(D50) → ProPhoto RGB

**矩阵**:
```c
// ProPhoto RGB: 需要先转换白点 D65 -> D50
double xyz_to_prophoto[9] = {
   1.3460, -0.2556, -0.0511,
  -0.5446,  1.5082,  0.0205,
   0.0000,  0.0000,  1.2123
};

// Bradford 色适应矩阵 D65 -> D50
double d65_to_d50[9] = {
   1.0478, 0.0229, -0.0502,
   0.0295, 0.9904, -0.0171,
  -0.0092, 0.0150,  0.7518
};

// 组合: XYZ(D65) -> XYZ(D50) -> ProPhoto
x3f_3x3_3x3_mul(xyz_to_prophoto, d65_to_d50, xyz_d50_to_prophoto);
x3f_3x3_3x3_mul(xyz_d50_to_prophoto, raw_to_xyz, raw_to_target);
```

### 2.5 图像处理步骤

#### 步骤 1: 色彩转换
**实现位置**: `x3f_output_jpeg.c:x3f_dump_raw_data_as_jpeg()` (lines 270-337)

```c
// 对每个像素
for (row = 0; row < rows; row++) {
  for (col = 0; col < columns; col++) {
    // 归一化
    input[0] = (pixel[0] - black[0]) / (white[0] - black[0]);
    input[1] = (pixel[1] - black[1]) / (white[1] - black[1]);
    input[2] = (pixel[2] - black[2]) / (white[2] - black[2]);

    // 应用色彩矩阵
    x3f_3x3_3x1_mul(raw_to_target, input, output);

    // 存储为 float [0, 1]
    float_image[i] = clamp(output, 0.0, 1.0);
  }
}
```

#### 步骤 2: 自动曝光
**实现位置**: `x3f_output_jpeg.c:calculate_auto_exposure()` (lines 62-110)

**算法**:
```c
// 1. 计算亮度（Rec.709）
luminance = 0.2126*R + 0.7152*G + 0.0722*B

// 2. 排序所有亮度值
qsort(luminance, total_pixels)

// 3. 取中间 90% 的平均值（去除极亮和极暗）
current_brightness = mean(luminance[5%...95%])

// 4. 计算曝光补偿（目标：18% 灰）
exposure = 0.18 / current_brightness

// 5. 限制范围 [0.3, 5.0]
exposure = clamp(exposure, 0.3, 5.0)
```

**目标**: 18% 灰（摄影标准中间调亮度）

#### 步骤 3: ACES 色调映射
**实现位置**: `x3f_output_jpeg.c:aces_tonemap()` (lines 49-60)

**算法** (Academy Color Encoding System):
```c
float aces_tonemap(float x) {
  float a = 2.51, b = 0.03;
  float c = 2.43, d = 0.59, e = 0.14;

  return (x * (a*x + b)) / (x * (c*x + d) + e);
}

// 应用到每个像素
for (i = 0; i < total_pixels * 3; i++) {
  image[i] = aces_tonemap(image[i] * exposure);
}
```

**特点**:
- 将 HDR 范围 [0, ∞] 映射到 [0, 1]
- 保持色调过渡自然
- 高光压缩平滑

#### 步骤 4: 伽马校正
**实现位置**: `x3f_output_jpeg.c:apply_gamma_correction()` (lines 132-147)

**算法**:
```c
// 根据色彩空间选择伽马
if (color_encoding == PPRGB) {
  gamma = 1.0 / 1.8;  // ProPhoto RGB
} else {
  gamma = 1.0 / 2.2;  // sRGB, Adobe RGB
}

// 应用到每个像素
for (i = 0; i < total_pixels * 3; i++) {
  image[i] = pow(image[i], gamma);
}
```

**伽马值**:
- **sRGB**: 2.2
- **Adobe RGB**: 2.2
- **ProPhoto RGB**: 1.8

#### 步骤 5: 锐化
**实现位置**: `x3f_output_jpeg.c:apply_sharpening()` (lines 204-223)

**算法** (Unsharp Mask):
```c
// 1. 高斯模糊 (sigma=1.0)
blurred = gaussian_blur(image, sigma=1.0);

// 2. 计算高频细节
high_freq = original - blurred;

// 3. 增强细节 (strength=1.2)
sharpened = original + 1.2 * high_freq;

// 4. 限制范围
result = clamp(sharpened, 0.0, 1.0);
```

**参数**:
- **sigma**: 1.0（模糊半径）
- **strength**: 1.2（锐化强度）

#### 步骤 6: 8-bit 转换
**实现位置**: `x3f_output_jpeg.c:x3f_dump_raw_data_as_jpeg()` (lines 364-366)

```c
for (i = 0; i < total_pixels * 3; i++) {
  output_image[i] = (uint8_t)(clamp(float_image[i], 0.0, 1.0) * 255.0);
}
```

### 2.6 JPEG 参数

#### 色彩空间
- **值**: `JCS_RGB`
- **用途**: JPEG 使用 RGB 色彩空间

#### 质量
- **值**: `98`
- **用途**: 高质量 JPEG 编码（范围 0-100）

#### 子采样
- **值**: 无（`TRUE` 参数禁用色度子采样）
- **用途**: 保持完整色彩信息

---

## 3. 白平衡

### 3.1 白平衡预设

支持的白平衡模式（通过 `-wb` 参数）：

| 预设名称 | 中文 | 色温 (K) | 使用场景 |
|---------|------|---------|---------|
| Auto | 自动 | 变化 | 相机自动计算 |
| Sunlight | 日光 | 5500 | 晴天户外 |
| Shadow | 阴影 | 7000 | 阴影区域 |
| Overcast | 阴天 | 6500 | 多云天气 |
| Incandescent | 白炽灯 | 3200 | 钨丝灯 |
| Florescent | 荧光灯 | 4000 | 日光灯 |
| Flash | 闪光灯 | 5500 | 使用闪光灯 |
| Custom | 自定义 | 变化 | 用户设定 |
| ColorTemp | 色温 | 变化 | 指定色温值 |
| AutoLSP | 自动LSP | 变化 | 相机LSP算法 |

### 3.2 白平衡实现

**实现位置**: `x3f_meta.c:x3f_get_wb_preset_gain()`

白平衡增益直接从 X3F 文件的 CAMF 段读取，然后应用到 `raw_to_xyz` 矩阵的对角线上：

```c
// 读取白平衡增益
x3f_get_wb_preset_gain(x3f, wb, &wb_gain);

// 应用到色彩矩阵
raw_to_xyz[0] *= wb_gain.r;  // R 通道
raw_to_xyz[4] *= wb_gain.g;  // G 通道
raw_to_xyz[8] *= wb_gain.b;  // B 通道
```

---

## 4. 技术对比

| 特性 | DNG (-linear-srgb) | JPEG (-jpg-from-raw) |
|------|-------------------|---------------------|
| **输出格式** | TIFF/DNG 容器 | JPEG |
| **位深度** | 16-bit | 8-bit |
| **色彩空间** | 线性 sRGB | 非线性 sRGB/Adobe/ProPhoto |
| **伽马** | 1.0 (线性) | 2.2 / 1.8 |
| **白点** | D65 | D65 / D50 |
| **色调映射** | 无 | ACES Filmic |
| **曝光调整** | 无 | 自动 (18% 灰) |
| **锐化** | 无 | Unsharp Mask |
| **文件大小** | 大 (~数十MB) | 小 (~数MB) |
| **后期空间** | 大 (可二次调整) | 小 (已定型) |
| **用途** | RAW 后期处理 | 直接预览/分享 |

---

## 5. 参考资料

### 5.1 色彩空间标准

- **sRGB IEC 61966-2-1**: https://www.color.org/sRGB.pdf
- **Adobe RGB (1998)**: https://www.adobe.com/digitalimag/pdfs/AdobeRGB1998.pdf
- **ProPhoto RGB (ROMM RGB)**: ISO 22028-2

### 5.2 DNG 规范

- **DNG Specification 1.4**: https://www.adobe.com/content/dam/acom/en/products/photoshop/pdfs/dng_spec_1.4.0.0.pdf
- **TIFF 6.0 Specification**: https://www.adobe.io/content/dam/udp/en/open/standards/tiff/TIFF6.pdf

### 5.3 色彩科学

- **CIE XYZ Color Space**: https://en.wikipedia.org/wiki/CIE_1931_color_space
- **Bradford Chromatic Adaptation**: http://www.brucelindbloom.com/index.html?Eqn_ChromAdapt.html
- **ACES Filmic Tone Mapping**: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/

### 5.4 相关代码文件

| 文件 | 功能 |
|------|------|
| `x3f_output_dng.c` | DNG 输出实现 |
| `x3f_output_jpeg.c` | JPEG 输出实现 |
| `x3f_matrix.c` | 色彩矩阵定义和运算 |
| `x3f_meta.c` | X3F 元数据读取（白平衡等） |
| `x3f_process.c` | RAW 数据处理 |

---

## 6. 常见问题

### Q1: 为什么 DNG 输出是线性的？
**A**: 线性数据保留了原始动态范围，便于后期软件进行精确的色彩和曝光调整。如果输出非线性数据，会丢失高光和阴影的细节。

### Q2: ACES 色调映射的优势是什么？
**A**: ACES 是电影工业标准，相比简单的线性或 S 曲线映射，能更自然地处理高动态范围，保持色彩一致性，避免高光过曝和色彩偏移。

### Q3: 为什么需要两个 ColorMatrix 标签？
**A**: 在 `-linear-srgb` 模式下，ColorMatrix1 是**参考矩阵**（sRGB → XYZ），用于让 DNG 阅读器理解数据的色彩空间定义。实际的色彩转换已经在写入前完成。

### Q4: 白平衡增益是如何工作的？
**A**: 白平衡增益是对 R、G、B 三个通道分别乘以系数，补偿不同光源下的色温偏差。这些增益值直接嵌入到 `raw_to_xyz` 矩阵中。

### Q5: 为什么 ProPhoto RGB 使用不同的伽马？
**A**: ProPhoto RGB 标准定义使用 1.8 伽马，而 sRGB 和 Adobe RGB 使用 2.2 伽马。这是各自色彩空间规范的要求。

---

## 版本历史

- **v0.60**: 添加 `-linear-srgb` 和 `-jpg-from-raw` 功能
- **日期**: 2025-11-10
- **作者**: weaming `garden.yuen#gmail.com`
