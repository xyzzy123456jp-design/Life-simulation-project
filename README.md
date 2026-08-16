# Life Simulation Project

架空の山道をVRで運転し、AIキャラクターと過ごす、VRカーライフシミュレーター（Unreal Engine 5）

- 仕様書: [docs/spec.md](docs/spec.md)

位置調整のキーは:
I/K: 前後
J/L: 左右
U/O: 下/上
[ / ]: VRの目の高さ

& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\Users\hueda\Documents\Unreal Projects\LifeSimulation 5.8\LifeSimulation.uproject" -dx11 -game -vr -log -execcmds="r.ScreenPercentage 30"

& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\Users\hueda\Documents\Unreal Projects\LifeSimulation 5.8\LifeSimulation.uproject" -dx11 -game -vr -log -execcmds="r.ScreenPercentage 30, vr.MirrorMode 4"
