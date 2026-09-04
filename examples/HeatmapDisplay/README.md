# HeatmapDisplay

VL53L5CX の 8x8 距離マップをM5StackのLCDにヒートマップとして表示するサンプルです。[Basic](../Basic) と同じ表示処理を、他のジェスチャー系サンプル（[GestureDemo](../GestureDemo)、[FingerDirection](../FingerDirection)、[RockPaperScissors](../RockPaperScissors)）の土台として切り出したものです。

## 仕組み

1. `Wire`（I2C, Port A: SDA=21, SCL=22）でセンサーを初期化し、8x8（64ゾーン）・15Hzでレンジングを開始する。
2. 1フレームごとに64ゾーン分の距離データを取得し、各ゾーンを近い（赤）〜遠い（青）のグラデーションで着色して8x8グリッドに描画する。距離が取得できない/無効なゾーンはグレーで表示する。
3. 画面中央ゾーンの距離（mm）を上部にテキストで表示する。

## 使い方

M5StackにセンサーをGrove（I2C, Port A）で接続して書き込むと、LCDに8x8のヒートマップと中央ゾーンの距離が表示されます。センサーの前に手や物を置くと、近さに応じて色が変化する様子を確認できます。
