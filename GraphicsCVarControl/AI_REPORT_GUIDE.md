# Graphics CVar Control AI Report Guide

## 목적

이 문서는 `Graphics CVar Control` 플러그인이 내보낸 GPU Baseline/Candidate 보고서를 AI가 일관된 기준으로 해석하기 위한 가이드입니다.

AI에게 다음 파일을 함께 제공하는 방식을 권장합니다.

1. 이 `AI_REPORT_GUIDE.md`
2. 측정 후 생성된 `GPUProfile_*.md`
3. 세부 데이터나 자동 처리가 필요하면 같은 이름의 `GPUProfile_*.json`

내보낸 `GPUProfile_*.md`에도 핵심 분석 지침이 포함되어 있으므로 해당 파일만 전달해도 기본 분석은 가능합니다.

## 생성 파일

`Export AI Report`를 누르면 플러그인의 `Reports` 폴더에 같은 이름으로 두 파일이 생성됩니다.

- `GPUProfile_YYYYMMDD_HHMMSS.md`
- `GPUProfile_YYYYMMDD_HHMMSS.json`

현재 프로젝트의 기본 저장 경로는 다음과 같습니다.

```text
C:\Users\user\Desktop\Pharaoh\Project\Plugins\GraphicsCVarControl\Reports
```

플러그인이 다른 프로젝트나 경로로 이동하면 `GraphicsCVarControl` 플러그인 폴더를 기준으로 `Reports` 위치를 자동 계산합니다.

## 파일별 용도

### Markdown

사람과 AI가 바로 읽기 위한 요약 보고서입니다.

- 캡처 환경 및 측정 조건
- Total GPU Frame 통계
- 변경된 CVar
- GPU 악화 및 개선 상위 항목
- 전체 GPU Pass 비교
- 관리 대상 CVar 전체 상태

일반적인 AI 대화 분석에는 Markdown 파일을 우선 사용합니다.

### JSON

구조화된 원본 비교 데이터입니다.

- 자동 분석 도구
- 자체 AI API 연동
- 여러 보고서의 일괄 비교
- Markdown에서 생략되거나 재가공될 수 있는 필드 확인

## 핵심 계산 규칙

### 시간 차이

```text
delta_ms = candidate_average_ms - baseline_average_ms
```

- `delta_ms > 0`: Candidate가 느려진 것으로 해석
- `delta_ms < 0`: Candidate가 개선된 것으로 해석
- `delta_ms = 0`: 평균 GPU 시간 변화가 없음

### 변화율

```text
change_percent = delta_ms / baseline_average_ms * 100
```

Baseline이 `0 ms`이면 정상적인 백분율을 계산할 수 없으므로 `N/A` 또는 JSON의 `null`로 기록됩니다.

### 누락된 GPU Pass

GPU Pass가 한쪽 Snapshot에서만 감지된 경우 누락된 쪽의 시간은 `0 ms`로 계산됩니다.

반드시 `Presence` 또는 JSON의 `presence` 필드를 함께 확인해야 합니다.

- `Both`: 양쪽 모두 감지
- `Baseline only`: Baseline에서만 감지
- `Candidate only`: Candidate에서만 감지

예를 들어 `Candidate only`이면서 `+0.5 ms`라면 기존 Pass가 단순히 느려진 것이 아니라 Candidate에서 새 Pass가 등장했을 가능성이 큽니다.

## 측정값 의미

- `Average`: 캡처 구간의 평균 GPU 시간
- `Min`: 캡처 구간에서 가장 낮은 GPU 시간
- `Max`: 캡처 구간에서 가장 높은 GPU 시간
- `Total GPU Frame`: 전체 GPU Frame 시간
- `GPU Pass`: `stat gpu`에서 수집된 개별 렌더링 Pass 또는 GPU Queue 항목

평균값만 보지 말고 Min/Max 범위도 확인해야 합니다. Max가 평균보다 크게 높으면 간헐적인 Spike가 있었을 수 있습니다.

## 캡처 모드

- `Fixed Frames`: 기존 `Capture Baseline/Candidate`로 지정 프레임을 측정
- `Manual Stop`: `Start` 후 사용자가 `Stop`을 누를 때까지 측정
- `Auto Stop`: `Target Frames`에 도달하면 자동 종료

모든 모드에서 워밍업 프레임은 실제 `Sample Frames`에 포함되지 않습니다.

