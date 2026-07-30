# Graphics CVar Control

Unreal Engine 5.7 에디터에서 그래픽 CVar를 제어하고, Baseline/Candidate GPU 성능을 비교하며, 결과를 AI 분석용 보고서로 내보내는 Editor 전용 플러그인입니다.

- 버전: `1.7.0`
- 제작자: `DotheZac`
- 모듈: `GraphicsCVarControlEditor`
- 지원 환경: Unreal Engine 5.7 Editor

## 주요 기능

- 자주 사용하는 그래픽 CVar를 버튼으로 변경
- 표시 이름, 카테고리, 실제 CVar 명령어로 설정 검색
- CVar 조합을 Preset 1~5에 저장하고 다시 적용
- CVar 제어 창과 GPU 비교 창을 독립된 탭으로 제공
- Baseline/Candidate GPU Snapshot 캡처
- 고정 프레임, 수동 종료, 목표 프레임 자동 종료 지원
- Total GPU Frame의 프레임별 그래프 표시
- Total 및 GPU Pass별 평균, 최솟값, 최댓값 기록
- 절대 시간 차이와 변화율 비교
- 의미 있는 변화 하이라이트 및 개선/악화 색상 표시
- Shader Complexity, Wireframe, Lumen 등 Rendering Debug View 전환
- Baseline 단독 또는 Baseline/Candidate 비교용 Markdown/JSON AI 보고서 생성

## 에디터 창 열기

Unreal Editor의 상단 메뉴에서 다음 항목을 선택합니다.

- `Tools > Graphics CVar Control`
- `Tools > GPU Snapshot Comparison`
- `Tools > Rendering Debug Views`

`Graphics CVar Control`은 CVar와 Preset을 관리하고, `GPU Snapshot Comparison`은 GPU 성능을 측정하고 비교합니다. `Rendering Debug Views`에서는 Viewport의 렌더링 시각화 모드를 버튼으로 전환합니다.

## Graphics CVar Control

### 화면 구성

- 왼쪽 고정 영역: Preset 1~5
- 오른쪽 스크롤 영역: 카테고리별 CVar

CVar 목록을 아래로 스크롤해도 Preset의 `Save`, `Load`, `Clear` 버튼은 항상 표시됩니다.

오른쪽 설정 영역 상단의 검색창은 표시 이름, 카테고리, 실제 CVar 명령어를 대소문자 구분 없이 부분 검색합니다. 일치하지 않는 항목과 비어 있는 카테고리는 숨겨집니다.

### 카테고리

- `Anti-Aliasing`
  - AA 방식과 품질
- `Resolution`
  - 내부 렌더링 해상도와 표시 거리
- `Lighting`
  - Lumen, Virtual Shadow Map, 그림자, Ambient Occlusion
- `Post Process`
  - Motion Blur, Bloom, SSR, Depth of Field, Volumetric Fog, Translucency
- `Geometry`
  - Nanite와 Static Mesh LOD
- `Foliage`
  - Foliage 렌더링 밀도
- `Scalability`
  - Unreal 품질 설정 묶음

각 CVar와 옵션 버튼에 마우스를 올리면 한국어 설명과 실제 적용 값이 표시됩니다.

### 주요 CVar

기본 렌더링 설정:

- `r.AntiAliasingMethod`
- `r.TemporalAA.Quality`
- `r.FXAA.Quality`
- `r.ScreenPercentage`
- `r.ViewDistanceScale`
- `r.Lumen.DiffuseIndirect.Allow`
- `r.Lumen.Reflections.Allow`
- `r.ShadowQuality`
- `r.Shadow.Virtual.Enable`
- `r.AmbientOcclusionLevels`
- `r.MotionBlurQuality`
- `r.BloomQuality`
- `r.SSR.Quality`
- `r.DepthOfFieldQuality`
- `r.VolumetricFog`
- `r.Nanite`

GPU 병목 진단용 설정:

- `r.Lumen.Reflections.DownsampleFactor`
- `r.Lumen.ScreenProbeGather.DownsampleFactor`
- `r.Lumen.ScreenProbeGather.ScreenTraces`
- `r.Lumen.ScreenProbeGather.TraceMeshSDFs`
- `r.Lumen.ScreenProbeGather.TracingOctahedronResolution`
- `r.Lumen.ScreenProbeGather.GatherOctahedronResolutionScale`
- `r.Lumen.ScreenProbeGather.NumAdaptiveProbes`
- `r.Lumen.Reflections.ScreenTraces`
- `r.Lumen.Reflections.TraceMeshSDFs`
- `r.Lumen.Reflections.MaxRoughnessToTrace`
- `r.Shadow.Virtual.SMRT.RayCountDirectional`
- `r.Shadow.Virtual.SMRT.RayCountLocal`
- `r.VolumetricFog.GridPixelSize`
- `r.SeparateTranslucencyScreenPercentage`
- `r.StaticMeshLODDistanceScale`
- `foliage.DensityScale`

