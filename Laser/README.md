# Laser Plugin

`Laser`는 Line Trace로 레이저 경로를 계산하고 Niagara로 각 구간을 표시하는 Unreal Engine 5.7 Runtime 플러그인입니다.

레이저는 Cone 꼭짓점에서 액터의 로컬 `+X` 방향으로 발사되며, `Mirror` 태그가 지정된 표면에서 최대 설정 횟수만큼 반사됩니다.

## 주요 기능

* Cone 형태의 발사 방향 표시
* 전체 길이를 유지하는 다중 반사 Line Trace
* 최대 반사 횟수 설정
* Niagara를 이용한 직선 및 반사 구간 표시
* Component Tag 또는 Actor Tag를 이용한 반사체 지정
* `Clear` 태그 목표 도달 시 퍼즐 완료 상태 제공
* Blueprint에서 경로 갱신 및 결과 조회
* 선택적인 디버그 라인 표시

## 기본 사용법

### 1. 레이저 액터 배치

에디터의 Place Actors 또는 Content Browser에서 `Laser Reflection Actor`를 찾아 레벨에 배치합니다.

Cone의 꼭짓점이 레이저 발사 위치이며, 액터의 로컬 `+X` 방향으로 레이저가 발사됩니다. 액터를 회전해 발사 방향을 조절할 수 있습니다.

### 2. 반사 표면 설정

특정 Mesh Component만 반사시키려면 해당 컴포넌트를 선택하고 다음 항목에 태그를 추가합니다.

```text
Details → Tags → Component Tags → Mirror
```

액터에 포함된 모든 충돌 컴포넌트를 반사 대상으로 사용하려면 액터에 태그를 추가합니다.

```text
Details → Actor → Tags → Mirror
```

충돌한 Component 또는 Actor 중 하나에 `Mirror` 태그가 있으면 반사됩니다. 태그가 없다면 레이저는 해당 충돌점에서 종료됩니다.

`Laser Reflection Actor`의 `Reflection Tag`를 변경하면 `Mirror` 대신 다른 이름을 사용할 수 있습니다. `Reflection Tag`를 `None`으로 비우면 모든 충돌 표면에서 반사됩니다.

### 3. Niagara 설정

기본 Niagara System은 다음 에셋을 사용합니다.

```text
/Laser/Laser.Laser
```

Niagara System에는 다음 User Parameter가 필요합니다.

```text
User.Start
User.End
```

반사 경로의 각 직선 구간마다 Niagara Component 하나가 사용됩니다. 플러그인은 각 구간의 시작점과 끝점을 두 파라미터에 월드 좌표로 전달합니다.

다른 Niagara System을 사용하려면 `Laser Niagara System`을 교체합니다. 파라미터 이름이 다르면 `Niagara Start Parameter`와 `Niagara End Parameter`도 함께 변경합니다.

## 주요 설정

### Trace

| 속성 | 기본값 | 설명 |
| --- | ---: | --- |
| `Max Bounces` | `3` | 허용할 최대 반사 횟수입니다. 최초 직진 구간은 반사 횟수에 포함되지 않습니다. |
| `Max Distance` | `2000 cm` | 반사를 포함한 전체 레이저 경로의 최대 길이입니다. |
| `Surface Offset` | `1 cm` | 반사 후 같은 표면에 즉시 다시 충돌하지 않도록 다음 Trace 시작점을 이동합니다. |
| `Trace Channel` | `Visibility` | 충돌 검사에 사용할 채널입니다. |
| `Trace Complex` | `false` | Complex Collision을 사용할지 결정합니다. |
| `Trace Every Frame` | `true` | 매 프레임 레이저 경로를 갱신합니다. |

### Reflection

| 속성 | 기본값 | 설명 |
| --- | ---: | --- |
| `Reflection Tag` | `Mirror` | 반사 가능한 Component 또는 Actor를 식별하는 태그입니다. |

### Niagara

