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
