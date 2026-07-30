# Graphics CVar Control AI Report Guide

## 목적

이 문서는 `Graphics CVar Control` 플러그인이 내보낸 GPU Baseline 단독 분석 보고서, Baseline/Candidate 비교 보고서와 Spike Log를 AI가 일관된 기준으로 해석하기 위한 가이드입니다.

AI에게 다음 파일을 함께 제공하는 방식을 권장합니다.

1. 이 `AI_REPORT_GUIDE.md`
2. 측정 후 생성된 `GPUBaseline_*.md` 또는 `GPUProfile_*.md`
3. 동일한 Capture ID의 `GPUSpikeLog_*.md`
4. 세부 데이터나 자동 처리가 필요하면 각 보고서의 `.json`

내보낸 Markdown 보고서에도 핵심 분석 지침이 포함되어 있으므로 해당 파일만 전달해도 기본 분석은 가능합니다.

## 생성 파일

`Export AI Report`를 누르면 플러그인의 `Reports` 폴더에 같은 이름으로 두 파일이 생성됩니다.

- `GPUProfile_YYYYMMDD_HHMMSS_MMM.md`
- `GPUProfile_YYYYMMDD_HHMMSS_MMM.json`
- `GPUBaseline_YYYYMMDD_HHMMSS_MMM.md`
- `GPUBaseline_YYYYMMDD_HHMMSS_MMM.json`
- `GPUSpikeLog_YYYYMMDD_HHMMSS_MMM.md`
- `GPUSpikeLog_YYYYMMDD_HHMMSS_MMM.json`

Candidate가 있으면 `GPUProfile_*`, Baseline만 있으면 `GPUBaseline_*` 보고서가 생성됩니다.

`Export Spike Log`를 누르면 Baseline/Candidate에서 감지된 스파이크 사건만 `GPUSpikeLog_*`로 생성됩니다. AI Report와 Spike Log를 함께 전달할 때는 각 Snapshot의 `Capture ID`가 일치해야 합니다.

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
- Baseline 단독 보고서의 상위 최적화 후보와 Pass별 변동 폭
- 스파이크 사건별 Peak, Pass 정렬 정보와 반복 기여 Pass

일반적인 AI 대화 분석에는 Markdown 파일을 우선 사용합니다.

### Spike Log

Spike Log는 전체 구간 평균과 별도로 순간적인 Total GPU 증가 원인을 조사합니다.

- 스파이크 전후 Total GPU 프레임 범위
- 이동 중앙값과 Peak의 절대 차이
- Peak 기준 ±5프레임에서 연결한 가장 가까운 유효 GPU Pass 표본
- GPU Pass별 이동 평균, Peak 표본과 증가량
- 여러 사건에서 반복적으로 증가한 Pass의 발생 횟수, 평균 증가량과 최대 증가량
- 전체 Pass 표본 시도 수, 유효 표본 수와 유효률

전체 GPU Pass Snapshot이 비거나 `Queue Total`이 없으면 무효 표본입니다. 무효 표본을 모든 Pass가 `0 ms`가 된 것으로 해석하지 않습니다. `has_aligned_pass_sample`이 `false`인 사건은 Pass 원인을 추정하지 않습니다.

Pass 표본은 `stat_history_frames = 20`인 HUD 히스토리 평균입니다. Total GPU의 정확한 Peak와 동일한 단일 프레임 측정값으로 간주하지 말고 `pass_frame_offset` 및 유효률과 함께 신뢰도를 판단합니다.

`Aggregate Positive Spike Contributors`는 반복되는 원인 후보를 찾기 위한 표입니다. 등장 횟수가 많더라도 증가량이 매우 작을 수 있으므로 `Average Increase`와 `Maximum Increase`를 함께 확인합니다.

## Baseline 단독 보고서 해석

Baseline 단독 보고서에는 Candidate와 `delta_ms`가 없습니다. 다음 순서로 병목 후보를 찾습니다.

1. Total GPU Frame의 Average/Min/Max와 측정 프레임 수를 확인합니다.
2. `Top Optimization Starting Points` 또는 JSON의 `optimization_candidates`에서 평균 시간이 큰 Pass를 우선 확인합니다.
3. `Range ms` 또는 `timing_range_ms`가 큰 Pass는 평균 비용과 별도로 Spike 가능성을 점검합니다.
4. `Percent of Total GPU`는 우선순위 참고값으로만 사용합니다. GPU Pass는 중첩되거나 동시에 실행될 수 있으므로 비율을 합산하지 않습니다.
5. CVar 상태와 비용이 큰 Pass를 연결하되, Candidate 검증 전에는 원인이 확정된 것처럼 표현하지 않습니다.

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

- `Manual Stop`: `Start` 후 사용자가 `Stop`을 누를 때까지 측정
- `Auto Stop`: `Target Frames`에 도달하면 자동 종료

