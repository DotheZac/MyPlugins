# Camera Focus FX

`CameraFocusFX`는 매니저 액터 없이 줌 거리에 따라 비네트와 Depth of Field 집중 효과를 적용하는 경량 Runtime 플러그인입니다.

## 설정

1. `CameraComponent`와 `SpringArmComponent`를 소유한 Pawn 또는 Actor 블루프린트에 `Camera Focus FX` 컴포넌트를 추가합니다.
2. `Use Automatic Zoom`을 활성화합니다.
3. 카메라 줌 범위에 맞춰 `Effect Start Arm Length`와 `Full Effect Arm Length`를 조절합니다.

컴포넌트는 소유자의 첫 번째 Camera와 SpringArm을 자동으로 찾습니다. 기본적으로 로컬 조작 Pawn에만 효과를 적용하며, 효과가 해제되면 원래 Post Process 설정을 복원합니다.

## ProjectP 기본값

- `Effect Start Arm Length`: `1200`
- `Full Effect Arm Length`: `600`
- `Max Vignette Intensity`: `0.65`
- `Focus FStop`: `1.4`

연출에서 직접 강도를 제어하려면 `Set Manual Focus Alpha`에 `0~1` 값을 전달합니다. 이후 `Clear Manual Focus Alpha`를 호출하면 자동 줌 판정으로 돌아갑니다.

## Data Asset 프리셋

1. 콘텐츠 브라우저에서 새 `Data Asset` 생성을 선택합니다.
2. Data Asset Class로 `CameraFocusFXPreset`을 선택합니다.
3. 생성한 프리셋의 줌, 비네트, DOF 값을 조절합니다.
4. 캐릭터의 `Camera Focus FX` 컴포넌트에서 `Focus Preset`에 에셋을 지정합니다.

`Apply Preset On Begin Play`가 활성화되어 있으면 시작 시 자동 적용됩니다. 실행 중에는 `Apply Preset` 블루프린트 함수로 다른 프리셋을 즉시 적용할 수 있습니다.
