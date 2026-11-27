# Raspberry Pi LED Matrix Clock

라즈베리 파이와 RGB LED 매트릭스를 사용한 디지털 시계 프로젝트입니다.  
96x16 (32x16 패널 3개 연결) 크기의 도트 매트릭스에 날짜, 요일, 시간과 함께  
한밭대학교 정류장(대전역동광장 방향)에 오는 버스의 도착 정보를 표시합니다.

![LED Clock](https://img.shields.io/badge/Raspberry_Pi-LED_Matrix-red?style=flat-square&logo=raspberrypi)
![License](https://img.shields.io/badge/License-GPL--2.0-blue?style=flat-square)

---

## 목차

- [기능](#기능)
- [하드웨어 요구사항](#하드웨어-요구사항)
- [프로젝트 구조](#프로젝트-구조)
- [설치 및 컴파일](#설치-및-컴파일)
- [실행](#실행)
- [버스 도착 정보](#버스-도착-정보)
- [하드웨어 최적화](#하드웨어-최적화)
- [트러블슈팅](#트러블슈팅)
- [기술 설명](#기술-설명)
- [라이선스 및 출처](#라이선스-및-출처)

---

## 기능

- **날짜 표시**: `MM-DD` 형식 (예: `01-05`)
- **요일 표시**: 영문 약어 대문자 (예: `SUN`, `MON`)
- **시간 표시**: `HH:MM:SS` 형식의 실시간 시계
- **요일별 색상**: 날짜/요일은 평일 초록, 토요일 파랑, 일요일 빨강 / 시간은 흰색
- **버스 도착 정보**: 정류장에 오는 모든 노선의 남은 시간과 곧 도착(깜빡임) 표시
  - `./clockmode` 명령으로 표시 방식 4가지 중 선택 (sudo·재시작 없이 약 1초 안에 반영)
  - 2안·3안은 켜자마자 그 화면으로 시작합니다(첫 정보를 받기 전에는 `...`, 버스가 없으면 `--`)

---

## 하드웨어 요구사항

| 항목 | 사양 |
|------|------|
| **본체** | Raspberry Pi 4/5 |
| **매트릭스** | 32x16 RGB LED 패널 × 3개 (직렬 연결) |
| **해상도** | 96×16 픽셀 |
| **전원** | 외부 5V 전원 장치 (6A 이상 권장) |

> **주의**: 96x16 매트릭스는 최대 구동 시 약 6A 이상의 전류를 소모합니다.  
> 외부 5V 전원 장치를 사용하고, 파이와 GND를 공통으로 연결하세요.

---

## 프로젝트 구조

```
clock-rpi/
├── clock.cpp          # 메인 시계 프로그램 (초기화와 화면 갱신 루프)
├── clockmode          # 표시 모드 전환 명령 (display.conf 에 저장)
├── settings.cpp/.h    # display.conf 읽기 (1초마다 변경 확인)
├── screen.cpp/.h      # 모드에 맞는 화면 선택
├── screen_alternate.cpp  # 1안: 시계 <-> 전체 버스 화면
├── screen_rotate.cpp     # 2안: 오른쪽 버스 정보 노선별 순환
├── screen_fixed.cpp      # 3안: 한 노선 고정
├── layout_common.cpp/.h  # 공통 그리기 (날짜·요일·시간, 색, 오른쪽 버스 영역)
├── bus.cpp/.h         # 버스 정보 백그라운드 스레드와 표시 상태 계산
├── busfetch.py        # 버스 API 호출 헬퍼 (Python 표준 라이브러리만 사용)
├── bus.conf.example   # 버스 설정 예시 (bus.conf 로 복사해 인증키 입력)
├── preview.cpp        # 개발용: LED 없이 모든 모드의 화면을 텍스트로 출력
├── tests/             # 테스트, 실제 API 응답 샘플, 화면 전환 타임라인 기대값
├── Makefile           # 빌드 설정 파일
├── COPYING            # GPL-2.0 라이선스 전문
├── include/           # rpi-rgb-led-matrix 헤더 파일
├── lib/               # 컴파일된 라이브러리 파일 (.a)
└── fonts/             # BDF 폰트 파일
    ├── 5x8.bdf        # 날짜/요일용 작은 폰트
    └── 8x13B.bdf      # 시간용 큰 폰트 (Bold)
```

---

## 설치 및 컴파일

### 1. 저장소 클론

```bash
git clone https://github.com/Ksyear/clock-rpi.git
cd clock-rpi
```

### 2. 컴파일

#### Makefile 사용 (권장)

```bash
cd lib
make

cd ..
make
```

#### 직접 컴파일

```bash
g++ -I./include -O3 -std=c++17 clock.cpp bus.cpp settings.cpp layout_common.cpp \
    screen.cpp screen_alternate.cpp screen_rotate.cpp screen_fixed.cpp \
    -o clock -L./lib -lrgbmatrix -lrt -lm -lpthread
```

#### 컴파일 옵션 설명

| 옵션 | 설명 |
|------|------|
| `-I./include` | 라이브러리 헤더 파일 위치 지정 |
| `-O3` | 최적화 레벨 3 적용 (실행 속도 향상) |
| `-o clock` | 출력 실행 파일 이름을 `clock`으로 지정 |
| `-L./lib` | 컴파일된 라이브러리 파일(`.a`) 위치 지정 |
| `-lrgbmatrix` | RGB 매트릭스 라이브러리 링크 |
| `-lrt -lm -lpthread` | 실시간, 수학, 쓰레드 라이브러리 링크 |

#### 빌드 정리

```bash
make clean
```

---

## 실행

```bash
sudo ./clock --led-rows=16 --led-cols=32 --led-chain=3 --led-brightness=50 --led-slowdown-gpio=2
```

### 실행 옵션 설명

| 옵션 | 설명 |
|------|------|
| `--led-rows=16` | 패널 1개의 세로 픽셀 수 |
| `--led-cols=32` | 패널 1개의 가로 픽셀 수 |
| `--led-chain=3` | 직렬로 연결된 총 패널 개수 |
| `--led-brightness=50` | LED 밝기 50% 제한 (전력 소모 방지) |
| `--led-slowdown-gpio=2` | GPIO 속도 조절 (신호 노이즈 해결) |

> **팁**: 밝기를 낮추면 전력 소모를 줄일 수 있습니다.

---

## 버스 도착 정보

### 표시 모드 바꾸기

```bash
./clockmode            # 현재 모드, 시간 설정, 새벽 중지 시간 보기
./clockmode 1          # 1안: 시계 <-> 정류장 전체 버스 화면 번갈아 보기
./clockmode 2          # 2안: 3~15분 안에 오는 노선만 오른쪽에 10초씩 순환 (기본)
./clockmode 3          # 3안: 오른쪽에 102번만 고정
./clockmode 0          # 원래 시계만 (버스 API를 부르지 않음)

./clockmode 2 --min 3 --max 15 --rotate 10   # 2안 기본값 (3~15분, 노선당 10초)
./clockmode 2 --min 0                  # 2안: 가까운 버스도 빼지 않음 (곧 도착은 SOON 깜빡임)
./clockmode 1 --clock 20 --bus 10      # 1안: 시계 20초, 버스 화면 10초
./clockmode 3 --route 104              # 3안: 104번 고정
./clockmode --quiet 01:00-05:00        # 새벽 중지 시간 (off 로 끄기)
./clockmode --defaults                 # 시간/노선/중지 시간을 기본값으로 (모드는 그대로)
./clockmode --help
```

- 설정은 `display.conf`에 저장되고, 시계가 1초마다 확인하므로 **sudo도 재시작도 필요 없이 약 1초 안에** 바뀝니다.
- `display.conf`가 없거나 값이 잘못되면 기본값으로 동작합니다.
- 이름으로도 쓸 수 있습니다: `off`, `alternate`, `rotate`, `fixed`.

| 모드 | 화면 | 기본값 (바꿀 수 있는 범위) |
|------|------|------|
| `0` off | 원래 시계 `HH:MM:SS` | - |
| `1` alternate | 원래 시계 ↔ 전체 버스 화면 | 시계 15초 (5~300), 버스 화면 한 쪽 8초 (2~30) |
| `2` rotate | 날짜 \| `HH:MM` \| 3~15분 안에 오는 노선 순환 | 최대 15분 (5~99), 최소 3분 (0~10), 노선당 10초 (2~60) |
| `3` fixed | 날짜 \| `HH:MM` \| 한 노선 고정 | 노선 102 |
| 공통 | 새벽 중지 시간 | 01:00~05:00 (최대 12시간, `off` 가능) |

### 1안: 시계 ↔ 전체 버스 화면

```
   11 58     102  7     104 14      <- 하늘색: 노선 번호, 숫자: 남은 분
  113  8     117 25     212 20
```

- 시계를 15초 보여 준 뒤 버스 화면을 8초 보여 주고 다시 시계로 돌아갑니다.
- 한 쪽에 6개 노선(2줄 × 3칸)이 노선 번호순으로 들어갑니다. 7개 이상이면 두 쪽으로 나뉘고 오른쪽 끝에 쪽 표시 점이 생깁니다.
- 숫자 색: **주황색**은 달리고 있는 버스, **연보라색**은 아직 기점에서 출발을 기다리는 버스(시간표 기준 추정)입니다.
- 숫자가 **깜빡이면 곧 도착**(1정류장 이내 또는 1분 미만)입니다. 데이터가 오래되면 숫자가 어두워집니다.
- `./clockmode 1`을 실행하면 버스 화면부터 바로 보여 줍니다.

### 2안: 3~15분 안에 오는 노선만 순환 (기본)

```
 MM-DD |          | 102     <- 3~15분 안에 오는 노선만 10초씩 번호순으로 바뀜
  DAY  |  HH:MM   | 8M    <- 남은 시간
```

- 화면 배치는 글꼴 크기로 계산합니다: 양쪽 끝 3칸 여백, `HH:MM`(큰 글꼴) 앞뒤 빈 칸이 같도록
  `여백 3 | 날짜 3~25열 | 3 | 시계 29~67열 | 3 | 버스 아이콘+노선 71~92열 | 여백 3`.
  요일과 남은 시간은 각 칸 안에서 가운데 맞춤하고, 남은 시간은 `8M`처럼 분 단위 `M`으로 표시합니다.
  (큰 글꼴의 `1`은 왼쪽 한 칸이 비어 있어 10~19시에는 날짜와 시계 사이가 한 칸 넓어 보입니다.)
- 15분 이하가 되면 순환에 들어오고, **17분 이상**이 되어야 빠집니다(15분 근처에서 들락거리지 않게).
- **3분 미만으로 남은 버스와 곧 도착 버스는 뺍니다**(지금 나가도 타기 어려움). `--min`으로 바꾸고, `--min 0`이면
  빼지 않고 곧 도착 노선을 `SOON` 깜빡임으로 2배(20초) 동안 보여 줍니다. 최대는 `--max`(예전 이름 `--within`).
- **배차 간격이 짧은 노선은 다음 버스까지 봅니다.** 가장 가까운 버스가 너무 가까우면 그다음 버스를
  **연보라색**으로 보여 줍니다(예: 102번이 곧 도착 → `102 8M`). 다음 버스 = 가장 가까운 버스 + 노선정보의
  배차 간격(102번 평일·토 8분, 일 10분)이며 요일별 막차 이후에는 만들지 않습니다. 배차가 긴 노선(104·113·117·212·11)은
  다음 버스가 15분을 넘으므로 사실상 102번에만 해당합니다. 공휴일은 평일 간격으로 계산합니다.
- 해당 노선이 하나면 바뀌지 않습니다.
- 15분 안에 오는 노선이 없으면 **가장 빨리 오는 노선 하나**(3분 이하 제외)를 바꾸지 않고 보여 줍니다
  (다른 노선이 2분 이상 더 빨라질 때만 바뀜). 그것도 없으면 오른쪽에 `BUS` / `--`(회색)를 표시합니다.

### 3안: 한 노선 고정

```
 MM-DD |          | 102     <- ./clockmode 3 --route 102
  DAY  |  HH:MM   | 3M
```

### 공통 상태 표시

| 상태 | 표시 |
|------|------|
| 달리는 중 | `23M` (1안은 `23`) 주황색, 최대 99 |
| 기점에서 출발 대기 중 | `8M` **연보라색**: 출발 시각까지 + 정류장당 60초. 출발 시각이 지나면 기점→정류장 시간(102번은 2분)에서 멈춤 |
| 다음 버스 추정 (2안, 3안에서 버스가 막 지나갔을 때) | `8M` **연보라색**: 가장 가까운 버스 + 배차 간격. 공식 배차가 실제보다 짧을 수 있어 1~2분 일찍 표시될 수 있음 |
| 곧 도착 (1정류장 이내 또는 1분 미만) | `SOON` 깜빡임 (1안은 숫자 깜빡임) |
| 데이터가 150초 이상 지남 | 남은 시간을 어둡게 표시 |
| 3안에서 버스가 지나갔고 새 정보 대기 중 | `102` / `--` (배차 정보가 있으면 다음 버스 추정) |
| 인증키 오류 / 호출 한도 초과 | 2·3안 `ERR`, 1안 `KEY ERROR` / `DAILY LIMIT` 화면 |
| 켜진 뒤 첫 정보를 받기 전 (약 20초, 시각 동기화·네트워크 대기 포함) | 2안 `BUS` / `...`, 3안 `102` / `...` (회색). 1안은 시계부터 |
| 운행 종료, 인증키 없음, 300초 넘게 갱신 실패 | 2안 `BUS` / `--`, 3안 `102` / `--` (회색), 1안은 원래 시계 |
| 새벽 중지 시간 (기본 01:00~05:00) | 모든 안에서 원래 시계 화면 (`HH:MM:SS`) |

> **출발 대기 버스**: 대전시 API는 기점에서 기다리는 버스(`MSG_TP=07`)의 "남은 초" 자리에
> **출발 예정 시각(HHMM)** 을 보냅니다(`1519` = 15:19 출발). 이를 남은 시간으로 바꿔 표시합니다.
> 정류장당 시간은 `bus.conf`의 `WAIT_STOP_SECS`(기본 60초)로 조정할 수 있습니다.

### 설정

1. 공공데이터포털(data.go.kr)에서 아래 API를 활용신청하고 **일반 인증키**를 확인합니다.
   - [대전광역시_정류소별 도착정보 조회](https://www.data.go.kr/data/15157920/openapi.do) (기본)
   - [국토교통부_(TAGO)_버스도착정보](https://www.data.go.kr/data/15098530/openapi.do) (예비)
   - [대전광역시_노선정보조회](https://www.data.go.kr/data/15157896/openapi.do) (배차 간격 → 다음 버스 추정, 없으면 이 기능만 빠짐)
2. 프로젝트 폴더에 `bus.conf`를 만들고 인증키를 적습니다.

```bash
cp bus.conf.example bus.conf
vi bus.conf          # SERVICE_KEY= 뒤에 인증키 입력
chmod 644 bus.conf
```

- `bus.conf`가 생기면 **30초 안에** 버스 정보를 받기 시작합니다. 재시작은 필요 없습니다.
- `bus.conf`를 지우면 버스 정보가 사라집니다(2·3안은 `--`, 원래 시계만 보려면 `./clockmode 0`).
- `bus.conf`는 `.gitignore`에 들어 있습니다(편집기 백업 `bus.conf.*`, `.bus.conf.swp` 포함).
  **인증키가 들어 있으니 커밋하지 마세요.** `make check`가 저장소에 키 모양 문자열이 없는지 검사합니다.

### 동작 방식

- 시계 프로그램의 백그라운드 스레드가 약 90초마다 `python3 -I - < ./busfetch.py`를 실행합니다.
  화면 갱신 루프는 네트워크를 기다리지 않으므로 시계가 멈추지 않습니다.
- 두 호출 사이에는 남은 시간을 시계 안에서 직접 줄여 가며 표시합니다.
- **새벽 중지 시간(기본 01:00~05:00)에는 API를 한 번도 부르지 않습니다.** 시작 30초 전부터 새 호출을 멈추고,
  화면은 바로 원래 시계가 되며, 끝나는 시각에 즉시 다시 받아 옵니다.
  (212번 막차가 00:30 전후에 지날 수 있고, 102번 첫차는 05:40 출발이라 이렇게 잡았습니다.)
- 부팅 직후 시각 동기화(NTP) 전에는 호출하지 않습니다(라즈베리파이는 RTC가 없어 정전 후 시각이 틀릴 수 있음).
  5분이 지나도 동기화되지 않으면 시스템 시각으로 진행합니다.
- 프로그램이 시작되고 **20초 뒤에 첫 호출**을 합니다. 프로그램이 반복해서 죽었다 살아나도 하루 호출이 한도를 넘지 않습니다.
- 운행하는 버스가 없으면 5분, 실패하면 15초 → 30초 → 60초 간격으로 다시 시도합니다.
  정상 사용은 하루 약 800회이고, 버그에 대비해 **하루 2,000회**에서 멈춥니다(API 한도는 10,000회).
- 처음 실행할 때와 6시간마다(또는 처음 보는 노선이 나오면 30분에 한 번까지) `--routes`로 노선정보조회 API를 함께
  불러 노선별 배차 간격과 막차 시각을 받아 둡니다(하루 수십 회, 실패하면 이전 값을 계속 사용).
- 대전시 BIS API를 먼저 쓰고, 실패할 때만 TAGO API를 씁니다.
  TAGO는 대전시 데이터를 늦게 받아 곧 도착하는 버스가 빠지는 경우가 있었고, 출발 대기 여부도 알려 주지 않습니다.
- 정류장: 한밭대학교 (정류소번호 `41690`, 대전시 `8002920`, TAGO `DJB8002920`),
  대전역동광장 방향입니다. API 한 번에 정류장의 모든 노선이 오므로, 모드와 관계없이 호출 횟수는 같습니다.
- 모드 `0`(off)일 때는 버스 API를 부르지 않습니다.
- `clock`은 시작 후 `daemon` 사용자로 권한을 낮춥니다. `daemon`은 홈 폴더를 지나갈 수 없으므로
  `busfetch.py`, `bus.conf`, `display.conf`는 **작업 폴더 기준 상대 경로**로 읽습니다.
  (`WorkingDirectory`가 프로젝트 폴더여야 합니다.)
  `python3 ./busfetch.py`처럼 파일 이름으로 실행하면 Python이 절대 경로로 파일을 열어
  `Permission denied`가 나므로, 스크립트를 표준 입력으로 넘깁니다.

### 보안

- 인증키는 `bus.conf`에만 있고, HTTPS로만 보냅니다. 리다이렉트·프록시·http 주소는 따르지 않습니다.
- 헬퍼는 `daemon` 권한으로만 실행됩니다. `--led-no-drop-privs` 등으로 root인 채 실행되면 헬퍼를 돌리지 않습니다.
- 로그에는 상태와 예외 이름만 남고, 인증키·요청 주소·응답 내용은 남지 않습니다. 코어 덤프도 만들지 않습니다.
- 응답은 256KB까지만 읽고, DTD/ENTITY가 들어 있는 XML은 거부합니다.
- `clockmode`는 임시 파일을 예측할 수 없는 이름으로 만든 뒤 원자적으로 교체합니다.
- **인증키가 대화나 메시지 등 다른 곳에 노출되었다면 재발급하세요.** 공공데이터포털 마이페이지에서
  재발급한 뒤 `bus.conf`의 `SERVICE_KEY`만 바꾸면 됩니다(재시작 불필요).

### 선택 작업 (sudo 필요, 서로 독립)

설정 폴더 `/etc/systemd/system/ledclock.service.d/`에 파일을 하나씩 추가하는 방식이라,
되돌릴 때는 해당 파일을 지우고 `sudo systemctl daemon-reload && sudo systemctl restart ledclock` 하면 됩니다.

**1. 재시작 정책 (권장)**: 프로그램이 반복해서 죽어도 포기하지 않고 5초 간격으로 다시 시작합니다.

```bash
sudo mkdir -p /etc/systemd/system/ledclock.service.d
printf '[Unit]\nStartLimitIntervalSec=0\n[Service]\nRestartSec=5\n' \
  | sudo tee /etc/systemd/system/ledclock.service.d/10-restart.conf
sudo systemctl daemon-reload && sudo systemctl restart ledclock
systemctl show ledclock -p RestartUSec -p StartLimitIntervalUSec   # 5s / 0 이면 적용됨
```

**2. systemd 보안 옵션**: 헬퍼가 공격받더라도 피해 범위를 줄입니다.

```bash
printf '[Service]\nNoNewPrivileges=yes\nProtectSystem=full\nProtectHome=read-only\nPrivateTmp=yes\nProtectControlGroups=yes\nLimitCORE=0\n' \
  | sudo tee /etc/systemd/system/ledclock.service.d/20-hardening.conf
sudo systemctl daemon-reload && sudo systemctl restart ledclock
journalctl -u ledclock -b -n 40      # 오류 없이 [bus] ok 가 나오는지, 화면이 깜빡이지 않는지 확인
```

`PrivateDevices`, `ProtectClock`, `ProtectKernelTunables`, `RestrictRealtime`, `ProtectHome=yes`는
LED 제어(`/dev/mem`, 실시간 스레드)나 작업 폴더를 막으므로 **쓰지 마세요.**

**3. bus.conf 권한 강화**: 다른 사용자가 읽지 못하게 합니다.

```bash
sudo chgrp daemon bus.conf && chmod 640 bus.conf   # 되돌리기: chmod 644 bus.conf
```

### 확인과 테스트

```bash
# API 호출 결과 확인 (bus.conf 필요)
python3 -I busfetch.py

# 서비스 로그에서 버스 상태, 모드 변경, 새벽 중지 확인
journalctl -u ledclock -f | grep -E '\[bus\]|\[display\]|\[clock\]'

# 개발용: LED 없이 모든 모드의 화면을 텍스트로 확인 (macOS에서도 빌드 가능)
make preview && ./preview
./preview --timeline                  # 모드별 화면 전환 시점만 보기
python3 -I busfetch.py | ./preview - 2 30   # 실제 데이터를 2안으로 30초 동안

# 테스트 + 모든 프레임이 96x16 안에 들어가는지 + 타임라인 비교 + 저장소 키 검사
make check
```

---

## 하드웨어 최적화

### 1. PWM 간섭 제거

라즈베리 파이의 내장 오디오와 LED 제어용 PWM 타이밍이 충돌하여 화면 깜빡임을 유발할 수 있습니다.

#### 사운드 모듈 블랙리스트 추가

```bash
# 사운드 카드 드라이버를 블랙리스트에 추가
cat <<EOF | sudo tee /etc/modprobe.d/blacklist-rgb-matrix.conf
blacklist snd_bcm2835
EOF

# 초기 램 디스크 이미지 업데이트
sudo update-initramfs -u
```

#### config.txt에서 오디오 비활성화

```bash
sudo vi /boot/firmware/config.txt
```

다음 라인을 찾아 수정
```
dtparam=audio=off
```

변경 후 재부팅
```bash
sudo reboot
```

### 2. GPIO 속도 조절

라즈베리 파이 4/5는 GPIO 속도가 매우 빨라 신호 노이즈가 발생할 수 있습니다.

- 실행 시: `--led-slowdown-gpio=2` (또는 3~4) 옵션 사용
- 코드 내: `runtime_defaults.gpio_slowdown = 4;` 설정

### 3. 전원 관리

- 외부 5V 전원 장치 사용 (최소 6A 권장)
- 파이와 LED 매트릭스의 GND 공통 연결 필수

---

## 트러블슈팅

### 문제: 화면 깜빡임 / 노이즈 발생

**원인**: PWM/PCM 타이머 충돌 또는 다른 프로그램과의 충돌

**해결**:
1. 위의 [PWM 간섭 제거](#1-pwm-간섭-제거) 단계 수행
2. `gpio_slowdown` 값 증가 (2 → 3 → 4)

### 문제: 버스 정보가 나오지 않음 / `ERR` 표시

1. `python3 -I busfetch.py` 실행 결과 확인
   - `ERR AUTH 30`: 인증키 미등록. 활용신청 직후에는 등록까지 1시간 정도 걸릴 수 있음
   - `ERR LIMIT`: 일일 호출 한도 초과. 자정 이후 다시 시도
     (정상 사용은 하루 약 800회입니다. 한도 10,000회가 찼다면 키가 다른 곳에서 쓰이는지 확인하고 재발급하세요)
   - `ERR NET`: 인터넷 연결 또는 DNS 확인
   - `OK 0`: 지금 운행 중인 버스가 없음 (정상)
2. `journalctl -u ledclock | grep '\[bus\]'`로 상태 변화 확인
3. `bus.conf` 권한이 `644`(또는 `640` + 그룹 `daemon`)인지 확인 (`daemon` 사용자가 읽을 수 있어야 함).
   로그에 `[bus] no-config (./bus.conf: Permission denied)`가 보이면 권한 문제입니다.
4. 새벽 중지 시간(기본 01:00~05:00)에는 원래 시계가 정상입니다. `./clockmode`로 확인하세요.
5. 부팅 직후 `[bus] polling paused (waiting for the clock to sync)`는 시각 동기화를 기다리는 중입니다.
   그동안 2·3안 오른쪽 아래에는 `...`가 보이고, 첫 정보를 받으면(보통 켜진 뒤 20초 남짓) 남은 시간으로 바뀝니다.

---

## 기술 설명

### `nanosleep` 사용 이유

| 항목 | `sleep()` | `nanosleep()` |
|------|-----------|---------------|
| **단위** | 초 | 나노초 |
| **최소 대기 시간** | 1초 | 1 나노초 |
| **정밀도** | 매우 낮음 | 매우 높음 |
| **시그널 처리** | 중간에 멈출 수 있음 | 안전함 (남은 시간 확인 가능) |
| **주요 용도** | 긴 시간 대기 | 정밀한 하드웨어 제어 |

### `std::cerr` 사용 이유

| 항목 | `std::cout` | `std::cerr` |
|------|-------------|-------------|
| **용도** | 일반적인 프로그램 결과 출력 | 에러/경고/진단 정보 출력 |
| **버퍼링** | 버퍼링 사용 | 버퍼링 미사용 (즉시 출력) |
| **스트림** | Standard Output | Standard Error |

---

## 라이선스 및 출처

### Credits / Attribution

이 프로젝트는 Henner Zeller가 개발한 [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) 라이브러리를 사용합니다.

- **라이브러리**: [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix)
- **원작자**: Henner Zeller
- **라이선스**: GNU General Public License v2.0 (GPL-2.0)

### 데이터 출처

버스 도착 정보는 공공데이터포털(data.go.kr)의 다음 API를 사용합니다.

- 대전광역시_정류소별 도착정보 조회 (제공: 대전광역시)
- 국토교통부_(TAGO)_버스도착정보 (제공: 국토교통부)

### 라이선스 정보

이 프로젝트는 GPL-2.0 라이선스 하에 배포됩니다.  
전체 라이선스 내용은 [COPYING](./COPYING) 파일을 참조하세요.

```
This project uses the rpi-rgb-led-matrix library
(https://github.com/hzeller/rpi-rgb-led-matrix).
Original code by Henner Zeller, licensed under GPL-2.0.
```

>  **GPL-2.0 의무사항**
> - 원본 코드의 저작권 문구와 라이선스 고지를 유지해야 합니다.
> - 이 프로젝트를 배포할 경우 소스 코드도 함께 공개해야 합니다.
