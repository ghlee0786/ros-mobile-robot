# PLAN — 대시보드 3D 로봇 모델 경량화 도구

## 목표
dashboard/model/*.obj(+mtl Kd 색) → 삼각형을 줄인 같은 이름 glb. 재질 유지, 좌표 그대로, 법선 재계산, 원본 불변.
검사: `checks\model_tool.cmd` (pytest tooling\fusion\tests).

## 진행
- [x] tooling/fusion/decimate_model.py — OBJ/MTL 직접 파싱(numpy), 재질 → 이어진 조각으로 나눠 조각별 몫으로 pymeshlab quadric edge collapse(preservenormal), 경계 상자 보호, 몫 재분배(최대 4회), 각진 모서리 분할 법선, trimesh 로 glb(PBR baseColorFactor) 출력, 결과 표.
- [x] tooling/fusion/tests/test_decimate.py (+conftest.py: tmp 를 tests/.tmp 에 — C: 회피)
- [x] .gitignore: dashboard/model/*.glb, tooling/fusion/tests/.tmp/
- [x] checks/model_tool.cmd: --with fast-simplification → --with pymeshlab
- [x] 실제 모델 실행: body 723134→63077(91.1→3.8 MB, 0.54 mm), lidar 126218→15134, wheel 17514→5998
- [x] 대시보드 시험 모드 비교(카메라 회전 actions): glb 37~38 fps · 315 MB / obj 7~13 fps · 1370 MB / --no-model 38 fps · 278 MB. 스크린샷 모양 동일.

## 발견
- fast-simplification 은 Fusion 판재·압출재의 가늘고 긴 삼각형에서 축약을 멈춰(flip/degenerate 검사) body 가 15만 이하로 안 내려갔다 → pymeshlab 로 교체.
- 재질 하나를 통째로 줄이면 작은 조각이 통째로 사라져 경계 상자가 30 mm 넘게 줄었다 → 조각별 처리 + 상자 검사.
- 연결 없는 시험 모드에서는 화면을 다시 그릴 일이 없어 fps 가 ~3 으로 같게 나온다 → scene.camYaw 를 25 ms 마다 바꾸는 actions 로 측정.
- bash 에서 cmd //c 로 run_test.cmd 에 인자를 넘기면 state 파일이 안 써졌다 → PowerShell 에서 실행.

## 남은 일
- 없음. (실기 대시보드에서 glb 로 fps 확인은 사람이 할 때)
