## 1. 축약 라이브러리: fast-simplification 대신 pymeshlab
- options: fast-simplification 유지(body ~15만 삼각형에서 멈춤) / pymeshlab(MeshLab quadric edge collapse) / open3d
- chosen: pymeshlab, checks\model_tool.cmd 의 --with 도 fast-simplification → pymeshlab 로 바꿈
- why: fast-simplification 은 가늘고 긴 삼각형(판재·3030 프로파일)에서 축약을 거부해 body 가 목표 6만의 2.5 배에 머물렀다. pymeshlab 은 같은 부품을 목표 수까지 줄이고 오차도 1 mm 미만. open3d 보다 가볍다(약 53 MB).
- effect: uv 실행 명령이 `--with pymeshlab` 이 된다. 되돌리려면 decimate_model.reduce() 의 축약 호출만 바꾸면 된다.

## 2. 조각별 경계 상자 허용치
- options: 모든 조각 1 mm / 파일 경계 상자에 닿는 면만 1 mm, 안쪽 조각은 제 크기의 15 % / 검사 없음
- chosen: 바깥 면 1 mm(MAX_BOX_ERR), 안쪽 조각은 max(1 mm, 크기×15 %)(PART_BOX_ERR)
- why: 모든 조각 1 mm 면 6 mm 커넥터 같은 작은 부품이 원본 그대로 남아 목표를 못 맞췄고, 검사가 없으면 작은 부품이 통째로 사라졌다.
- effect: 파일 전체 경계 상자 오차는 1 mm 이내(body 0.54 mm). 상수 두 개로 조절.

## 3. 재질 표현
- options: PBR metallic 0 · roughness 0.8 / metallic 1 / glTF 기본값
- chosen: baseColorFactor=Kd, metallicFactor 0, roughnessFactor 0.8
- why: mtl 에 Kd 만 있어 OBJ 로 읽을 때와 비슷한 무광 색이 되도록. 스크린샷에서 obj 와 거의 같은 색.
- effect: decimate() 의 PBRMaterial 인자만 바꾸면 된다.
