## 1. 장면 레이어들이 map→odom 을 어디서 얻을지
- options: Robot 에 공개 속성 추가(남의 파일) / TfTree 인스턴스에 의존 / 레이어마다 /tf 를 받아 map→odom 만 기억
- chosen: 공통 바탕 SceneSource 가 /tf(200 ms) 를 함께 구독해 map→odom 을 기억한다
- why: robot.* 는 고칠 수 없고 RosBridge 공개 API 만 써야 한다. 레이어가 서로 묶이지 않는다
- effect: 켠 레이어가 있으면 /tf 를 200 ms 로 구독한다. 나중에 Robot 이 map→odom 을 공개하면 SceneSource::toOdom 만 바꾸면 된다

## 2. ScanAccumulator 가 /scan 을 직접 구독할지
- options: want("/scan") 로 직접 구독 / Robot 의 LiDAR 센서 구독을 받기만 함
- chosen: 받기만 한다(topics() 비어 있음)
- why: LiDAR 표시는 Robot 센서 토글이 다스린다. 잔상이 센서를 몰래 켜면 꺼 둔 /scan 이 다시 흐른다
- effect: LiDAR 센서를 끄면 잔상도 멈춘다. 바꾸려면 ScanAccumulator::topics() 에 /scan 을 넣는다

## 3. 저장된 뷰(카메라 북마크) 모델을 C++ 로 만들지
- options: C++ 모델 / QML QtCore Settings 배열(JSON)
- chosen: 이번에는 만들지 않고 P8b 의 QML 에 맡긴다
- why: 요구서가 QtCore Settings 배열 저장을 지정했고 값이 카메라 QML 상태뿐이라 C++ 로 떼어도 시험할 계산이 없다
- effect: P8b 가 SceneView 쪽에서 만든다

## 4. 코스트맵 팔레트 위치
- options: P4 msgparse::occupancyToImage(costmap) 를 기다려 사용 / src/scene 안에 작게 둠
- chosen: CostmapLayer::costColor · toImage 로 src/scene 안에 둔다(RViz costmap: 0 투명, 1~98 파랑→빨강, 99 청록, 100 자홍, -1 회청)
- why: msgparse 는 이 worktree 에 아직 없고 남의 파일이다
- effect: P4 가 병합되면 CostmapLayer::toImage 를 msgparse 호출로 바꾸면 된다

## 5. 공분산 표시 클래스 하나로 /amcl_pose 와 /odom 을 함께 처리
- options: AmclPose 에 odom 옵션 속성 추가 / topic 속성으로 인스턴스를 나눔
- chosen: AmclPose.topic 을 "/odom" 으로 둔 두 번째 인스턴스를 쓴다(두 메시지 모두 pose.pose · pose.covariance)
- why: 코드가 가장 적고 켜고 끄기도 독립적이다
- effect: QML 에서 AmclPose { topic: "/odom" } 하나를 더 둔다. valid 는 공분산이 모두 0 이면 false