두 모드 모두 GPU Pass를 프레임마다 누적하며, 워밍업 프레임은 실제 `Sample Frames`에 포함되지 않습니다.

레벨 시작 또는 CVar 변경 직후에는 Streaming과 Lumen, Shadow 등 렌더링 캐시 상태가 안정되지 않았을 수 있습니다. 가능하면 Snapshot 창의 안정화 타이머가 권장 시간 `30.000초`에 도달한 뒤 캡처합니다. 타이머는 측정을 지연하거나 버튼을 비활성화하지 않는 안내 기능입니다.

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
environment
analysis_instructions
baseline
```

Baseline 단독 JSON에는 다음 필드가 추가됩니다.

```text
optimization_candidates
```

Baseline/Candidate 비교 JSON에는 다음 필드가 추가됩니다.

```text
highlight_threshold_ms
candidate
changed_cvars
gpu_comparison
```

각 Snapshot의 주요 필드는 다음과 같습니다.

```text
capture_id
label
captured_at
capture_mode
sample_frames
target_frames
pass_sample_quality
total_gpu_frame
spike_tracking_settings
spike_events
gpu_passes
cvars
```

Spike Log JSON의 `schema_version`은 `3`이며 Capture별 주요 필드는 다음과 같습니다.

```text
capture_id
label
captured_at
capture_mode
sample_frames
pass_sample_quality
settings
events
aggregate_positive_contributors
```

각 스파이크 사건에는 다음 필드가 포함됩니다.

```text
event_index
start_frame
peak_frame
last_spike_frame
window_start_frame
window_end_frame
has_aligned_pass_sample
pass_sample_frame
pass_frame_offset
rolling_baseline_total_ms
peak_total_ms
delta_total_ms
pass_deltas
frame_samples
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

## Baseline/Candidate 비교 분석 순서

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

Baseline 단독 보고서는 아래 문장을 AI에 그대로 전달할 수 있습니다.

```text
AI_REPORT_GUIDE.md의 Baseline 단독 보고서 해석 규칙을 먼저 읽어주세요.
그다음 첨부한 GPUBaseline 보고서에서 GPU 비용을 줄일 우선순위를 분석해 주세요.

다음을 포함해서 답변해 주세요.
1. Total GPU Frame의 평균과 변동성
2. 평균 GPU 시간이 큰 Pass
3. Min/Max 범위가 커서 Spike가 의심되는 Pass
4. 현재 CVar와 비용이 큰 Pass의 연관 가능성
5. 효과가 클 것으로 예상되는 최적화 실험
6. 각 추정을 검증하기 위한 Candidate 캡처 방법

GPU Pass의 Percent of Total 값은 합산하지 말고, 확정된 원인과 추정을 구분해 주세요.
```

Baseline/Candidate 비교 보고서는 아래 문장을 사용할 수 있습니다.

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

AI Report와 Spike Log를 함께 분석할 때는 아래 문장을 사용할 수 있습니다.

```text
AI_REPORT_GUIDE.md를 먼저 읽고 Capture ID가 같은 AI Report와 Spike Log를 함께 분석해 주세요.

AI Report에서는 전체 구간의 평균 비용이 큰 GPU Pass를 찾고,
Spike Log에서는 순간적으로 반복해서 증가한 Pass와 최대 증가량을 찾아 주세요.

다음을 구분해서 답변해 주세요.
1. 평상시 지속적으로 큰 비용
2. 순간 스파이크에서만 증가하는 비용
3. 두 보고서에서 공통으로 우선순위가 높은 Pass
4. Pass 표본 유효률과 정렬 오차를 고려한 신뢰도
5. 효과가 클 것으로 예상되는 최적화 및 검증 순서

GPU Pass와 Queue 시간은 합산하지 말고, stat history 평균과 정확한 Total GPU Peak를 같은 단일 프레임 값으로 단정하지 마세요.
```

## 해석 시 주의사항

- Editor 측정은 Development 또는 Shipping 실행 파일과 비용 구조가 다를 수 있습니다.
- `stat gpu` Pass 이름과 계층은 렌더링 경로와 프레임 상태에 따라 달라질 수 있습니다.
- 비동기 Compute와 Graphics Queue 시간은 단순 합산하면 실제 Total GPU Frame과 일치하지 않을 수 있습니다.
- 첫 캡처에는 Shader Compilation, PSO 생성 또는 Texture Streaming Spike가 포함될 수 있습니다.
- Candidate에서 새 Pass가 나타났다면 기존 Pass의 비용 증가와 구분해서 분석해야 합니다.
- 한 번의 결과만으로 결론을 확정하지 말고 같은 조건에서 여러 번 측정하는 것이 좋습니다.
