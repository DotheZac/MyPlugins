# Changelog

이 문서는 `Laser` 플러그인의 사용자 체감 기능 변경을 기록합니다.

## 1.1.0 - 2026-08-07

### Changed

* `IsClear`가 완료 상태를 영구 유지하지 않고 레이저가 `Clear` 태그 목표에 닿아 있는 동안에만 `true`가 되도록 변경했습니다.
* 레이저가 `Clear` 목표에서 벗어나면 `IsClear`가 즉시 `false`로 돌아가도록 변경했습니다.
* `IsClear`가 `true`로 바뀔 때 표시되던 녹색 디버그 문구를 제거했습니다.

## 1.0.0 - 2026-08-06

### Added

* Cone 꼭짓점에서 로컬 `+X` 방향으로 발사되는 레이저 액터를 추가했습니다.
* Line Trace 충돌 노멀을 이용한 다중 반사를 추가했습니다.
* 최대 반사 횟수와 전체 레이저 길이를 설정할 수 있습니다.
* 반사 후 같은 표면에 다시 충돌하는 현상을 줄이기 위한 `Surface Offset` 설정을 추가했습니다.
* 각 직선 및 반사 구간을 `/Laser/Laser.Laser` Niagara System으로 표시하도록 추가했습니다.
* Niagara의 `User.Start`와 `User.End` 파라미터를 통해 구간 위치를 전달합니다.
* 구간 수가 변경될 때 Niagara Component를 재사용하고 남는 구간을 비활성화하도록 추가했습니다.
* Component Tag 또는 Actor Tag가 `Mirror`인 표면에서만 반사되도록 추가했습니다.
* 반사 판정에 사용할 `Reflection Tag`를 액터에서 변경할 수 있습니다.
* 계산된 경로를 `Laser Points`와 `Laser Hits`로 Blueprint에 제공합니다.
* 필요할 때 Trace 경로를 확인할 수 있는 선택적 디버그 라인을 추가했습니다.

#### Clear 목표 판정

* Component 또는 Actor의 `Clear` 태그를 레이저 목표로 판정합니다.
* 레이저가 `Clear` 목표에 도달하면 Blueprint에서 읽을 수 있는 `IsClear`를 `true`로 변경합니다.
* `IsClear`가 처음 `true`로 변경될 때 화면에 녹색 `true`를 한 번 출력합니다.
* 새 플레이 세션의 `BeginPlay`에서 `IsClear`를 `false`로 초기화합니다.
* 같은 플레이 세션에서는 목표에서 벗어나더라도 `IsClear` 완료 상태를 유지합니다.
