# VL53L5CX Module for M5Stack Unit

VL53L5CX（8x8 ToF距離センサー）をM5Stackで扱いやすくするためのGrove接続モジュールです。

---

## 概要

STMicroelectronics製のVL53L5CX（8x8ゾーンのToF距離センサー）を搭載したユニットで、M5Stackに直接接続して使用できます。  
Grove（I2C）接続により、配線を気にせず距離マップの取得を手軽に実現できます。

---

## 特徴

- Grove（I2C）接続で簡単に利用可能
- 8x8（64ゾーン）の距離マップを取得可能
- M5Stackシリーズに最適化

---

## 必要なもの

- M5Stack本体
- 本モジュール（vl53l5cx-m5-unit）

---

## 接続方法

Groveポート（Port A / I2C）に接続するだけで利用できます。

---

## 想定用途

- 人・物の検知
- 距離マップによる形状把握
- ジェスチャー検出
- 非接触センシング

---

## Examples

本リポジトリにはサンプルを用意しています。

### [Basic](examples/Basic)

VL53L5CXの基本動作確認

- センサー初期化
- 8x8距離マップの取得
- M5StackのLCDへヒートマップ表示

### [HeatmapDisplay](examples/HeatmapDisplay)

8x8距離マップをヒートマップとしてLCDに表示する、他のジェスチャー系サンプルの土台

### [GestureDemo](examples/GestureDemo)

手の重心追跡による上下左右スワイプジェスチャーの認識

### [FingerDirection](examples/FingerDirection)

かざした指がさす8方向（上下左右・斜め）の認識

### [RockPaperScissors](examples/RockPaperScissors)

手の形（グー・チョキ・パー）によるじゃんけんポーズ判定


---

## 購入先

[スイッチサイエンス](https://www.switch-science.com/search?q=xmakers)

[BASE](https://xmakers.base.shop/)

---

## ライセンス

[MIT License](LICENSE)