Scalability 설정:

- `sg.GlobalIlluminationQuality`
- `sg.ReflectionQuality`
- `sg.ShadowQuality`
- `sg.TextureQuality`
- `sg.EffectsQuality`
- `sg.PostProcessQuality`

## Preset

Preset은 `Preset 1`부터 `Preset 5`까지 제공합니다.

- `Save`
  - 플러그인에 등록된 모든 CVar의 현재 값을 저장
- `Load`
  - 저장된 CVar 값을 현재 에디터 세션에 다시 적용
- `Clear`
  - 해당 Preset 삭제

Preset 데이터는 `GEditorPerProjectIni`에 저장됩니다. 에디터를 다시 실행해도 유지되지만 `DefaultEngine.ini`나 프로젝트 렌더 설정을 직접 변경하지는 않습니다.

## GPU Snapshot Comparison

### Snapshot 데이터

각 Snapshot에는 다음 정보가 저장됩니다.

- 캡처 시간과 캡처 모드
- 측정 프레임 수와 목표 프레임 수
- 캡처 당시 관리 대상 CVar 값
- Total GPU Frame의 평균, 최솟값, 최댓값
- Total GPU Frame의 프레임별 샘플
- `stat gpu` GPU Pass별 평균, 최솟값, 최댓값

Snapshot은 에디터 메모리에만 유지됩니다. 에디터를 종료하면 사라지므로 필요한 결과는 AI 보고서로 내보내야 합니다.

### 고정 프레임 캡처

1. `Sample Frames`를 설정합니다.
2. 기준 상태에서 `Capture Baseline`을 누릅니다.
3. CVar 또는 장면 설정을 변경합니다.
4. `Capture Candidate`를 누릅니다.

설정한 프레임 수를 측정하면 자동으로 Snapshot이 확정됩니다.

### 연속 기록

#### Manual Stop

1. `Auto Stop`을 끕니다.
2. `Start Baseline` 또는 `Start Candidate`를 누릅니다.
3. 원하는 플레이 구간을 진행합니다.
4. `Stop`을 누릅니다.

#### Auto Stop

1. `Auto Stop`을 켭니다.
2. `Target Frames`를 `10~36,000` 사이로 설정합니다.
3. `Start Baseline` 또는 `Start Candidate`를 누릅니다.
4. 목표 프레임에 도달하면 자동으로 종료됩니다.

기본 목표값은 `300`프레임입니다. 모든 캡처는 실제 측정 전에 10프레임 워밍업을 수행하며, 워밍업 프레임은 결과의 측정 프레임 수에 포함되지 않습니다.

### 비교 결과

비교표에는 다음 값이 표시됩니다.

- Baseline `Avg / Min / Max`
- Candidate `Avg / Min / Max`
- `Candidate - Baseline` 절대 시간 차이
- Baseline 대비 변화율

시간 차이가 양수이면 Candidate가 느려진 것이므로 빨간색, 음수이면 개선된 것이므로 초록색으로 표시됩니다.

한쪽 Snapshot에서만 감지된 GPU Pass는 누락된 쪽을 `0 ms`로 계산합니다. Snapshot 자체가 없는 경우에는 `--`로 표시합니다.

### Highlight

`Highlight >= (ms)`에서 의미 있는 변화로 판단할 최소 절대 시간 차이를 설정합니다.

기준 이상의 행은 배경색과 굵은 글자로 강조됩니다. 기본값은 `0.2 ms`입니다.

### 그래프

`Total GPU Frame History`에서 Baseline과 Candidate의 Total GPU Frame 변화를 확인할 수 있습니다.

- Baseline: 파란색
- Candidate: 주황색

기록 데이터는 모두 유지하지만 장시간 캡처 시 UI 부하를 줄이기 위해 그래프에 그리는 점은 화면 폭에 맞게 축약됩니다.

### 초기화

`Clear`를 누르면 Baseline과 Candidate Snapshot을 모두 초기화합니다.

## AI 분석 보고서

Baseline을 캡처한 후 `Export AI Report`를 누르면 보고서가 자동 생성됩니다. Candidate도 있으면 기존 비교 보고서를 생성하고, Candidate가 없으면 비용이 큰 GPU Pass를 찾기 위한 Baseline 단독 분석 보고서를 생성합니다.

저장 폴더:

```text
Plugins/GraphicsCVarControl/Reports
```

현재 프로젝트의 절대 경로:

