# Changelog

## 1.0.0

- 월드에 배치하여 사용할 수 있는 `SunZoneManager`를 추가했습니다.
- 태양의 시작 및 끝 회전을 지정하고, 정수 Zone 번호와 전체 Zone 개수에 따라 목표 위치를 계산할 수 있습니다.
- Zone 변경 시 두 태양 회전 사이를 부드럽게 보간하도록 지원합니다.
- 태양 대상으로 `Directional Light`만 지정할 수 있습니다.
- Details 패널에서 `Current Zone`을 변경해도 에디터와 PIE에서 부드러운 이동을 확인할 수 있습니다.
- Blueprint에서 Zone 변경, 즉시 이동 및 에디터 미리보기를 사용할 수 있습니다.