서로 비교할 때는 가능하면 다음 조건을 맞춥니다.

- 동일한 카메라와 장면
- 동일하거나 비슷한 측정 프레임 수
- 동일한 플레이 동선
- 동일한 해상도
- Dynamic Resolution과 VSync 조건 고정
- Shader Compilation과 Texture Streaming이 안정된 이후 측정

## Highlight 기준

보고서의 `Highlight threshold`는 플러그인 UI의 `Highlight >= (ms)` 값입니다.

- 절대 시간 차이가 기준 이상이면 `Highlighted = Yes`
- JSON에서는 `exceeds_highlight_threshold = true`

Highlight는 검토 우선순위 기준일 뿐이며, 작은 변화가 항상 무의미하다는 뜻은 아닙니다.

## JSON 주요 구조

```text
schema_version
report_type
generated_at
highlight_threshold_ms
environment
analysis_instructions
baseline
candidate
changed_cvars
gpu_comparison
```

각 Snapshot의 주요 필드는 다음과 같습니다.

```text
label
captured_at
capture_mode
sample_frames
target_frames
total_gpu_frame
gpu_passes
cvars
```

각 비교 행의 주요 필드는 다음과 같습니다.

```text
id
display_name
presence
baseline
candidate
delta_ms
change_percent
exceeds_highlight_threshold
```

## AI 권장 분석 순서

1. Total GPU Frame 평균 차이로 전체 성능 방향을 판단합니다.
2. Total의 Min/Max 범위와 측정 프레임 수를 확인해 결과 안정성을 평가합니다.
3. 변경된 CVar를 확인합니다.
4. 절대 `delta_ms`가 큰 GPU Pass부터 원인을 추정합니다.
5. 한쪽에만 존재하는 Pass를 별도로 분류합니다.
6. CVar 변경과 Pass 변화를 연결하되, 인과관계가 확정된 것처럼 표현하지 않습니다.
7. 추가로 검증할 CVar 또는 캡처 조건을 제안합니다.
8. 기대 효과와 위험도를 기준으로 최적화 우선순위를 정합니다.

## AI에게 요청할 출력 형식

AI의 답변은 다음 순서를 권장합니다.

1. 결론 요약
2. Total GPU Frame 변화
3. 주요 성능 악화 Pass
4. 주요 성능 개선 Pass
5. 변경된 CVar와 연관 가능성
6. 신뢰도와 측정상 주의점
7. 추가 검증 절차
8. 최적화 우선순위

## 권장 프롬프트

아래 문장을 AI에 그대로 전달할 수 있습니다.

```text
AI_REPORT_GUIDE.md의 계산 규칙과 분석 순서를 먼저 읽어주세요.
그다음 첨부한 GPUProfile 보고서의 Baseline과 Candidate를 비교해 주세요.

다음을 포함해서 답변해 주세요.
1. Total GPU Frame의 절대 변화와 변화율
2. GPU 시간이 가장 크게 증가한 Pass와 감소한 Pass
3. 한쪽 캡처에서만 나타난 Pass
4. 변경된 CVar와 성능 변화의 연관 가능성
5. 결과의 신뢰도를 낮출 수 있는 변동 또는 측정 조건
6. 원인을 검증하기 위한 다음 캡처 방법
7. 효과가 클 것으로 예상되는 최적화 항목의 우선순위

단순한 상관관계를 확정된 원인으로 표현하지 말고, 각 추정의 신뢰도를 함께 표시해 주세요.
```

## 해석 시 주의사항

- Editor 측정은 Development 또는 Shipping 실행 파일과 비용 구조가 다를 수 있습니다.
- `stat gpu` Pass 이름과 계층은 렌더링 경로와 프레임 상태에 따라 달라질 수 있습니다.
- 비동기 Compute와 Graphics Queue 시간은 단순 합산하면 실제 Total GPU Frame과 일치하지 않을 수 있습니다.
- 첫 캡처에는 Shader Compilation, PSO 생성 또는 Texture Streaming Spike가 포함될 수 있습니다.
- Candidate에서 새 Pass가 나타났다면 기존 Pass의 비용 증가와 구분해서 분석해야 합니다.
- 한 번의 결과만으로 결론을 확정하지 말고 같은 조건에서 여러 번 측정하는 것이 좋습니다.
