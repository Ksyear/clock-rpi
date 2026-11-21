# Raspberry Pi LED Matrix Clock

라즈베리 파이와 RGB LED 매트릭스를 사용한 디지털 시계 프로젝트입니다.  
96x16 (32x16 패널 3개 연결) 크기의 도트 매트릭스에 날짜, 요일, 시간과 함께  
한밭대학교 정류장(대전역동광장 방향)의 102번 버스 도착 정보를 표시합니다.

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
- **버스 도착 정보**: 102번 버스 번호와 남은 시간(`3MIN`), 곧 도착(`SOON`, 깜빡임) 표시
  - 버스 정보가 있으면 시간을 `HH:MM`으로 줄이고 오른쪽에 버스 정보를 함께 표시
  - 인증키가 없거나 운행이 끝나면 원래 화면(`HH:MM:SS`)으로 표시

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
├── layout.cpp/.h      # 화면 배치 (날짜·요일·시간·버스 정보 그리기)
├── bus.cpp/.h         # 버스 정보 백그라운드 스레드와 표시 상태 계산
├── busfetch.py        # 버스 API 호출 헬퍼 (Python 표준 라이브러리만 사용)
├── bus.conf.example   # 버스 설정 예시 (bus.conf 로 복사해 인증키 입력)
├── preview.cpp        # 개발용: LED 없이 화면을 텍스트로 출력
├── tests/             # busfetch.py 테스트와 실제 API 응답 샘플
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
g++ -I./include -O3 clock.cpp -o clock -L./lib -lrgbmatrix -lrt -lm -lpthread
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

### 화면 배치

```
 MM-DD |          | 102     <- 버스 번호 (하늘색)
  DAY  |  HH:MM   | 3MIN    <- 남은 시간 (주황색), 곧 도착이면 SOON 깜빡임
```

| 상태 | 표시 |
|------|------|
| 정상 | `102` / `3MIN` (남은 시간, 최대 `99MIN`) |
| 곧 도착 (1정류장 이내 또는 1분 미만) | `102` / `SOON` 깜빡임 |
| 데이터가 150초 이상 지남 | 남은 시간을 어둡게 표시 |
| 버스가 지나갔고 새 정보 대기 중 | `102` / `--` |
| 인증키 오류 / 호출 한도 초과 | `102` / `ERR` (빨간색) |
| 운행 종료, 인증키 없음, 300초 넘게 갱신 실패 | 원래 시계 화면 (`HH:MM:SS`) |

### 설정

1. 공공데이터포털(data.go.kr)에서 아래 API를 활용신청하고 **일반 인증키**를 확인합니다.
   - [대전광역시_정류소별 도착정보 조회](https://www.data.go.kr/data/15157920/openapi.do) (기본)
   - [국토교통부_(TAGO)_버스도착정보](https://www.data.go.kr/data/15098530/openapi.do) (예비)
2. 프로젝트 폴더에 `bus.conf`를 만들고 인증키를 적습니다.

```bash
cp bus.conf.example bus.conf
vi bus.conf          # SERVICE_KEY= 뒤에 인증키 입력
chmod 644 bus.conf
```

- `bus.conf`가 생기면 **30초 안에** 버스 정보를 받기 시작합니다. 재시작은 필요 없습니다.
- `bus.conf`를 지우면 원래 시계 화면으로 돌아갑니다.
- `bus.conf`는 `.gitignore`에 들어 있습니다. **인증키가 들어 있으니 커밋하지 마세요.**

### 동작 방식

- 시계 프로그램의 백그라운드 스레드가 약 90초마다 `python3 -I ./busfetch.py`를 실행합니다.
  화면 갱신 루프는 네트워크를 기다리지 않으므로 시계가 멈추지 않습니다.
- 두 호출 사이에는 남은 시간을 시계 안에서 직접 줄여 가며 표시합니다.
- 운행하는 버스가 없으면 5분, 실패하면 15초 → 30초 → 60초 간격으로 다시 시도하고,
  하루 950회를 넘기지 않습니다.
- 대전시 BIS API를 먼저 쓰고, 실패할 때만 TAGO API를 씁니다.
  TAGO는 대전시 데이터를 늦게 받아 곧 도착하는 버스가 빠지는 경우가 있었습니다.
- 정류장: 한밭대학교 (정류소번호 `41690`, 대전시 `8002920`, TAGO `DJB8002920`),
  대전역동광장 방향입니다. `bus.conf`에서 `ROUTES=`를 비우면 모든 노선 중 가장 빨리 오는 버스를 표시합니다.
- `clock`은 시작 후 `daemon` 사용자로 권한을 낮춥니다. `daemon`은 홈 폴더를 지나갈 수 없으므로
  `busfetch.py`와 `bus.conf`는 **작업 폴더 기준 상대 경로**로 읽습니다.
  (`WorkingDirectory`가 프로젝트 폴더여야 합니다.)

### 확인과 테스트

```bash
# API 호출 결과 확인 (bus.conf 필요)
python3 -I busfetch.py

# 서비스 로그에서 버스 상태 확인
journalctl -u ledclock -f | grep '\[bus\]'

# 개발용: LED 없이 모든 화면 상태를 텍스트로 확인 (macOS에서도 빌드 가능)
make preview && ./preview
python3 -I busfetch.py | ./preview -

# 파서 테스트 + 화면이 96x16 안에 들어가는지 검사
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
   - `ERR NET`: 인터넷 연결 또는 DNS 확인
   - `OK 0`: 지금 운행 중인 버스가 없음 (정상)
2. `journalctl -u ledclock | grep '\[bus\]'`로 상태 변화 확인
3. `bus.conf` 권한이 `644`인지 확인 (`daemon` 사용자가 읽을 수 있어야 함)

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
