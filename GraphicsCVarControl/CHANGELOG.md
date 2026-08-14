## 2.3.0
- 현재 CVar 값과 일치하는 옵션을 초록색 패널로 강조하고, 프리셋 외 값·미적용·조회 실패 상태를 서로 다른 색상으로 구분하는 시각적 상태 표시 추가
- `Tools > Rendering Debug Views`에서 Lit, Wireframe, Shader Complexity, Quad Overdraw, Light Complexity 및 Lumen Visualization을 전환하고 현재 활성 모드와 적용 결과를 색상으로 확인하는 기능 추가

## 2.2.1
- Stat GPU와 ProfileGPU 창에서 공통 `Reports` 폴더를 Windows Explorer로 바로 여는 버튼 추가
- `PolygonCounter`로 인해 플러그인 빌드가 실패하던 오류 수정

## 2.2.0
- Single Layer Water의 Virtual Shadow Map 셰이더 지원 및 필터링을 0/1로 제어하는 CVar 옵션 추가

## 2.1.0
- 각 CVar 옵션 버튼 위에 현재값과 일치하는 프리셋을 나타내는 초록색 선택 표시 추가
- 문자열 및 숫자형 CVar 값을 비교하여 `1`, `1.0`처럼 표기만 다른 값도 동일한 프리셋으로 인식
- AA 방식, Lumen GI/Reflection, Virtual Shadow Map, Volumetric Fog의 상위 설정에 따라 관련 하위 옵션을 자동 비활성화하고 반투명 표시
- 현재 CVar 상태를 프리셋 일치, 프리셋 외 값, 상위 옵션으로 인한 미적용, 조회 실패로 구분하여 색상과 함께 표시
- 종속 옵션 툴팁에 활성화에 필요한 상위 CVar 조건 표시

## 2.0.0
- `Tools > GPU Profile Helper` 독립 창과 ProfileGPU Baseline/Candidate 캡처 기능 추가
- 단일 촬영과 Multi Capture 모드, 촬영 횟수 및 간격 설정, 캡처 취소와 결과 초기화 기능 추가
- ProfileGPU 실행 중 필요한 CVar를 임시 적용하고 캡처 완료 또는 취소 시 기존 값으로 복구하도록 구성
- Graphics, Compute, Copy Queue와 Pass 계층을 파싱하고 Exclusive/Inclusive 시간 및 Draw/Dispatch 정보를 수집
- Queue 및 Pass별 Median, Average, Range, Seen 통계와 Baseline/Candidate 변화량 비교 UI 추가
- 누락된 Pass는 유효한 캡처 표본 안에서 `0 ms`로 계산하고 악화는 빨간색, 개선은 초록색으로 표시
- 캡처 메모와 관련 에셋·World Outliner Actor 등록, 드래그 앤 드롭, 선택 추가, 개별 제거 및 초기화 기능 추가
- 에셋별 `Quantity` 입력과 실제 Actor 인스턴스의 중복 수량 계산 방지 기능 추가
- Blueprint/Actor 컴포넌트, Mesh, Material, Light, SceneCapture 및 주요 렌더링 플래그 분석 기능 추가
- Niagara System의 Emitter 활성 상태, CPU/GPU Sim, Bounds, Renderer, Material과 반투명 여부 상세 분석 기능 추가
- Actor와 원본 Blueprint, Niagara System, Static Mesh, Skeletal Mesh의 참조 관계를 Report에 연결하고 동일 렌더링 대상의 이중 집계 방지
- 관련 대상의 추가, 제거, 수량 변화, 내부 분석 변화 및 연결된 인스턴스를 자동 비교하는 `Capture Context Changes` 추가
- ProfileGPU 비교와 에셋 분석을 함께 제공하는 AI용 Markdown/JSON Report 출력 기능 추가
- Markdown은 Median 절대 변화량 상위 50개만 표시하고 JSON은 전체 Pass와 원본 샘플 배열을 유지하도록 구성
- Stat GPU AI Report, Spike Log, ProfileGPU Report를 각각 `Reports/StatGPU`, `Reports/SpikeLogs`, `Reports/ProfileGPU` 폴더로 분리
- Niagara 상세 분석을 위해 플러그인 및 Editor 모듈에 Niagara 의존성 추가