```text
C:\Users\user\Desktop\Pharaoh\Project\Plugins\GraphicsCVarControl\Reports
```

생성 파일:

- `GPUBaseline_YYYYMMDD_HHMMSS_MMM.md`
- `GPUBaseline_YYYYMMDD_HHMMSS_MMM.json`
- `GPUProfile_YYYYMMDD_HHMMSS_MMM.md`
- `GPUProfile_YYYYMMDD_HHMMSS_MMM.json`

보고서 파일은 Git 및 SVN 상태 목록에서 제외됩니다.

### Markdown 보고서

사람이 읽거나 ChatGPT 같은 AI에 직접 전달하기 위한 문서입니다.

- 캡처 환경과 측정 조건
- Total GPU Frame 통계
- 변경된 CVar
- 주요 성능 악화 및 개선 항목
- 전체 GPU Pass 비교
- Highlight 기준
- 전체 관리 CVar 상태
- AI 분석 지침

일반적인 AI 분석에는 Markdown 보고서만 전달해도 충분합니다.

### JSON 보고서

프로그램과 AI API가 구조적으로 처리하기 위한 데이터입니다.

- 여러 보고서 일괄 비교
- 자동 분석 도구
- 자체 AI API 연동
- 후속 시각화와 데이터 가공

### AI 분석 가이드

보고서 필드와 해석 규칙은 [`AI_REPORT_GUIDE.md`](AI_REPORT_GUIDE.md)에 정리되어 있습니다.

최초 분석에서는 다음 파일을 함께 전달하는 방식을 권장합니다.

1. `AI_REPORT_GUIDE.md`
2. `GPUBaseline_*.md` 또는 `GPUProfile_*.md`
3. 필요하면 같은 이름의 `.json`

## 측정 권장 사항

- Baseline과 Candidate에서 동일한 카메라와 플레이 동선을 사용합니다.
- 두 Snapshot의 측정 프레임 수를 가능한 한 동일하게 맞춥니다.
- Dynamic Resolution, VSync, 해상도 조건을 고정합니다.
- Shader Compilation, PSO 생성, Texture Streaming이 안정된 이후 캡처합니다.
- 한 번의 결과로 결론을 확정하지 말고 같은 조건에서 여러 번 측정합니다.
- Editor 결과는 Development 또는 Shipping 실행 파일과 비용 구조가 다를 수 있습니다.
- 최종 성능 판단은 목표 플랫폼의 Development/Shipping 빌드에서도 확인합니다.

## 데이터 저장 범위

| 데이터 | 저장 위치 | 에디터 재실행 후 유지 |
|---|---|---|
| Preset 1~5 | `GEditorPerProjectIni` | 유지 |
| Baseline/Candidate Snapshot | 에디터 메모리 | 유지되지 않음 |
| AI 보고서 | `Reports` 폴더 | 유지 |

## 구현 구조

- `GraphicsCVarControl.uplugin`
  - 플러그인 버전, 제작자, Editor 모듈 정의
- `Source/GraphicsCVarControlEditor/GraphicsCVarControlEditor.Build.cs`
  - Slate, RHI, JSON, Projects 등 모듈 의존성
- `Private/GraphicsCVarControlEditor.cpp`
  - Tools 메뉴, 탭, CVar/Preset UI, CVar 검색, GPU 비교 UI
- `Private/GraphicsCVarDebugViews.h/.cpp`
  - Viewport 렌더링 시각화 모드 전환 UI
- `Private/GraphicsCVarProfiler.h/.cpp`
  - GPU 캡처, 연속 기록, 통계 누적, Snapshot 비교
- `Private/GraphicsCVarReportExporter.h/.cpp`
  - Markdown/JSON 보고서 생성과 `Reports` 저장
- `AI_REPORT_GUIDE.md`
  - AI 보고서 해석 규칙과 권장 프롬프트
- `CHANGELOG.md`
  - 버전별 변경사항

## 주의사항

- 이 플러그인은 Editor 전용 모듈이며 Runtime 렌더 패스를 추가하지 않습니다.
- CVar는 현재 에디터 세션에 적용되며 일부 값은 렌더러 상태에 따라 즉시 반영되지 않을 수 있습니다.
- GPU Pass 이름과 구성은 렌더링 경로와 프레임 상태에 따라 달라질 수 있습니다.
- Graphics Queue와 Async Compute Queue 시간은 단순 합산하면 Total GPU Frame과 일치하지 않을 수 있습니다.
- `Binaries`와 `Intermediate`는 소스 관리 대상이 아닙니다.

## 변경 이력

자세한 버전별 변경 내용은 [`CHANGELOG.md`](CHANGELOG.md)를 참고합니다.