| 속성 | 기본값 | 설명 |
| --- | ---: | --- |
| `Use Niagara Laser` | `true` | Niagara 레이저 표시를 활성화합니다. |
| `Laser Niagara System` | `/Laser/Laser.Laser` | 구간을 표시할 Niagara System입니다. |
| `Niagara Start Parameter` | `User.Start` | 구간 시작점을 받을 User Parameter입니다. |
| `Niagara End Parameter` | `User.End` | 구간 끝점을 받을 User Parameter입니다. |

### Emitter

| 속성 | 기본값 | 설명 |
| --- | ---: | --- |
| `Cone Length` | `100 cm` | 발사 방향을 표시하는 Cone의 길이입니다. |
| `Cone Radius` | `50 cm` | Cone 밑면 반지름입니다. |
| `Muzzle Forward Offset` | `1 cm` | 기하학적 꼭짓점보다 앞쪽에 발사점을 배치하는 거리입니다. |

### Debug

| 속성 | 기본값 | 설명 |
| --- | ---: | --- |
| `Draw Debug Laser` | `false` | Niagara와 별도로 디버그 라인을 표시합니다. |
| `Debug Laser Color` | Red | 디버그 라인의 색상입니다. |

## Blueprint 사용

`Update Laser Trace`를 호출하면 즉시 레이저 경로를 다시 계산합니다.

결과는 다음 배열에서 확인할 수 있습니다.

* `Laser Points`: 발사점, 반사점, 최종 끝점이 순서대로 저장됩니다.
* `Laser Hits`: 경로에서 발생한 충돌 결과가 순서대로 저장됩니다. 반사하지 않는 마지막 충돌도 포함됩니다.

방향과 발사 위치는 다음 함수로 가져올 수 있습니다.

```text
Get Laser Origin
Get Laser Direction
```

## 동작 예시

`Max Bounces`가 `3`인 경우 최초 Trace 이후 최대 세 번까지 방향을 반사합니다.

```text
발사점 → Mirror 1 → Mirror 2 → Mirror 3 → 마지막 충돌 또는 최대 거리
```

중간에 `Mirror` 태그가 없는 표면과 충돌하면 해당 위치에서 즉시 종료됩니다.

## 목표 지점과 IsClear

목표 Mesh Component 또는 Actor에 다음 태그를 추가합니다.

```text
Clear
```

새 플레이 세션이 시작되면 `Laser Reflection Actor`의 `IsClear`는 항상 `false`로 초기화됩니다. 레이저 경로가 `Clear` 태그에 닿아 있는 동안에는 `IsClear`가 `true`이며, 목표에서 벗어나는 즉시 다시 `false`가 됩니다.

다른 Blueprint에서 `IsClear`가 일정 시간 동안 계속 `true`인지 검사하면, 레이저가 목표를 지정된 시간만큼 유지했을 때 원하는 함수를 실행할 수 있습니다.

목표가 반사체 역할도 해야 한다면 동일한 Component 또는 Actor에 다음 태그를 함께 지정합니다.

```text
Mirror
Clear
```

`Clear Tag` 속성에서 목표 판정에 사용할 태그 이름을 변경할 수 있습니다.

## 문제 해결

### 레이저가 보이지 않는 경우

* `Use Niagara Laser`가 활성화되어 있는지 확인합니다.
* `Laser Niagara System`이 지정되어 있는지 확인합니다.
* Niagara System에 `User.Start`와 `User.End` Position User Parameter가 있는지 확인합니다.
* Niagara Emitter와 Renderer가 활성화되어 있는지 확인합니다.
* 확인이 필요하면 `Draw Debug Laser`를 임시로 활성화해 Trace 경로부터 점검합니다.

### 표면에서 반사되지 않는 경우

* 충돌 Component의 `Component Tags` 또는 Actor의 `Actor Tags`에 `Mirror`가 있는지 확인합니다.
* 해당 표면이 설정된 `Trace Channel`을 Block하는지 확인합니다.
* 눈에 보이는 Mesh와 실제 Collision 표면의 방향이 일치하는지 확인합니다.

### 반사 지점이 떨리거나 같은 면에 반복 충돌하는 경우

`Surface Offset`을 조금 높입니다. 너무 큰 값은 얇은 물체나 가까운 표면을 건너뛸 수 있으므로 작은 값부터 조절하는 것이 좋습니다.

## 제작자

DotheZac