## 1.9.0
- 연속 Baseline/Candidate 기록 중 Total GPU의 이동 중앙값과 프레임 예산을 기준으로 순간 부하를 감지하는 `Spike Tracking` 기능 추가
- Frame Budget, Delta Threshold, Rolling Frames 설정과 스파이크 전 30프레임/후 60프레임 보존 기능 추가
- Total GPU 그래프에 스파이크 Peak를 빨간 마커로 표시하고 사건별 상위 GPU Pass 변화량을 별도 영역에 표시
- 연속된 스파이크를 하나의 사건으로 묶고 긴 사건에서도 Peak와 마지막 후속 프레임이 잘리지 않도록 전체 Total GPU 구간 저장
- `Export Spike Log` 버튼으로 스파이크 사건 전용 `GPUSpikeLog_*.md/.json` 파일 생성
- AI Report와 Spike Log에 동일 Snapshot을 식별하는 `Capture ID`와 Pass 표본 유효률 추가
- 전체 GPU Pass Snapshot이 비거나 `Queue Total`이 없는 표본은 모든 Pass가 `0 ms`인 것으로 계산하지 않고 무효 처리
- Total GPU Peak 기준 ±5프레임에서 가장 가까운 유효 Pass 표본을 연결하고 실제 사용 프레임과 정렬 오차 기록
- 유효 Snapshot 내부에서 감지되지 않은 Pass는 `0 ms`로 계산하여 간헐적 Pass의 구간 평균을 실제 측정 프레임 기준으로 계산
- 스파이크 Pass를 증가 항목 우선으로 정렬하고 반복 발생 횟수, 평균 증가량, 최대 증가량을 집계
- 보고서 JSON Schema를 3으로 갱신하고 `stat gpu` Pass 값이 20프레임 히스토리 평균임을 신뢰도 정보와 함께 기록

## 1.8.0
- CVar 현재 값 UI가 `FindConsoleVariable()`을 반복 호출하지 않도록 `IConsoleVariable*` 캐시를 추가하여 Console Manager 성능 경고 제거
- PIE/Simulate 시작, Graphics CVar 변경, Preset Load 시 0초부터 시작하는 30초 안정화 타이머 추가
- 안정화 타이머를 소수점 셋째 자리까지 표시하고 30초 도달 시 `30.000초`에서 정지하도록 구성
- Snapshot 창에 레벨 시작 또는 CVar 변경 후 약 30초 안정화를 권장하는 빨간색 안내 문구 추가
- 중복되던 `Sample Frames`, `Capture Baseline`, `Capture Candidate` UI를 제거하고 `Start Baseline/Candidate`의 Manual Stop 및 Auto Stop 방식으로 통합
- 모든 사용자 캡처 경로에서 GPU Pass를 프레임마다 누적하여 평균/최소/최대 통계를 계산하도록 측정 방식 통일

## 1.7.0
- `Graphics CVar Control`에 표시 이름, 카테고리, 실제 CVar 명령어를 대상으로 하는 대소문자 무시 부분 검색 기능 추가
- Lumen GI Screen Traces, Mesh SDF Tracing, Trace/Gather Resolution, Adaptive Probes와 Lumen Reflection Screen Traces, Mesh SDF Tracing, Max Roughness 세부 제어 8개 추가
- `Rendering Debug Views` 창을 추가하고 Lit, Wireframe, Shader Complexity, Quad Overdraw, Light Complexity 및 Lumen 시각화 모드를 버튼으로 전환하도록 구성
- Baseline만 캡처한 상태에서도 AI 분석용 Markdown/JSON 보고서를 생성하도록 확장
- Baseline 단독 보고서에 GPU Pass별 평균/최소/최대, 변동 폭, Total GPU 대비 비율, 상위 최적화 후보와 전체 CVar 상태 포함
- Baseline 단독 보고서는 `GPUBaseline_*`, Baseline/Candidate 비교 보고서는 기존 `GPUProfile_*` 파일명으로 구분
- 생성 보고서가 SVN 상태 목록에 나타나지 않도록 `Reports` 관련 `svn:ignore` 속성 추가

