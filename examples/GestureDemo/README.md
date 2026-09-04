# GestureDemo

VL53L5CX の 8x8 距離マップから手の動き（上下左右のスワイプ）を認識するサンプルです。[HeatmapDisplay](../HeatmapDisplay) のヒートマップ表示をベースに、手の重心追跡によるジェスチャー判定を追加しています。

## 仕組み

1. 各フレームで、距離が `HAND_MIN_MM`〜`HAND_MAX_MM`（既定: 30〜400mm）の範囲にあるゾーンを「手がある」とみなす。
2. 該当ゾーンが `MIN_VALID_ZONES` 個以上あれば手を検出したと判断し、それらのゾーンの重心（グリッド座標）を計算する。
3. 手を検出し続けている間、開始位置から最新位置までを追跡する。
4. 手がフレーム上から `MISSING_FRAMES_TO_END` 回連続で検出されなくなったら追跡終了とみなし、開始位置と終了位置の移動距離・移動時間からスワイプ方向（LEFT / RIGHT / UP / DOWN）を判定して画面上部に表示する。

## しきい値の調整

`src/main.cpp` 冒頭の定数で挙動を調整できます。

| 定数 | 説明 |
| ---- | ---- |
| `HAND_MIN_MM` / `HAND_MAX_MM` | 手として検出する距離範囲 |
| `MIN_VALID_ZONES` | 手ありと判定するために必要な有効ゾーン数 |
| `MIN_SWIPE_DISTANCE_CELLS` | スワイプと認めるための最小移動距離（グリッドセル単位） |
| `MIN_SWIPE_DURATION_MS` / `MAX_SWIPE_DURATION_MS` | スワイプと認める移動時間の範囲（誤検出・止まった手の除外） |
| `GESTURE_DISPLAY_MS` | 判定結果を画面に表示しておく時間 |
| `MISSING_FRAMES_TO_END` | 追跡を終了するまでの連続未検出フレーム数 |

## 使い方

センサーの上を左右・上下に手を動かすと、画面上部に検出したジェスチャー（LEFT / RIGHT / UP / DOWN）が表示されます。
