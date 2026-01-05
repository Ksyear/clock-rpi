# Raspberry Pi LED Matrix Clock

라즈베리 파이와 RGB LED 매트릭스를 사용한 디지털 시계 프로젝트입니다.  
96x16 (32x16 패널 3개 연결) 크기의 도트 매트릭스에 날짜, 요일, 시간을 표시합니다.

![LED Clock](https://img.shields.io/badge/Raspberry_Pi-LED_Matrix-red?style=flat-square&logo=raspberrypi)
![License](https://img.shields.io/badge/License-GPL--2.0-blue?style=flat-square)

---

## 목차

- [기능](#-기능)
- [하드웨어 요구사항](#-하드웨어-요구사항)
- [프로젝트 구조](#-프로젝트-구조)
- [설치 및 컴파일](#️-설치-및-컴파일)
- [실행](#-실행)
- [하드웨어 최적화](#-하드웨어-최적화)
- [트러블슈팅](#-트러블슈팅)
- [기술 설명](#-기술-설명)
- [라이선스 및 출처](#-라이선스-및-출처)

---

## 기능

- **날짜 표시**: `MM-DD` 형식 (예: `01-05`)
- **요일 표시**: 영문 약어 대문자 (예: `SUN`, `MON`)
- **시간 표시**: `HH:MM:SS` 형식의 실시간 시계
- **색상 구분**: 날짜/요일은 빨간색, 시간은 흰색으로 구분하여 가독성 향상

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
├── clock.cpp          # 메인 시계 프로그램 소스 코드
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
git clone https://github.com/YOUR_USERNAME/clock-rpi.git
cd clock-rpi
```

### 2. 컴파일

프로젝트 루트 디렉토리에서 다음 명령 실행

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