## 1.6.0
- 연속 GPU 기록에 `Auto Stop` 토글과 `Target Frames` 설정을 추가하여 목표 프레임 도달 시 Snapshot을 자동 확정
- 수동 종료와 자동 종료 모드를 전환할 수 있도록 구성하고 자동 기록 진행률 및 현재/목표 프레임 표시
- Baseline/Candidate 비교 결과를 AI 분석용 Markdown 및 JSON 보고서로 내보내는 `Export AI Report` 기능 추가
- 보고서에 캡처 모드, 목표 프레임, Total 및 Pass별 평균/최소/최대, 변화량, 변화율, Pass 존재 여부, Highlight 기준, CVar 상태 포함
- AI 분석 규칙, JSON 구조, 권장 분석 순서와 프롬프트를 정리한 `AI_REPORT_GUIDE.md` 추가
- 생성 보고서를 플러그인의 `Reports` 폴더에 자동 저장하고 보고서 파일은 Git 대상에서 제외하도록 구성

## 1.5.0
- Lumen Reflection, Lumen GI, Virtual Shadow Map, Volumetric Fog, Separate Translucency, Static Mesh LOD 및 Foliage 진단용 CVar 8개 추가
- 새 CVar를 기존 Preset 저장 및 Baseline/Candidate Snapshot에 포함하도록 구성
- Preset 1~5 영역을 왼쪽 고정 사이드바로 분리하고 오른쪽 CVar 목록만 스크롤되도록 UI 개선
- 프리셋 사이드바 폭과 `Empty`/`Saved` 상태 및 버튼 영역 간격 조정
- `Anti-Aliasing`, `Resolution`, `Lighting`, `Post Process`, `Geometry`, `Foliage`, `Scalability` 카테고리에 한국어 설명 추가

## 1.4.0
- `Start Baseline`, `Start Candidate`, `Stop` 버튼을 이용한 연속 GPU 기록 기능 추가
- 플레이 구간의 전체 GPU Frame Time을 프레임별로 저장하고 Baseline/Candidate 그래프로 표시
- 전체 GPU Frame 및 GPU Pass별 평균값, 최솟값, 최댓값 기록과 비교 기능 추가
- 장시간 기록 시 전체 샘플은 유지하면서 그래프 출력 점을 화면 폭에 맞게 축약하도록 최적화
- 한쪽 Snapshot에서만 감지된 GPU Pass는 누락된 쪽을 `0 ms`로 계산하여 차이를 표시

## 1.3.0
- `Graphics CVar Control`과 `GPU Snapshot Comparison`을 각각 독립된 에디터 창과 Tools 메뉴 항목으로 분리
- 현재 그래픽 CVar 상태를 포함한 Baseline 및 Candidate Snapshot 캡처 기능 추가
- 설정한 프레임 수 동안 전체 GPU Frame Time과 `stat gpu` Pass별 평균 시간을 수집하도록 구성
- GPU Pass별 Baseline/Candidate 절대 시간 차이와 변화율 비교 기능 추가
- `Queue Total` 항목에 Graphics, Compute 등 GPU Queue 정보를 표시하여 동일한 이름의 항목을 구분
- Baseline과 Candidate Snapshot을 함께 초기화하는 `Clear` 버튼 및 한글 호버 설명 추가
- `Highlight >= (ms)` 기준 이상의 의미 있는 변화에 행 배경 하이라이트 적용
- Difference 값의 성능 개선은 초록색, 성능 악화는 빨간색 글자로 표시

## 1.2.0
- 등록된 모든 그래픽 CVar 항목에 한국어 호버 설명 추가
- 각 옵션 버튼의 툴팁에서 변경되는 CVar 기능과 실제 적용 값을 확인할 수 있도록 수정
- 기존 프리셋 저장, 불러오기 및 CVar 적용 동작을 유지하면서 UI 안내 방식 개선

## 1.1.0
- 그래픽 CVar 값을 프리셋처럼 저장하고 불러올 수 있는 기능 추가
- Preset 1부터 Preset 5까지 슬롯을 제공하도록 수정
- 각 프리셋 슬롯에 Save, Load, Clear 버튼 추가
- 프리셋 데이터는 프로젝트 렌더 설정을 변경하지 않고 에디터 사용자 설정에 저장되도록 구성

## 1.0.0
- Graphics CVar Control 플러그인 최초 추가
- Tools 메뉴에서 여는 Graphics CVar Control 에디터 창 추가
- r.AntiAliasingMethod, r.ScreenPercentage, Lumen, Shadow, Post Process, Scalability 관련 콘솔 변수를 버튼으로 제어하도록 구성
- 콘솔 명령 입력 없이 그래픽 설정을 토글할 수 있도록 구성
